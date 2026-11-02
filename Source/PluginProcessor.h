#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "ResonanceSuppressor.h"

// PRI Resonance Suppressor 本体。DSP は ResonanceSuppressor。
// 遅延は常に FFT サイズ分。バイパス中も同じ遅延をかけた元の音を出す（DAW の遅延補正がずれないように）
class PRIResonanceSuppressorProcessor final : public juce::AudioProcessor,
                                              private juce::AudioProcessorValueTreeState::Listener,
                                              private juce::AsyncUpdater
{
public:
    PRIResonanceSuppressorProcessor();
    ~PRIResonanceSuppressorProcessor() override;

    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // DAW のバイパスボタンもこのパラメータを使う（processBlockBypassed() は呼ばれなくなる）
    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }

    juce::AudioProcessorValueTreeState& getState() { return state; }
    const ResonanceSuppressor& getSuppressor() const { return suppressor; }
    bool isBypassed() const { return bypassParam->get(); }

    // 実際に使う FFT の次数（Hi-Res FFT で倍にした後）
    int currentFftOrder() const;

    // エディターの倍率（0.5〜1.5）。プロジェクトに保存する。状態の保存はホストが別のスレッドで呼ぶことがあるので、
    // APVTS の ValueTree ではなくここに持ち、保存・復元のときだけ ValueTree とやりとりする
    float getUiScale() const       { return uiScale.load(); }
    void setUiScale (float scale)  { uiScale = juce::jlimit (0.5f, 1.5f, scale); }

private:
    void processChunk (juce::AudioBuffer<float>& buffer);
    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void handleAsyncUpdate() override;

    static constexpr const char* uiScaleProperty = "uiScale";

    juce::AudioProcessorValueTreeState state;
    ResonanceSuppressor suppressor;

    std::atomic<float>* minHz = nullptr;
    std::atomic<float>* maxHz = nullptr;
    std::atomic<float>* threshold = nullptr;
    std::atomic<float>* cut = nullptr;
    std::atomic<float>* attack = nullptr;
    std::atomic<float>* release = nullptr;
    std::atomic<float>* resolution = nullptr;
    std::atomic<float>* width = nullptr;
    std::atomic<float>* listen = nullptr;
    std::atomic<float>* silence = nullptr;
    std::atomic<float>* mix = nullptr;
    std::atomic<float>* hiResFft = nullptr;
    std::atomic<float>* stereo = nullptr;
    juce::AudioParameterBool* bypassParam = nullptr;
    std::atomic<double> preparedSampleRate { 0.0 };
    std::atomic<float> uiScale { 1.0f };

    // MIX とバイパス用: 元の音を DSP と同じサンプル数だけ遅らせて取っておく。
    // MIX は 50 ms で、バイパスは 20 ms で切り替わる
    std::vector<std::vector<float>> dryRings;
    int dryPosition = 0;
    juce::AudioBuffer<float> dryBuffer;
    juce::SmoothedValue<float> wetMix, bypassMix;

    // デルタ試聴の切り替えは、DSP の出力が FFT サイズ分かけて入れ替わるので、MIX の式も同じ時間をかけて切り替える
    juce::SmoothedValue<float> listenBlend;
    int listenBlendLength = 0;

    // DSP に M/S で渡しているか（DSP の中身はこの形で入っている。切り替えたら DSP の中身も変換する）
    bool suppressorInMidSide = false;

    std::atomic<int> pendingLatency { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PRIResonanceSuppressorProcessor)
};
