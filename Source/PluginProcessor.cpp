#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters.h"

PRIResonanceSuppressorProcessor::PRIResonanceSuppressorProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      state (*this, nullptr, "PRIResonanceSuppressor", params::createLayout())
{
    minHz = state.getRawParameterValue (params::minHz);
    maxHz = state.getRawParameterValue (params::maxHz);
    threshold = state.getRawParameterValue (params::threshold);
    cut = state.getRawParameterValue (params::cut);
    attack = state.getRawParameterValue (params::attack);
    release = state.getRawParameterValue (params::release);
    resolution = state.getRawParameterValue (params::resolution);
    width = state.getRawParameterValue (params::width);
    listen = state.getRawParameterValue (params::listen);
    silence = state.getRawParameterValue (params::silence);
    mix = state.getRawParameterValue (params::mix);
    hiResFft = state.getRawParameterValue (params::hiResFft);
    stereo = state.getRawParameterValue (params::stereo);
    bypassParam = dynamic_cast<juce::AudioParameterBool*> (state.getParameter (params::bypass));
    jassert (bypassParam != nullptr);

    // 遅延が変わるパラメータ。再生していなくても（processBlock が呼ばれなくても）ホストに伝わるよう、変わったときに報告する
    state.addParameterListener (params::resolution, this);
    state.addParameterListener (params::hiResFft, this);

    pendingLatency = 1 << currentFftOrder();
    setLatencySamples (pendingLatency.load());
}

PRIResonanceSuppressorProcessor::~PRIResonanceSuppressorProcessor()
{
    state.removeParameterListener (params::resolution, this);
    state.removeParameterListener (params::hiResFft, this);
    cancelPendingUpdate();
}

void PRIResonanceSuppressorProcessor::parameterChanged (const juce::String&, float)
{
    pendingLatency = 1 << currentFftOrder();

    if (juce::MessageManager::existsAndIsCurrentThread())
    {
        cancelPendingUpdate();
        setLatencySamples (pendingLatency.load());
    }
    else
    {
        triggerAsyncUpdate();
    }
}

// 分解能の選択（1024 / 2048 / 4096 点）に、Hi-Res FFT が ON なら 88.2 kHz 以上で 1 段、176.4 kHz 以上で 2 段足す
int PRIResonanceSuppressorProcessor::currentFftOrder() const
{
    const int selected = ResonanceSuppressor::minFftOrder + juce::jlimit (0, 2, juce::roundToInt (resolution->load()));
    const double sampleRate = preparedSampleRate.load();
    int extra = 0;

    if (hiResFft->load() >= 0.5f)
        extra = sampleRate >= 176000.0 ? 2 : (sampleRate >= 88000.0 ? 1 : 0);

    return juce::jmin (selected + extra, ResonanceSuppressor::maxFftOrder);
}

void PRIResonanceSuppressorProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    const int numChannels = getTotalNumInputChannels();
    preparedSampleRate = sampleRate;

    suppressor.fftOrderSetting = currentFftOrder();
    suppressor.prepare (sampleRate, numChannels);

    dryRings.assign ((size_t) numChannels, std::vector<float> (ResonanceSuppressor::maxFftSize, 0.0f));
    dryPosition = 0;
    dryBuffer.setSize (numChannels, juce::jmax (1, maximumExpectedSamplesPerBlock));

    wetMix.reset (sampleRate, 0.05);
    wetMix.setCurrentAndTargetValue (mix->load() / 100.0f);
    bypassMix.reset (sampleRate, 0.02);
    bypassMix.setCurrentAndTargetValue (bypassParam->get() ? 1.0f : 0.0f);

    pendingLatency = 1 << currentFftOrder();
    setLatencySamples (pendingLatency.load());

    listenBlendLength = pendingLatency.load();
    listenBlend.reset (listenBlendLength);
    listenBlend.setCurrentAndTargetValue (listen->load() >= 0.5f ? 1.0f : 0.0f);
}

void PRIResonanceSuppressorProcessor::releaseResources()
{
}

