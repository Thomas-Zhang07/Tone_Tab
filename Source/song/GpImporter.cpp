// Guitar Pro 导入：GP3 / GP4 / GP5（二进制）、GPX（GP6，BCFZ 压缩文件系统）、GP7 / GP8（zip + GPIF XML）
//
// 二进制格式和 GPIF 结构参考了 alphaTab（https://github.com/CoderLine/alphaTab，MPL-2.0）
// 的 Gp3To5Importer / GpxFileSystem / GpifParser 的读取顺序。本文件按 MPL-2.0 发布。

#include "Song.h"
#include "Text.h"
#include <juce_core/juce_core.h>
#include <stdexcept>

#if JUCE_WINDOWS
 #include <windows.h>
#endif

namespace
{
    // =========================================================================
    // 字符串：GP3-5 用系统本地编码（中文谱一般是 GBK）
    juce::String decodeAnsi (const uint8_t* p, size_t n)
    {
        bool ascii = true;
        for (size_t i = 0; i < n; ++i)
            if (p[i] >= 0x80) { ascii = false; break; }
        if (ascii || n == 0)
            return juce::String (reinterpret_cast<const char*> (p), n);

        if (juce::CharPointer_UTF8::isValidString (reinterpret_cast<const char*> (p), (int) n))
            return juce::String::fromUTF8 (reinterpret_cast<const char*> (p), (int) n);

       #if JUCE_WINDOWS
        for (UINT cp : { 936u, (UINT) CP_ACP, 1252u })   // GBK、系统默认、西欧
        {
            const int wlen = MultiByteToWideChar (cp, MB_ERR_INVALID_CHARS, reinterpret_cast<const char*> (p), (int) n, nullptr, 0);
            if (wlen > 0)
            {
                std::wstring w ((size_t) wlen, L'\0');
                MultiByteToWideChar (cp, 0, reinterpret_cast<const char*> (p), (int) n, w.data(), wlen);
                return juce::String (w.c_str());
            }
        }
       #endif
        // 其它系统：按 Latin-1 解读
        juce::String s;
        for (size_t i = 0; i < n; ++i)
            s += (juce::juce_wchar) p[i];
        return s;
    }

    struct EndOfData : std::runtime_error { EndOfData() : std::runtime_error ("unexpected end of file") {} };

    class Reader
    {
    public:
        Reader (const void* data, size_t size) : d (static_cast<const uint8_t*> (data)), n (size) {}

        size_t position() const { return pos; }
        void need (size_t k) const { if (pos + k > n) throw EndOfData(); }
        void skip (size_t k)       { need (k); pos += k; }
        uint8_t byte()             { need (1); return d[pos++]; }
        int sbyte()                { return (int) (int8_t) byte(); }
        bool boolean()             { return byte() != 0; }
        int i16()                  { need (2); const int v = (int16_t) (d[pos] | (d[pos + 1] << 8)); pos += 2; return v; }
        int i32()
        {
            need (4);
            const uint32_t v = (uint32_t) d[pos] | ((uint32_t) d[pos + 1] << 8) | ((uint32_t) d[pos + 2] << 16) | ((uint32_t) d[pos + 3] << 24);
            pos += 4;
            return (int) (int32_t) v;
        }
        double f64be()
        {
            need (8);
            uint64_t v = 0;
            for (int i = 0; i < 8; ++i) v = (v << 8) | d[pos + (size_t) i];
            pos += 8;
            double r;
            std::memcpy (&r, &v, 8);
            return r;
        }
        juce::String str (int len)
        {
            if (len < 0 || len > 100000) throw std::runtime_error ("bad string length");
            need ((size_t) len);
            auto s = decodeAnsi (d + pos, (size_t) len);
            pos += (size_t) len;
            return s;
        }
        // 1 字节长度 + 固定 len 字节
        juce::String strByteLen (int len)
        {
            const int l = byte();
            need ((size_t) len);
            auto s = decodeAnsi (d + pos, (size_t) juce::jmin (l, len));
            pos += (size_t) len;
            return s;
        }
        juce::String strIntUnused() { skip (4); return str (byte()); }
        juce::String strInt()       { return str (i32()); }
        juce::String strIntByte()   { const int l = i32() - 1; byte(); return str (l); }

    private:
        const uint8_t* d;
        size_t n, pos = 0;
    };

    // =========================================================================
    // GP3 / GP4 / GP5
    class Gp5Parser
    {
    public:
        Gp5Parser (const juce::MemoryBlock& data, RawScore& out) : r (data.getData(), data.getSize()), score (out) {}

