#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "Parameters.h"

#include <iostream>

// README の画像と動画のコマを書き出すツール。音はすべてこの中で合成する（素材の権利の心配がないように）。
//   <画像フォルダ>/theme-vintage.png, theme-black.png, theme-silver.png  テーマごとの画面（曲を処理中）
//   <画像フォルダ>/settings.png                                          設定画面
//   <コマのフォルダ>/hero/frame_0000.png ...       画面全体（75%）。曲を処理しながら DEPTH を上げ下げし、途中でバイパス（8 秒）
//   <コマのフォルダ>/reduction/frame_0000.png ...  液晶と GR。ピンクノイズの上を往復する共振を削る様子（6 秒）
// コマは 20 fps。GIF にするのは Tools/MakeReadmeMedia.ps1（ffmpeg を使う）。
// 表示の設定は既定値で撮る（更新頻度だけ 60 FPS）。パソコンの表示設定は実行前の内容に戻す
// 使い方: ReadmeMedia <画像フォルダ> <コマのフォルダ>
namespace
{
    constexpr double demoSampleRate = 48000.0;
    constexpr int ticksPerSecond = 60;                                      // 撮るときの画面の更新頻度（DISPLAY RATE 60 FPS）
    constexpr int ticksPerFrame = 3;                                        // 20 fps
    constexpr int samplesPerTick = (int) demoSampleRate / ticksPerSecond;

    // RBJ のピーキング EQ（係数を毎回作り直せるように自前で持つ）
    struct Peak
    {
        void set (double hz, double q, double gainDb)
        {
            const double a = std::pow (10.0, gainDb / 40.0), w = juce::MathConstants<double>::twoPi * hz / demoSampleRate;
            const double alpha = std::sin (w) / (2.0 * q), a0 = 1.0 + alpha / a;
            b0 = (1.0 + alpha * a) / a0;
            b1 = a1 = -2.0 * std::cos (w) / a0;
            b2 = (1.0 - alpha * a) / a0;
            a2 = (1.0 - alpha / a) / a0;
        }