void PRIResonanceSuppressorProcessor::reset()
{
    suppressor.reset();

    for (auto& ring : dryRings)
        std::fill (ring.begin(), ring.end(), 0.0f);
}

// モノラル→モノラル、ステレオ→ステレオのみ
bool PRIResonanceSuppressorProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto output = layouts.getMainOutputChannelSet();

    if (output != juce::AudioChannelSet::mono() && output != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainInputChannelSet() == output;
}

void PRIResonanceSuppressorProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();

    // 入力より出力のチャンネルが多い場合、余った出力を消す
    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    // prepareToPlay で伝えられた長さを超えるブロックは、分けて処理する（作業用のバッファを確保し直さないため。
    // 参照するだけの AudioBuffer はメモリを確保しない）
    const int maxChunk = juce::jmax (1, dryBuffer.getNumSamples());

    if (numSamples <= maxChunk)
    {
        processChunk (buffer);
        return;
    }

    for (int start = 0; start < numSamples; start += maxChunk)
    {
        juce::AudioBuffer<float> chunk (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), start, juce::jmin (maxChunk, numSamples - start));
        processChunk (chunk);
    }
}

void PRIResonanceSuppressorProcessor::processChunk (juce::AudioBuffer<float>& buffer)
{
    const int numSamples = buffer.getNumSamples();
    const int channels = juce::jmin (getTotalNumInputChannels(), (int) dryRings.size(), buffer.getNumChannels());

    // パラメータ → DSP の設定値（検出範囲は上下が逆なら入れ替える）
    const int order = currentFftOrder();
    const float lo = minHz->load(), hi = maxHz->load();
    suppressor.minHz = juce::jmin (lo, hi);
    suppressor.maxHz = juce::jmax (lo, hi);
    suppressor.thresholdDb = threshold->load();
    suppressor.cutDb = cut->load();
    suppressor.attackSeconds = attack->load() / 1000.0f;
    suppressor.releaseSeconds = release->load() / 1000.0f;
    suppressor.fftOrderSetting = order;
    suppressor.peakWidthOctaves = 1.0f / juce::jmax (1.0f, width->load());
    suppressor.silenceThresholdDb = silence->load();
    suppressor.listenToRemovedSignal = listen->load() >= 0.5f;

    // ステレオの扱い（2 チャンネルのときだけ）。M/S は DSP の前後で変換する
    const int stereoMode = channels == 2 ? juce::jlimit (0, 2, juce::roundToInt (stereo->load())) : params::stereoLinked;
    const bool midSide = stereoMode == params::stereoMidSide;
    suppressor.linkChannels = stereoMode == params::stereoLinked;

    // 遅延の報告はメッセージスレッドで行う。ふつうは parameterChanged で報告済み
    const int latency = 1 << order;

    if (latency != pendingLatency.load())
    {
        pendingLatency = latency;
        triggerAsyncUpdate();
    }

    if (latency != listenBlendLength)
    {
        listenBlendLength = latency;
        listenBlend.reset (latency);
    }

    if (numSamples == 0 || channels == 0)
        return;

    // 元の音を DSP と同じだけ遅らせて取っておく（dryBuffer は processBlock で分けた長さに収まる）
    constexpr int mask = ResonanceSuppressor::maxFftSize - 1;

    for (int ch = 0; ch < channels; ++ch)
    {
        auto& ring = dryRings[(size_t) ch];
        const auto* in = buffer.getReadPointer (ch);
        auto* out = dryBuffer.getWritePointer (ch);
        int pos = dryPosition;

        for (int i = 0; i < numSamples; ++i)
        {
            out[i] = ring[(size_t) ((pos - latency) & mask)];
            ring[(size_t) pos] = in[i];
            pos = (pos + 1) & mask;
        }
    }

    dryPosition = (dryPosition + numSamples) & mask;

    if (midSide != suppressorInMidSide)
    {
        suppressor.convertMidSide();
        suppressorInMidSide = midSide;
    }

    if (midSide)
        ResonanceSuppressor::rotateMidSide (buffer.getWritePointer (0), buffer.getWritePointer (1), numSamples);

    suppressor.process (buffer, numSamples);

    if (midSide)
        ResonanceSuppressor::rotateMidSide (buffer.getWritePointer (0), buffer.getWritePointer (1), numSamples);

    // MIX: 元の音と処理後の音を混ぜる。デルタ試聴中は、実際に出力から減っている分（削った成分 × MIX）を出す。
    // DSP の出力は試聴を切り替えてから FFT サイズ分かけて入れ替わるので、2 つの式も同じ時間をかけて入れ替える
    wetMix.setTargetValue (mix->load() / 100.0f);
    listenBlend.setTargetValue (suppressor.listenToRemovedSignal ? 1.0f : 0.0f);

    if (wetMix.isSmoothing() || wetMix.getTargetValue() < 1.0f)
    {
        auto* const* wet = buffer.getArrayOfWritePointers();
        const auto* const* dry = dryBuffer.getArrayOfReadPointers();

        for (int i = 0; i < numSamples; ++i)
        {
            const float amount = wetMix.getNextValue();
            const float listening = listenBlend.getNextValue();

            for (int ch = 0; ch < channels; ++ch)
            {
                const float mixed = dry[ch][i] + (wet[ch][i] - dry[ch][i]) * amount;
                const float removed = wet[ch][i] * amount;
                wet[ch][i] = mixed + (removed - mixed) * listening;
            }
        }
    }
    else
    {
        listenBlend.skip (numSamples);
    }

    // バイパス: 処理後の音から遅延だけかけた元の音へクロスフェードする。DSP は止めないので、戻したときもすぐ効く
    bypassMix.setTargetValue (bypassParam->get() ? 1.0f : 0.0f);

    if (bypassMix.isSmoothing())
    {
        auto* const* wet = buffer.getArrayOfWritePointers();
        const auto* const* dry = dryBuffer.getArrayOfReadPointers();

        for (int i = 0; i < numSamples; ++i)
        {
            const float toDry = bypassMix.getNextValue();

            for (int ch = 0; ch < channels; ++ch)
                wet[ch][i] += (dry[ch][i] - wet[ch][i]) * toDry;
        }
    }
    else if (bypassMix.getTargetValue() >= 1.0f)
    {
        for (int ch = 0; ch < channels; ++ch)
            buffer.copyFrom (ch, 0, dryBuffer, ch, 0, numSamples);
    }
}