        void parse()
        {
            readVersion();
            score.format = "Guitar Pro " + juce::String (version / 100) + "." + juce::String (version % 100).paddedLeft ('0', 2);

            // 曲目信息
            score.title = r.strIntUnused();
            r.strIntUnused();                         // 副标题
            score.artist = r.strIntUnused();
            r.strIntUnused();                         // 专辑
            r.strIntUnused();                         // 作词
            if (version >= 500) r.strIntUnused();     // 作曲
            r.strIntUnused();                         // 版权
            r.strIntUnused();                         // 制谱
            r.strIntUnused();                         // 说明
            const int notices = r.i32();
            check (notices, 1000);
            for (int i = 0; i < notices; ++i) r.strIntUnused();

            if (version < 500) r.boolean();           // 全局三连音感觉
            if (version >= 400)                       // 歌词
            {
                r.i32();
                for (int i = 0; i < 5; ++i) { r.i32(); r.strInt(); }
            }
            if (version >= 510) r.skip (19);          // RSE 总设置
            if (version >= 500)                       // 页面设置
            {
                r.skip (28);
                r.i16();
                for (int i = 0; i < 10; ++i) r.strIntByte();
                r.strIntByte();                       // 速度名称
            }
            score.initialTempo = r.i32();
            if (version >= 510) r.boolean();
            r.i32();                                  // 调号
            if (version >= 400) r.byte();

            // MIDI 通道
            for (int port = 0; port < 4; ++port)
                for (int ch = 0; ch < 16; ++ch)
                {
                    programs[port][ch] = r.i32();
                    r.skip (6 + 2);
                }

            if (version >= 500) { r.skip (19 * 2); r.skip (4); }   // 反复跳转记号

            barCount = r.i32();
            trackCount = r.i32();
            check (barCount, 5000);
            check (trackCount, 100);

            for (int i = 0; i < barCount; ++i) readMasterBar();
            for (int i = 0; i < trackCount; ++i) readTrack();
            for (auto& t : score.tracks) t.bars.resize ((size_t) barCount);
            for (int b = 0; b < barCount; ++b)
                for (int t = 0; t < trackCount; ++t)
                    readBar (b, t);
        }

    private:
        static void check (int v, int max) { if (v < 0 || v > max) throw std::runtime_error ("value out of range"); }

        void readVersion()
        {
            const auto v = r.strByteLen (30);
            if (! v.startsWith ("FICHIER GUITAR PRO"))
                throw std::runtime_error ("not a Guitar Pro 3-5 file");
            auto num = v.fromFirstOccurrenceOf ("v", false, false);
            version = num.upToFirstOccurrenceOf (".", false, false).getIntValue() * 100
                    + num.fromFirstOccurrenceOf (".", false, false).getIntValue();
            if (version < 300 || version > 599)
                throw std::runtime_error ("unsupported version");
        }

        void readMasterBar()
        {
            RawMasterBar mb;
            if (! score.masterBars.empty())
            {
                mb.numerator = score.masterBars.back().numerator;
                mb.denominator = score.masterBars.back().denominator;
            }
            const int flags = r.byte();
            if (flags & 0x01) mb.numerator = r.byte();
            if (flags & 0x02) mb.denominator = r.byte();
            mb.repeatStart = (flags & 0x04) != 0;
            if (flags & 0x08) mb.repeatCount = r.byte() + (version >= 500 ? 0 : 1);

            if ((flags & 0x10) && version < 500)
            {
                // GP3/4：这里存的是“第几遍结尾”，换算成位掩码
                int existing = 0;
                for (int i = (int) score.masterBars.size() - 1; i >= 0; --i)
                {
                    const auto& prev = score.masterBars[(size_t) i];
                    if ((prev.repeatCount > 0 && i != (int) score.masterBars.size() - 1) || prev.repeatStart)
                        break;
                    existing |= prev.alternateEndings;
                }
                const int mask = r.byte();
                int alt = 0;
                for (int i = 0; i < 8; ++i)
                    if (mask > i && (existing & (1 << i)) == 0)
                        alt |= 1 << i;
                mb.alternateEndings = alt;
            }
            if (flags & 0x20)
            {
                mb.section = r.strIntByte();
                r.skip (4);
            }
            if (flags & 0x40) { r.sbyte(); r.byte(); }
            if (version >= 500 && (flags & 0x03)) r.skip (4);
            if (version >= 500) mb.alternateEndings = r.byte();
            if (version >= 500) { r.byte(); r.byte(); }

            if (mb.numerator <= 0) mb.numerator = 4;
            if (mb.denominator <= 0) mb.denominator = 4;
            score.masterBars.push_back (mb);
        }

        void readTrack()
        {
            RawTrack t;
            const int flags = r.byte();
            t.name = r.strByteLen (40);
            t.percussion = (flags & 0x01) != 0;

            const int stringCount = r.i32();
            for (int i = 0; i < 7; ++i)
            {
                const int tune = r.i32();
                if (i < stringCount) t.tuning.push_back (tune);
            }
            const int port = juce::jlimit (0, 3, r.i32() - 1);
            const int channel = juce::jlimit (0, 15, r.i32() - 1);
            r.i32();                                   // 效果通道
            r.skip (4);                                // 品数
            t.capo = r.i32();
            r.skip (4);                                // 颜色
            t.program = programs[port][channel];
            if (channel == 9) t.percussion = true;

            if (version >= 500)
            {
                r.byte(); r.byte(); r.byte(); r.byte(); r.byte();
                r.i32(); r.i32(); r.i32();
                r.skip (10);
                r.byte(); r.byte();
                r.skip (16);                           // RSE 音色库
                if (version >= 510)
                {
                    r.skip (4);
                    r.strIntByte();
                    r.strIntByte();
                }
            }
            score.tracks.push_back (std::move (t));
        }

