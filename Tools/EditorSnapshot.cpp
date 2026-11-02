#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "Parameters.h"

// 確認用ツール。
// 1. 遅延の確認: 削らない設定で、バイパスの切り替えをまたいで出力が「入力を FFT サイズ分遅らせた音」と一致するか（分解能ごと）
// 2. 画面の確認: ピンクノイズに人工の共振（3870 Hz、Q=16、+18 dB）を足した音を処理しながらエディターを表示し、
//    PNG に書き出す: rack.png（通常）、rack_bypass.png（バイパス中）、rack_75.png（75% に縮小）、
//    rack_settings.png（設定画面）、rack_analyzer.png（ピークホールド、縦軸 60 dB）、rack_oneline.png（抑制後の線 1 本）、rack_black.png / rack_silver.png / rack_vintage.png（テーマ）
// 1c. DEPTH 24 dB で GR の LED が 24 まで点くか
// 1d. ステレオの扱い（左右共通 / 左右別 / M/S）: 削らない設定での一致（途中の切り替えを含む）と、片側・左右差だけの共振の削れ方
// 1b. MIX の確認: 0% と 50% の出力が「元の音 +（処理後 − 元の音）× MIX」と一致するか
// 1e. 崖（MP3 の高域カット、ローカット）の手前を削らないか。崖の近くの本物の共振は削るか
// 3. 液晶の線のドラッグ、RESOLUTION のホイール・ドラッグ・クリック、設定画面の UI SIZE の確認
// パソコンの表示設定（テーマなど）は実行前の内容に戻す
// 使い方: EditorSnapshot <出力フォルダ>
namespace
{
    class TestSignal
    {
    public:
        // lowpassHz / highpassHz が 0 より大きいときは、16 次のバターワースで切る（MP3 の高域カットやローカットの代わり）
        TestSignal (double sampleRate, float resonanceDb, int seed = 1, float resonanceHz = 3870.0f, float lowpassHz = 0.0f, float highpassHz = 0.0f)
            : random (seed)
        {
            resonance.coefficients = juce::dsp::IIR::Coefficients<float>::makePeakFilter (sampleRate, resonanceHz, 16.0, juce::Decibels::decibelsToGain (resonanceDb));

            auto addFilters = [this] (const juce::ReferenceCountedArray<juce::dsp::IIR::Coefficients<float>>& coefficients)
            {
                for (auto* c : coefficients)
                {
                    cuts.emplace_back();
                    cuts.back().coefficients = c;
                }
            };

            if (lowpassHz > 0.0f)
                addFilters (juce::dsp::FilterDesign<float>::designIIRLowpassHighOrderButterworthMethod (lowpassHz, sampleRate, 16));

            if (highpassHz > 0.0f)
                addFilters (juce::dsp::FilterDesign<float>::designIIRHighpassHighOrderButterworthMethod (highpassHz, sampleRate, 16));
        }

        void fill (juce::AudioBuffer<float>& buffer)
        {
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                // Paul Kellet のピンクノイズ近似
                const float white = random.nextFloat() * 2.0f - 1.0f;
                b0 = 0.99886f * b0 + white * 0.0555179f;
                b1 = 0.99332f * b1 + white * 0.0750759f;
                b2 = 0.96900f * b2 + white * 0.1538520f;
                b3 = 0.86650f * b3 + white * 0.3104856f;
                b4 = 0.55000f * b4 + white * 0.5329522f;
                b5 = -0.7616f * b5 - white * 0.0168980f;
                const float pink = (b0 + b1 + b2 + b3 + b4 + b5 + b6 + white * 0.5362f) * 0.05f;
                b6 = white * 0.115926f;

                float sample = resonance.processSample (pink);

                for (auto& cut : cuts)
                    sample = cut.processSample (sample);

                for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                    buffer.setSample (ch, i, sample);
            }
        }

