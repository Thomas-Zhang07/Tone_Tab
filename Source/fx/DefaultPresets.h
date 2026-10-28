#pragma once

// 第一次运行时生成的几个默认音色（只用内置效果器）
// 每项：{ 名称, 预设 xml }
namespace DefaultPresets
{
    inline const char* list[][2] = {
        { "清音",
          R"(<Chain>
  <Slot kind="builtin" id="gate"><Params threshold="-66" release="90"/></Slot>
  <Slot kind="builtin" id="comp"><Params threshold="-24" ratio="3" attack="12" release="150" makeup="3"/></Slot>
  <Slot kind="builtin" id="amp"><Params gain="1.8" bass="5.5" mid="5" treble="6.2" presence="6" master="-4" cab="1"/></Slot>
  <Slot kind="builtin" id="chorus" bypass="1"><Params rate="0.7" depth="0.35" mix="0.45"/></Slot>
  <Slot kind="builtin" id="delay"><Params time="380" feedback="0.28" mix="0.2" tone="4500"/></Slot>
  <Slot kind="builtin" id="reverb"><Params size="0.55" damping="0.5" mix="0.26"/></Slot>
</Chain>)" },
        { "过载",
          R"(<Chain>
  <Slot kind="builtin" id="gate"><Params threshold="-60" release="80"/></Slot>
  <Slot kind="builtin" id="overdrive"><Params drive="16" tone="0.55" level="-5"/></Slot>
  <Slot kind="builtin" id="amp"><Params gain="4.5" bass="5" mid="6" treble="5.5" presence="5.5" master="-6" cab="1"/></Slot>
  <Slot kind="builtin" id="delay" bypass="1"><Params time="400" feedback="0.3" mix="0.22" tone="4000"/></Slot>
  <Slot kind="builtin" id="reverb"><Params size="0.45" damping="0.55" mix="0.18"/></Slot>
</Chain>)" },
        { "高增益",
          R"(<Chain>
  <Slot kind="builtin" id="gate"><Params threshold="-52" release="50"/></Slot>
  <Slot kind="builtin" id="overdrive"><Params drive="4" tone="0.65" level="0"/></Slot>
  <Slot kind="builtin" id="amp"><Params gain="8.5" bass="6" mid="4" treble="6" presence="6.5" master="-10" cab="1"/></Slot>
  <Slot kind="builtin" id="eq"><Params low="1" mid="-2" midFreq="750" high="0" level="0"/></Slot>
  <Slot kind="builtin" id="reverb"><Params size="0.4" damping="0.6" mix="0.12"/></Slot>
</Chain>)" },
        { "Solo 延迟",
          R"(<Chain>
  <Slot kind="builtin" id="gate"><Params threshold="-58" release="80"/></Slot>
  <Slot kind="builtin" id="overdrive"><Params drive="20" tone="0.6" level="-6"/></Slot>
  <Slot kind="builtin" id="amp"><Params gain="6.5" bass="5" mid="6.5" treble="5.5" presence="6" master="-9" cab="1"/></Slot>
  <Slot kind="builtin" id="boost"><Params gain="3"/></Slot>
  <Slot kind="builtin" id="delay"><Params time="420" feedback="0.4" mix="0.3" tone="3500" pingpong="1"/></Slot>
  <Slot kind="builtin" id="reverb"><Params size="0.6" damping="0.5" mix="0.28"/></Slot>
</Chain>)" },
    };