        void readBar (int barIndex, int trackIndex)
        {
            auto& bar = score.tracks[(size_t) trackIndex].bars[(size_t) barIndex];
            int voiceCount = 1;
            if (version >= 500) { r.byte(); voiceCount = 2; }

            for (int v = 0; v < voiceCount; ++v)
            {
                const int beatCount = r.i32();
                check (beatCount, 500);
                RawVoice voice;
                double cursor = 0.0;
                for (int b = 0; b < beatCount; ++b)
                    voice.beats.push_back (readBeat (barIndex, trackIndex, cursor));
                bar.voices.push_back (std::move (voice));
            }
        }

        RawBeat readBeat (int barIndex, int trackIndex, double& cursor)
        {
            RawBeat beat;
            const auto& track = score.tracks[(size_t) trackIndex];
            const int flags = r.byte();
            const bool dotted = (flags & 0x01) != 0;
            if (flags & 0x40) r.byte();                // 休止 / 空拍

            const int durationCode = r.sbyte();
            double len = 4.0 / std::pow (2.0, juce::jlimit (-2, 6, durationCode) + 2);
            if (dotted) len *= 1.5;

            if (flags & 0x20)
            {
                const int tupletNum = r.i32();
                int den = 1;
                switch (tupletNum)
                {
                    case 3: den = 2; break;
                    case 5: case 6: case 7: den = 4; break;
                    case 9: case 10: case 11: case 12: case 13: den = 8; break;
                    case 2: case 4: case 8: den = tupletNum; break;   // 不改变时值
                    default: den = tupletNum > 0 ? tupletNum : 1; break;
                }
                if (tupletNum > 0)
                    len *= (double) den / (double) tupletNum;
            }
            beat.duration = len;

            if (flags & 0x02) readChord();
            if (flags & 0x04) r.strIntUnused();         // 拍上的文字

            bool beatHarmonic = false;
            if (flags & 0x08) beatHarmonic = readBeatEffects();
            if (flags & 0x10) readMixTable (barIndex, trackIndex, cursor);

            const int stringFlags = r.byte();
            for (int i = 6; i >= 0; --i)
            {
                if ((stringFlags & (1 << i)) != 0 && 6 - i < (int) track.tuning.size())
                {
                    auto note = readNote (6 - i);
                    note.harmonic = note.harmonic || beatHarmonic;
                    beat.notes.push_back (note);
                }
            }

            if (version >= 500)
            {
                const int flags2 = r.i16();
                if (flags2 & 0x800) r.byte();
            }

            cursor += len;
            return beat;
        }

        void readChord()
        {
            if (version >= 500)
            {
                r.skip (17); r.strByteLen (21); r.skip (4); r.i32();
                for (int i = 0; i < 7; ++i) r.i32();
                r.byte(); r.skip (5); r.skip (26);
                return;
            }
            if (r.byte() != 0)
            {
                if (version >= 400)
                {
                    r.skip (16); r.strByteLen (21); r.skip (4); r.i32();
                    for (int i = 0; i < 7; ++i) r.i32();
                    r.byte(); r.skip (5); r.skip (26);
                }
                else
                {
                    r.skip (25); r.strByteLen (34); r.i32();
                    for (int i = 0; i < 6; ++i) r.i32();
                    r.skip (36);
                }
            }
            else
            {
                const int strings = version >= 406 ? 7 : 6;
                r.strIntByte();
                const int firstFret = r.i32();
                if (firstFret > 0)
                    for (int i = 0; i < strings; ++i) r.i32();
            }
        }

        bool readBeatEffects()
        {
            const int flags = r.byte();
            const int flags2 = version >= 400 ? r.byte() : 0;
            if (flags & 0x20)
            {
                r.sbyte();
                if (version < 400) r.skip (4);
            }
            if (flags2 & 0x04) readBendPoints();       // 摇把
            if (flags & 0x40) { r.byte(); r.byte(); }  // 扫弦
            if (flags2 & 0x02) r.sbyte();              // 拨片方向
            return version < 400 && (flags & 0x0C) != 0;
        }

        float readBendPoints()
        {
            r.byte(); r.i32();
            const int count = r.i32();
            check (count, 1000);
            int maxValue = 0;
            for (int i = 0; i < count; ++i)
            {
                r.i32();
                maxValue = juce::jmax (maxValue, r.i32());
                r.byte();
            }
            return (float) maxValue / 50.0f;           // 100 = 全音 = 2 个半音
        }

