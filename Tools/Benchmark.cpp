#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "Parameters.h"

// 負荷の測定ツール。
// 1. DSP: processBlock にかかる時間を、処理した音の長さに対する割合（1 コアの何 %）で出す（設定ごと）
// 2. FFT 単体: juce::dsp::FFT の実数 FFT（順＋逆）1 組の時間と、半分の大きさの複素 FFT の時間（実数 FFT をきちんと作った場合の目安）
// 3. 細かい処理: decibelsToGain・log10 をビンの数だけ計算する時間
// 4. 画面: 液晶 1 フレームの描画時間（100% と 150%、抑制後の線が 3 本重ねと 1 本）と、エディター全体の描画時間
// 使い方: Benchmark
namespace
{
    class PinkNoise
    {
    public:
        PinkNoise (double sampleRate, float resonanceDb, int seed) : random (seed)
        {
            resonance.coefficients = juce::dsp::IIR::Coefficients<float>::makePeakFilter (sampleRate, 3870.0, 16.0, juce::Decibels::decibelsToGain (resonanceDb));
        }

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
            return resonance.processSample (pink);
        }

    private:
        juce::Random random;
        float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
        juce::dsp::IIR::Filter<float> resonance;
    };

    void setParameter (PRIResonanceSuppressorProcessor& processor, const char* id, float value)
    {
        auto* p = processor.getState().getParameter (id);
        p->setValueNotifyingHost (p->convertTo0to1 (value));
    }

    double seconds (juce::int64 ticks) { return juce::Time::highResolutionTicksToSeconds (ticks); }

    struct DspCase
    {
        const char* name;
        double sampleRate;
        int resolution, stereo;
        bool silent = false, wideRange = false;
    };

    // 音を前もって作っておき（作る時間を測らないため）、10 秒分を processBlock に通す時間を 3 回測って一番短いものを使う
    double measureDsp (const DspCase& c)
    {
        constexpr int blockSize = 512;
        const int totalSamples = (int) (c.sampleRate * 10.0);
        const int numBlocks = totalSamples / blockSize;

        juce::AudioBuffer<float> source (2, numBlocks * blockSize);
        PinkNoise left (c.sampleRate, 18.0f, 1), right (c.sampleRate, 18.0f, 2);

        for (int i = 0; i < source.getNumSamples(); ++i)
        {
            source.setSample (0, i, c.silent ? 0.0f : left.next());
            source.setSample (1, i, c.silent ? 0.0f : right.next());
        }

        PRIResonanceSuppressorProcessor processor;
        setParameter (processor, params::resolution, (float) c.resolution);
        setParameter (processor, params::stereo, (float) c.stereo);

        if (c.wideRange)
        {
            setParameter (processor, params::minHz, 25.0f);
            setParameter (processor, params::maxHz, 20000.0f);
        }

        processor.prepareToPlay (c.sampleRate, blockSize);

        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;
        double best = 1.0e9;

        for (int run = 0; run < 4; ++run)
        {
            const auto start = juce::Time::getHighResolutionTicks();

            for (int b = 0; b < numBlocks; ++b)
            {
                for (int ch = 0; ch < 2; ++ch)
                    buffer.copyFrom (ch, 0, source, ch, b * blockSize, blockSize);

                processor.processBlock (buffer, midi);
            }

            const double elapsed = seconds (juce::Time::getHighResolutionTicks() - start);

            if (run > 0)   // 1 回目は温め
                best = juce::jmin (best, elapsed);
        }

        return 100.0 * best / ((double) numBlocks * blockSize / c.sampleRate);
    }

    // 1 回あたりの時間（マイクロ秒）
    template <typename Fn>
    double microseconds (int repeats, Fn&& fn)
    {
        double best = 1.0e9;

        for (int run = 0; run < 3; ++run)
        {
            const auto start = juce::Time::getHighResolutionTicks();

            for (int i = 0; i < repeats; ++i)
                fn();

            best = juce::jmin (best, seconds (juce::Time::getHighResolutionTicks() - start) * 1.0e6 / repeats);
        }

        return best;
    }

    void measureFft()
    {
        std::cout << "\n[FFT] juce::dsp::FFT (" << "order, real fwd+inv pair, complex N/2 fwd+inv pair) in microseconds" << std::endl;

        for (int order = 10; order <= 14; ++order)
        {
            const int n = 1 << order;
            juce::dsp::FFT real (order), half (order - 1);
            std::vector<float> data ((size_t) n * 2);
            std::vector<juce::dsp::Complex<float>> in ((size_t) n / 2), out ((size_t) n / 2);
            juce::Random random (3);

            for (auto& v : data)
                v = random.nextFloat();

            const double realPair = microseconds (2000 >> (order - 10), [&]
            {
                real.performRealOnlyForwardTransform (data.data(), true);
                real.performRealOnlyInverseTransform (data.data());
            });

            const double halfPair = microseconds (2000 >> (order - 10), [&]
            {
                half.perform (in.data(), out.data(), false);
                half.perform (out.data(), in.data(), true);
            });

            std::cout << "  N=" << n << ": real pair " << juce::String (realPair, 1) << " us, complex N/2 pair " << juce::String (halfPair, 1) << " us" << std::endl;
        }
    }

    void measureSmallParts()
    {
        std::cout << "\n[per-bin math] 2049 bins (FFT 4096), microseconds per frame" << std::endl;
        constexpr int bins = 2049;
        std::vector<float> values (bins), out (bins);
        juce::Random random (4);

        for (auto& v : values)
            v = random.nextFloat() * 9.0f;

        const double gain = microseconds (2000, [&]
        {
            for (int k = 0; k < bins; ++k)
                out[(size_t) k] = juce::Decibels::decibelsToGain (-values[(size_t) k]);
        });

        const double logs = microseconds (2000, [&]
        {
            for (int k = 0; k < bins; ++k)
                out[(size_t) k] = 10.0f * std::log10 (values[(size_t) k] + 1.0e-20f);
        });

        std::cout << "  decibelsToGain x bins: " << juce::String (gain, 1) << " us, log10 x bins: " << juce::String (logs, 1) << " us" << std::endl;
    }

    void measureUi()
    {
        std::cout << "\n[UI] software renderer, milliseconds per frame" << std::endl;

        constexpr double sampleRate = 48000.0;
        constexpr int blockSize = 512;
        PRIResonanceSuppressorProcessor processor;
        processor.prepareToPlay (sampleRate, blockSize);
        PinkNoise left (sampleRate, 18.0f, 1);
        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;

        auto run = [&] (double secondsToRun)
        {
            for (int n = 0; n < juce::roundToInt (secondsToRun * sampleRate / blockSize); ++n)
            {
                for (int i = 0; i < blockSize; ++i)
                {
                    const float s = left.next();
                    buffer.setSample (0, i, s);
                    buffer.setSample (1, i, s);
                }

                processor.processBlock (buffer, midi);
            }
        };

        run (1.0);
        std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorAndMakeActive());
        editor->addToDesktop (juce::ComponentPeer::windowIsTemporary);
        editor->setVisible (true);

        for (int i = 0; i < 20; ++i)
        {
            run (1.0 / 30.0);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (33);
        }

        auto* panel = dynamic_cast<RackPanel*> (editor->getChildComponent (0));

        if (panel == nullptr)
            return;

        for (const float scale : { 1.0f, 1.5f })
        {
            // 液晶だけの再描画（ふだんの 1 フレーム）。作り置きの絵は 1 回目に作られるので、温めてから測る
            auto& lcd = panel->lcd;
            juce::Image image (juce::Image::RGB, (int) (lcd.getWidth() * scale), (int) (lcd.getHeight() * scale), true, juce::SoftwareImageType());
            auto paintLcd = [&]
            {
                juce::Graphics g (image);
                g.addTransform (juce::AffineTransform::scale (scale));
                lcd.paintEntireComponent (g, true);
            };

            // 抑制後の線: 3 本重ね（既定）と 1 本
            double lcdMs = 0.0, lcdOneLineMs = 0.0;

            for (const bool glow : { true, false })
            {
                lcd.setDisplayOptions (45.0f, 90.0f, false, true, true, glow);
                paintLcd();
                (glow ? lcdMs : lcdOneLineMs) = microseconds (60, paintLcd) / 1000.0;
            }

            lcd.setDisplayOptions (45.0f, 90.0f, false, true, true, true);

            juce::Image whole (juce::Image::RGB, (int) (panel->getWidth() * scale), (int) (panel->getHeight() * scale), true, juce::SoftwareImageType());
            auto paintAll = [&]
            {
                juce::Graphics g (whole);
                g.addTransform (juce::AffineTransform::scale (scale));
                panel->paintEntireComponent (g, true);
            };

            paintAll();
            const double allMs = microseconds (20, paintAll) / 1000.0;

            auto describe = [] (double ms)
            {
                return juce::String (ms, 2) + " ms (= " + juce::String (ms * 30.0 / 10.0, 1) + " % of a core at 30 fps, "
                       + juce::String (ms * 60.0 / 10.0, 1) + " % at 60 fps)";
            };

            std::cout << "  scale " << scale << ": LCD frame " << describe (lcdMs) << ", with 1 line " << describe (lcdOneLineMs)
                      << ", whole panel " << juce::String (allMs, 2) << " ms" << std::endl;
        }

        editor.reset();
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;

    std::cout << "CPU: " << juce::SystemStats::getCpuModel() << " (" << juce::SystemStats::getCpuSpeedInMegahertz() << " MHz, "
              << juce::SystemStats::getNumCpus() << " threads)" << std::endl;

    std::cout << "\n[DSP] stereo, block 512, pink noise with a resonance; % of one core (lower is better)" << std::endl;

    const DspCase cases[] = {
        { "48k  FFT 1024  LINKED", 48000.0, 0, params::stereoLinked },
        { "48k  FFT 2048  LINKED", 48000.0, 1, params::stereoLinked },
        { "48k  FFT 4096  LINKED", 48000.0, 2, params::stereoLinked },
        { "48k  FFT 2048  L/R   ", 48000.0, 1, params::stereoDual },
        { "48k  FFT 2048  M/S   ", 48000.0, 1, params::stereoMidSide },
        { "48k  FFT 2048  LINKED, range 25 Hz-20 kHz", 48000.0, 1, params::stereoLinked, false, true },
        { "48k  FFT 2048  LINKED, silence (gate)", 48000.0, 1, params::stereoLinked, true },
        { "96k  FFT 4096 (hi-res x2)  LINKED", 96000.0, 1, params::stereoLinked },
        { "96k  FFT 8192 (hi-res x2)  LINKED", 96000.0, 2, params::stereoLinked },
        { "192k FFT 16384 (hi-res x4) LINKED", 192000.0, 2, params::stereoLinked },
    };

    for (const auto& c : cases)
        std::cout << "  " << c.name << ": " << juce::String (measureDsp (c), 2) << " %" << std::endl;

    measureFft();
    measureSmallParts();

    // 画面の測定はパソコンの表示設定を読むだけだが、念のため退避して戻す
    const auto settingsFile = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                                  .getChildFile ("PRITEZZA").getChildFile ("PRIResonanceSuppressor.settings");
    const bool hadSettings = settingsFile.existsAsFile();
    const auto savedSettings = hadSettings ? settingsFile.loadFileAsString() : juce::String();

    measureUi();

    if (hadSettings)
        settingsFile.replaceWithText (savedSettings);

    return 0;
}
