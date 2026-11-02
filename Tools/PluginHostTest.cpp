#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <iostream>

// ビルドしたプラグイン（VST3 の .vst3）をホストとして読み込んで確かめるツール。
// 1. 名前・カテゴリ・パラメータの一覧（オートメーションできるか）
// 2. 遅延の確認: 削らない設定で、バイパスの切り替えをまたいで出力が「入力を報告された遅延分ずらした音」と一致するか
// 3. 遅延の変更: RESOLUTION を変えたあと、ホスト側に新しい遅延が届くか
// 4. 状態の保存と復元: 別のインスタンスに読み込んで、パラメータが同じになるか
// 5. エディターが開けるか（大きさ）
// パソコンの表示設定は実行前の内容に戻す
// 使い方: PluginHostTest <plugin.vst3> ...
namespace
{
    void print (const juce::String& text)
    {
        std::cout << text.toStdString() << std::endl;
    }

    juce::AudioProcessorParameter* findParameter (juce::AudioPluginInstance& plugin, const juce::String& name)
    {
        for (auto* p : plugin.getParameters())
            if (p->getName (64) == name)
                return p;

        print ("  ! parameter not found: " + name);
        return nullptr;
    }

    void setNormalised (juce::AudioPluginInstance& plugin, const juce::String& name, float value)
    {
        if (auto* p = findParameter (plugin, name))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (value);
            p->endChangeGesture();
        }
    }

    // プラグインからの非同期の通知（遅延の報告など）を処理させる
    void pumpMessages (int milliseconds)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (milliseconds);
    }

    std::unique_ptr<juce::AudioPluginInstance> load (juce::AudioPluginFormatManager& formats, const juce::PluginDescription& description,
                                                     double sampleRate, int blockSize)
    {
        juce::String error;
        auto plugin = formats.createPluginInstance (description, sampleRate, blockSize, error);

        if (plugin == nullptr)
            print ("  ! cannot create plugin: " + error);

        return plugin;
    }

    // 共振を足さないノイズ（決まった種）を、全チャンネルに同じ値で入れる
    void fillNoise (juce::Random& random, juce::AudioBuffer<float>& buffer)
    {
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const float sample = (random.nextFloat() * 2.0f - 1.0f) * 0.25f;

            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                buffer.setSample (ch, i, sample);
        }
    }

    // しきい値 20 dB（最大）、カット量 1 dB（最小）で 3 秒処理し、1 秒と 2 秒でバイパスを切り替える。
    // 「出力 − 入力を遅延分ずらした音」の最大値を、入力の RMS に対する dB で返す
    float checkAlignment (juce::AudioPluginFormatManager& formats, const juce::PluginDescription& description,
                          float resolutionNormalised, double sampleRate, int& latency)
    {
        constexpr int blockSize = 480;

        auto plugin = load (formats, description, sampleRate, blockSize);
        if (plugin == nullptr)
            return 0.0f;

        setNormalised (*plugin, "Threshold", 1.0f);
        setNormalised (*plugin, "Depth", 0.0f);
        setNormalised (*plugin, "Resolution", resolutionNormalised);

        plugin->setPlayConfigDetails (2, 2, sampleRate, blockSize);
        plugin->prepareToPlay (sampleRate, blockSize);
        pumpMessages (50);
        latency = plugin->getLatencySamples();

        juce::Random random (1);
        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;
        std::vector<float> input, output;
        auto* bypass = findParameter (*plugin, "Bypass");

        for (int n = 0; n < juce::roundToInt (3.0 * sampleRate / blockSize); ++n)
        {
            const double t = n * blockSize / sampleRate;
            if (bypass != nullptr)
                bypass->setValue (t >= 1.0 && t < 2.0 ? 1.0f : 0.0f);

            fillNoise (random, buffer);
            input.insert (input.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + blockSize);
            plugin->processBlock (buffer, midi);
            output.insert (output.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + blockSize);
        }

        plugin->releaseResources();

        double sumSquares = 0.0, maxError = 0.0;

        for (size_t i = (size_t) latency * 3; i < output.size(); ++i)
        {
            sumSquares += (double) input[i] * input[i];
            maxError = juce::jmax (maxError, (double) std::abs (output[i] - input[i - (size_t) latency]));
        }

        const double rms = std::sqrt (sumSquares / (double) (output.size() - (size_t) latency * 3));
        return juce::Decibels::gainToDecibels ((float) (maxError / rms), -200.0f);
    }

    void checkPlugin (juce::AudioPluginFormatManager& formats, const juce::File& file)
    {
        juce::OwnedArray<juce::PluginDescription> descriptions;

        for (auto* format : formats.getFormats())
            if (format->fileMightContainThisPluginType (file.getFullPathName()))
                format->findAllTypesForFile (descriptions, file.getFullPathName());

        if (descriptions.isEmpty())
        {
            print ("! no plugin found in " + file.getFullPathName());
            return;
        }

        const auto& description = *descriptions[0];
        print ("== " + description.pluginFormatName + ": " + description.name + " / " + description.manufacturerName
               + " / " + description.category + " / version " + description.version);

        constexpr double sampleRate = 48000.0;
        constexpr int blockSize = 512;

        // 1. パラメータの一覧
        {
            auto plugin = load (formats, description, sampleRate, blockSize);
            if (plugin == nullptr)
                return;

            print ("  parameters: " + juce::String (plugin->getParameters().size()));
            for (auto* p : plugin->getParameters())
                print ("    " + p->getName (64).paddedRight (' ', 14) + p->getCurrentValueAsText().paddedRight (' ', 12)
                       + (p->isAutomatable() ? "automatable" : "not automatable"));
        }

        // 2. 遅延と、バイパスをまたいだ一致
        const std::pair<float, double> alignmentCases[] = { { 0.0f, 48000.0 }, { 0.5f, 48000.0 }, { 1.0f, 48000.0 }, { 1.0f, 96000.0 } };

        for (const auto& [resolution, rate] : alignmentCases)
        {
            int latency = 0;
            const float error = checkAlignment (formats, description, resolution, rate, latency);
            print ("  alignment (resolution " + juce::String (resolution, 1) + " @ " + juce::String (rate / 1000.0) + " kHz, latency "
                   + juce::String (latency) + "): max error " + juce::String (error, 1) + " dB (re RMS)");
        }

        // 3. 再生中に RESOLUTION を変えたときの遅延の報告
        {
            auto plugin = load (formats, description, sampleRate, blockSize);
            if (plugin == nullptr)
                return;

            plugin->setPlayConfigDetails (2, 2, sampleRate, blockSize);
            plugin->prepareToPlay (sampleRate, blockSize);
            const int before = plugin->getLatencySamples();

            setNormalised (*plugin, "Resolution", 1.0f);
            juce::AudioBuffer<float> buffer (2, blockSize);
            juce::MidiBuffer midi;
            buffer.clear();
            plugin->processBlock (buffer, midi);
            pumpMessages (200);

            print ("  latency change: " + juce::String (before) + " -> " + juce::String (plugin->getLatencySamples()));
            plugin->releaseResources();
        }

        // 4. 状態の保存と復元
        {
            auto source = load (formats, description, sampleRate, blockSize);
            auto target = load (formats, description, sampleRate, blockSize);
            if (source == nullptr || target == nullptr)
                return;

            const std::pair<const char*, float> changes[] = { { "Low Freq", 0.3f }, { "High Freq", 0.8f }, { "Threshold", 0.25f },
                                                              { "Resolution", 1.0f }, { "Width", 0.6f }, { "Hi-Res FFT", 0.0f } };
            for (const auto& [name, value] : changes)
                setNormalised (*source, name, value);

            juce::MemoryBlock data;
            source->getStateInformation (data);
            target->setStateInformation (data.getData(), (int) data.getSize());
            pumpMessages (50);

            int mismatches = 0;
            const auto& sourceParams = source->getParameters();
            const auto& targetParams = target->getParameters();

            // 値は表示の文字で比べる（VST3 のホスト側は、プラグインが刻みに丸める前の値を覚えているため）
            for (int i = 0; i < juce::jmin (sourceParams.size(), targetParams.size()); ++i)
            {
                if (sourceParams[i]->getCurrentValueAsText() != targetParams[i]->getCurrentValueAsText())
                {
                    ++mismatches;
                    print ("    ! " + sourceParams[i]->getName (64) + ": " + sourceParams[i]->getCurrentValueAsText() + " -> "
                           + targetParams[i]->getCurrentValueAsText());
                }
            }

            print ("  state: " + juce::String ((int) data.getSize()) + " bytes, mismatches " + juce::String (mismatches)
                   + " (Low " + findParameter (*target, "Low Freq")->getCurrentValueAsText()
                   + ", Resolution " + findParameter (*target, "Resolution")->getCurrentValueAsText()
                   + ", Hi-Res FFT " + findParameter (*target, "Hi-Res FFT")->getCurrentValueAsText() + ")");
        }

        // 5. エディター
        {
            auto plugin = load (formats, description, sampleRate, blockSize);
            if (plugin == nullptr)
                return;

            std::unique_ptr<juce::AudioProcessorEditor> editor (plugin->createEditorAndMakeActive());
            pumpMessages (100);

            if (editor != nullptr)
                print ("  editor: " + juce::String (editor->getWidth()) + " x " + juce::String (editor->getHeight()));
            else
                print ("  ! editor: none");
        }
    }
}

int main (int argc, char* argv[])
{
    // プラグインの読み込みはメッセージスレッドを前提にしているので、このスレッドをメッセージスレッドにする
    juce::ScopedJuceInitialiser_GUI juceInit;

    if (argc < 2)
    {
        print ("Usage: PluginHostTest <plugin.vst3> ...");
        return 1;
    }

    // エディターを開くとパソコンの表示設定を読むので、念のため退避して最後に戻す
    const auto settingsFile = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                                  .getChildFile ("PRITEZZA").getChildFile ("PRIResonanceSuppressor.settings");
    const bool hadSettings = settingsFile.existsAsFile();
    const auto savedSettings = hadSettings ? settingsFile.loadFileAsString() : juce::String();

    juce::AudioPluginFormatManager formats;
    formats.addFormat (std::make_unique<juce::VST3PluginFormat>());

    const auto cwd = juce::File::getCurrentWorkingDirectory();
    for (int i = 1; i < argc; ++i)
        checkPlugin (formats, cwd.getChildFile (juce::String::fromUTF8 (argv[i])));

    if (hadSettings)
        settingsFile.replaceWithText (savedSettings);
    else
        settingsFile.deleteFile();

    return 0;
}