        void readMixTable (int barIndex, int trackIndex, double cursor)
        {
            r.sbyte();                                  // 音色
            if (version >= 500) r.skip (16);
            const int volume = r.sbyte(), balance = r.sbyte(), chorus = r.sbyte();
            const int reverb = r.sbyte(), phaser = r.sbyte(), tremolo = r.sbyte();
            if (version >= 500) r.strIntByte();
            const int tempo = r.i32();
            for (int v : { volume, balance, chorus, reverb, phaser, tremolo })
                if (v >= 0) r.byte();
            if (tempo >= 0)
            {
                r.sbyte();
                if (version >= 510) r.byte();
            }
            if (version >= 400) r.byte();
            if (version >= 500) r.sbyte();
            if (version >= 510) { r.strIntByte(); r.strIntByte(); }

            // 速度变化写到总小节上（只取一条轨，避免重复）
            if (tempo > 0 && (trackIndex == 0 || tempoTrack < 0 || tempoTrack == trackIndex))
            {
                tempoTrack = trackIndex;
                auto& mb = score.masterBars[(size_t) barIndex];
                const double barLen = 4.0 * mb.numerator / mb.denominator;
                mb.tempos.push_back ({ barLen > 0 ? cursor / barLen : 0.0, (double) tempo });
            }
        }

        RawNote readNote (int stringIndex)
        {
            RawNote note;
            note.string = stringIndex;
            const int flags = r.byte();
            note.accent = (flags & 0x42) != 0;
            if (flags & 0x20)
            {
                const int type = r.byte();
                note.dead = type == 3;
                note.tieDestination = type == 2;
            }
            if ((flags & 0x01) && version < 500) { r.byte(); r.byte(); }
            if (flags & 0x10) r.sbyte();
            if (flags & 0x20) note.fret = r.sbyte();
            if (flags & 0x80) { r.sbyte(); r.sbyte(); }
            if (version >= 500)
            {
                if (flags & 0x01) r.f64be();
                r.byte();
            }
            if (flags & 0x08) readNoteEffects (note);
            if (note.fret < 0) note.fret = 0;
            return note;
        }

        void readNoteEffects (RawNote& note)
        {
            const int flags = r.byte();
            const int flags2 = version >= 400 ? r.byte() : 0;
            if (flags & 0x01) note.bend = readBendPoints();
            if (flags & 0x10)
            {
                r.sbyte(); r.sbyte(); r.sbyte(); r.skip (1);
                if (version >= 500) r.byte();
            }
            if (flags2 & 0x04) r.byte();                // 轮指
            if (flags2 & 0x08)
            {
                const int type = r.sbyte();
                if (version >= 500)
                {
                    note.slideOut = (type & 0x0F) != 0;
                    note.slideIn = (type & 0x30) != 0;
                }
                else
                {
                    note.slideOut = type > 0;
                    note.slideIn = type < 0;
                }
            }
            else if (version < 400 && (flags & 0x04))
                note.slideOut = true;
            if (flags2 & 0x10)
            {
                const int type = r.byte();
                note.harmonic = true;
                if (version >= 500)
                {
                    if (type == 2) r.skip (3);
                    else if (type == 3) r.skip (1);
                }
            }
            if (flags2 & 0x20) r.skip (2);              // 颤音（trill）
            note.letRing = (flags & 0x08) != 0;
            note.hammerPull = (flags & 0x02) != 0;
            note.vibrato = (flags2 & 0x40) != 0;
            note.palmMute = (flags2 & 0x02) != 0;
        }

        Reader r;
        RawScore& score;
        int version = 0, barCount = 0, trackCount = 0, tempoTrack = -1;
        int programs[4][16] {};
    };

    // =========================================================================
    // GPX（GP6）：BCFZ 压缩 + BCFS 扇区文件系统
    class BitReader
    {
    public:
        BitReader (const uint8_t* data, size_t size) : d (data), n (size) {}
        int bit()
        {
            if (bitPos >= 8)
            {
                if (pos >= n) throw EndOfData();
                current = d[pos++];
                bitPos = 0;
            }
            return (current >> (7 - bitPos++)) & 1;
        }
        int bits (int count)         { int v = 0; for (int i = count - 1; i >= 0; --i) v |= bit() << i; return v; }
        int bitsReversed (int count) { int v = 0; for (int i = 0; i < count; ++i) v |= bit() << i; return v; }
        int byte()                   { return bits (8); }

    private:
        const uint8_t* d;
        size_t n, pos = 0;
        int current = 0, bitPos = 8;
    };

    int le32 (const std::vector<uint8_t>& d, size_t o)
    {
        if (o + 4 > d.size()) return 0;
        return (int) ((uint32_t) d[o] | ((uint32_t) d[o + 1] << 8) | ((uint32_t) d[o + 2] << 16) | ((uint32_t) d[o + 3] << 24));
    }
}

