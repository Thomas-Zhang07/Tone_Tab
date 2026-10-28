# Tone_Tab naming policy

**Rule: every path we create is ASCII.** No Chinese (or any non-ASCII) characters in
file names, directory names, preset file names, or app-data paths. Localized names are
**display metadata**, stored inside the file (XML/JSON), never in the path.

Why:

- Build tools, `subst`, scripts, zip packaging, git on other machines, and CI all behave
  better with ASCII paths.
- Users can change the UI language without the files changing.
- Two people on different systems can share presets without encoding problems.

Third-party asset file names (e.g. `.nam` captures) keep their original spelling so
provenance/attribution stays intact. All of the bundled ones are already ASCII.

---

## 1. Repository layout

```
Tone_Tab/
  CMakeLists.txt
  build.bat / build.ps1
  package.ps1
  README.md
  NAMING.md
  LICENSE
  docs/
    user_guide.en.md
    user_guide.zh.md
    legacy_name_map.csv        # 1:1 old -> new names during migration
  Source/
    ...                        # existing English source tree, unchanged
  tests/
  licenses/
    THIRD_PARTY_NOTICES.txt    # was licenses/第三方声明.txt
  third_party/
    JUCE-9.0.4.zip
    NAM-core.zip
```

The source root itself is renamed from `老干部活动中心-源码` to the repo root
(`Tone_Tab`). No nested Chinese directory.

## 2. Runtime folder layout (next to the executable)

| Old (LianqinHost)             | New (Tone_Tab)                   | Contents |
| ----------------------------- | -------------------------------- | -------- |
| `音色库/`                     | `assets/`                        | tone assets root |
| `音色库/NAM/`                 | `assets/nam/`                    | `.nam` amp models |
| `音色库/NAM/1-清音/` etc.     | `assets/nam/index.json` tags     | `clean`, `crunch`, `high_gain`, `boost` become metadata, not folders |
| `音色库/NAM/说明.txt`         | `assets/nam/README.md`           | folder notes |
| `音色库/IR/`                  | `assets/ir/`                     | cabinet IRs |
| `音色库/Gervana/`             | `assets/gervana/`                | effect artwork |
| `音色库/Gervana/图片放这里.txt` | `assets/gervana/PUT_IMAGES_HERE.txt` | |
| `音色预设/`                   | `presets/`                       | presets shipped with the app |
| `默认设置/`                   | `defaults/`                      | first-run settings bundle |
| `默认设置/LianqinHost.settings` | `defaults/Tone_Tab.settings`   | |
| `吉他谱/`                     | `scores/`                        | user score files |
| `吉他谱/把吉他谱放这里.txt`   | `scores/PUT_SCORES_HERE.txt`     | |
| `许可证/`                     | `licenses/`                      | third-party licenses |
| `录音/`                       | `recordings/`                    | recordings (if stored next to exe) |
| `使用说明.txt`                | `docs/user_guide.en.md` + `.zh.md` | |

New folders introduced by Tone_Tab use the same style: lowercase, `snake_case`,
no spaces.

### Merged model library

Both source collections go into one deduplicated library. Hashing shows that the 24
`starter` captures and all 18 `otherproject` captures are byte-identical copies of
files already in `community-pack`; the merged library is therefore:

```
assets/nam/
  community/       259 unique A1 (WaveNet) captures, GPL-3.0
  tone3000_a2/      11 unique A2 (SlimmableContainer) full-rig captures
  index.json        catalog: hash, metadata, tags, and aliases
  LICENSES/         per-collection license + notice files
```

- **Physical files: 270** (259 + 11), 106 MB.
- The curated `starter` names and the `otherproject` category names are kept as
  **aliases and tags** in `index.json` — no duplicate copies on disk.
- Category folders (`clean`, `crunch`, `high_gain`, `boost`) are tags in the index;
  the browser groups by tag, not by directory.
- A2 models are tagged `full_rig` and must never be paired with a cab IR.