    private:
        juce::Random random;
        float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
        juce::dsp::IIR::Filter<float> resonance;
        std::vector<juce::dsp::IIR::Filter<float>> cuts;
    };

    void setParameter (PRIResonanceSuppressorProcessor& processor, const char* id, float value)
    {
        auto* p = processor.getState().getParameter (id);
        p->setValueNotifyingHost (p->convertTo0to1 (value));
    }

    // 削らない設定（しきい値 20 dB、カット量 1 dB、共振なしのノイズ）で 3 秒処理し、1 秒と 2 秒でバイパスを切り替える。
    // 「出力 − 入力を遅延分ずらした音」の最大値を、入力の RMS に対する dB で返す。latency には報告された遅延を入れる
    // hostBlockSize: 実際に渡すブロックの長さ（prepareToPlay で伝える 480 より長いと、プラグインの中で分けて処理される）
    float checkAlignment (int resolutionIndex, double sampleRate, int& latency, int hostBlockSize = 480)
    {
        const int blockSize = hostBlockSize;

        PRIResonanceSuppressorProcessor processor;
        setParameter (processor, params::threshold, 20.0f);
        setParameter (processor, params::cut, 1.0f);
        setParameter (processor, params::resolution, (float) resolutionIndex);
        processor.prepareToPlay (sampleRate, 480);

        latency = processor.getLatencySamples();
        TestSignal signal (sampleRate, 0.0f);
        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;
        std::vector<float> input, output;

        for (int n = 0; n < juce::roundToInt (3.0 * sampleRate / blockSize); ++n)
        {
            const double t = n * blockSize / sampleRate;
            setParameter (processor, params::bypass, t >= 1.0 && t < 2.0 ? 1.0f : 0.0f);
            signal.fill (buffer);
            input.insert (input.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + blockSize);
            processor.processBlock (buffer, midi);
            output.insert (output.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + blockSize);
        }

        double sumSquares = 0.0, maxError = 0.0;

        for (size_t i = (size_t) latency * 3; i < output.size(); ++i)
        {
            sumSquares += (double) input[i] * input[i];
            maxError = juce::jmax (maxError, (double) std::abs (output[i] - input[i - (size_t) latency]));
        }

        const double rms = std::sqrt (sumSquares / (double) (output.size() - (size_t) latency * 3));
        return juce::Decibels::gainToDecibels ((float) (maxError / rms), -200.0f);
    }

    // 共振を足したノイズを MIX を変えて 2 秒処理し、出力を返す（48 kHz、既定の設定）
    std::vector<float> processWithMix (float mixPercent, std::vector<float>& input, int& latency)
    {
        constexpr double sampleRate = 48000.0;
        constexpr int blockSize = 480;

        PRIResonanceSuppressorProcessor processor;
        setParameter (processor, params::mix, mixPercent);
        processor.prepareToPlay (sampleRate, blockSize);
        latency = processor.getLatencySamples();

        TestSignal signal (sampleRate, 18.0f);
        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;
        std::vector<float> output;
        input.clear();

        for (int n = 0; n < juce::roundToInt (2.0 * sampleRate / blockSize); ++n)
        {
            signal.fill (buffer);
            input.insert (input.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + blockSize);
            processor.processBlock (buffer, midi);
            output.insert (output.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + blockSize);
        }

        return output;
    }

    // MIX の確認: 100% の出力を基準に、0% と 50% が「元の音 +（処理後 − 元の音）× MIX」になっているか。
    // 差の最大値を、100% で削った量（処理後 − 元の音）の最大値に対する dB で出す
    void checkMix()
    {
        std::vector<float> input;
        int latency = 0;
        const auto full = processWithMix (100.0f, input, latency);

        double removedMax = 0.0;
        for (size_t i = (size_t) latency; i < full.size(); ++i)
            removedMax = juce::jmax (removedMax, (double) std::abs (full[i] - input[i - (size_t) latency]));

        for (const float mixPercent : { 0.0f, 50.0f })
        {
            const auto mixed = processWithMix (mixPercent, input, latency);
            const float amount = mixPercent / 100.0f;
            double maxError = 0.0;

            for (size_t i = (size_t) latency; i < mixed.size(); ++i)
            {
                const float dry = input[i - (size_t) latency];
                maxError = juce::jmax (maxError, (double) std::abs (mixed[i] - (dry + (full[i] - dry) * amount)));
            }

            std::cout << "mix " << mixPercent << "%: max error " << juce::Decibels::gainToDecibels ((float) (maxError / removedMax), -200.0f)
                      << " dB (re removed peak " << juce::Decibels::gainToDecibels ((float) removedMax) << " dBFS)" << std::endl;
        }
    }

    // ステレオの扱いの確認。a と b は別々のピンクノイズ（b だけ共振を足せる）で、
    //   midSide == false: L = a、R = b
    //   midSide == true:  L = a + b、R = a − b（中央 = a、左右差 = b）
    // として 3 秒処理し、入力と出力（L/R）を返す。switches は {秒, ステレオの扱い} の切り替え
    struct StereoRun
    {
        std::vector<float> inL, inR, outL, outR;
        int latency = 0;
    };

    StereoRun runStereo (int mode, bool noCut, float resonanceDb, bool midSide, std::vector<std::pair<double, int>> switches = {})
    {
        constexpr double sampleRate = 48000.0;
        constexpr int blockSize = 480;

        PRIResonanceSuppressorProcessor processor;
        setParameter (processor, params::stereo, (float) mode);

        if (noCut)
        {
            setParameter (processor, params::threshold, 20.0f);
            setParameter (processor, params::cut, 1.0f);
        }

        processor.prepareToPlay (sampleRate, blockSize);

        StereoRun run;
        run.latency = processor.getLatencySamples();
        TestSignal a (sampleRate, 0.0f, 1), b (sampleRate, resonanceDb, 2);
        juce::AudioBuffer<float> bufA (1, blockSize), bufB (1, blockSize), buffer (2, blockSize);
        juce::MidiBuffer midi;

        for (int n = 0; n < juce::roundToInt (3.0 * sampleRate / blockSize); ++n)
        {
            const double t = n * blockSize / sampleRate;

            for (const auto& [at, newMode] : switches)
                if (std::abs (t - at) < 0.5 * blockSize / sampleRate)
                    setParameter (processor, params::stereo, (float) newMode);

            a.fill (bufA);
            b.fill (bufB);

            for (int i = 0; i < blockSize; ++i)
            {
                const float x = bufA.getSample (0, i), y = bufB.getSample (0, i);
                buffer.setSample (0, i, midSide ? x + y : x);
                buffer.setSample (1, i, midSide ? x - y : y);
            }

            run.inL.insert (run.inL.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + blockSize);
            run.inR.insert (run.inR.end(), buffer.getReadPointer (1), buffer.getReadPointer (1) + blockSize);
            processor.processBlock (buffer, midi);
            run.outL.insert (run.outL.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + blockSize);
            run.outR.insert (run.outR.end(), buffer.getReadPointer (1), buffer.getReadPointer (1) + blockSize);
        }

        return run;
    }

    // 「出力 − 入力を遅延分ずらした音」の大きさを、入力の RMS に対する dB で返す（最大値か RMS）。最初の 1 秒は除く
    float differenceDb (const std::vector<float>& in, const std::vector<float>& out, int latency, bool useMax)
    {
        double sumIn = 0.0, sumDiff = 0.0, maxDiff = 0.0;
        size_t count = 0;

        for (size_t i = 48000; i < out.size(); ++i, ++count)
        {
            const double d = out[i] - in[i - (size_t) latency];
            sumIn += (double) in[i] * in[i];
            sumDiff += d * d;
            maxDiff = juce::jmax (maxDiff, std::abs (d));
        }

        const double rms = std::sqrt (sumIn / (double) count);
        return juce::Decibels::gainToDecibels ((float) ((useMax ? maxDiff : std::sqrt (sumDiff / (double) count)) / rms), -200.0f);
    }

    void checkStereo()
    {
        const char* names[] = { "LINKED", "L/R", "M/S" };

        // 削らない設定では、どの扱いでも（途中で切り替えても）出力は遅延させた入力と一致する
        for (int mode = 0; mode < 3; ++mode)
        {
            const auto r = runStereo (mode, true, 0.0f, false);
            std::cout << "stereo " << names[mode] << " no cut: max error L " << differenceDb (r.inL, r.outL, r.latency, true)
                      << " dB, R " << differenceDb (r.inR, r.outR, r.latency, true) << " dB" << std::endl;
        }

        {
            const auto r = runStereo (params::stereoLinked, true, 0.0f, false, { { 1.0, params::stereoMidSide }, { 2.0, params::stereoDual } });
            std::cout << "stereo switch LINKED -> M/S -> L/R no cut: max error L " << differenceDb (r.inL, r.outL, r.latency, true)
                      << " dB, R " << differenceDb (r.inR, r.outR, r.latency, true) << " dB" << std::endl;
        }

        // 共振は R だけ。左右共通なら L も一緒に削られ、左右別なら L はほぼそのまま
        for (int mode : { (int) params::stereoLinked, (int) params::stereoDual })
        {
            const auto r = runStereo (mode, false, 18.0f, false);
            std::cout << "stereo " << names[mode] << ", resonance in R only: change L " << differenceDb (r.inL, r.outL, r.latency, false)
                      << " dB, R " << differenceDb (r.inR, r.outR, r.latency, false) << " dB (RMS)" << std::endl;
        }

        // 共振は左右差だけ。M/S なら中央（L + R）はほぼそのまま、左右別では中央も削られる
        for (int mode : { (int) params::stereoDual, (int) params::stereoMidSide })
        {
            const auto r = runStereo (mode, false, 18.0f, true);
            std::vector<float> inMid (r.inL.size()), outMid (r.outL.size()), inSide (r.inL.size()), outSide (r.outL.size());

            for (size_t i = 0; i < inMid.size(); ++i)
            {
                inMid[i] = r.inL[i] + r.inR[i];
                outMid[i] = r.outL[i] + r.outR[i];
                inSide[i] = r.inL[i] - r.inR[i];
                outSide[i] = r.outL[i] - r.outR[i];
            }

            std::cout << "stereo " << names[mode] << ", resonance in side only: change mid " << differenceDb (inMid, outMid, r.latency, false)
                      << " dB, side " << differenceDb (inSide, outSide, r.latency, false) << " dB (RMS)" << std::endl;
        }
    }

    // 再生していない（processBlock が呼ばれない）ときも、RESOLUTION を変えたら遅延が報告されるか。
    // 倍率（UI SIZE）とデルタ試聴が、状態の保存・復元で正しく扱われるか
    void checkStateAndLatency()
    {
        PRIResonanceSuppressorProcessor processor;
        processor.prepareToPlay (48000.0, 480);
        const int before = processor.getLatencySamples();
        setParameter (processor, params::resolution, 2.0f);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        std::cout << "latency while stopped: " << before << " -> " << processor.getLatencySamples() << std::endl;

        processor.setUiScale (1.25f);
        setParameter (processor, params::listen, 1.0f);
        juce::MemoryBlock data;
        processor.getStateInformation (data);

        PRIResonanceSuppressorProcessor restored;
        restored.setStateInformation (data.getData(), (int) data.getSize());
        std::cout << "state restore: uiScale " << restored.getUiScale()
                  << ", listen " << restored.getState().getRawParameterValue (params::listen)->load()
                  << ", uiScale left in parameter tree " << (restored.getState().state.hasProperty ("uiScale") ? "yes" : "no") << std::endl;
    }

    // 崖（MP3 の高域カット、ローカット）の手前を共振と見誤らないか。ピンクノイズ（共振は任意）を切り落として 3 秒処理し、
    // fromHz〜toHz での削る量（最後の 1 秒の平均）の最大を出す。DEPTH は既定の 9 dB
    void checkEdge (const char* name, float resonanceDb, float resonanceHz, float lowpassHz, float highpassHz,
                    float lowHz, float highHz, float width, float threshold, float fromHz, float toHz)
    {
        constexpr double sampleRate = 48000.0;
        constexpr int blockSize = 480;

        PRIResonanceSuppressorProcessor processor;
        setParameter (processor, params::minHz, lowHz);
        setParameter (processor, params::maxHz, highHz);
        setParameter (processor, params::width, width);
        setParameter (processor, params::threshold, threshold);
        processor.prepareToPlay (sampleRate, blockSize);

        TestSignal signal (sampleRate, resonanceDb, 5, resonanceHz, lowpassHz, highpassHz);
        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;
        std::vector<double> sum (ResonanceSuppressor::maxBins, 0.0);
        int frames = 0;

        for (int n = 0; n < juce::roundToInt (3.0 * sampleRate / blockSize); ++n)
        {
            signal.fill (buffer);
            processor.processBlock (buffer, midi);

            if (n * blockSize >= 2.0 * sampleRate)
            {
                for (int k = 0; k < processor.getSuppressor().getDisplayNumBins(); ++k)
                    sum[(size_t) k] += processor.getSuppressor().getDisplayReductionDb (k);

                ++frames;
            }
        }

        const float binHz = processor.getSuppressor().getDisplayBinHz();
        double best = 0.0, bestHz = 0.0;

        for (int k = juce::jmax (1, (int) std::ceil (fromHz / binHz)); (float) k * binHz <= toHz; ++k)
        {
            if (sum[(size_t) k] / frames > best)
            {
                best = sum[(size_t) k] / frames;
                bestHz = k * binHz;
            }
        }

        std::cout << "edge: " << name << ": max reduction " << juce::String (best, 1) << " dB"
                  << (best > 0.0 ? " at " + juce::String (juce::roundToInt (bestHz)) + " Hz" : juce::String()) << std::endl;
    }

    void checkEdges()
    {
        checkEdge ("MP3-like cut at 16 kHz, 8-16 kHz (WIDTH 1/6, THR 6)", 0.0f, 3870.0f, 16000.0f, 0.0f, 1000.0f, 20000.0f, 6.0f, 6.0f, 8000.0f, 16000.0f);
        checkEdge ("MP3-like cut at 16 kHz, 8-16 kHz (WIDTH 1/6, THR 1)", 0.0f, 3870.0f, 16000.0f, 0.0f, 1000.0f, 20000.0f, 6.0f, 1.0f, 8000.0f, 16000.0f);
        checkEdge ("MP3-like cut at 19.5 kHz, 10-19.5 kHz (WIDTH 1/48, THR 13)", 0.0f, 3870.0f, 19500.0f, 0.0f, 1000.0f, 20000.0f, 48.0f, 13.0f, 10000.0f, 19500.0f);
        checkEdge ("low cut at 80 Hz, 25-400 Hz (LOW 25 Hz, THR 6)", 0.0f, 3870.0f, 0.0f, 80.0f, 25.0f, 1000.0f, 6.0f, 6.0f, 25.0f, 400.0f);
        checkEdge ("resonance +18 dB at 12 kHz below a 16 kHz cut, 11-13 kHz", 18.0f, 12000.0f, 16000.0f, 0.0f, 1000.0f, 20000.0f, 6.0f, 6.0f, 11000.0f, 13000.0f);
        checkEdge ("resonance +18 dB at 12 kHz, no cut, 11-13 kHz", 18.0f, 12000.0f, 0.0f, 0.0f, 1000.0f, 20000.0f, 6.0f, 6.0f, 11000.0f, 13000.0f);
        checkEdge ("resonance +18 dB at 14 kHz below a 16 kHz cut, 13-15 kHz", 18.0f, 14000.0f, 16000.0f, 0.0f, 1000.0f, 20000.0f, 6.0f, 6.0f, 13000.0f, 15000.0f);
        checkEdge ("resonance +18 dB at 14 kHz, no cut, 13-15 kHz", 18.0f, 14000.0f, 0.0f, 0.0f, 1000.0f, 20000.0f, 6.0f, 6.0f, 13000.0f, 15000.0f);
        checkEdge ("no cut, pink only, 1-20 kHz (WIDTH 1/6, THR 6)", 0.0f, 3870.0f, 0.0f, 0.0f, 1000.0f, 20000.0f, 6.0f, 6.0f, 1000.0f, 20000.0f);
        checkEdge ("resonance +18 dB at 3870 Hz, no cut, 3-5 kHz", 18.0f, 3870.0f, 0.0f, 0.0f, 1000.0f, 10000.0f, 6.0f, 6.0f, 3000.0f, 5000.0f);
    }

    // DEPTH 24 dB（最大）で強い共振を削ったとき、GR の LED が 24 まで全部点くか
    void checkMaxReduction()
    {
        constexpr double sampleRate = 48000.0;
        constexpr int blockSize = 480;

        PRIResonanceSuppressorProcessor processor;
        setParameter (processor, params::threshold, 1.0f);
        setParameter (processor, params::cut, 24.0f);
        setParameter (processor, params::attack, 1.0f);
        setParameter (processor, params::width, 48.0f);   // 削る量を周波数方向にならす幅を最小にして、24 dB に届かせる
        processor.prepareToPlay (sampleRate, blockSize);

        TestSignal signal (sampleRate, 40.0f);
        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;

        for (int n = 0; n < juce::roundToInt (2.0 * sampleRate / blockSize); ++n)
        {
            signal.fill (buffer);
            processor.processBlock (buffer, midi);
        }

        const float peak = processor.getSuppressor().getPeakReductionDb();
        const auto theme = rack::Theme::get (rack::Theme::black);
        rack::GainReductionMeter meter (theme);
        meter.setLevel (peak, 100.0f);
        std::cout << "max reduction: " << peak << " dB, GR LEDs lit " << meter.getLitCount() << " / 10" << std::endl;
    }

    bool writePng (juce::Component& component, const juce::File& file)
    {
        const auto image = component.createComponentSnapshot (component.getLocalBounds());
        file.deleteFile();
        juce::FileOutputStream out (file);
        return out.openedOk() && juce::PNGImageFormat().writeImageToStream (image, out);
    }
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI gui;

    // 48 kHz の 3 段と、Hi-Res FFT（既定 ON）で倍になる 96 / 192 kHz
    const std::pair<int, double> alignmentCases[] = { { 0, 48000.0 }, { 1, 48000.0 }, { 2, 48000.0 }, { 0, 96000.0 }, { 2, 96000.0 }, { 2, 192000.0 } };

    for (const auto& [index, rate] : alignmentCases)
    {
        int latency = 0;
        const float error = checkAlignment (index, rate, latency);
        std::cout << "alignment (" << (1024 << index) << " @ " << rate / 1000.0 << " kHz, latency " << latency << "): max error "
                  << error << " dB (re RMS)" << std::endl;
    }

    {
        int latency = 0;
        const float error = checkAlignment (1, 48000.0, latency, 2000);
        std::cout << "alignment with 2000-sample blocks (prepared for 480): max error " << error << " dB (re RMS)" << std::endl;
    }

    checkMix();
    checkMaxReduction();
    checkStereo();
    checkStateAndLatency();
    checkEdges();

    // 画面の確認ではパソコンの表示設定（テーマなど）を切り替えるので、先に退避して最後に戻す
    const auto settingsFile = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                                  .getChildFile ("PRITEZZA").getChildFile ("PRIResonanceSuppressor.settings");
    const bool hadSettings = settingsFile.existsAsFile();
    const auto savedSettings = hadSettings ? settingsFile.loadFileAsString() : juce::String();

    const auto outDir = juce::File::getCurrentWorkingDirectory().getChildFile (argc > 1 ? argv[1] : ".");
    outDir.createDirectory();

    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;

    PRIResonanceSuppressorProcessor processor;
    processor.prepareToPlay (sampleRate, blockSize);

    TestSignal signal (sampleRate, 18.0f);
    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;

    auto run = [&] (double seconds)
    {
        for (int n = 0; n < juce::roundToInt (seconds * sampleRate / blockSize); ++n)
        {
            signal.fill (buffer);
            processor.processBlock (buffer, midi);
        }
    };

    // 画面の更新（30 Hz）と音の処理を交互に進める
    auto runWithEditor = [&] (int frames)
    {
        for (int i = 0; i < frames; ++i)
        {
            run (1.0 / 30.0);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (33);
        }
    };

    run (2.0);

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorAndMakeActive());
    editor->setTopLeftPosition (40, 40);
    editor->addToDesktop (juce::ComponentPeer::windowIsTemporary);
    editor->setVisible (true);

    runWithEditor (45);
    std::cout << "rack.png: " << writePng (*editor, outDir.getChildFile ("rack.png")) << std::endl;

    processor.getState().getParameter (params::bypass)->setValueNotifyingHost (1.0f);
    runWithEditor (30);
    std::cout << "rack_bypass.png: " << writePng (*editor, outDir.getChildFile ("rack_bypass.png")) << std::endl;

    processor.getState().getParameter (params::bypass)->setValueNotifyingHost (0.0f);
    editor->setSize (RackPanel::designWidth * 3 / 4, RackPanel::designHeight * 3 / 4);
    runWithEditor (30);
    std::cout << "rack_75.png: " << writePng (*editor, outDir.getChildFile ("rack_75.png")) << std::endl;

    editor->setSize (RackPanel::designWidth, RackPanel::designHeight);
    auto* panel = dynamic_cast<RackPanel*> (editor->getChildComponent (0));
    runWithEditor (2);

    // 液晶の検出範囲の線のドラッグ: LOW は HIGH を、HIGH は LOW を越えないこと
    if (panel != nullptr)
    {
        auto& lcd = panel->lcd;
        auto& state = processor.getState();
        auto value = [&state] (const char* id) { return state.getRawParameterValue (id)->load(); };
        const float x0 = 36.0f, x1 = (float) lcd.getWidth() - 42.0f;
        auto xFor = [&] (float hz) { return x0 + (x1 - x0) * std::log10 (hz / 20.0f) / 3.0f; };

        auto drag = [&] (float fromX, float toX)
        {
            const auto source = juce::Desktop::getInstance().getMainMouseSource();
            const juce::Point<float> from { fromX, 100.0f }, to { toX, 100.0f };
            const auto now = juce::Time::getCurrentTime();
            auto event = [&] (juce::Point<float> p, bool dragged)
            {
                return juce::MouseEvent (source, p, juce::ModifierKeys::leftButtonModifier, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                         &lcd, &lcd, now, from, now, 1, dragged);
            };

            lcd.mouseDown (event (from, false));
            lcd.mouseDrag (event (from + (to - from) * 0.5f, true));
            lcd.mouseDrag (event (to, true));
            lcd.mouseUp (event (to, true));
            runWithEditor (2);
        };

        drag (xFor (value (params::minHz)), xFor (15000.0f));
        std::cout << "drag LOW past HIGH: low " << value (params::minHz) << " high " << value (params::maxHz) << std::endl;
        drag (xFor (value (params::minHz)), xFor (2000.0f));
        std::cout << "drag overlapped lines left: low " << value (params::minHz) << " high " << value (params::maxHz) << std::endl;
        drag (xFor (value (params::maxHz)), xFor (100.0f));
        std::cout << "drag HIGH below LOW: low " << value (params::minHz) << " high " << value (params::maxHz) << std::endl;
        drag (xFor (value (params::minHz)), x0 - 300.0f);
        std::cout << "drag LOW far left: low " << value (params::minHz) << " high " << value (params::maxHz) << std::endl;

        for (auto* id : { params::minHz, params::maxHz })
            state.getParameter (id)->setValueNotifyingHost (state.getParameter (id)->getDefaultValue());

        runWithEditor (2);
    }

    // RESOLUTION のつまみ: ホイールと上下ドラッグ、数字のクリック
    if (panel != nullptr)
    {
        rack::ResolutionSwitch* resolution = nullptr;

        for (auto* child : panel->getChildren())
            if (auto* r = dynamic_cast<rack::ResolutionSwitch*> (child))
                resolution = r;

        if (resolution != nullptr)
        {
            auto& state = processor.getState();
            auto index = [&state] { return juce::roundToInt (state.getRawParameterValue (params::resolution)->load()); };
            const auto source = juce::Desktop::getInstance().getMainMouseSource();
            const auto now = juce::Time::getCurrentTime();
            const juce::Point<float> knob { 59.0f, 48.0f };

            auto event = [&] (juce::Point<float> p, juce::Point<float> downAt, bool dragged)
            {
                return juce::MouseEvent (source, p, juce::ModifierKeys::leftButtonModifier, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                         resolution, resolution, now, downAt, now, 1, dragged);
            };

            juce::MouseWheelDetails wheel { 0.0f, 0.14f, false, false, false };

            for (int i = 0; i < 2; ++i)
                resolution->mouseWheelMove (event (knob, knob, false), wheel);

            std::cout << "wheel up x2: " << index();
            wheel.deltaY = -0.14f;
            resolution->mouseWheelMove (event (knob, knob, false), wheel);
            std::cout << ", wheel down x1: " << index();

            resolution->mouseDown (event (knob, knob, false));
            resolution->mouseDrag (event (knob + juce::Point<float> (0.0f, 30.0f), knob, true));
            resolution->mouseDrag (event (knob + juce::Point<float> (0.0f, 60.0f), knob, true));
            resolution->mouseUp (event (knob + juce::Point<float> (0.0f, 60.0f), knob, true));
            std::cout << ", drag down 60 px: " << index();

            resolution->mouseDown (event (knob, knob, false));
            resolution->mouseDrag (event (knob - juce::Point<float> (0.0f, 30.0f), knob, true));
            resolution->mouseUp (event (knob - juce::Point<float> (0.0f, 30.0f), knob, true));
            std::cout << ", drag up 30 px: " << index();

            const juce::Point<float> label4096 { 100.0f, 34.0f };
            resolution->mouseDown (event (label4096, label4096, false));
            resolution->mouseUp (event (label4096, label4096, false));
            std::cout << ", click 4096: " << index() << std::endl;

            state.getParameter (params::resolution)->setValueNotifyingHost (0.0f);
            runWithEditor (2);
        }
    }

    // 設定画面、アナライザーの表示設定、テーマ
    rack::SettingsOverlay* settings = nullptr;

    if (panel != nullptr)
        for (auto* child : panel->getChildren())
            if (auto* s = dynamic_cast<rack::SettingsOverlay*> (child))
                settings = s;

    if (settings != nullptr)
    {
        // 文字でボタンを探す（"ON" は HI-RES FFT と PEAK HOLD にあるので n 番目を指定）
        auto findButton = [settings] (const juce::String& text, int occurrence = 0) -> juce::Button*
        {
            for (auto* child : settings->getChildren())
                if (auto* b = dynamic_cast<rack::SegmentButton*> (child); b != nullptr && b->getButtonText() == text)
                    if (occurrence-- == 0)
                        return b;

            return nullptr;
        };

        settings->setVisible (true);
        runWithEditor (10);
        std::cout << "rack_settings.png: " << writePng (*editor, outDir.getChildFile ("rack_settings.png")) << std::endl;
        settings->setVisible (false);

        findButton ("ON", 1)->triggerClick();   // PEAK HOLD
        findButton ("60 dB")->triggerClick();
        runWithEditor (45);
        std::cout << "rack_analyzer.png: " << writePng (*editor, outDir.getChildFile ("rack_analyzer.png")) << std::endl;
        findButton ("OFF", 1)->triggerClick();
        findButton ("90 dB")->triggerClick();

        findButton ("1 LINE")->triggerClick();   // LINE GLOW
        runWithEditor (10);
        std::cout << "rack_oneline.png: " << writePng (*editor, outDir.getChildFile ("rack_oneline.png")) << std::endl;
        findButton ("3 LINES")->triggerClick();

        for (auto* name : { "BLACK", "SILVER", "VINTAGE" })
        {
            findButton (name)->triggerClick();
            runWithEditor (10);
            const auto file = "rack_" + juce::String (name).toLowerCase() + ".png";
            std::cout << file << ": " << writePng (*editor, outDir.getChildFile (file)) << std::endl;
        }

        findButton ("150%")->triggerClick();
        runWithEditor (5);
        std::cout << "UI SIZE 150%: " << editor->getWidth() << "x" << editor->getHeight() << std::endl;
    }

    editor.reset();

    if (hadSettings)
        settingsFile.replaceWithText (savedSettings);
    else
        settingsFile.deleteFile();

    return 0;
}
