#pragma once

#include <juce_dsp/juce_dsp.h>

#include <array>
#include <atomic>
#include <cmath>
#include <memory>
#include <vector>

// スペクトル方式の共振抑制（STFT: Hann 窓で分析・合成、ホップは FFT サイズの 1/4）。
// FFT サイズ（周波数分解能）は 1024〜16384 点。プラグインでは 1024 / 2048 / 4096 点から選び、88.2 kHz 以上では 2 倍・4 倍にする
// （周波数の刻みと遅延の時間を 48 kHz のときと同じに保つため）。大きいほど細かい周波数で見られるが遅延が増える
// （CPU は、1 フレームの計算が増える分フレーム数が減るのでほぼ変わらない）。
// フレームごとに、スペクトル（dB）を
//   中心: ±検出幅/2 の平均
//   周囲: 下側と上側それぞれ「検出幅 × 2」〜「検出幅 × 2 ＋ 2/3 オクターブ」の平均（の平均）
// でならし、「中心 − 周囲」を突出量とする。検出幅を狭くすると、細く飛び出した山だけを突出とみなす。
// ただし MP3 の高域カットやローカットのような「崖」の手前を突出と見誤らないよう、
//   ・音のある範囲（端から見て、1 オクターブ内側より 12 dB 以上低いところは音なし）の外は周囲の平均に入れない
//     （片側に音がなければ、もう片側だけで比べる）
//   ・それでも下側と上側の差が大きいときは、周囲を大きいほうに寄せる（差 12 dB から寄せ始め、24 dB 以上で大きいほうそのもの）
// 検出範囲内で
//   削る量 = min(カット量, 突出量 − しきい値)
// を周波数方向に ±検出幅/2 でならし、ビンごとにかかる時間／戻る時間で時間方向にもならしてから掛ける。
// 遅延は FFT サイズ分（FFT サイズの切り替え時は音が少し飛ぶ）。
// バイパスは呼び出し側で行う（遅延を変えないよう、同じだけ遅らせた元の音とクロスフェードする）。
// 無音のフレームは FFT を省略して遅延だけかけた音を出す（遅延量は変えない）。
// 複数チャンネルは、linkChannels が true なら全チャンネル平均のパワーで 1 つの削る量を求めて全チャンネルに掛け、
// false ならチャンネルごとに求めて掛ける（M/S で処理するときは、呼び出し側が M/S に変換してから渡す）。
class ResonanceSuppressor
{
public:
    static constexpr int minFftOrder = 10, maxFftOrder = 14;
    static constexpr int maxFftSize = 1 << maxFftOrder;
    static constexpr int maxBins = maxFftSize / 2 + 1;

    enum class State { off, silent, none, cutting };

