#pragma once

#include "BuiltinEffect.h"
#include <vector>

struct BuiltinEffectInfo
{
    juce::String id;
    juce::String name;      // 中文名
    juce::String category;  // 菜单分组
};

// 所有内置效果器（按菜单顺序）
const std::vector<BuiltinEffectInfo>& getBuiltinEffectList();

// 按 id 创建，未知 id 返回 nullptr
std::unique_ptr<BuiltinEffect> createBuiltinEffect (const juce::String& id);

// ---- Gervana 系列（单独分类，排在最前面）---------------------------------------
namespace Gervana
{
    inline const juce::String category { "Gervana" };
    inline bool isGervana (const juce::String& id)        { return id.startsWith ("gv_"); }
    inline bool isGervanaPreset (const juce::String& name) { return name.startsWithIgnoreCase ("Gervana"); }

    // 醒目的品牌色：金色字，暗红底
    inline juce::Colour gold()   { return juce::Colour (0xffffc23a); }
    inline juce::Colour red()    { return juce::Colour (0xffd8322c); }

    struct Info { const char* id; const char* model; const char* tagline; };
    inline const Info infos[] = {
        { "gv_wuyin",   "52 Big Muff",          "Mythic fuzz voice" },
        { "gv_wuwu",    "56 Plexi",             "Plexi wall of sound" },
        { "gv_bingzi",  "32 Overdrive",         "Transparent drive" },
        { "gv_jiachen", "15 OTA Compressor",    "OTA compression and sparkle" },
        { "gv_yiyou",   "23 Brown Sound Preamp", "Brown sound preamp" },
        { "gv_yimao",   "26 British Crystal Preamp", "British crystal preamp" },
    };
    inline const Info* infoFor (const juce::String& id)
    {
        for (auto& i : infos) if (id == i.id) return &i;
        return nullptr;
    }

    // 效果器图片：放在 程序目录\音色库\Gervana\<id>.png（或 .jpg），没有就显示“图片待替换”的预留框
    juce::File imageFileFor (const juce::String& id);
    juce::Image imageFor (const juce::String& id);
}
