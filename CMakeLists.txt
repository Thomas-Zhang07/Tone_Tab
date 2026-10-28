cmake_minimum_required(VERSION 3.22)

# Statically link the MSVC runtime so a packaged build runs without the VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(Tone_Tab VERSION 0.4.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- JUCE ------------------------------------------------------------------
# Lookup order:
#   1. -DTONE_JUCE_DIR=<path>
#   2. third_party/JUCE (build.bat extracts third_party/JUCE-9.0.4.zip here)
#   3. FetchContent from GitHub
set(TONE_JUCE_DIR "" CACHE PATH "Path to a JUCE checkout")
set(TONE_JUCE_TAG "9.0.4")

if(TONE_JUCE_DIR)
    add_subdirectory("${TONE_JUCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE" EXCLUDE_FROM_ALL)
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/JUCE/CMakeLists.txt")
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/third_party/JUCE" "${CMAKE_BINARY_DIR}/JUCE" EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        ${TONE_JUCE_TAG}
        GIT_SHALLOW    ON)
    FetchContent_MakeAvailable(JUCE)
endif()

# ---- NAM (Neural Amp Modeler) core ------------------------------------------
# build.bat extracts third_party/NAM-core.zip to third_party/nam.
set(NAM_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/nam")
if(NOT EXISTS "${NAM_ROOT}/NAM/dsp.h")
    message(FATAL_ERROR "NAM sources not found: extract third_party/NAM-core.zip to third_party/nam")
endif()

file(GLOB NAM_SOURCES "${NAM_ROOT}/NAM/*.cpp" "${NAM_ROOT}/NAM/*/*.cpp")
# OBJECT library: NAM registers model types through static initialisers, which the
# linker drops from a static library.
add_library(nam_core OBJECT ${NAM_SOURCES})
target_include_directories(nam_core SYSTEM PUBLIC
    "${NAM_ROOT}"
    "${NAM_ROOT}/eigen"
    "${NAM_ROOT}/nlohmann")
target_compile_definitions(nam_core PUBLIC NAM_SAMPLE_FLOAT NAM_ENABLE_A2_FAST)
target_compile_features(nam_core PUBLIC cxx_std_20)
set_target_properties(nam_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_definitions(nam_core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(nam_core PRIVATE /bigobj /utf-8 "$<$<CONFIG:Release>:/O2>")
else()
    target_compile_options(nam_core PRIVATE -w "$<$<CONFIG:Release>:-O3>")
endif()

# ---- ToneCore: engine, effects, NAM, drums, songs, practice ------------------
set(TONE_CORE_SOURCES
    Source/app/AppPaths.cpp
    Source/i18n/Localisation.cpp
    Source/AudioEngine.cpp
    Source/fx/BuiltinEffects.cpp
    Source/fx/MoreEffects.cpp
    Source/fx/GervanaEffects.cpp
    Source/fx/NamProcessor.cpp
    Source/fx/EffectChain.cpp
    Source/drums/DrumMachine.cpp
    Source/song/SongBuilder.cpp
    Source/song/GpImporter.cpp
    Source/practice/TabPlayer.cpp
    Source/practice/Judge.cpp
    Source/practice/Recorder.cpp
    Source/practice/PracticeLog.cpp)

add_library(ToneCore STATIC ${TONE_CORE_SOURCES})
target_include_directories(ToneCore PUBLIC Source)
target_compile_definitions(ToneCore PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=0
    JUCE_PLUGINHOST_LV2=0
    JUCE_ASIO=1
    JUCE_WASAPI=1
    JUCE_DIRECTSOUND=1)
target_link_libraries(ToneCore PUBLIC
    nam_core
    juce::juce_audio_utils
    juce::juce_dsp)
set_target_properties(ToneCore PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_options(ToneCore PRIVATE /utf-8 /bigobj)
endif()

# ---- ToneUI: shared JUCE panels --------------------------------------------
set(TONE_UI_SOURCES
    Source/MainComponent.cpp
    Source/ui/PedalBoard.cpp
    Source/ui/PresetBar.cpp
    Source/ui/PedalEditor.cpp
    Source/ui/DrumPanel.cpp
    Source/ui/TabHighway.cpp
    Source/ui/PracticePanel.cpp
    Source/ui/TabSearch.cpp
    Source/ui/StatsView.cpp)

add_library(ToneUI STATIC ${TONE_UI_SOURCES})
target_link_libraries(ToneUI PUBLIC ToneCore juce::juce_gui_extra)
set_target_properties(ToneUI PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_options(ToneUI PRIVATE /utf-8 /bigobj)
endif()

# ---- Standalone app --------------------------------------------------------
juce_add_gui_app(Tone_Tab
    PRODUCT_NAME "Tone_Tab"
    COMPANY_NAME "Tone_Tab"
    VERSION      ${PROJECT_VERSION})

target_sources(Tone_Tab PRIVATE
    Source/Main.cpp
    tests/OfflineTests.cpp)

target_compile_definitions(Tone_Tab PRIVATE
    TONE_TESTS_EMBEDDED=1
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:Tone_Tab,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:Tone_Tab,JUCE_VERSION>"
    JUCE_ASIO=1
    JUCE_WASAPI=1
    JUCE_DIRECTSOUND=1)
if(MSVC)
    target_compile_options(Tone_Tab PRIVATE /utf-8 /bigobj)
endif()
target_link_libraries(Tone_Tab
    PRIVATE ToneUI ToneCore nam_core
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---- Offline tests (no audio device required) -------------------------------
option(TONE_BUILD_TESTS "Build offline DSP tests" OFF)
if(TONE_BUILD_TESTS)
    juce_add_console_app(ToneTests PRODUCT_NAME "ToneTests")
    target_sources(ToneTests PRIVATE tests/OfflineTests.cpp)
    target_compile_definitions(ToneTests PRIVATE
        JUCE_ALSA=0 JUCE_JACK=0 JUCE_USE_XRANDR=0 JUCE_USE_XINERAMA=0
        JUCE_USE_XCURSOR=0 JUCE_USE_XINPUT=0)
    if(MSVC)
        target_compile_options(ToneTests PRIVATE /utf-8 /bigobj)
    endif()
    target_link_libraries(ToneTests
        PRIVATE ToneUI ToneCore nam_core
        PUBLIC juce::juce_recommended_config_flags)
endif()

# ---- VST3 plug-in (tone chain only) ----------------------------------------
option(TONE_BUILD_PLUGIN "Build the Tone_Tab VST3 plug-in" ON)
if(TONE_BUILD_PLUGIN)
    juce_add_plugin(Tone_TabPlugin
        PRODUCT_NAME "Tone_Tab"
        COMPANY_NAME "Tone_Tab"
        PLUGIN_MANUFACTURER_CODE Tntb
        PLUGIN_CODE Ttb1
        FORMATS VST3
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        IS_MIDI_EFFECT FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE
        VST3_CATEGORIES Fx)

    target_sources(Tone_TabPlugin PRIVATE
        Source/plugin/PluginProcessor.cpp
        Source/plugin/PluginEditor.cpp)

    target_compile_definitions(Tone_TabPlugin PRIVATE
        JUCE_VST3_CAN_REPLACE_VST2=0)
    if(MSVC)
        target_compile_options(Tone_TabPlugin PRIVATE /utf-8 /bigobj)
    endif()
    target_link_libraries(Tone_TabPlugin
        PRIVATE ToneUI ToneCore nam_core
        PUBLIC juce::juce_recommended_config_flags)
endif()