bool extractGpxScore (const juce::MemoryBlock& data, juce::String& xmlOut, juce::String& error)
{
    const auto* p = static_cast<const uint8_t*> (data.getData());
    const size_t n = data.getSize();
    if (n < 8) { error = "file too small"; return false; }

    std::vector<uint8_t> fs;
    const juce::String header (reinterpret_cast<const char*> (p), 4);

    if (header == "BCFZ")
    {
        try
        {
            BitReader br (p + 4, n - 4);
            int expected = 0;
            for (int i = 0; i < 4; ++i) expected |= br.byte() << (8 * i);
            if (expected <= 0 || expected > 200 * 1024 * 1024) { error = "bad gpx size"; return false; }
            fs.reserve ((size_t) expected);

            try
            {
                while ((int) fs.size() < expected)
                {
                    if (br.bit() == 1)
                    {
                        const int wordSize = br.bits (4);
                        const int offset = br.bitsReversed (wordSize);
                        const int size = br.bitsReversed (wordSize);
                        const int src = (int) fs.size() - offset;
                        const int toCopy = juce::jmin (offset, size);
                        if (src < 0) throw std::runtime_error ("bad back reference");
                        for (int i = 0; i < toCopy; ++i)
                            fs.push_back (fs[(size_t) (src + i)]);
                    }
                    else
                    {
                        const int size = br.bitsReversed (2);
                        for (int i = 0; i < size; ++i)
                            fs.push_back ((uint8_t) br.byte());
                    }
                }
            }
            catch (const EndOfData&) {}

            if (fs.size() < 4) { error = "empty gpx"; return false; }
            fs.erase (fs.begin(), fs.begin() + 4);    // 跳过解压后的 "BCFS"
        }
        catch (const std::exception& e)
        {
            error = e.what();
            return false;
        }
    }
    else if (header == "BCFS")
    {
        fs.assign (p + 4, p + n);
    }
    else
    {
        error = "not a gpx file";
        return false;
    }

    // 扇区文件系统：每个扇区 0x1000 字节
    const size_t sector = 0x1000;
    for (size_t off = sector; off + 3 < fs.size(); off += sector)
    {
        if (le32 (fs, off) != 2)
            continue;
        juce::String name;
        for (size_t i = 0; i < 127 && off + 4 + i < fs.size() && fs[off + 4 + i] != 0; ++i)
            name += (char) fs[off + 4 + i];
        const int size = le32 (fs, off + 0x8c);

        std::vector<uint8_t> content;
        for (size_t k = 0;; ++k)
        {
            const int s = le32 (fs, off + 0x94 + 4 * k);
            if (s == 0 || (size_t) s * sector >= fs.size())
                break;
            const size_t start = (size_t) s * sector;
            const size_t end = juce::jmin (fs.size(), start + sector);
            content.insert (content.end(), fs.begin() + (long) start, fs.begin() + (long) end);
        }
        if (name == "score.gpif")
        {
            content.resize (juce::jmin (content.size(), (size_t) juce::jmax (0, size)));
            xmlOut = juce::String::fromUTF8 (reinterpret_cast<const char*> (content.data()), (int) content.size());
            return true;
        }
    }
    error = "score.gpif not found in gpx";
    return false;
}

bool importGp3To5 (const juce::MemoryBlock& data, RawScore& out, juce::String& error)
{
    try
    {
        Gp5Parser parser (data, out);
        parser.parse();
        return true;
    }
    catch (const std::exception& e)
    {
        error = juce::String ("Guitar Pro 3-5: ") + e.what();
        return false;
    }
}

// =============================================================================
// GPIF（GP6 / GP7 / GP8 的乐谱 XML）
namespace
{
    std::vector<juce::String> splitIds (const juce::String& s)
    {
        std::vector<juce::String> out;
        for (auto& t : juce::StringArray::fromTokens (s, " \t\r\n", ""))
            if (t.isNotEmpty()) out.push_back (t);
        return out;
    }

    juce::String childText (const juce::XmlElement* e, const char* name)
    {
        if (e == nullptr) return {};
        if (auto* c = e->getChildByName (name)) return c->getAllSubText().trim();
        return {};
    }

    double noteValueBeats (const juce::String& v)
    {
        if (v == "Long")        return 16.0;
        if (v == "DoubleWhole") return 8.0;
        if (v == "Whole")       return 4.0;
        if (v == "Half")        return 2.0;
        if (v == "Quarter")     return 1.0;
        if (v == "Eighth")      return 0.5;
        if (v == "16th")        return 0.25;
        if (v == "32nd")        return 0.125;
        if (v == "64th")        return 0.0625;
        if (v == "128th")       return 0.03125;
        if (v == "256th")       return 0.015625;
        return 1.0;
    }