Rebuild the library at any time with `tools/import_assets.ps1` (idempotent).

`assets/` is **git-ignored** and must never be pushed to a remote. The tones stay
local; only code, build scripts, and the import tool are versioned.

## 3. App data (user profile) layout

| Old                                                     | New |
| ------------------------------------------------------- | --- |
| `%APPDATA%\LianqinHost\LianqinHost.settings`            | `%APPDATA%\Tone_Tab\settings.xml` |
| `%APPDATA%\LianqinHost\练习记录.json`                   | `%APPDATA%\Tone_Tab\practice_log.json` |
| `Documents\LianqinHost\音色预设\`                       | `Documents\Tone_Tab\presets\` |
| `Documents\LianqinHost\录音\`                           | `Documents\Tone_Tab\recordings\` |

One-time import: if the old folders exist and the new settings file does not, copy
presets/recordings and offer to migrate the settings. Never write to the old folders.

## 4. Presets: ASCII file name, localized display name

Preset **file names** are ASCII slugs. The **display name** lives inside the XML:

```xml
<Preset version="5"
        name="Bingzi · Transparent clean boost"
        name_zh="丙子 · 透明推清音"
        category="gervana">
```

`PresetBar` reads `name` / `name_zh` instead of deriving the label from the file name.
Parameter IDs stay unchanged, so existing chains still load.

Naming pattern: `<category>_<name>.xml`, lowercase `snake_case`. Categories:
`clean`, `overdrive`, `high_gain`, `solo`, `classic`, `nam_capture`, `gervana`,
`pioneer`, `post_rock`, `post_punk`, `psychedelic`, `weird`, `bass`.

### Representative renames

| Old file name | New file name |
| --- | --- |
| `清音.xml` | `clean.xml` |
| `过载.xml` | `overdrive.xml` |
| `高增益.xml` | `high_gain.xml` |
| `Solo 1.xml` | `solo_1.xml` |
| `Solo 延迟.xml` | `solo_delay.xml` |
| `经典 · 5150 高增益（TS808 推）.xml` | `classic_5150_high_gain_ts808_boost.xml` |
| `经典 · 美式清音（Twin 风格）.xml` | `classic_american_clean_twin.xml` |
| `真实采样 · 5150 III + TS9.xml` | `nam_capture_5150_iii_ts9.xml` |
| `真实采样 · Fender Twin 清音.xml` | `nam_capture_fender_twin_clean.xml` |
| `Gervana · 丙子 Timmy 透明推清音.xml` | `gervana_bingzi_timmy_transparent_clean.xml` |
| `Gervana · 丙子 奶油过载推 JCM800.xml` | `gervana_bingzi_creamy_od_jcm800.xml` |
| `Gervana · 全家桶（甲辰 → 丙子 → 戊午）.xml` | `gervana_full_stack_jiachen_bingzi_wuwu.xml` |
| `Gervana · 戊午 Plexi 跳线全开（前级直出）.xml` | `gervana_wuwu_plexi_jumpered_direct.xml` |
| `Gervana · 戊寅 动态法兹 + 复古 Vox 中频.xml` | `gervana_wuyin_dynamic_fuzz_vox_mids.xml` |
| `Gervana · 戊寅 俄产厚墙（后摇、盯鞋）.xml` | `gervana_wuyin_russian_wall_shoegaze.xml` |
| `Gervana · 戊寅 法兹主音（Pi 时代）.xml` | `gervana_wuyin_fuzz_lead_pi.xml` |
| `Gervana · 乙卯 AC30 玻璃清音.xml` | `gervana_yimao_ac30_glass_clean.xml` |
| `Gervana · 乙卯 AC30 推满（丙子推）.xml` | `gervana_yimao_ac30_maxed_bingzi_boost.xml` |
| `Gervana · 乙酉 Brown Sound 主音.xml` | `gervana_yiyou_brown_sound_lead.xml` |
| `Gervana · 乙酉 清音通道 + 甲辰压缩.xml` | `gervana_yiyou_clean_channel_jiachen_comp.xml` |
| `先锋 · Gilmour 法兹 Solo（Comfortably Numb 风）.xml` | `pioneer_gilmour_fuzz_solo_comfortably_numb.xml` |
| `先锋 · Gilmour 清音旋转音箱（Breathe 风）.xml` | `pioneer_gilmour_clean_rotary_breathe.xml` |
| `先锋 · 附点八分延迟（Run Like Hell 风）.xml` | `pioneer_dotted_eighth_delay_run_like_hell.xml` |
| `后摇 · 清音氛围轮指（Mogwai 风）.xml` | `post_rock_clean_ambient_tremolo_mogwai.xml` |
| `后摇 · 渐强音墙（MONO、Explosions 风）.xml` | `post_rock_crescendo_wall_mono_explosions.xml` |
| `后朋克 · 冷清合唱（Joy Division 风）.xml` | `post_punk_cold_chorus_joy_division.xml` |
| `后朋克 · 刺耳失真 + 短回声（Bauhaus 风）.xml` | `post_punk_harsh_distortion_short_echo_bauhaus.xml` |
| `后朋克 · 干硬切音（Gang of Four 风）.xml` | `post_punk_dry_cutting_gang_of_four.xml` |
| `后朋克 · 镶边清音（The Cure 风）.xml` | `post_punk_flanged_clean_the_cure.xml` |
| `迷幻 · 高八度法兹 + 揉音（Hendrix 风）.xml` | `psychedelic_octave_up_fuzz_vibrato_hendrix.xml` |
| `迷幻 · 慢相位泥沼（Small Stone 风）.xml` | `psychedelic_slow_phaser_small_stone.xml` |
| `迷幻 · 太空回授（磁带延迟自激）.xml` | `psychedelic_space_feedback_tape_delay.xml` |
| `怪异 · 低八度法兹（Seven Nation Army 风）.xml` | `weird_octave_down_fuzz_seven_nation_army.xml` |
| `怪异 · 喷气机镶边起飞.xml` | `weird_jet_flanger_takeoff.xml` |
| `怪异 · 断音开关（Tom Morello 风，跟鼓机十六分）.xml` | `weird_stutter_switch_morello_16th.xml` |
| `怪异 · 破收音机 Lo-Fi.xml` | `weird_lo_fi_radio.xml` |
| `怪异 · 空灵五度和声.xml` | `weird_ethereal_fifth_harmony.xml` |
| `怪异 · 自动哇哇 + 门限法兹（撕裂声）.xml` | `weird_auto_wah_gated_fuzz.xml` |
| `贝斯 · 吉他变贝斯（指弹）.xml` | `bass_guitar_to_bass_fingerstyle.xml` |
| `贝斯 · 吉他变失真贝斯（单音合成味）.xml` | `bass_guitar_to_distorted_bass_mono_synth.xml` |

The complete 1:1 list is kept in `docs/legacy_name_map.csv` when the migration runs.

## 5. Code changes implied

- Add `AppPaths` (product name `Tone_Tab`, new folders above). Replace every hardcoded
  `LianqinHost` / Chinese folder literal in `MainComponent.cpp`, `PresetBar.cpp`,
  `Recorder.cpp`, `PracticeLog.cpp`, `PracticePanel.cpp`, `EffectChain.cpp`,
  `GervanaEffects.cpp`, `package.ps1`.
- Add a localization string table; all paths are ASCII keys, not translated strings.
- Preset XML gains `name` / `name_zh`; `PresetBar` displays the localized one.
- `package.ps1` stages the new ASCII layout and must fail the build if a non-ASCII path
  appears under the staged app assets.
- Add a CI/pre-commit check: no non-ASCII characters in any tracked path.

## 6. What may stay non-ASCII

Only **content**, not paths:

- Chinese text inside `docs/user_guide.zh.md`
- Chinese strings in the localization table
- Chinese comments in legacy source (can be translated over time)
- Original third-party file names, if they were non-ASCII to begin with