    ResonanceSuppressor()
    {
        for (int order = minFftOrder; order <= maxFftOrder; ++order)
        {
            const int size = 1 << order;
            auto& w = windows[(size_t) (order - minFftOrder)];
            w.resize ((size_t) size);

            for (int i = 0; i < size; ++i)
                w[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) size);

            ffts[(size_t) (order - minFftOrder)] = std::make_unique<juce::dsp::FFT> (order);
        }
    }

    // 設定（メッセージスレッドから書く）
    std::atomic<int> fftOrderSetting { minFftOrder };      // 10〜14（1024〜16384 点）
    std::atomic<float> peakWidthOctaves { 1.0f / 6.0f };   // 検出幅（オクターブ）
    std::atomic<float> minHz { 3000.0f }, maxHz { 5000.0f };
    std::atomic<float> thresholdDb { 6.0f };       // 周囲よりこれ以上突出した分を削る
    std::atomic<float> cutDb { 9.0f };             // 削る量の上限
    std::atomic<float> attackSeconds { 0.02f };    // 削り始める速さ（時定数）
    std::atomic<float> releaseSeconds { 0.15f };   // 戻る速さ（時定数）
    std::atomic<float> silenceThresholdDb { -60.0f }; // フレームの RMS がこれ未満なら無音とみなして FFT を省略する
    std::atomic<bool> linkChannels { true };       // true: 全チャンネル共通の削る量、false: チャンネルごと

    // 試聴: 出力を「元の音 − 抑制後の音」（＝実際に抑制している分の音）に置き換える。抑制していないときは無音
    std::atomic<bool> listenToRemovedSignal { false };

    // 状態（メッセージスレッドから読む）
    State getState() const                  { return state.load(); }
    float getPeakReductionDb() const        { return peakReductionDb.load(); }
    float getPeakReductionHz() const        { return peakReductionHz.load(); }
    double getSampleRate() const            { return sampleRateForUi.load(); }

    // スペクトル表示用（ビンごと）。入力レベルは dBFS（正弦波のピークが 0 dB）、削る量は dB（正の値）
    int getDisplayNumBins() const               { return displayNumBins.load(); }
    float getDisplayBinHz() const               { return displayBinHz.load(); }
    float getDisplayLevelDb (int bin) const     { return displayLevelDb[(size_t) bin].load (std::memory_order_relaxed); }
    float getDisplayReductionDb (int bin) const { return displayReductionDb[(size_t) bin].load (std::memory_order_relaxed); }

    // オーディオスレッドが止まっているときに呼ぶ
    void prepare (double newSampleRate, int numChannelsToUse)
    {
        sampleRate = newSampleRate;
        sampleRateForUi = newSampleRate;
        numChannels = numChannelsToUse;

        inputRings.assign ((size_t) numChannels, std::vector<float> (maxFftSize, 0.0f));
        outputAccumulators.assign ((size_t) numChannels, std::vector<float> (maxFftSize, 0.0f));
        spectra.assign ((size_t) numChannels, std::vector<float> (maxFftSize * 2, 0.0f));
        levelDb.assign (maxBins, 0.0f);
        prefixSum.assign (maxBins + 1, 0.0);
        targetReduction.assign (maxBins, 0.0f);
        reductions.assign ((size_t) juce::jmax (1, numChannels), std::vector<float> (maxBins, 0.0f));
        fineRanges.resize (maxBins);
        lowerRanges.resize (maxBins);
        upperRanges.resize (maxBins);

        configure (fftOrderSetting.load(), peakWidthOctaves.load());
        reset();
    }

    void reset()
    {
        for (auto& ring : inputRings)
            std::fill (ring.begin(), ring.end(), 0.0f);

        for (auto& accumulator : outputAccumulators)
            std::fill (accumulator.begin(), accumulator.end(), 0.0f);

        for (auto& r : reductions)
            std::fill (r.begin(), r.end(), 0.0f);

        position = 0;
        hopCounter = 0;
        clearDisplay();
        peakReductionDb = 0.0f;
        state = State::off;
    }

    void process (juce::AudioBuffer<float>& buffer, int numSamples)
    {
        if (sampleRate <= 0.0)
            return;

        // FFT サイズが変わったら作り直す（遅延が変わるので音は少し飛ぶ）。検出幅だけなら範囲を計算し直す
        const int order = juce::jlimit (minFftOrder, maxFftOrder, fftOrderSetting.load());
        const float width = juce::jlimit (1.0f / 96.0f, 1.0f, peakWidthOctaves.load());

        if (order != fftOrder)
        {
            configure (order, width);
            reset();
        }
        else if (width != configuredWidth)
        {
            configure (order, width);
        }

        const int channels = juce::jmin (buffer.getNumChannels(), numChannels);

        for (int i = 0; i < numSamples; ++i)
        {
            for (int ch = 0; ch < channels; ++ch)
            {
                auto* data = buffer.getWritePointer (ch);
                auto& accumulator = outputAccumulators[(size_t) ch];

                inputRings[(size_t) ch][(size_t) position] = data[i];
                data[i] = accumulator[(size_t) position];
                accumulator[(size_t) position] = 0.0f;
            }

            position = (position + 1) & (fftSize - 1);

            if (++hopCounter >= hopSize)
            {
                hopCounter = 0;
                processFrame (channels);
            }
        }
    }

    // 最初の 2 チャンネルの中身（入力の履歴と出力の重ね合わせ途中の音）を L/R ⇔ M/S に変換する。
    // 呼び出し側が渡す音を L/R と M/S で切り替えるときに、オーディオスレッドから呼ぶ（変換は線形なので、処理途中の音もそのまま続く）。
    // M = (L + R) / √2、S = (L − R) / √2。逆変換も同じ式。
    // チャンネルごとの削る量は変換できない（L の削る量が M に合うわけではない）ので、2 チャンネルとも大きいほうにそろえる
    void convertMidSide()
    {
        if (numChannels < 2)
            return;

        for (auto* pair : { &inputRings, &outputAccumulators })
            rotateMidSide ((*pair)[0].data(), (*pair)[1].data(), maxFftSize);

        auto& first = reductions[0];
        auto& second = reductions[1];

        for (size_t k = 0; k < first.size(); ++k)
            first[k] = second[k] = juce::jmax (first[k], second[k]);
    }

    static void rotateMidSide (float* a, float* b, int numSamples)
    {
        constexpr float c = juce::MathConstants<float>::sqrt2 * 0.5f;

        for (int i = 0; i < numSamples; ++i)
        {
            const float x = a[i], y = b[i];
            a[i] = (x + y) * c;
            b[i] = (x - y) * c;
        }
    }