void PRIResonanceSuppressorProcessor::handleAsyncUpdate()
{
    setLatencySamples (pendingLatency.load());
}

juce::AudioProcessorEditor* PRIResonanceSuppressorProcessor::createEditor()
{
    return new PRIResonanceSuppressorEditor (*this);
}

void PRIResonanceSuppressorProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto tree = state.copyState();
    tree.setProperty (uiScaleProperty, (double) uiScale.load(), nullptr);

    if (const auto xml = tree.createXml())
        copyXmlToBinary (*xml, destData);
}

void PRIResonanceSuppressorProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (const auto xml = getXmlFromBinary (data, sizeInBytes); xml != nullptr && xml->hasTagName (state.state.getType()))
    {
        auto tree = juce::ValueTree::fromXml (*xml);

        // 倍率はエディターが開いていれば次の画面更新で反映される
        if (tree.hasProperty (uiScaleProperty))
            setUiScale ((float) (double) tree.getProperty (uiScaleProperty));

        tree.removeProperty (uiScaleProperty, nullptr);

        // デルタ試聴は確認用なので、復元時は常に OFF（聞こえ方が変わったと誤解しないように）。
        // 復元したあとに改めて変えるとホストへの変更通知になることがあるので、復元する中身のほうを書き換える
        if (auto child = tree.getChildWithProperty ("id", params::listen); child.isValid())
            child.setProperty ("value", 0.0f, nullptr);

        state.replaceState (tree);
    }

    // 保存された状態に試聴の値がなかったとき
    if (listen->load() >= 0.5f)
        if (auto* p = state.getParameter (params::listen))
            p->setValueNotifyingHost (0.0f);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PRIResonanceSuppressorProcessor();
}
