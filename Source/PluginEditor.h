#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "LcdDisplay.h"
#include "PluginProcessor.h"
#include "RackControls.h"
#include "RackStyle.h"
#include "SettingsOverlay.h"

// パネルの動かない部分（地、耳、液晶の枠、ロゴ、見出し）。光の表現に手間がかかるので、絵として作り置きする
class PanelFace final : public juce::Component
{
public:
    explicit PanelFace (const rack::Theme& themeToUse) : theme (themeToUse)
    {
        setOpaque (true);
        setInterceptsMouseClicks (false, false);
        setBufferedToImage (true);
    }

    void paint (juce::Graphics&) override;

private:
    void paintEar (juce::Graphics&, float left, bool isLeft) const;
    void paintBrand (juce::Graphics&) const;

    const rack::Theme& theme;
};

// ラック実機風のパネル。原寸 1280×608 で配置し、エディターが全体を拡大・縮小する
class RackPanel final : public juce::Component
{
public:
    static constexpr int designWidth = 1280, designHeight = 608;

    RackPanel (PRIResonanceSuppressorProcessor&, const rack::Theme&);

    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

    // テーマを変えたあとに呼ぶ（作り置きの絵を描き直す）
    void refreshTheme();

    PanelFace face;
    rack::LcdDisplay lcd;
    rack::GainReductionMeter grMeter;
    rack::SettingsOverlay settings;

private:
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    const rack::Theme& theme;
    rack::Knob lowKnob, highKnob, widthKnob;
    rack::Knob thresholdKnob, depthKnob;
    rack::Knob attackKnob, releaseKnob;
    rack::Knob mixKnob;
    rack::ResolutionSwitch resolutionSwitch;
    rack::LeverSwitch listenSwitch;
    rack::BypassButton bypassButton;
    rack::GearButton gearButton;
    ButtonAttachment listenAttachment, bypassAttachment;
};

class PRIResonanceSuppressorEditor final : public juce::AudioProcessorEditor,
                                           private juce::Timer
{
public:
    explicit PRIResonanceSuppressorEditor (PRIResonanceSuppressorProcessor&);
    ~PRIResonanceSuppressorEditor() override;

    void resized() override;
    void parentHierarchyChanged() override;

private:
    void timerCallback() override;
    void applyOptions (const rack::DisplayOptions&, bool save);

    PRIResonanceSuppressorProcessor& pluginProcessor;
    juce::SharedResourcePointer<rack::ImageCache> imageCache;   // 開いている間、作り置きの絵を持っておく
    std::unique_ptr<juce::PropertiesFile> userSettings;
    rack::DisplayOptions options;
    rack::Theme theme;
    rack::LookAndFeel lookAndFeel { theme };
    RackPanel panel;
    juce::TooltipWindow tooltipWindow { this, 600 };
    bool sizeRestored = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PRIResonanceSuppressorEditor)
};