    // 第 2 版新增：经典箱头 / 单块组合。
    // “经典 ·” 只用内置的数字模拟；“真实采样 ·” 用程序自带音色库里的 NAM 模型（文件不在就不生成）。
    // 名字只是说明参考的是哪种风格，和这些厂商没有关系。
    inline const char* listV2[][2] = {
        { "经典 · 美式清音（Twin 风格）",
          R"(<Chain>
  <Slot kind="builtin" id="comp"><Params threshold="-26" ratio="3" attack="15" release="180" makeup="4"/></Slot>
  <Slot kind="builtin" id="amp"><Params voicing="3" gain="3" bass="5" mid="5" treble="6" presence="5.5" master="-6" bright="1" cab="1"/></Slot>
  <Slot kind="builtin" id="chorus" bypass="1"><Params rate="0.6" depth="0.3" mix="0.4"/></Slot>
  <Slot kind="builtin" id="reverb"><Params type="2" size="0.5" damping="0.4" predelay="10" mix="0.25"/></Slot>
</Chain>)" },
        { "经典 · 黑面布鲁斯（Deluxe + 人马）",
          R"(<Chain>
  <Slot kind="builtin" id="overdrive"><Params model="3" drive="10" tone="0.55" level="-4"/></Slot>
  <Slot kind="builtin" id="amp"><Params voicing="4" gain="5" bass="5" mid="5.5" treble="5.5" presence="5" master="-6" cab="1"/></Slot>
  <Slot kind="builtin" id="reverb"><Params type="0" size="0.45" damping="0.5" mix="0.2"/></Slot>
</Chain>)" },
        { "经典 · 英式 Plexi 摇滚",
          R"(<Chain>
  <Slot kind="builtin" id="gate"><Params threshold="-62" release="90"/></Slot>
  <Slot kind="builtin" id="amp"><Params voicing="5" gain="7" bass="5" mid="6" treble="6" presence="6" master="-8" cab="1"/></Slot>
  <Slot kind="builtin" id="reverb"><Params type="1" size="0.5" damping="0.55" mix="0.15"/></Slot>
</Chain>)" },
        { "经典 · JCM800 + TS808",
          R"(<Chain>
  <Slot kind="builtin" id="gate"><Params threshold="-56" release="70"/></Slot>
  <Slot kind="builtin" id="overdrive"><Params model="1" drive="4" tone="0.6" level="2"/></Slot>
  <Slot kind="builtin" id="amp"><Params voicing="6" gain="7.5" bass="5" mid="6.5" treble="5.5" presence="6" master="-9" cab="1"/></Slot>
  <Slot kind="builtin" id="delay" bypass="1"><Params time="380" feedback="0.3" mix="0.2" type="2"/></Slot>
  <Slot kind="builtin" id="reverb"><Params type="1" size="0.45" damping="0.6" mix="0.14"/></Slot>
</Chain>)" },
        { "经典 · AC30 Top Boost",
          R"(<Chain>
  <Slot kind="builtin" id="amp"><Params voicing="7" gain="5" bass="4.5" mid="5" treble="6.5" presence="5" master="-7" cab="1"/></Slot>
  <Slot kind="builtin" id="tremolo" bypass="1"><Params rate="5" depth="0.45"/></Slot>
  <Slot kind="builtin" id="delay"><Params time="360" feedback="0.25" mix="0.18" type="1"/></Slot>
  <Slot kind="builtin" id="reverb"><Params type="0" size="0.5" damping="0.45" mix="0.2"/></Slot>
</Chain>)" },
        { "经典 · Mesa Mark 主音",
          R"(<Chain>
  <Slot kind="builtin" id="gate"><Params threshold="-54" release="60"/></Slot>
  <Slot kind="builtin" id="amp"><Params voicing="8" gain="7" bass="4" mid="6" treble="5.5" presence="5.5" master="-10" cab="1"/></Slot>
  <Slot kind="builtin" id="delay"><Params time="420" feedback="0.35" mix="0.24" type="2" pingpong="1"/></Slot>
  <Slot kind="builtin" id="reverb"><Params type="1" size="0.55" damping="0.5" mix="0.18"/></Slot>
</Chain>)" },
        { "经典 · 双整流金属（TS9 推）",
          R"(<Chain>
  <Slot kind="builtin" id="gate"><Params threshold="-50" release="45"/></Slot>
  <Slot kind="builtin" id="overdrive"><Params model="2" drive="2" tone="0.6" level="4"/></Slot>
  <Slot kind="builtin" id="amp"><Params voicing="9" gain="7.5" bass="6" mid="3.5" treble="6" presence="6" master="-11" cab="1"/></Slot>
  <Slot kind="builtin" id="reverb"><Params type="0" size="0.35" damping="0.6" mix="0.08"/></Slot>
</Chain>)" },
        { "经典 · 5150 高增益（TS808 推）",
          R"(<Chain>
  <Slot kind="builtin" id="gate"><Params threshold="-50" release="45"/></Slot>
  <Slot kind="builtin" id="overdrive"><Params model="1" drive="0" tone="0.55" level="6"/></Slot>
  <Slot kind="builtin" id="amp"><Params voicing="10" gain="7" bass="5.5" mid="5" treble="5.5" presence="6" master="-11" cab="1"/></Slot>
  <Slot kind="builtin" id="reverb"><Params type="0" size="0.35" damping="0.6" mix="0.08"/></Slot>
</Chain>)" },
        { "经典 · RAT 进清音箱头",
          R"(<Chain>
  <Slot kind="builtin" id="distortion"><Params model="1" gain="30" tone="0.45" level="-12" mid="0" tight="120"/></Slot>
  <Slot kind="builtin" id="amp"><Params voicing="3" gain="3" bass="5" mid="5.5" treble="5" presence="5" master="-6" cab="1"/></Slot>
  <Slot kind="builtin" id="reverb"><Params type="0" size="0.4" damping="0.5" mix="0.14"/></Slot>
</Chain>)" },
        { "经典 · DS-1 + Plexi",
          R"(<Chain>
  <Slot kind="builtin" id="gate"><Params threshold="-58" release="70"/></Slot>
  <Slot kind="builtin" id="distortion"><Params model="2" gain="28" tone="0.5" level="-12" mid="0" tight="150"/></Slot>
  <Slot kind="builtin" id="amp"><Params voicing="5" gain="3.5" bass="5" mid="6" treble="5.5" presence="5.5" master="-6" cab="1"/></Slot>
  <Slot kind="builtin" id="reverb"><Params type="1" size="0.45" damping="0.55" mix="0.14"/></Slot>
</Chain>)" },

        // ---- 真实采样（NAM）。没带箱体的模型后面接内置箱体模拟 ----
        { "真实采样 · 5150 高增益",
          R"(<Chain>
  <Slot kind="builtin" id="gate"><Params threshold="-52" release="45"/></Slot>
  <Slot kind="nam" path="@app/assets/nam/community/Helga B 5150 BlockLetter - Boosted.nam"><Params output="4"/></Slot>
  <Slot kind="builtin" id="cab"><Params type="2" mic="0.35"/></Slot>
  <Slot kind="builtin" id="reverb"><Params type="0" size="0.35" damping="0.6" mix="0.08"/></Slot>
</Chain>)" },
        { "真实采样 · 5150 III + TS9",
          R"(<Chain>
  <Slot kind="builtin" id="gate"><Params threshold="-52" release="45"/></Slot>
  <Slot kind="nam" path="@app/assets/nam/community/Tim R 5152 Lead + TS9.nam"><Params output="-3"/></Slot>
  <Slot kind="builtin" id="cab"><Params type="2" mic="0.3"/></Slot>
  <Slot kind="builtin" id="reverb"><Params type="0" size="0.35" damping="0.6" mix="0.08"/></Slot>
</Chain>)" },
        { "真实采样 · Mesa Mark IV",
          R"(<Chain>
  <Slot kind="builtin" id="gate"><Params threshold="-54" release="60"/></Slot>
  <Slot kind="nam" path="@app/assets/nam/community/Roman A LT_MESA_MARKIV_1.nam"><Params output="-10.5"/></Slot>
  <Slot kind="builtin" id="cab"><Params type="2" mic="0.3"/></Slot>
  <Slot kind="builtin" id="delay"><Params time="420" feedback="0.3" mix="0.2" type="2"/></Slot>
</Chain>)" },
        { "真实采样 · Fender Twin 清音",
          R"(<Chain>
  <Slot kind="builtin" id="comp"><Params threshold="-26" ratio="3" attack="15" release="180" makeup="3"/></Slot>
  <Slot kind="nam" path="@app/assets/nam/community/Tim R Fender TwinVerb Norm Bright.nam"><Params output="12"/></Slot>
  <Slot kind="builtin" id="cab"><Params type="1" mic="0.4" bright="0.6" level="5"/></Slot>
  <Slot kind="builtin" id="reverb"><Params type="2" size="0.5" damping="0.4" mix="0.25"/></Slot>
</Chain>)" },
        { "真实采样 · Klon 克隆 + Twin",
          R"(<Chain>
  <Slot kind="nam" path="@app/assets/nam/community/Keith B klone_g6_t6_o5.nam"><Params output="0"/></Slot>
  <Slot kind="nam" path="@app/assets/nam/community/Tim R Fender TwinVerb Norm Bright.nam"><Params output="12"/></Slot>
  <Slot kind="builtin" id="cab"><Params type="1" mic="0.4" level="3"/></Slot>
  <Slot kind="builtin" id="reverb"><Params type="0" size="0.45" damping="0.5" mix="0.2"/></Slot>
</Chain>)" },
        { "真实采样 · OD808 + JCM2000",
          R"(<Chain>
  <Slot kind="builtin" id="gate"><Params threshold="-56" release="70"/></Slot>
  <Slot kind="nam" path="@app/assets/nam/community/Phillipe P Maxon-OD808-Feather-ESR0,001.nam"><Params output="2"/></Slot>
  <Slot kind="nam" path="@app/assets/nam/community/Tim R JCM2000 Crunch.nam"><Params output="-4"/></Slot>
  <Slot kind="builtin" id="cab"><Params type="2" mic="0.3"/></Slot>
  <Slot kind="builtin" id="reverb"><Params type="1" size="0.45" damping="0.6" mix="0.12"/></Slot>
</Chain>)" },
        { "真实采样 · Marshall JVM（含箱体）",
          R"(<Chain>
  <Slot kind="builtin" id="gate"><Params threshold="-54" release="60"/></Slot>
  <Slot kind="nam" path="@app/assets/nam/community/Phillipe P JVM-OD2-RD-NoDrive-Cab-ESR0,006.nam"><Params output="-1"/></Slot>
  <Slot kind="builtin" id="reverb"><Params type="1" size="0.45" damping="0.6" mix="0.12"/></Slot>
</Chain>)" },
        { "真实采样 · Vox AC15",
          R"(<Chain>
  <Slot kind="nam" path="@app/assets/nam/community/Phillipe P VOXAC15-TopBoost.nam"><Params output="4"/></Slot>
  <Slot kind="builtin" id="cab"><Params type="1" mic="0.35" bright="0.6"/></Slot>
  <Slot kind="builtin" id="delay"><Params time="360" feedback="0.25" mix="0.18" type="1"/></Slot>
  <Slot kind="builtin" id="reverb"><Params type="0" size="0.5" damping="0.45" mix="0.2"/></Slot>
</Chain>)" },
    };

