#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "fx/EffectChain.h"

// 每种效果器的外壳颜色，参考大家熟悉的经典踏板配色
namespace EffectStyle
{
    inline juce::Colour colourFor (EffectSlot::Kind kind, const juce::String& id)
    {
        switch (kind)
        {
            case EffectSlot::Kind::nam:  return juce::Colour (0xffb8862b);   // 金色：NAM 箱头
            case EffectSlot::Kind::ir:   return juce::Colour (0xff6b4a2e);   // 木纹棕：箱体
            case EffectSlot::Kind::vst3: return juce::Colour (0xff35556e);   // 石板蓝：外部插件
            case EffectSlot::Kind::builtin:
            default: break;
        }

        static const std::pair<const char*, juce::uint32> table[] = {
            { "denoise",    0xff2c2f36 },   // 黑：降噪
            { "gate",       0xff454a52 },   // 深灰：噪声门
            { "comp",       0xff2a6fdb },   // 蓝：压缩（CS-3）
            { "boost",      0xffe9e3d2 },   // 奶白：推子 / 增益
            { "overdrive",  0xff2e9e4f },   // 绿：过载（Tube Screamer）
            { "distortion", 0xfff28a1c },   // 橙：失真（DS-1）
            { "fuzz",       0xffc0392b },   // 红：法兹（Fuzz Face）
            { "amp",        0xff3d2f27 },   // 黑棕：音箱
            { "cab",        0xff7a5a3a },   // 木纹：箱体
            { "eq",         0xffc8ccd2 },   // 银：均衡
            { "geq",        0xffb4bac3 },   // 银灰：图示均衡（GE-7）
            { "wah",        0xff1c1c1c },   // 黑：哇音（Cry Baby）
            { "octave",     0xfff2c623 },   // 黄：八度（OC-2）
            { "pitch",      0xff8e44ad },   // 紫：移调 / 和声
            { "chorus",     0xff57b8ea },   // 浅蓝：合唱（CE-2）
            { "flanger",    0xff7a68d8 },   // 紫罗兰：镶边
            { "phaser",     0xffe2582b },   // 红橙：相位（Phase 90）
            { "tremolo",    0xff17a589 },   // 青绿：颤音
            { "vibrato",    0xff3cc2b0 },   // 浅青：揉音
            { "delay",      0xffeef0f2 },   // 白：延迟（DD-3）
            { "reverb",     0xff1f4e8c },   // 深蓝：混响
            { "shimmer",    0xffd99be0 },   // 浅紫：空灵混响
            // Gervana：照实物配色
            { "gv_wuyin",   0xff7fa58f },   // 戊寅：灰绿
            { "gv_wuwu",    0xff2b2a2e },   // 戊午：暗金黑
            { "gv_bingzi",  0xff2c8a64 },   // 丙子：孔雀绿
            { "gv_jiachen", 0xffc4262e },   // 甲辰：中国红
            { "gv_yiyou",   0xff3a3d44 },   // 乙酉：石墨灰
            { "gv_yimao",   0xff6b3a33 },   // 乙卯：枣红
        };
        for (auto& e : table)
            if (id == e.first)
                return juce::Colour (e.second);
        return juce::Colour (0xff2b6f5e);
    }

    inline juce::Colour colourFor (const EffectSlot& s) { return colourFor (s.kind, s.typeId); }

    inline bool isGervana (const EffectSlot& s)    { return s.kind == EffectSlot::Kind::builtin && s.typeId.startsWith ("gv_"); }

    // Gervana 的醒目字色：深色外壳用金色，浅色外壳用深红
    inline juce::Colour gervanaInk (juce::Colour body)
    {
        return body.getPerceivedBrightness() > 0.55f ? juce::Colour (0xff8e0f14) : juce::Colour (0xffffc23a);
    }

    // 图片预留框：还没有实物图时画一个虚线框 + “图片待替换”
    inline void drawImageSlot (juce::Graphics& g, juce::Rectangle<float> r, const juce::Image& img, juce::Colour ink, float fontSize = 11.0f)
    {
        if (img.isValid())
        {
            g.drawImageWithin (img, (int) r.getX(), (int) r.getY(), (int) r.getWidth(), (int) r.getHeight(),
                               juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize);
            return;
        }
        juce::Path p;
        p.addRoundedRectangle (r, 6.0f);
        juce::Path dashed;
        const float dashes[] = { 5.0f, 4.0f };
        juce::PathStrokeType (1.2f).createDashedStroke (dashed, p, dashes, 2);
        g.setColour (ink.withAlpha (0.45f));
        g.fillPath (dashed);
        g.setFont (juce::FontOptions (fontSize));
        g.drawFittedText (TR ("Effect artwork\n(replace image)", "效果器图片\n（待替换）"),
                      r.toNearestInt(), juce::Justification::centred, 2);
    }

    // 浅色外壳用深色字
    inline bool isLight (juce::Colour c)           { return c.getPerceivedBrightness() > 0.62f; }
    inline juce::Colour textOn (juce::Colour c)    { return isLight (c) ? juce::Colour (0xff1d1d1d) : juce::Colours::white; }
}