    // 速度参考音符：1 = 八分，2 = 四分，3 = 附点四分，4 = 二分，5 = 附点二分
    double tempoToQuarterBpm (double value, int reference)
    {
        switch (reference)
        {
            case 1:  return value * 0.5;
            case 3:  return value * 1.5;
            case 4:  return value * 2.0;
            case 5:  return value * 3.0;
            default: return value;
        }
    }
}

bool importGpif (const juce::String& xmlText, RawScore& out, juce::String& error)
{
    auto root = juce::parseXML (xmlText);
    if (root == nullptr || ! root->hasTagName ("GPIF"))
    {
        error = "invalid GPIF";
        return false;
    }

    if (auto* s = root->getChildByName ("Score"))
    {
        out.title = childText (s, "Title");
        out.artist = childText (s, "Artist");
    }

    // ---- 速度 -------------------------------------------------------------
    struct TempoAuto { int bar; double pos, bpm; };
    std::vector<TempoAuto> tempoAutos;
    if (auto* mt = root->getChildByName ("MasterTrack"))
        if (auto* autos = mt->getChildByName ("Automations"))
            for (auto* a : autos->getChildWithTagNameIterator ("Automation"))
                if (childText (a, "Type") == "Tempo")
                {
                    const auto parts = splitIds (childText (a, "Value"));
                    if (parts.empty()) continue;
                    const double v = parts[0].getDoubleValue();
                    const int ref = parts.size() > 1 ? parts[1].getIntValue() : 2;
                    tempoAutos.push_back ({ childText (a, "Bar").getIntValue(), childText (a, "Position").getDoubleValue(),
                                            tempoToQuarterBpm (v, ref) });
                }

    // ---- 节奏 / 音符 / 拍 / 声部 / 小节（都按 id 存起来） ---------------------
    std::map<juce::String, double> rhythmBeats;
    if (auto* rs = root->getChildByName ("Rhythms"))
        for (auto* r : rs->getChildWithTagNameIterator ("Rhythm"))
        {
            double len = noteValueBeats (childText (r, "NoteValue"));
            if (auto* dot = r->getChildByName ("AugmentationDot"))
            {
                const int c = dot->getIntAttribute ("count", 1);
                len *= c >= 2 ? 1.75 : (c == 1 ? 1.5 : 1.0);
            }
            if (auto* tup = r->getChildByName ("PrimaryTuplet"))
            {
                const int num = tup->getIntAttribute ("num", 1), den = tup->getIntAttribute ("den", 1);
                if (num > 0 && den > 0) len *= (double) den / (double) num;
            }
            rhythmBeats[r->getStringAttribute ("id")] = len;
        }

    struct NoteInfo { int stringFromLow = 0; int fret = 0; int articulation = -1; RawNote n; };
    std::map<juce::String, NoteInfo> notes;
    if (auto* ns = root->getChildByName ("Notes"))
        for (auto* ne : ns->getChildWithTagNameIterator ("Note"))
        {
            NoteInfo info;
            if (auto* tie = ne->getChildByName ("Tie"))
                info.n.tieDestination = tie->getStringAttribute ("destination").equalsIgnoreCase ("true");
            if (ne->getChildByName ("Vibrato") != nullptr) info.n.vibrato = true;
            if (ne->getChildByName ("LetRing") != nullptr) info.n.letRing = true;
            if (ne->getChildByName ("Accent") != nullptr) info.n.accent = true;
            info.articulation = childText (ne, "InstrumentArticulation").getIntValue();
            if (ne->getChildByName ("InstrumentArticulation") == nullptr) info.articulation = -1;

            if (auto* props = ne->getChildByName ("Properties"))
            {
                float bendDest = 0.0f, bendMid = 0.0f;
                bool bended = false;
                for (auto* p : props->getChildWithTagNameIterator ("Property"))
                {
                    const auto name = p->getStringAttribute ("name");
                    if (name == "String")              info.stringFromLow = childText (p, "String").getIntValue();
                    else if (name == "Fret")           info.fret = childText (p, "Fret").getIntValue();
                    else if (name == "Muted")          info.n.dead = true;
                    else if (name == "PalmMuted")      info.n.palmMute = true;
                    else if (name == "HopoOrigin")     info.n.hammerPull = true;
                    else if (name == "HarmonicType")   info.n.harmonic = childText (p, "HType") != "NoHarmonic";
                    else if (name == "Bended")         bended = true;
                    else if (name == "BendDestinationValue") bendDest = childText (p, "Float").getFloatValue();
                    else if (name == "BendMiddleValue")      bendMid = childText (p, "Float").getFloatValue();
                    else if (name == "Slide")
                    {
                        const int f = childText (p, "Flags").getIntValue();
                        info.n.slideOut = (f & 0x0F) != 0;
                        info.n.slideIn = (f & 0x30) != 0;
                    }
                }
                if (bended)
                    info.n.bend = juce::jmax (bendDest, bendMid) / 50.0f;   // 100 = 全音
            }
            notes[ne->getStringAttribute ("id")] = info;
        }

    struct BeatInfo { juce::String rhythm; std::vector<juce::String> noteIds; bool grace = false; };
    std::map<juce::String, BeatInfo> beats;
    if (auto* bs = root->getChildByName ("Beats"))
        for (auto* be : bs->getChildWithTagNameIterator ("Beat"))
        {
            BeatInfo bi;
            if (auto* rh = be->getChildByName ("Rhythm")) bi.rhythm = rh->getStringAttribute ("ref");
            bi.noteIds = splitIds (childText (be, "Notes"));
            bi.grace = be->getChildByName ("GraceNotes") != nullptr;
            beats[be->getStringAttribute ("id")] = bi;
        }

    std::map<juce::String, std::vector<juce::String>> voiceBeats, barVoices;
    if (auto* vs = root->getChildByName ("Voices"))
        for (auto* ve : vs->getChildWithTagNameIterator ("Voice"))
            voiceBeats[ve->getStringAttribute ("id")] = splitIds (childText (ve, "Beats"));
    if (auto* bs = root->getChildByName ("Bars"))
        for (auto* be : bs->getChildWithTagNameIterator ("Bar"))
            barVoices[be->getStringAttribute ("id")] = splitIds (childText (be, "Voices"));

    // ---- 音轨 ---------------------------------------------------------------
    std::vector<int> staffCounts;
    std::vector<std::vector<int>> articulationMidi;   // 鼓轨：InstrumentArticulation 下标 -> MIDI 鼓号
    if (auto* ts = root->getChildByName ("Tracks"))
        for (auto* te : ts->getChildWithTagNameIterator ("Track"))
        {
            RawTrack t;
            t.name = childText (te, "Name");

            std::vector<int> pitchesLowFirst;
            auto readTuningFrom = [&] (const juce::XmlElement* props)
            {
                if (props == nullptr) return;
                for (auto* p : props->getChildWithTagNameIterator ("Property"))
                {
                    const auto name = p->getStringAttribute ("name");
                    if (name == "Tuning")
                    {
                        std::vector<int> pitches;
                        for (auto& s : splitIds (childText (p, "Pitches"))) pitches.push_back (s.getIntValue());
                        if (! pitches.empty()) pitchesLowFirst = pitches;
                    }
                    else if (name == "CapoFret")
                        t.capo = childText (p, "Fret").getIntValue();
                }
            };
            readTuningFrom (te->getChildByName ("Properties"));
            int staves = 0;
            if (auto* st = te->getChildByName ("Staves"))
                for (auto* staff : st->getChildWithTagNameIterator ("Staff"))
                {
                    if (staves == 0) readTuningFrom (staff->getChildByName ("Properties"));
                    ++staves;
                }
            staffCounts.push_back (juce::jmax (1, staves));

            for (auto it = pitchesLowFirst.rbegin(); it != pitchesLowFirst.rend(); ++it)
                t.tuning.push_back (*it);

            // 音色、是否为鼓
            if (auto* gm = te->getChildByName ("GeneralMidi"))
            {
                t.program = childText (gm, "Program").getIntValue();
                if (childText (gm, "PrimaryChannel").getIntValue() == 9) t.percussion = true;
            }
            if (auto* sounds = te->getChildByName ("Sounds"))
                if (auto* sound = sounds->getChildByName ("Sound"))
                    if (auto* midi = sound->getChildByName ("MIDI"))
                        t.program = childText (midi, "Program").getIntValue();

            std::vector<int> artic;
            if (auto* is = te->getChildByName ("InstrumentSet"))
            {
                if (childText (is, "Type").containsIgnoreCase ("drum")) t.percussion = true;
                if (auto* els = is->getChildByName ("Elements"))
                    for (auto* el : els->getChildWithTagNameIterator ("Element"))
                        if (auto* arts = el->getChildByName ("Articulations"))
                            for (auto* a : arts->getChildWithTagNameIterator ("Articulation"))
                                artic.push_back (childText (a, "OutputMidiNumber").getIntValue());
            }
            if (auto* inst = te->getChildByName ("Instrument"))
                if (inst->getStringAttribute ("ref").startsWith ("drm")) t.percussion = true;
            articulationMidi.push_back (artic);

            out.tracks.push_back (std::move (t));
        }

    // ---- 总小节 ---------------------------------------------------------------
    std::vector<std::vector<juce::String>> barsOfMaster;
    if (auto* ms = root->getChildByName ("MasterBars"))
        for (auto* me : ms->getChildWithTagNameIterator ("MasterBar"))
        {
            RawMasterBar mb;
            const auto time = childText (me, "Time");
            if (time.containsChar ('/'))
            {
                mb.numerator = juce::jmax (1, time.upToFirstOccurrenceOf ("/", false, false).getIntValue());
                mb.denominator = juce::jmax (1, time.fromFirstOccurrenceOf ("/", false, false).getIntValue());
            }
            if (auto* sec = me->getChildByName ("Section"))
            {
                mb.section = childText (sec, "Text");
                if (mb.section.isEmpty()) mb.section = childText (sec, "Letter");
            }
            if (auto* rep = me->getChildByName ("Repeat"))
            {
                mb.repeatStart = rep->getStringAttribute ("start").equalsIgnoreCase ("true");
                if (rep->getStringAttribute ("end").equalsIgnoreCase ("true"))
                    mb.repeatCount = juce::jmax (2, rep->getIntAttribute ("count", 2));
            }
            for (auto& e : splitIds (childText (me, "AlternateEndings")))
                if (e.getIntValue() > 0) mb.alternateEndings |= 1 << (e.getIntValue() - 1);

            barsOfMaster.push_back (splitIds (childText (me, "Bars")));
            out.masterBars.push_back (mb);
        }

    for (auto& ta : tempoAutos)
    {
        if (ta.bar == 0 && ta.pos <= 0.0 && out.masterBars.size() > 0)
            out.initialTempo = ta.bpm;
        else if (juce::isPositiveAndBelow (ta.bar, (int) out.masterBars.size()))
            out.masterBars[(size_t) ta.bar].tempos.push_back ({ ta.pos, ta.bpm });
    }

    // ---- 把小节内容挂到每条音轨上 -------------------------------------------
    for (size_t ti = 0; ti < out.tracks.size(); ++ti)
    {
        auto& track = out.tracks[ti];
        int staffOffset = 0;
        for (size_t k = 0; k < ti; ++k) staffOffset += staffCounts[k];
        const int stringCount = (int) track.tuning.size();

        for (size_t m = 0; m < barsOfMaster.size(); ++m)
        {
            RawBar bar;
            const auto& ids = barsOfMaster[m];
            if (juce::isPositiveAndBelow (staffOffset, (int) ids.size()))
            {
                for (auto& vid : barVoices[ids[(size_t) staffOffset]])
                {
                    if (vid == "-1") continue;
                    RawVoice voice;
                    for (auto& bid : voiceBeats[vid])
                    {
                        const auto& bi = beats[bid];
                        RawBeat beat;
                        beat.grace = bi.grace;
                        beat.duration = rhythmBeats.count (bi.rhythm) ? rhythmBeats[bi.rhythm] : 1.0;
                        for (auto& nid : bi.noteIds)
                        {
                            auto it = notes.find (nid);
                            if (it == notes.end()) continue;
                            RawNote n = it->second.n;
                            if (track.percussion)
                            {
                                const auto& artic = articulationMidi[ti];
                                const int a = it->second.articulation;
                                n.fret = juce::isPositiveAndBelow (a, (int) artic.size()) ? artic[(size_t) a] : 38;
                                n.string = 0;
                            }
                            else
                            {
                                n.fret = it->second.fret;
                                n.string = stringCount - 1 - it->second.stringFromLow;   // GPIF 0 = 最粗的弦
                                if (! juce::isPositiveAndBelow (n.string, stringCount)) continue;
                            }
                            beat.notes.push_back (n);
                        }
                        voice.beats.push_back (std::move (beat));
                    }
                    bar.voices.push_back (std::move (voice));
                }
            }
            track.bars.push_back (std::move (bar));
        }
    }
    return true;
}