    // 第 3 版：先锋 / 后摇 / 迷幻 / 后朋克 / 怪异音色，和吉他变贝斯
    inline const char* listV3[][2] = {
        { "先锋 · Gilmour 法兹 Solo（Comfortably Numb 风）",
          R"(<Chain>
  <Slot kind="builtin" id="gate"><Params threshold="-60" release="120"/></Slot>
  <Slot kind="builtin" id="comp"><Params threshold="-28" ratio="3" attack="20" release="200" makeup="3"/></Slot>
  <Slot kind="builtin" id="fuzz"><Params fuzz="0.62" tone="0.42" level="-14" bias="0.12"/></Slot>
  <Slot kind="builtin" id="amp"><Params voicing="3" gain="4" bass="5" mid="6" treble="5.5" presence="5" master="-8" cab="1"/></Slot>
  <Slot kind="builtin" id="delay"><Params time="480" feedback="0.38" mix="0.26" tone="3800" type="1" mod="0.2"/></Slot>
  <Slot kind="builtin" id="reverb"><Params type="1" size="0.75" damping="0.45" predelay="30" mix="0.26"/></Slot>
</Chain>)" },
        { "先锋 · Gilmour 清音旋转音箱（Breathe 风）",
          R"(<Chain>
  <Slot kind="builtin" id="comp"><Params threshold="-30" ratio="4" attack="15" release="200" makeup="5"/></Slot>
  <Slot kind="builtin" id="amp"><Params voicing="3" gain="2.5" bass="4.5" mid="5" treble="6.5" presence="6" master="-6" bright="1" cab="1"/></Slot>
  <Slot kind="builtin" id="vibrato"><Params rate="5.5" depth="0.18" mix="0.55"/></Slot>
  <Slot kind="builtin" id="chorus"><Params rate="0.5" depth="0.3" mix="0.3"/></Slot>
  <Slot kind="builtin" id="delay"><Params time="440" feedback="0.3" mix="0.22" tone="4000" type="1"/></Slot>
  <Slot kind="builtin" id="reverb"><Params type="1" size="0.7" damping="0.4" predelay="25" mix="0.3"/></Slot>
</Chain>)" },
        { "先锋 · 附点八分延迟（Run Like Hell 风）",
          R"(<Chain>
  <Slot kind="builtin" id="comp"><Params threshold="-28" ratio="4" attack="8" release="150" makeup="5"/></Slot>
  <Slot kind="builtin" id="amp"><Params voicing="3" gain="3" bass="4" mid="5" treble="6.5" presence="6" master="-6" bright="1" cab="1"/></Slot>
  <Slot kind="builtin" id="delay"><Params time="320" feedback="0.32" mix="0.45" tone="5000" type="0" sync="2"/></Slot>
  <Slot kind="builtin" id="reverb"><Params type="0" size="0.4" damping="0.5" mix="0.15"/></Slot>
</Chain>)" },
        { "后摇 · 渐强音墙（MONO、Explosions 风）",
          R"(<Chain>
  <Slot kind="builtin" id="gate"><Params threshold="-62" release="150"/></Slot>
  <Slot kind="builtin" id="overdrive"><Params model="3" drive="14" tone="0.55" level="-4"/></Slot>
  <Slot kind="builtin" id="amp"><Params voicing="7" gain="6" bass="5" mid="5.5" treble="6" presence="5.5" master="-10" cab="1"/></Slot>
  <Slot kind="builtin" id="delay"><Params time="620" feedback="0.55" mix="0.32" tone="3200" type="1" mod="0.3"/></Slot>
  <Slot kind="builtin" id="shimmer"><Params size="0.9" shimmer="0.35" interval="0" mix="0.3"/></Slot>
  <Slot kind="builtin" id="reverb"><Params type="1" size="0.92" damping="0.35" predelay="40" mix="0.35"/></Slot>
</Chain>)" },
        { "后摇 · 清音氛围轮指（Mogwai 风）",
          R"(<Chain>
  <Slot kind="builtin" id="comp"><Params threshold="-30" ratio="4" attack="5" release="180" makeup="5" blend="0.3"/></Slot>
  <Slot kind="builtin" id="amp"><Params voicing="3" gain="2.5" bass="5" mid="4.5" treble="6" presence="5.5" master="-6" cab="1"/></Slot>
  <Slot kind="builtin" id="chorus"><Params rate="0.3" depth="0.25" mix="0.3"/></Slot>
  <Slot kind="builtin" id="delay"><Params time="500" feedback="0.45" mix="0.3" tone="3500" type="1" sync="1" mod="0.25"/></Slot>
  <Slot kind="builtin" id="reverb"><Params type="1" size="0.95" damping="0.3" predelay="50" mix="0.45"/></Slot>
</Chain>)" },
        { "迷幻 · 高八度法兹 + 揉音（Hendrix 风）",
          R"(<Chain>
  <Slot kind="builtin" id="octave"><Params mode="1" dry="0.7" down="0" up="0.75" down2="0"/></Slot>
  <Slot kind="builtin" id="fuzz"><Params fuzz="0.75" tone="0.55" level="-16" bias="0.2"/></Slot>
  <Slot kind="builtin" id="vibrato"><Params rate="1.3" depth="0.45" mix="0.6"/></Slot>
  <Slot kind="builtin" id="amp"><Params voicing="5" gain="5.5" bass="5" mid="6" treble="6" presence="6" master="-10" cab="1"/></Slot>
  <Slot kind="builtin" id="reverb"><Params type="0" size="0.5" damping="0.5" mix="0.2"/></Slot>
</Chain>)" },
        { "迷幻 · 慢相位泥沼（Small Stone 风）",
          R"(<Chain>
  <Slot kind="builtin" id="fuzz"><Params fuzz="0.45" tone="0.45" level="-12" bias="0.15"/></Slot>
  <Slot kind="builtin" id="phaser"><Params rate="0.35" depth="0.85" feedback="0.65" stages="0" mix="0.6"/></Slot>
  <Slot kind="builtin" id="amp"><Params voicing="4" gain="4.5" bass="5" mid="5.5" treble="5.5" presence="5" master="-8" cab="1"/></Slot>
  <Slot kind="builtin" id="reverb"><Params type="2" size="0.6" damping="0.4" mix="0.28"/></Slot>
</Chain>)" },
        { "迷幻 · 太空回授（磁带延迟自激）",
          R"(<Chain>
  <Slot kind="builtin" id="overdrive"><Params model="0" drive="12" tone="0.5" level="-6"/></Slot>
  <Slot kind="builtin" id="amp"><Params voicing="7" gain="5" bass="5" mid="5" treble="5.5" presence="5" master="-9" cab="1"/></Slot>
  <Slot kind="builtin" id="flanger"><Params rate="0.08" depth="0.5" feedback="0.4" mix="0.35"/></Slot>
  <Slot kind="builtin" id="delay"><Params time="720" feedback="0.78" mix="0.38" tone="2400" type="1" mod="0.55"/></Slot>
  <Slot kind="builtin" id="reverb"><Params type="1" size="0.9" damping="0.4" predelay="20" mix="0.35"/></Slot>
</Chain>)" },
        { "后朋克 · 冷清合唱（Joy Division 风）",
          R"(<Chain>
  <Slot kind="builtin" id="comp"><Params threshold="-26" ratio="4" attack="6" release="150" makeup="4"/></Slot>
  <Slot kind="builtin" id="amp"><Params voicing="7" gain="3.5" bass="4" mid="4.5" treble="7" presence="6.5" master="-7" bright="1" cab="1"/></Slot>
  <Slot kind="builtin" id="eq"><Params low="-3" mid="-3" midFreq="500" high="3" level="1"/></Slot>
  <Slot kind="builtin" id="chorus"><Params rate="0.9" depth="0.5" mix="0.5"/></Slot>
  <Slot kind="builtin" id="reverb"><Params type="0" size="0.55" damping="0.6" predelay="10" mix="0.25"/></Slot>
</Chain>)" },
        { "后朋克 · 镶边清音（The Cure 风）",
          R"(<Chain>
  <Slot kind="builtin" id="comp"><Params threshold="-28" ratio="4" attack="8" release="160" makeup="5"/></Slot>
  <Slot kind="builtin" id="amp"><Params voicing="3" gain="3" bass="4.5" mid="5" treble="6.5" presence="6" master="-6" bright="1" cab="1"/></Slot>
  <Slot kind="builtin" id="flanger"><Params rate="0.18" depth="0.65" feedback="0.55" mix="0.5"/></Slot>
  <Slot kind="builtin" id="delay"><Params time="360" feedback="0.3" mix="0.22" tone="4500" type="2" sync="3"/></Slot>
  <Slot kind="builtin" id="reverb"><Params type="1" size="0.65" damping="0.45" mix="0.28"/></Slot>
</Chain>)" },
        { "后朋克 · 干硬切音（Gang of Four 风）",
          R"(<Chain>
  <Slot kind="builtin" id="gate"><Params threshold="-58" release="40"/></Slot>
  <Slot kind="builtin" id="comp"><Params threshold="-30" ratio="8" attack="2" release="80" makeup="6"/></Slot>
  <Slot kind="builtin" id="amp"><Params voicing="7" gain="4.5" bass="3" mid="6" treble="7" presence="7" master="-8" bright="1" cab="1"/></Slot>
  <Slot kind="builtin" id="eq"><Params low="-5" mid="4" midFreq="1800" high="2" level="0"/></Slot>
</Chain>)" },
        { "后朋克 · 刺耳失真 + 短回声（Bauhaus 风）",
          R"(<Chain>
  <Slot kind="builtin" id="gate"><Params threshold="-58" release="60"/></Slot>
  <Slot kind="builtin" id="distortion"><Params model="2" gain="30" tone="0.7" level="-14" mid="-5" tight="200"/></Slot>
  <Slot kind="builtin" id="amp"><Params voicing="6" gain="5" bass="4" mid="5" treble="6.5" presence="6.5" master="-10" cab="1"/></Slot>
  <Slot kind="builtin" id="delay"><Params time="120" feedback="0.15" mix="0.25" tone="3000" type="2"/></Slot>
  <Slot kind="builtin" id="reverb"><Params type="0" size="0.5" damping="0.5" mix="0.2"/></Slot>
</Chain>)" },
        { "怪异 · 低八度法兹（Seven Nation Army 风）",
          R"(<Chain>
  <Slot kind="builtin" id="gate"><Params threshold="-58" release="60"/></Slot>
  <Slot kind="builtin" id="pitch"><Params semis="-12" cents="0" mix="0.55"/></Slot>
  <Slot kind="builtin" id="fuzz"><Params fuzz="0.8" tone="0.5" level="-16" bias="0.25"/></Slot>
  <Slot kind="builtin" id="amp"><Params voicing="5" gain="5" bass="6" mid="5.5" treble="5" presence="5" master="-10" cab="1"/></Slot>
</Chain>)" },
        { "怪异 · 断音开关（Tom Morello 风，跟鼓机十六分）",
          R"(<Chain>
  <Slot kind="builtin" id="gate"><Params threshold="-56" release="50"/></Slot>
  <Slot kind="builtin" id="distortion"><Params model="1" gain="32" tone="0.55" level="-14" mid="-2" tight="160"/></Slot>
  <Slot kind="builtin" id="amp"><Params voicing="9" gain="5.5" bass="5" mid="5.5" treble="5.5" presence="5.5" master="-11" cab="1"/></Slot>
  <Slot kind="builtin" id="tremolo"><Params rate="8" depth="1" wave="2" sync="4"/></Slot>
</Chain>)" },
        { "怪异 · 自动哇哇 + 门限法兹（撕裂声）",
          R"(<Chain>
  <Slot kind="builtin" id="wah"><Params mode="1" sensitivity="0.7" resonance="7" mix="1"/></Slot>
  <Slot kind="builtin" id="fuzz"><Params fuzz="0.85" tone="0.6" level="-18" bias="0.85"/></Slot>
  <Slot kind="builtin" id="amp"><Params voicing="4" gain="4" bass="5" mid="6" treble="5.5" presence="5" master="-9" cab="1"/></Slot>
  <Slot kind="builtin" id="reverb"><Params type="0" size="0.35" damping="0.5" mix="0.15"/></Slot>
</Chain>)" },
        { "怪异 · 喷气机镶边起飞",
          R"(<Chain>
  <Slot kind="builtin" id="overdrive"><Params model="1" drive="18" tone="0.6" level="-6"/></Slot>
  <Slot kind="builtin" id="amp"><Params voicing="5" gain="6" bass="5" mid="6" treble="6" presence="6" master="-10" cab="1"/></Slot>
  <Slot kind="builtin" id="flanger"><Params rate="0.1" depth="0.9" feedback="0.9" manual="1.5" mix="0.55"/></Slot>
  <Slot kind="builtin" id="reverb"><Params type="1" size="0.5" damping="0.5" mix="0.18"/></Slot>
</Chain>)" },
        { "怪异 · 空灵五度和声",
          R"(<Chain>
  <Slot kind="builtin" id="comp"><Params threshold="-28" ratio="4" attack="10" release="200" makeup="5"/></Slot>
  <Slot kind="builtin" id="pitch"><Params semis="7" cents="0" mix="0.4"/></Slot>
  <Slot kind="builtin" id="amp"><Params voicing="3" gain="2.5" bass="4.5" mid="5" treble="6" presence="5.5" master="-6" cab="1"/></Slot>
  <Slot kind="builtin" id="shimmer"><Params size="0.9" shimmer="0.5" interval="1" mix="0.45"/></Slot>
  <Slot kind="builtin" id="reverb"><Params type="1" size="0.9" damping="0.35" predelay="40" mix="0.35"/></Slot>
</Chain>)" },
        { "怪异 · 破收音机 Lo-Fi",
          R"(<Chain>
  <Slot kind="builtin" id="distortion"><Params model="0" gain="22" tone="0.4" level="-14" mid="4" tight="350"/></Slot>
  <Slot kind="builtin" id="geq"><Params b0="-12" b1="-10" b2="-2" b3="6" b4="5" b5="-8" b6="-12" level="3"/></Slot>
  <Slot kind="builtin" id="tremolo"><Params rate="9" depth="0.25" wave="1"/></Slot>
  <Slot kind="builtin" id="cab"><Params type="0" lowcut="200" bright="0.1" resonance="0" mic="0.8" level="0"/></Slot>
  <Slot kind="builtin" id="reverb"><Params type="0" size="0.25" damping="0.7" mix="0.12"/></Slot>
</Chain>)" },
        { "贝斯 · 吉他变贝斯（指弹）",
          R"(<Chain>
  <Slot kind="builtin" id="gate"><Params threshold="-62" release="120"/></Slot>
  <Slot kind="builtin" id="comp"><Params threshold="-30" ratio="5" attack="12" release="180" makeup="6"/></Slot>
  <Slot kind="builtin" id="octave"><Params mode="0" dry="0" down="1" up="0" down2="0"/></Slot>
  <Slot kind="builtin" id="eq"><Params low="6" mid="-3" midFreq="450" high="-9" level="4" midQ="0.8"/></Slot>
  <Slot kind="builtin" id="comp"><Params threshold="-24" ratio="3" attack="20" release="200" makeup="3"/></Slot>
  <Slot kind="builtin" id="reverb" bypass="1"><Params type="0" size="0.2" damping="0.7" mix="0.08"/></Slot>
</Chain>)" },
        { "贝斯 · 吉他变失真贝斯（单音合成味）",
          R"(<Chain>
  <Slot kind="builtin" id="gate"><Params threshold="-58" release="80"/></Slot>
  <Slot kind="builtin" id="comp"><Params threshold="-28" ratio="6" attack="8" release="150" makeup="5"/></Slot>
  <Slot kind="builtin" id="octave"><Params mode="1" dry="0" down="1" up="0" down2="0"/></Slot>
  <Slot kind="builtin" id="overdrive"><Params model="0" drive="14" tone="0.35" level="-4" bass="6" blend="0.5"/></Slot>
  <Slot kind="builtin" id="eq"><Params low="7" mid="1" midFreq="700" high="-8" level="0"/></Slot>
</Chain>)" },
    };

