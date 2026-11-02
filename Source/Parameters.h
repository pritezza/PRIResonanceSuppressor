#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// パラメータの ID と定義。
// ID は DAW のプロジェクトに保存されるので、公開後は変えないこと
namespace params
{
    inline constexpr const char* minHz = "minHz";
    inline constexpr const char* maxHz = "maxHz";
    inline constexpr const char* threshold = "threshold";
    inline constexpr const char* cut = "cut";
    inline constexpr const char* attack = "attack";
    inline constexpr const char* release = "release";
    inline constexpr const char* resolution = "resolution";
    inline constexpr const char* width = "width";
    inline constexpr const char* listen = "listen";
    inline constexpr const char* silence = "silence";
    inline constexpr const char* mix = "mix";
    inline constexpr const char* bypass = "bypass";
    inline constexpr const char* hiResFft = "hiResFft";
    inline constexpr const char* stereo = "stereo";

    // ステレオの扱い（stereo パラメータの値）
    enum StereoMode { stereoLinked, stereoDual, stereoMidSide };

    inline constexpr int versionHint = 1;

    inline constexpr float minFrequency = 25.0f, maxFrequency = 20000.0f;

    inline juce::String hzText (float hz)
    {
        return hz >= 1000.0f ? juce::String (hz / 1000.0f, 2) + " kHz" : juce::String (juce::roundToInt (hz)) + " Hz";
    }

    inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        using juce::AudioParameterFloatAttributes;
        using Range = juce::NormalisableRange<float>;

        auto skewed = [] (float min, float max, float step, float centre)
        {
            Range range (min, max, step);
            range.setSkewForCentre (centre);
            return range;
        };

        auto withText = [] (std::function<juce::String (float)> toText)
        {
            return AudioParameterFloatAttributes().withStringFromValueFunction ([toText] (float v, int) { return toText (v); });
        };

        // 検出範囲: 25 Hz〜20 kHz を対数で。1 kHz 未満は 1 Hz、以上は 10 Hz 刻み
        auto frequencyRange = []
        {
            return Range (minFrequency, maxFrequency,
                          [] (float start, float end, float x) { return start * std::pow (end / start, x); },
                          [] (float start, float end, float v) { return std::log (v / start) / std::log (end / start); },
                          [] (float start, float end, float v)
                          {
                              const float snapped = v < 1000.0f ? std::round (v) : 10.0f * std::round (v / 10.0f);
                              return juce::jlimit (start, end, snapped);
                          });
        };

        const auto dbText = [] (float v) { return juce::String (v, 1) + " dB"; };
        const auto msText = [] (float v) { return juce::String (juce::roundToInt (v)) + " ms"; };

        juce::AudioProcessorValueTreeState::ParameterLayout layout;

        // LOW ≦ HIGH は画面側で守る（DAW のオートメーションで逆になったときは DSP 側で入れ替える）
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { minHz, versionHint }, "Low Freq",
                                                                 frequencyRange(), 1000.0f, withText (hzText)));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { maxHz, versionHint }, "High Freq",
                                                                 frequencyRange(), 10000.0f, withText (hzText)));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { threshold, versionHint }, "Threshold",
                                                                 Range (1.0f, 20.0f, 0.5f), 6.0f, withText (dbText)));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { cut, versionHint }, "Depth",
                                                                 Range (1.0f, 24.0f, 0.5f), 9.0f, withText (dbText)));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { attack, versionHint }, "Attack",
                                                                 skewed (1.0f, 200.0f, 1.0f, 20.0f), 20.0f, withText (msText)));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { release, versionHint }, "Release",
                                                                 skewed (10.0f, 2000.0f, 1.0f, 200.0f), 150.0f, withText (msText)));

        // FFT 1024 / 2048 / 4096 点。遅延が変わるのでオートメーション不可（DAW によっては再生中の遅延の変更をすぐに反映しない）
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { resolution, versionHint }, "Resolution",
                                                                  juce::StringArray { "1024", "2048", "4096" }, 0,
                                                                  juce::AudioParameterChoiceAttributes().withAutomatable (false)));

        // 検出幅: 1/n オクターブの n
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { width, versionHint }, "Width",
                                                                 skewed (3.0f, 48.0f, 1.0f, 12.0f), 6.0f,
                                                                 withText ([] (float n) { return "1/" + juce::String (juce::roundToInt (n)) + " oct"; })));

        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { listen, versionHint }, "Delta Listen", false));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { silence, versionHint }, "Silence Gate",
                                                                 Range (-90.0f, -30.0f, 1.0f), -60.0f,
                                                                 withText ([] (float v) { return juce::String (juce::roundToInt (v)) + " dBFS"; })));

        // 処理後の音と元の音（同じだけ遅らせたもの）の割合。100% で処理後の音だけ
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { mix, versionHint }, "Mix",
                                                                 Range (0.0f, 100.0f, 1.0f), 100.0f,
                                                                 withText ([] (float v) { return juce::String (juce::roundToInt (v)) + " %"; })));
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { bypass, versionHint }, "Bypass", false));

        // 88.2 kHz 以上で FFT サイズを 2 倍（176.4 kHz 以上は 4 倍）にして、周波数の刻みと遅延の時間を 48 kHz と同じに保つ。
        // 遅延が変わるのでオートメーション不可（設定画面から切り替える）
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { hiResFft, versionHint }, "Hi-Res FFT", true,
                                                                juce::AudioParameterBoolAttributes().withAutomatable (false)));

        // ステレオの扱い: 左右共通で削る / 左右別に削る / M/S（中央と左右差を別に削る）。モノラルでは使わない。
        // 切り替えると削り方が急に変わるのでオートメーション不可（設定画面から切り替える）
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { stereo, versionHint }, "Stereo",
                                                                  juce::StringArray { "Linked", "L/R", "M/S" }, stereoLinked,
                                                                  juce::AudioParameterChoiceAttributes().withAutomatable (false)));

        return layout;
    }
}