// =============================================================================
std::unique_ptr<Song> loadSongFile (const juce::File& file, juce::String& error)
{
    juce::MemoryBlock data;
    if (! file.loadFileAsData (data) || data.getSize() < 8)
    {
        error = TR ("Cannot read this file: ", "读不了这个文件：") + file.getFullPathName();
        return nullptr;
    }

    RawScore raw;
    const auto* p = static_cast<const char*> (data.getData());
    bool ok = false;

    if (p[0] == 'P' && p[1] == 'K')
    {
        // GP7 / GP8：zip 包，里面是 Content/score.gpif
        juce::MemoryInputStream mis (data, false);
        juce::ZipFile zip (mis);
        if (auto* entry = zip.getEntry ("Content/score.gpif"))
        {
            std::unique_ptr<juce::InputStream> in (zip.createStreamForEntry (*entry));
            if (in != nullptr)
            {
                ok = importGpif (in->readEntireStreamAsString(), raw, error);
                raw.format = "Guitar Pro 7/8";
            }
        }
        else
            error = "Content/score.gpif not found";
    }
    else if (std::memcmp (p, "BCFZ", 4) == 0 || std::memcmp (p, "BCFS", 4) == 0)
    {
        juce::String xml;
        ok = extractGpxScore (data, xml, error) && importGpif (xml, raw, error);
        raw.format = "Guitar Pro 6";
    }
    else
    {
        ok = importGp3To5 (data, raw, error);
    }

    if (! ok)
    {
        error = TR ("Cannot parse this score (", "无法解析这个谱子（") + error + ")";
        return nullptr;
    }

    auto song = buildSong (raw);
    song->file = file;
    if (song->title.isEmpty())
        song->title = file.getFileNameWithoutExtension();
    return song;
}
