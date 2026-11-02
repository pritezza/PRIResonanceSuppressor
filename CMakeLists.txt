cmake_minimum_required(VERSION 3.22)

project(PRIResonanceSuppressor VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/utf-8)
endif()

# JUCE は external/JUCE に clone しておく（README 参照）
add_subdirectory(external/JUCE EXCLUDE_FROM_ALL)

#==============================================================================
# プラグイン（VST3）
juce_add_plugin(PRIResonanceSuppressor
    PRODUCT_NAME "PRI Resonance Suppressor"
    COMPANY_NAME "PRITEZZA"
    BUNDLE_ID "com.pritezza.PRIResonanceSuppressor"
    PLUGIN_MANUFACTURER_CODE Prtz   # PRITEZZA のプラグインで共通
    PLUGIN_CODE Rsup                # プラグインごとに別の値
    FORMATS VST3
    VST3_CATEGORIES Fx Dynamics
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    # C:\Program Files\Common Files\VST3 へのコピーは管理者権限が要るので行わない（README 参照）
    COPY_PLUGIN_AFTER_BUILD FALSE)

# JUCE は DLL のバージョン情報（.rc）を一度作ると作り直さないので、VERSION を含む Info.txt が変わったら作り直す
get_target_property(priGeneratedDir PRIResonanceSuppressor JUCE_GENERATED_SOURCES_DIRECTORY)
get_target_property(priInfoFile PRIResonanceSuppressor JUCE_INFO_FILE)
add_custom_command(OUTPUT "${priGeneratedDir}/PRIResonanceSuppressor_resources.rc" APPEND DEPENDS "${priInfoFile}")

target_sources(PRIResonanceSuppressor PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/Parameters.h
    Source/ResonanceSuppressor.h
    Source/RackStyle.h
    Source/RackControls.h
    Source/LcdDisplay.h
    Source/SettingsOverlay.h)

# 音声ファイルの読み書き（FLAC、Ogg Vorbis）は使わないので組み込まない
target_compile_definitions(PRIResonanceSuppressor PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_USE_FLAC=0
    JUCE_USE_OGGVORBIS=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(PRIResonanceSuppressor
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# リンク時最適化（LTO）。配布用のビルドでは ON（既定）。リンクが遅くなるので、開発中に切りたいときは -DPRI_ENABLE_LTO=OFF
option(PRI_ENABLE_LTO "Enable link-time optimisation" ON)

if(PRI_ENABLE_LTO)
    target_link_libraries(PRIResonanceSuppressor PUBLIC juce::juce_recommended_lto_flags)
endif()

# 開発用のツール（以下）は -DPRI_BUILD_TOOLS=ON のときだけ作る（docs/DEVELOPMENT.md 参照）
option(PRI_BUILD_TOOLS "Build the development tools (checks, benchmark, README media)" OFF)

if(NOT PRI_BUILD_TOOLS)
    return()
endif()

#==============================================================================
# ビルドしたプラグイン（VST3）をホストとして読み込んで確かめるツール
juce_add_console_app(PluginHostTest PRODUCT_NAME "PluginHostTest")
target_sources(PluginHostTest PRIVATE Tools/PluginHostTest.cpp)
target_compile_definitions(PluginHostTest PRIVATE
    JUCE_PLUGINHOST_VST3=1
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)
target_link_libraries(PluginHostTest
    PRIVATE
        juce::juce_audio_processors
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

#==============================================================================
# 負荷の測定ツール（DSP・FFT・画面の描画時間）。プラグインのソースを直接コンパイルする
juce_add_console_app(Benchmark PRODUCT_NAME "Benchmark")
target_sources(Benchmark PRIVATE
    Tools/Benchmark.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)
target_include_directories(Benchmark PRIVATE Source)
target_compile_definitions(Benchmark PRIVATE
    JucePlugin_Name="PRI Resonance Suppressor"
    JucePlugin_VersionString="${PROJECT_VERSION}"
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)
target_link_libraries(Benchmark
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

#==============================================================================
# README の画像と動画のコマを書き出すツール（GIF にするのは Tools/MakeReadmeMedia.ps1）。プラグインのソースを直接コンパイルする
juce_add_console_app(ReadmeMedia PRODUCT_NAME "ReadmeMedia")
target_sources(ReadmeMedia PRIVATE
    Tools/ReadmeMedia.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)
target_include_directories(ReadmeMedia PRIVATE Source)
target_compile_definitions(ReadmeMedia PRIVATE
    JucePlugin_Name="PRI Resonance Suppressor"
    JucePlugin_VersionString="${PROJECT_VERSION}"
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)
target_link_libraries(ReadmeMedia
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

#==============================================================================
# 画面の確認用ツール（エディターを PNG に書き出す）。プラグインのソースを直接コンパイルする
juce_add_console_app(EditorSnapshot PRODUCT_NAME "EditorSnapshot")
target_sources(EditorSnapshot PRIVATE
    Tools/EditorSnapshot.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)
target_include_directories(EditorSnapshot PRIVATE Source)
target_compile_definitions(EditorSnapshot PRIVATE
    JucePlugin_Name="PRI Resonance Suppressor"
    JucePlugin_VersionString="${PROJECT_VERSION}"
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)
target_link_libraries(EditorSnapshot
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