    // 第 4 版：Gervana 系列（排在所有预设最前面）
    inline const char* listGervana[][2] = {
        { "Gervana · 戊寅 法兹主音（Pi 时代）",
          R"(<Chain>
  <Slot kind="builtin" id="gate"><Params threshold="-60" release="120"/></Slot>
  <Slot kind="builtin" id="gv_wuyin"><Params volume="-10" sustain="0.7" tone="0.55" balance="0.7" mode="0" voice="1" filter="0" toneBypass="0"/></Slot>
  <Slot kind="builtin" id="amp"><Params voicing="3" gain="3.5" bass="5" mid="6" treble="5.5" presence="5" master="-8" cab="1"/></Slot>
  <Slot kind="builtin" id="delay"><Params time="460" feedback="0.35" mix="0.24" tone="3800" type="1" mod="0.2"/></Slot>
  <Slot kind="builtin" id="reverb"><Params type="1" size="0.7" damping="0.45" predelay="25" mix="0.24"/></Slot>
</Chain>)" },
        { "Gervana · 戊寅 俄产厚墙（后摇、盯鞋）",
          R"(<Chain>
  <Slot kind="builtin" id="gate"><Params threshold="-62" release="150"/></Slot>
  <Slot kind="builtin" id="gv_wuyin"><Params volume="-10" sustain="0.85" tone="0.4" balance="0.6" mode="0" voice="0" filter="0" toneBypass="0"/></Slot>
  <Slot kind="builtin" id="amp"><Params voicing="7" gain="4" bass="5" mid="5" treble="6" presence="5.5" master="-9" cab="1"/></Slot>
  <Slot kind="builtin" id="delay"><Params time="620" feedback="0.5" mix="0.3" tone="3000" type="1" mod="0.35"/></Slot>
  <Slot kind="builtin" id="reverb"><Params type="1" size="0.92" damping="0.35" predelay="40" mix="0.38"/></Slot>
</Chain>)" },
        { "Gervana · 戊寅 动态法兹 + 复古 Vox 中频",
          R"(<Chain>
  <Slot kind="builtin" id="gv_wuyin"><Params volume="-8" sustain="0.6" tone="0.5" balance="0.8" mode="1" voice="2" filter="1" toneBypass="0"/></Slot>
  <Slot kind="builtin" id="amp"><Params voicing="7" gain="3.5" bass="4.5" mid="5.5" treble="6" presence="5.5" master="-8" cab="1"/></Slot>
  <Slot kind="builtin" id="reverb"><Params type="0" size="0.45" damping="0.5" mix="0.18"/></Slot>
</Chain>)" },
        { "Gervana · 丙子 奶油过载推 JCM800",
          R"(<Chain>
  <Slot kind="builtin" id="gate"><Params threshold="-56" release="70"/></Slot>
  <Slot kind="builtin" id="gv_bingzi"><Params level="2" tone="0.55" gain="0.2" blend="1" tight="180" style="0" clip="0"/></Slot>
  <Slot kind="builtin" id="amp"><Params voicing="6" gain="7" bass="5" mid="6.5" treble="5.5" presence="6" master="-10" cab="1"/></Slot>
  <Slot kind="builtin" id="reverb"><Params type="1" size="0.45" damping="0.6" mix="0.14"/></Slot>
</Chain>)" },
        { "Gervana · 丙子 Timmy 透明推清音",
          R"(<Chain>
  <Slot kind="builtin" id="gv_bingzi"><Params level="0" tone="0.6" gain="0.35" blend="0.7" tight="100" style="2" clip="2"/></Slot>
  <Slot kind="builtin" id="amp"><Params voicing="4" gain="4" bass="5" mid="5.5" treble="5.5" presence="5" master="-7" cab="1"/></Slot>
  <Slot kind="builtin" id="reverb"><Params type="2" size="0.5" damping="0.45" mix="0.2"/></Slot>
</Chain>)" },
        { "Gervana · 戊午 Plexi 跳线全开（前级直出）",
          R"(<Chain>
  <Slot kind="builtin" id="gate"><Params threshold="-60" release="90"/></Slot>
  <Slot kind="builtin" id="gv_wuwu"><Params level="-10" tone="0.55" presence="0.55" brightGain="0.8" normalGain="0.5" voice="0" clip="2" output="1"/></Slot>
  <Slot kind="builtin" id="reverb"><Params type="1" size="0.5" damping="0.55" mix="0.15"/></Slot>
</Chain>)" },
        { "Gervana · 乙酉 Brown Sound 主音",
          R"(<Chain>
  <Slot kind="builtin" id="gate"><Params threshold="-54" release="60"/></Slot>
  <Slot kind="builtin" id="gv_yiyou"><Params channel="1" gain="7" volume="-10" low="5" mid="6.5" high="5.5" presence="5.5" resonance="1" output="1"/></Slot>
  <Slot kind="builtin" id="delay"><Params time="380" feedback="0.3" mix="0.18" tone="3500" type="2"/></Slot>
  <Slot kind="builtin" id="reverb"><Params type="1" size="0.55" damping="0.55" mix="0.16"/></Slot>
</Chain>)" },
        { "Gervana · 乙酉 清音通道 + 甲辰压缩",
          R"(<Chain>
  <Slot kind="builtin" id="gv_jiachen"><Params level="4" sustain="0.5" attack="0.5" blend="0.7" trebleBoost="0" tubeSim="1"/></Slot>
  <Slot kind="builtin" id="gv_yiyou"><Params channel="0" gain="5" volume="-6" low="5" mid="5" high="6" presence="5.5" resonance="1" output="1"/></Slot>
  <Slot kind="builtin" id="chorus"><Params rate="0.6" depth="0.3" mix="0.3"/></Slot>
  <Slot kind="builtin" id="reverb"><Params type="1" size="0.6" damping="0.45" mix="0.22"/></Slot>
</Chain>)" },
        { "Gervana · 乙卯 AC30 玻璃清音",
          R"(<Chain>
  <Slot kind="builtin" id="gv_jiachen"><Params level="3" sustain="0.4" attack="0.6" blend="0.6" trebleBoost="1" tubeSim="0"/></Slot>
  <Slot kind="builtin" id="gv_yimao"><Params channel="1" normal="5" brilliant="5" bass="4.5" treble="6.5" tonecut="2" master="-8" output="1"/></Slot>
  <Slot kind="builtin" id="delay"><Params time="360" feedback="0.28" mix="0.2" tone="4500" type="1"/></Slot>
  <Slot kind="builtin" id="reverb"><Params type="2" size="0.55" damping="0.4" mix="0.22"/></Slot>
</Chain>)" },
        { "Gervana · 乙卯 AC30 推满（丙子推）",
          R"(<Chain>
  <Slot kind="builtin" id="gate"><Params threshold="-58" release="80"/></Slot>
  <Slot kind="builtin" id="gv_bingzi"><Params level="1" tone="0.5" gain="0.3" blend="0.9" tight="150" style="2" clip="1"/></Slot>
  <Slot kind="builtin" id="gv_yimao"><Params channel="2" normal="6" brilliant="8" bass="5" treble="6" tonecut="4" master="-12" output="1"/></Slot>
  <Slot kind="builtin" id="reverb"><Params type="0" size="0.45" damping="0.5" mix="0.16"/></Slot>
</Chain>)" },
        { "Gervana · 全家桶（甲辰 → 丙子 → 戊午）",
          R"(<Chain>
  <Slot kind="builtin" id="gate"><Params threshold="-58" release="80"/></Slot>
  <Slot kind="builtin" id="gv_jiachen"><Params level="2" sustain="0.35" attack="0.6" blend="0.6" trebleBoost="0" tubeSim="1"/></Slot>
  <Slot kind="builtin" id="gv_bingzi"><Params level="0" tone="0.5" gain="0.3" blend="1" tight="160" style="0" clip="0"/></Slot>
  <Slot kind="builtin" id="gv_wuwu"><Params level="-12" tone="0.5" presence="0.5" brightGain="0.6" normalGain="0.35" voice="2" clip="2" output="1"/></Slot>
  <Slot kind="builtin" id="delay"><Params time="420" feedback="0.3" mix="0.2" tone="3800" type="2"/></Slot>
  <Slot kind="builtin" id="reverb"><Params type="1" size="0.55" damping="0.55" mix="0.18"/></Slot>
</Chain>)" },
    };

    constexpr int version = 4;
}