private:
    // Hann 窓で分析・合成し、ホップ 1/4 で重ね合わせると窓の二乗和が 1.5 になる
    static constexpr float overlapAddGain = 1.0f / 1.5f;

    // FFT サイズと検出幅に応じて、各ビンの平均範囲を計算し直す（確保はしない）
    void configure (int order, float widthOctaves)
    {
        fftOrder = order;
        fftSize = 1 << order;
        hopSize = fftSize / 4;
        numBins = fftSize / 2 + 1;
        binHz = (float) (sampleRate / fftSize);
        configuredWidth = widthOctaves;
        window = windows[(size_t) (order - minFftOrder)].data();
        fft = ffts[(size_t) (order - minFftOrder)].get();

        const float half = widthOctaves * 0.5f;
        const float inner = widthOctaves * 2.0f, outer = inner + 2.0f / 3.0f;

        auto binRange = [this] (int k, float fromOctaves, float toOctaves)
        {
            const float hz = (float) k * binHz;
            const int k0 = juce::jlimit (1, numBins - 1, juce::roundToInt (hz * std::exp2 (fromOctaves) / binHz));
            const int k1 = juce::jlimit (k0, numBins - 1, juce::roundToInt (hz * std::exp2 (toOctaves) / binHz));
            return std::pair<int, int> { k0, k1 };
        };

        for (int k = 0; k < numBins; ++k)
        {
            fineRanges[(size_t) k] = binRange (k, -half, half);
            lowerRanges[(size_t) k] = binRange (k, -outer, -inner);
            upperRanges[(size_t) k] = binRange (k, inner, outer);
        }

        displayNumBins = numBins;
        displayBinHz = binHz;
    }

    // position は次に書く位置 = 最も古いサンプル。出力は position から fftSize サンプル先まで重ね合わせる
    void processFrame (int channels)
    {
        const int mask = fftSize - 1;
        double sumSquares = 0.0;

        for (int ch = 0; ch < channels; ++ch)
        {
            const auto& ring = inputRings[(size_t) ch];
            auto& spectrum = spectra[(size_t) ch];

            for (int i = 0; i < fftSize; ++i)
            {
                const float sample = ring[(size_t) ((position + i) & mask)];
                sumSquares += (double) sample * sample;
                spectrum[(size_t) i] = sample * window[i];
            }
        }

        const bool listen = listenToRemovedSignal.load();

        if (std::sqrt (sumSquares / (fftSize * juce::jmax (1, channels))) < juce::Decibels::decibelsToGain (silenceThresholdDb.load()))
        {
            // 無音: FFT を省略。削る量は戻し、遅延だけかけた音（＝窓を 2 回かけて重ね合わせ）を出す
            for (auto& r : reductions)
                std::fill (r.begin(), r.end(), 0.0f);

            state = State::silent;
            peakReductionDb = 0.0f;
            clearDisplay();

            if (! listen)
                for (int ch = 0; ch < channels; ++ch)
                    overlapAdd (ch, spectra[(size_t) ch].data());

            return;
        }

        for (int ch = 0; ch < channels; ++ch)
        {
            auto& spectrum = spectra[(size_t) ch];
            std::fill (spectrum.begin() + fftSize, spectrum.begin() + fftSize * 2, 0.0f);
            fft->performRealOnlyForwardTransform (spectrum.data(), true);
        }

        computeReduction (channels);

        for (int ch = 0; ch < channels; ++ch)
        {
            auto* spectrum = spectra[(size_t) ch].data();
            const auto& reduction = reductions[(size_t) (linkedFrame ? 0 : ch)];

            for (int k = 0; k < numBins; ++k)
            {
                const float gain = juce::Decibels::decibelsToGain (-reduction[(size_t) k]);
                const float applied = listen ? 1.0f - gain : gain; // 試聴は抑制分 = 元 × (1 − ゲイン)
                spectrum[2 * k] *= applied;
                spectrum[2 * k + 1] *= applied;
            }

            fft->performRealOnlyInverseTransform (spectrum);
            overlapAdd (ch, spectrum);
        }
    }

    // 合成窓をかけて重ね合わせる
    void overlapAdd (int ch, const float* frame)
    {
        auto& accumulator = outputAccumulators[(size_t) ch];
        const int mask = fftSize - 1;

        for (int i = 0; i < fftSize; ++i)
            accumulator[(size_t) ((position + i) & mask)] += frame[i] * window[i] * overlapAddGain;
    }

    void computeReduction (int channels)
    {
        // 表示用に dBFS へ換算する係数: Hann 窓の振幅利得 fftSize/4
        const float toDbfs = -20.0f * std::log10 ((float) fftSize / 4.0f);
        const bool linked = linkChannels.load() || channels < 2;

        // 共通 ⇔ チャンネルごとを切り替えたときは、今の削る量を引き継ぐ（共通にするときは各チャンネルの大きいほう）
        if (linked != wasLinked)
        {
            for (int ch = 1; ch < channels; ++ch)
            {
                auto& first = reductions[0];
                auto& other = reductions[(size_t) ch];

                for (int k = 0; k < numBins; ++k)
                {
                    if (linked)
                        first[(size_t) k] = juce::jmax (first[(size_t) k], other[(size_t) k]);
                    else
                        other[(size_t) k] = first[(size_t) k];
                }
            }

            wasLinked = linked;
        }

        linkedFrame = linked;

        // 表示用の入力レベルは、どの扱いでも全チャンネル平均（中心と同じ幅でならす）。prefixSum を使い回す前に求めておく
        computeLevels (0, channels);

        for (int k = 0; k < numBins; ++k)
            displayLevelDb[(size_t) k].store ((float) bandMean (fineRanges[(size_t) k]) + toDbfs, std::memory_order_relaxed);

        float peakValue = 0.0f;
        int peakBin = 0;

        if (linked)
        {
            updateReduction (reductions[0], true, peakValue, peakBin);
        }
        else
        {
            for (int ch = 0; ch < channels; ++ch)
            {
                computeLevels (ch, ch + 1);
                updateReduction (reductions[(size_t) ch], ch == 0, peakValue, peakBin);
            }
        }

        peakReductionDb = peakValue;
        peakReductionHz = (float) peakBin * binHz;
        state = peakValue >= 0.5f ? State::cutting : State::none;
    }

    // チャンネル firstChannel〜endChannel−1 の平均パワー（dB）を levelDb と prefixSum に入れる
    void computeLevels (int firstChannel, int endChannel)
    {
        const float count = (float) juce::jmax (1, endChannel - firstChannel);

        for (int k = 0; k < numBins; ++k)
        {
            float power = 0.0f;

            for (int ch = firstChannel; ch < endChannel; ++ch)
            {
                const float re = spectra[(size_t) ch][2 * (size_t) k], im = spectra[(size_t) ch][2 * (size_t) k + 1];
                power += re * re + im * im;
            }

            levelDb[(size_t) k] = 10.0f * std::log10 (power / count + 1.0e-20f);
            prefixSum[(size_t) k + 1] = prefixSum[(size_t) k] + levelDb[(size_t) k];
        }
    }

    // prefixSum（computeLevels のレベル）から削る量の目標を求め、reduction を時間方向に近づける。
    // 表示用の削る量と最大の削減位置は、チャンネルごとのときは全チャンネルの最大（firstGroup で表示を書き直し始める）
    void updateReduction (std::vector<float>& reduction, bool firstGroup, float& peakValue, int& peakBin)
    {
        const float nyquist = (float) sampleRate * 0.5f;
        const int kMin = juce::jlimit (1, numBins - 1, (int) std::ceil (juce::jmin (minHz.load(), nyquist) / binHz));
        const int kMax = juce::jlimit (kMin, numBins - 1, (int) std::floor (juce::jmin (maxHz.load(), nyquist) / binHz));
        const float threshold = thresholdDb.load(), cut = juce::jmax (0.0f, cutDb.load());

        // 削る量の目標（検出範囲内のみ）
        std::fill (targetReduction.begin(), targetReduction.begin() + numBins, 0.0f);

        // 音のある範囲。この外（MP3 の高域カットより上、ローカットより下）は周囲の平均に入れない
        const auto [contentLow, contentHigh] = findContentRange();

        // 周囲の下側と上側の差がこの範囲で、周囲を左右の平均から大きいほうへ寄せる（崖の扱い。冒頭のコメント参照）。
        // ふつうの傾き（-6 dB/oct で検出幅 1/6 oct なら差 8 dB）は寄せない
        constexpr double edgeStartDb = 12.0, edgeFullDb = 24.0;

        for (int k = kMin; k <= kMax; ++k)
        {
            const std::pair<int, int> lowerRange { juce::jmax (lowerRanges[(size_t) k].first, contentLow), lowerRanges[(size_t) k].second };
            const std::pair<int, int> upperRange { upperRanges[(size_t) k].first, juce::jmin (upperRanges[(size_t) k].second, contentHigh) };
            const bool hasLower = lowerRange.first <= lowerRange.second, hasUpper = upperRange.first <= upperRange.second;

            if (! hasLower && ! hasUpper)
                continue;

            // 片側に音がないときは、もう片側だけで比べる
            const double peak = bandMean (fineRanges[(size_t) k]);
            const double lower = bandMean (hasLower ? lowerRange : upperRange), upper = bandMean (hasUpper ? upperRange : lowerRange);
            const double mean = 0.5 * (lower + upper);
            const double edge = juce::jlimit (0.0, 1.0, (std::abs (lower - upper) - edgeStartDb) / (edgeFullDb - edgeStartDb));
            const double surround = mean + (juce::jmax (lower, upper) - mean) * edge;
            targetReduction[(size_t) k] = juce::jlimit (0.0f, cut, (float) (peak - surround) - threshold);
        }

        // 周波数方向にならす（±検出幅/2 の平均）。prefixSum を使い回す
        for (int k = 0; k < numBins; ++k)
            prefixSum[(size_t) k + 1] = prefixSum[(size_t) k] + targetReduction[(size_t) k];

        const float dt = (float) hopSize / (float) sampleRate;
        const float attack = 1.0f - std::exp (-dt / juce::jmax (0.001f, attackSeconds.load()));
        const float release = 1.0f - std::exp (-dt / juce::jmax (0.001f, releaseSeconds.load()));

        for (int k = 0; k < numBins; ++k)
        {
            const float target = (float) bandMean (fineRanges[(size_t) k]);
            auto& current = reduction[(size_t) k];
            current += (target - current) * (target > current ? attack : release);

            if (current > peakValue)
            {
                peakValue = current;
                peakBin = k;
            }

            auto& shown = displayReductionDb[(size_t) k];
            shown.store (firstGroup ? current : juce::jmax (current, shown.load (std::memory_order_relaxed)), std::memory_order_relaxed);
        }
    }

    // 音のある範囲（ビンの番号）を、prefixSum（computeLevels のレベル）から求める。
    // 上の端から下へ見ていき、そのあたり（±1/12 オクターブ）の平均が 1 オクターブ下〜1/6 オクターブ下の平均より
    // deadDb 以上低いビンは「音がない」（MP3 の高域カットより上など）とみなす。下の端からも同じように上へ見る。
    // deadDb を大きくすると、カットの手前の落ち始め（数 dB〜十数 dB 下がったところ）を音ありとして周囲の平均に入れてしまい、
    // その手前を削りすぎる（16 次のローパスで試して、30 dB では THRESHOLD 1 dB で 3.3 dB、12 dB では 1.2 dB 削った）
    std::pair<int, int> findContentRange() const
    {
        constexpr double deadDb = 12.0;

        auto mean = [this] (double from, double to)
        {
            const int k0 = juce::jlimit (1, numBins - 1, (int) std::floor (from));
            const int k1 = juce::jlimit (k0, numBins - 1, (int) std::ceil (to));
            return bandMean ({ k0, k1 });
        };

        int high = numBins - 1;

        for (; high > 1; --high)
        {
            const double k = high;

            if (mean (k * 0.944, k * 1.059) > mean (k * 0.5, k * 0.891) - deadDb)
                break;
        }

        int low = 1;

        for (; low < high; ++low)
        {
            const double k = low;

            if (mean (k * 0.944, k * 1.059) > mean (k * 1.122, k * 2.0) - deadDb)
                break;
        }

        return { low, high };
    }

    double bandMean (std::pair<int, int> range) const
    {
        return (prefixSum[(size_t) range.second + 1] - prefixSum[(size_t) range.first]) / (range.second - range.first + 1);
    }

    void clearDisplay()
    {
        for (int k = 0; k < maxBins; ++k)
        {
            displayLevelDb[(size_t) k].store (-120.0f, std::memory_order_relaxed);
            displayReductionDb[(size_t) k].store (0.0f, std::memory_order_relaxed);
        }
    }

    double sampleRate = 0.0;
    int numChannels = 0;

    // 現在の FFT 設定
    int fftOrder = minFftOrder, fftSize = 1 << minFftOrder, hopSize = fftSize / 4, numBins = fftSize / 2 + 1;
    float binHz = 1.0f, configuredWidth = 0.0f;
    const float* window = nullptr;
    juce::dsp::FFT* fft = nullptr;

    std::array<std::vector<float>, maxFftOrder - minFftOrder + 1> windows;
    std::array<std::unique_ptr<juce::dsp::FFT>, maxFftOrder - minFftOrder + 1> ffts;

    std::vector<std::vector<float>> inputRings, outputAccumulators, spectra;
    int position = 0, hopCounter = 0;

    std::vector<float> levelDb, targetReduction;
    std::vector<std::vector<float>> reductions;   // チャンネルごとの削る量（共通のときは 0 番だけ使う）
    bool wasLinked = true, linkedFrame = true;
    std::vector<double> prefixSum;
    std::vector<std::pair<int, int>> fineRanges, lowerRanges, upperRanges;

    std::atomic<State> state { State::off };
    std::atomic<float> peakReductionDb { 0.0f }, peakReductionHz { 0.0f };
    std::atomic<double> sampleRateForUi { 0.0 };
    std::atomic<int> displayNumBins { 0 };
    std::atomic<float> displayBinHz { 1.0f };
    std::array<std::atomic<float>, maxBins> displayLevelDb {}, displayReductionDb {};
};