        float process (float x)
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return (float) y;
        }

        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    };

    // Paul Kellet のピンクノイズ近似
    struct Pink
    {
        float next()
        {
            const float white = random.nextFloat() * 2.0f - 1.0f;
            b0 = 0.99886f * b0 + white * 0.0555179f;
            b1 = 0.99332f * b1 + white * 0.0750759f;
            b2 = 0.96900f * b2 + white * 0.1538520f;
            b3 = 0.86650f * b3 + white * 0.3104856f;
            b4 = 0.55000f * b4 + white * 0.5329522f;
            b5 = -0.7616f * b5 - white * 0.0168980f;
            const float pink = (b0 + b1 + b2 + b3 + b4 + b5 + b6 + white * 0.5362f) * 0.05f;
            b6 = white * 0.115926f;
            return pink;
        }

        juce::Random random { 7 };
        float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
    };

    // 120 BPM、2 秒で 1 小節、4 小節（8 秒）で一回り: ドラム、ベース、和音のパッド、うっすらノイズ。
    // まとめたあとに耳につく共振（1.25 kHz、2.6 kHz、4.4 kHz）を足す
    class DemoSong
    {
    public:
        DemoSong()
        {
            resonances[0].set (1250.0, 12.0, 14.0);
            resonances[1].set (2600.0, 10.0, 18.0);
            resonances[2].set (4400.0, 10.0, 16.0);
            padFilter.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowPass (demoSampleRate, 2800.0f, 0.7f);
            hatFilter.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighPass (demoSampleRate, 7000.0f, 0.7f);
            snareFilter.coefficients = juce::dsp::IIR::Coefficients<float>::makeBandPass (demoSampleRate, 1800.0f, 0.8f);
        }

        void fill (juce::AudioBuffer<float>& buffer)
        {
            // Am - F - C - G（パッドの 3 音とベース）
            static constexpr double chords[4][4] = { { 220.0, 261.63, 329.63, 55.0 }, { 174.61, 220.0, 261.63, 43.65 },
                                                     { 261.63, 329.63, 392.0, 65.41 }, { 196.0, 246.94, 293.66, 49.0 } };

            for (int i = 0; i < buffer.getNumSamples(); ++i, ++position)
            {
                const double t = (double) (position % (8 * (int) demoSampleRate)) / demoSampleRate;
                const auto& chord = chords[(int) (t / 2.0)];
                const double beat = std::fmod (t, 0.5), eighth = std::fmod (t, 0.25);
                const int beatIndex = (int) (t / 0.5) % 4;

                float pad = 0.0f;
                for (int v = 0; v < 6; ++v)
                    pad += saw (padPhases[v], chord[v / 2] * (v % 2 == 0 ? 0.997 : 1.003) / demoSampleRate);

                const float bass = (float) std::sin (juce::MathConstants<double>::twoPi * bassPhase) * 0.9f
                                   + saw (bassSawPhase, chord[3] * 2.0 / demoSampleRate) * 0.15f;
                bassPhase += chord[3] / demoSampleRate;

                float drums = 0.0f;

                if (beatIndex % 2 == 0)
                {
                    kickPhase += (45.0 + 75.0 * std::exp (-beat / 0.03)) / demoSampleRate;
                    drums += (float) (std::sin (juce::MathConstants<double>::twoPi * kickPhase) * std::exp (-beat / 0.12)) * 0.55f;
                }
                else
                {
                    kickPhase = 0.0;
                    drums += snareFilter.processSample (noise() * (float) std::exp (-beat / 0.09)) * 0.5f
                             + (float) (std::sin (juce::MathConstants<double>::twoPi * 185.0 * beat) * std::exp (-beat / 0.05)) * 0.12f;
                }

                drums += hatFilter.processSample (noise() * (float) std::exp (-eighth / 0.03)) * 0.22f;

                float mix = padFilter.processSample (pad) * 0.07f + bass * 0.22f + drums + pink.next() * 0.6f;

                for (auto& r : resonances)
                    mix = r.process (mix);

                for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                    buffer.setSample (ch, i, mix);
            }
        }

    private:
        // PolyBLEP の鋸歯状波
        static float saw (double& phase, double increment)
        {
            phase += increment;
            if (phase >= 1.0)
                phase -= 1.0;

            double value = 2.0 * phase - 1.0;

            if (phase < increment)
            {
                const double x = phase / increment;
                value -= x + x - x * x - 1.0;
            }
            else if (phase > 1.0 - increment)
            {
                const double x = (phase - 1.0) / increment;
                value -= x * x + x + x + 1.0;
            }

            return (float) value;
        }

        float noise() { return random.nextFloat() * 2.0f - 1.0f; }

        juce::Random random { 3 };
        Pink pink;
        Peak resonances[3];
        juce::dsp::IIR::Filter<float> padFilter, hatFilter, snareFilter;
        double padPhases[6] {}, bassPhase = 0.0, bassSawPhase = 0.0, kickPhase = 0.0;
        juce::int64 position = 0;
    };

    // ピンクノイズの上で、細い共振（Q 16、+22 dB）が 1.3 kHz と 8 kHz の間を 6 秒で往復する
    class SweepSignal
    {
    public:
        static constexpr double period = 6.0;

        void fill (juce::AudioBuffer<float>& buffer)
        {
            for (int i = 0; i < buffer.getNumSamples(); ++i, ++position)
            {
                if (position % 32 == 0)
                {
                    const double phase = (double) (position % (juce::int64) (period * demoSampleRate)) / (period * demoSampleRate);
                    const double x = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * phase);
                    resonance.set (1300.0 * std::pow (8000.0 / 1300.0, x), 16.0, 22.0);
                }

                const float sample = resonance.process (pink.next() * 2.0f);

                for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                    buffer.setSample (ch, i, sample);
            }
        }

    private:
        Pink pink;
        Peak resonance;
        juce::int64 position = 0;
    };

    void setParameter (PRIResonanceSuppressorProcessor& processor, const char* id, float value)
    {
        auto* p = processor.getState().getParameter (id);
        p->setValueNotifyingHost (p->convertTo0to1 (value));
    }

    bool writePng (const juce::Image& image, const juce::File& file)
    {
        file.deleteFile();
        juce::FileOutputStream out (file);
        return out.openedOk() && juce::PNGImageFormat().writeImageToStream (image, out);
    }

    float smoothStep (double from, double to, double t)
    {
        const double x = juce::jlimit (0.0, 1.0, (t - from) / (to - from));
        return (float) (x * x * (3.0 - 2.0 * x));
    }
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI gui;

    if (argc < 3)
    {
        std::cout << "Usage: ReadmeMedia <image folder> <frame folder>" << std::endl;
        return 1;
    }

    const auto cwd = juce::File::getCurrentWorkingDirectory();
    const auto imageDir = cwd.getChildFile (juce::String::fromUTF8 (argv[1]));
    const auto frameDir = cwd.getChildFile (juce::String::fromUTF8 (argv[2]));
    imageDir.createDirectory();

    // 表示の設定は既定値で撮るので、パソコンの設定ファイルを退避してから消し、最後に戻す
    const auto settingsFile = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                                  .getChildFile ("PRITEZZA").getChildFile ("PRIResonanceSuppressor.settings");
    const bool hadSettings = settingsFile.existsAsFile();
    const auto savedSettings = hadSettings ? settingsFile.loadFileAsString() : juce::String();
    settingsFile.deleteFile();

    PRIResonanceSuppressorProcessor processor;
    processor.prepareToPlay (demoSampleRate, samplesPerTick);

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorAndMakeActive());
    editor->setTopLeftPosition (40, 40);
    editor->addToDesktop (juce::ComponentPeer::windowIsTemporary);
    editor->setVisible (true);

    auto* panel = dynamic_cast<RackPanel*> (editor->getChildComponent (0));
    auto* pluginEditor = dynamic_cast<PRIResonanceSuppressorEditor*> (editor.get());

    if (panel == nullptr || pluginEditor == nullptr)
        return 1;

    // 画面の更新はエディターのタイマーの処理を直接呼んで、音の処理と 1 対 1 で進める（メッセージループは回さない）。
    // タイマーはエディターの private な基底なので、C 形式のキャストで取り出す
    auto& displayTimer = (juce::Timer&) *pluginEditor;

    auto setOptions = [panel] (const rack::DisplayOptions& options)
    {
        panel->settings.setOptions (options);
        panel->settings.onOptionsChanged (options);
    };

    rack::DisplayOptions options;
    options.frameRate = 2;   // 60 FPS
    setOptions (options);

    juce::AudioBuffer<float> buffer (2, samplesPerTick);
    juce::MidiBuffer midi;

    // 1 回分（1/60 秒）の音を処理して、画面を 1 回更新する
    auto tick = [&] (auto& signal)
    {
        signal.fill (buffer);
        processor.processBlock (buffer, midi);
        displayTimer.timerCallback();
    };

    // 動画のコマを書き出す。scene (秒) で、そのコマの時刻に合わせてパラメータを動かす
    auto record = [&] (auto& signal, const juce::String& name, double seconds, juce::Rectangle<int> area, auto&& scene)
    {
        const int frames = juce::roundToInt (seconds * ticksPerSecond / ticksPerFrame);
        auto dir = frameDir.getChildFile (name);
        dir.deleteRecursively();
        dir.createDirectory();

        // 一回り先に流してから撮る（最後のコマと最初のコマがつながるように）
        for (int pass = 0; pass < 2; ++pass)
        {
            for (int f = 0; f < frames; ++f)
            {
                scene ((double) f * ticksPerFrame / ticksPerSecond);

                for (int t = 0; t < ticksPerFrame; ++t)
                    tick (signal);

                if (pass == 1)
                    writePng (editor->createComponentSnapshot (area), dir.getChildFile ("frame_" + juce::String (f).paddedLeft ('0', 4) + ".png"));
            }
        }

        std::cout << name << ": " << frames << " frames, " << area.getWidth() << "x" << area.getHeight() << std::endl;
    };

    // 1. 画面全体（75%）。DEPTH を 9 → 18 dB に上げ、途中でバイパスを入れて戻し、9 dB に下げる
    {
        DemoSong song;
        editor->setSize (RackPanel::designWidth * 3 / 4, RackPanel::designHeight * 3 / 4);

        record (song, "hero", 8.0, editor->getLocalBounds(), [&] (double t)
        {
            const float depth = 9.0f + 9.0f * (smoothStep (1.0, 2.5, t) - smoothStep (5.5, 7.0, t));
            setParameter (processor, params::cut, std::round (depth * 2.0f) / 2.0f);
            setParameter (processor, params::bypass, t >= 4.0 && t < 5.25 ? 1.0f : 0.0f);
        });

        setParameter (processor, params::cut, 9.0f);
        setParameter (processor, params::bypass, 0.0f);
    }

    // 2. 液晶と GR（原寸で切り出す）。往復する共振を削る様子。細い山を深く削るよう、RESOLUTION 2048、WIDTH 1/12、DEPTH 12 dB にする
    {
        SweepSignal sweep;
        editor->setSize (RackPanel::designWidth, RackPanel::designHeight);
        const auto area = panel->lcd.getBoundsInParent().getUnion (panel->grMeter.getBoundsInParent()).expanded (14, 10);

        setParameter (processor, params::resolution, 1.0f);
        setParameter (processor, params::width, 12.0f);
        setParameter (processor, params::cut, 12.0f);
        record (sweep, "reduction", SweepSignal::period, area, [] (double) {});

        for (auto* id : { params::resolution, params::width, params::cut })
            processor.getState().getParameter (id)->setValueNotifyingHost (processor.getState().getParameter (id)->getDefaultValue());
    }

    // 3. テーマごとの画面と設定画面（原寸）。更新頻度のほかは既定の表示設定
    {
        DemoSong song;
        options = {};
        options.frameRate = 2;

        const std::pair<int, const char*> stills[] = { { rack::Theme::black, "theme-black.png" }, { rack::Theme::silver, "theme-silver.png" },
                                                       { rack::Theme::vintage, "theme-vintage.png" } };

        // 曲の同じ位置（一回り 8 秒の 4.4 秒目）で撮る
        for (int t = 0; t < juce::roundToInt (4.4 * ticksPerSecond); ++t)
            tick (song);

        for (const auto& [theme, file] : stills)
        {
            options.theme = theme;
            setOptions (options);

            for (int t = 0; t < 8 * ticksPerSecond; ++t)
                tick (song);

            std::cout << file << ": " << writePng (editor->createComponentSnapshot (editor->getLocalBounds()), imageDir.getChildFile (file)) << std::endl;
        }

        // 設定画面には更新頻度も出るので、既定（30 FPS）に戻してから撮る
        options.frameRate = rack::DisplayOptions().frameRate;
        setOptions (options);
        panel->settings.setVisible (true);
        std::cout << "settings.png: " << writePng (editor->createComponentSnapshot (editor->getLocalBounds()), imageDir.getChildFile ("settings.png"))
                  << std::endl;
        panel->settings.setVisible (false);
    }

    editor.reset();

    if (hadSettings)
        settingsFile.replaceWithText (savedSettings);
    else
        settingsFile.deleteFile();

    return 0;
}
