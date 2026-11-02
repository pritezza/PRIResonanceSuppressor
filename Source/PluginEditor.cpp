#include "PluginEditor.h"
#include "Parameters.h"

namespace
{
    // 配置の原寸（1280×608）
    const juce::Rectangle<int> lcdBezel { 76, 22, 1068, 408 };
}

RackPanel::RackPanel (PRIResonanceSuppressorProcessor& processor, const rack::Theme& themeToUse)
    : face (themeToUse),
      lcd (processor.getSuppressor(), themeToUse,
           *processor.getState().getParameter (params::minHz), *processor.getState().getParameter (params::maxHz)),
      grMeter (themeToUse),
      settings (processor.getState(), themeToUse),
      theme (themeToUse),
      lowKnob (processor.getState(), params::minHz, "LOW", 64, rack::knobPointer, themeToUse),
      highKnob (processor.getState(), params::maxHz, "HIGH", 64, rack::knobPointer, themeToUse),
      widthKnob (processor.getState(), params::width, "WIDTH", 64, rack::knobPointer, themeToUse),
      thresholdKnob (processor.getState(), params::threshold, "THRESHOLD", 92, rack::accent, themeToUse),
      depthKnob (processor.getState(), params::cut, "DEPTH", 92, rack::accent, themeToUse),
      attackKnob (processor.getState(), params::attack, "ATTACK", 64, rack::knobPointer, themeToUse),
      releaseKnob (processor.getState(), params::release, "RELEASE", 64, rack::knobPointer, themeToUse),
      mixKnob (processor.getState(), params::mix, "MIX", 64, rack::knobPointer, themeToUse),
      resolutionSwitch (processor.getState(), themeToUse),
      listenSwitch (juce::String::fromUTF8 ("\xce\x94 LISTEN"), themeToUse),
      bypassButton (themeToUse),
      gearButton (themeToUse),
      listenAttachment (processor.getState(), params::listen, listenSwitch),
      bypassAttachment (processor.getState(), params::bypass, bypassButton)
{
    setOpaque (true);

    // LOW は HIGH を、HIGH は LOW を越えない
    auto* low = processor.getState().getRawParameterValue (params::minHz);
    auto* high = processor.getState().getRawParameterValue (params::maxHz);
    lowKnob.setConstraint ([high] (double v) { return juce::jmin (v, (double) high->load()); });
    highKnob.setConstraint ([low] (double v) { return juce::jmax (v, (double) low->load()); });

    for (auto* c : std::initializer_list<juce::Component*> { &face, &lcd, &grMeter, &lowKnob, &highKnob, &widthKnob, &thresholdKnob, &depthKnob,
                                                             &attackKnob, &releaseKnob, &mixKnob, &resolutionSwitch, &listenSwitch, &bypassButton,
                                                             &gearButton })
        addAndMakeVisible (c);

    gearButton.onClick = [this]
    {
        settings.setVisible (true);
        settings.toFront (false);
    };

    settings.onClose = [this] { settings.setVisible (false); };
    addChildComponent (settings);
}

void RackPanel::resized()
{
    face.setBounds (getLocalBounds());
    lcd.setBounds (lcdBezel.reduced (9));
    grMeter.setBounds (1158, 46, rack::GainReductionMeter::meterWidth + rack::GainReductionMeter::glowMargin, 360);

    auto placeKnobs = [] (std::initializer_list<rack::Knob*> knobs, int x, int width, int gap)
    {
        for (auto* knob : knobs)
        {
            const int height = knob->getPreferredHeight();
            knob->setBounds (x, 586 - height, width, height);
            x += width + gap;
        }
    };

    placeKnobs ({ &lowKnob, &highKnob, &widthKnob }, 248, 66, 6);
    placeKnobs ({ &thresholdKnob, &depthKnob }, 486, 100, 8);
    placeKnobs ({ &attackKnob, &releaseKnob }, 722, 66, 6);
    placeKnobs ({ &mixKnob }, 1084, 64, 0);

    resolutionSwitch.setBounds (888, 503, 118, 83);
    listenSwitch.setBounds (1004, 515, 52, 71);
    // ボタンの下は、パネルに落ちる影の分を空けておく
    bypassButton.setBounds (1150, 536, 56, 58);
    gearButton.setBounds (196, 552, 32, 40);

    settings.setBounds (getLocalBounds());
}

void RackPanel::refreshTheme()
{
    face.repaint();
    repaint();
}

// 全体にかかる光: 左上が明るく、下が暗い。液晶はガラスを別に描くので除く。設定画面を開いているときはかけない
void RackPanel::paintOverChildren (juce::Graphics& g)
{
    if (settings.isVisible())
        return;

    g.excludeClipRegion (lcdBezel);

    if (g.getClipBounds().isEmpty())
        return;

    const auto bounds = getLocalBounds().toFloat();
    const float width = bounds.getWidth(), height = bounds.getHeight();

    rack::fillRadialGlow (g, rack::circlePath (bounds.expanded (width)), { width * 0.18f, height * -0.05f },
                          width * 0.85f * 0.6f, height * 1.1f * 0.6f, rack::white (theme.lightOverlay));
    g.setGradientFill (juce::ColourGradient (rack::black (0.0f), 0.0f, height * 0.55f, rack::black (theme.darkOverlay), 0.0f, height, false));
    g.fillRect (bounds.withTop (height * 0.55f));
}

void PanelFace::paint (juce::Graphics& g)
{
    using namespace rack;

    const auto bounds = getLocalBounds().toFloat();
    const float width = bounds.getWidth(), height = bounds.getHeight();
    const auto everything = circlePath (bounds.expanded (width));

    // 地: 上が明るく下が暗い
    juce::ColourGradient base (theme.panelTop, 0.0f, 0.0f, theme.panelBottom, 0.0f, height, false);
    base.addColour (0.52, theme.panelMid);
    g.setGradientFill (base);
    g.fillRect (bounds);

    // ヘアラインと、ところどころの強い筋
    g.setColour (theme.hairline);

    for (float y = 0.0f; y < height; y += 3.0f)
        g.fillRect (0.0f, y, width, 1.0f);

    for (float y = 0.0f; y < height; y += 11.0f)
    {
        g.setColour (white (theme.streak * 0.5f));
        g.fillRect (0.0f, y, width, 1.0f);
        g.setColour (black (0.03f));
        g.fillRect (0.0f, y + 6.0f, width, 1.0f);
    }

    // 左上からの広い照り返し、斜めの淡い光の帯、下の隅の暗がり
    fillRadialGlow (g, everything, { width * 0.26f, height * -0.22f }, width * 1.1f * 0.62f, height * 0.75f * 0.62f, white (theme.sheen));
    g.setGradientFill (cssLinearGradient (bounds, 112.0f, { { 0.14f, white (0.0f) }, { 0.3f, white (theme.glare) }, { 0.46f, white (0.0f) } }));
    g.fillRect (bounds);
    fillRadialGlow (g, everything, { width * 0.5f, height * 1.25f }, width * 1.3f * 0.62f, height * 0.85f * 0.62f, black (theme.vignette));

    paintEar (g, 0.0f, true);
    paintEar (g, width - 56.0f, false);

    // 液晶の枠: パネルのくぼみ（上の縁は影、下の縁は光を受ける）と、内側に影の落ちる枠
    const auto recess = lcdBezel.toFloat().expanded (4.0f);
    juce::ColourGradient recessShade (theme.recessTop, 0.0f, recess.getY(), white (theme.recessBottom), 0.0f, recess.getBottom(), false);
    recessShade.addColour (0.45, white (0.0f));
    g.setGradientFill (recessShade);
    g.fillRoundedRectangle (recess, 9.0f);

    const auto bezel = lcdBezel.toFloat();
    g.setColour (theme.bezelFill);
    g.fillRoundedRectangle (bezel, 6.0f);
    {
        juce::Graphics::ScopedSaveState save (g);
        juce::Path clip;
        clip.addRoundedRectangle (bezel, 6.0f);
        g.reduceClipRegion (clip);
        g.setGradientFill (juce::ColourGradient (black (0.9f), 0.0f, bezel.getY(), black (0.0f), 0.0f, bezel.getY() + 5.0f, false));
        g.fillRect (bezel.withHeight (5.0f));
        g.setColour (white (0.08f));
        g.fillRect (bezel.withTop (bezel.getBottom() - 1.0f).reduced (6.0f, 0.0f));
    }

    paintBrand (g);

    drawGroupHeader (g, theme, "DETECT", { 248.0f, 475.0f, 210.0f, 14.0f });
    drawGroupHeader (g, theme, "REDUCE", { 486.0f, 445.0f, 208.0f, 14.0f });
    drawGroupHeader (g, theme, "TIME", { 722.0f, 475.0f, 138.0f, 14.0f });
    drawGroupHeader (g, theme, "MODE", { 888.0f, 475.0f, 168.0f, 14.0f });
    drawGroupHeader (g, theme, "OUTPUT", { 1084.0f, 475.0f, 122.0f, 14.0f });

    // 上下の縁（上は光を受け、下は影）
    g.setColour (theme.edgeTop);
    g.fillRect (0.0f, 0.0f, width, 1.0f);
    g.setColour (theme.edgeBottom);
    g.fillRect (0.0f, height - 1.0f, width, 1.0f);
}

// ラックの耳: 折り曲げたフランジ、ネジ 2 本と取っ手。右の耳は左右反転（光は左上から）
void PanelFace::paintEar (juce::Graphics& g, float left, bool isLeft) const
{
    using namespace rack;

    const float slotX = left + (isLeft ? 5.0f : 25.0f);
    const float screwX = left + (isLeft ? 18.0f : 38.0f);
    const float mountX = left + (isLeft ? 33.0f : 7.0f);
    const float handleX = left + (isLeft ? 35.0f : 9.0f);
    const float height = (float) getHeight();

    juce::ColourGradient plate (black (isLeft ? 0.16f : 0.04f), left, 0.0f, black (isLeft ? 0.04f : 0.16f), left + 56.0f, 0.0f, false);
    plate.addColour (0.55, white (0.05f));
    g.setGradientFill (plate);
    g.fillRect (left, 0.0f, 56.0f, height);

    // 境目の線と、光を受ける縁
    const float lineX = isLeft ? left + 55.0f : left;
    g.setColour (theme.earLine);
    g.fillRect (lineX, 1.0f, 1.0f, height - 2.0f);
    g.setColour (white (theme.earHighlight));
    g.fillRect (isLeft ? lineX + 1.0f : lineX - 1.0f, 1.0f, 1.0f, height - 2.0f);

    for (float cy : { 32.0f, height - 32.0f })
    {
        // 取り付け穴（下の縁が光を受ける）
        const auto slot = juce::Rectangle<float> (slotX, cy - 7.0f, 26.0f, 14.0f);
        g.setColour (white (theme.earHighlight * 0.5f));
        g.fillRoundedRectangle (slot.translated (0.0f, 1.0f), 7.0f);
        g.setColour (juce::Colour (0xff070708));
        g.fillRoundedRectangle (slot, 7.0f);

        // ネジ（左上に照り、右下に影）
        const auto screw = juce::Rectangle<float> (16.0f, 16.0f).withCentre ({ screwX, cy });
        juce::DropShadow (black (0.55f), 3, { 1, 2 }).drawForPath (g, circlePath (screw));
        const juce::Point<float> hot { screw.getX() + 16.0f * 0.35f, screw.getY() + 16.0f * 0.3f };
        juce::ColourGradient metal (juce::Colours::white, hot, juce::Colour (0xff48484c), hot.translated (15.3f, 0.0f), true);
        metal.addColour (0.18, juce::Colour (0xffd6d6da));
        metal.addColour (0.52, juce::Colour (0xff8f8f93));
        g.setGradientFill (metal);
        g.fillEllipse (screw);
        drawRim (g, screw, 0.0f, 0.4f);

        // 十字の溝（下の縁が光を受ける）
        g.setColour (white (0.45f));
        g.fillRect (screwX - 4.0f, cy, 8.0f, 2.0f);
        g.fillRect (screwX, cy - 4.0f, 2.0f, 8.0f);
        g.setColour (juce::Colour (0xff2a2a2d));
        g.fillRect (screwX - 4.0f, cy - 1.0f, 8.0f, 2.0f);
        g.fillRect (screwX - 1.0f, cy - 4.0f, 2.0f, 8.0f);
    }

    // 取っ手の取り付け部と、クロームの取っ手（右下に影）
    for (float y : { 56.0f, height - 68.0f })
    {
        const auto mount = juce::Rectangle<float> (mountX, y, 16.0f, 12.0f);
        juce::Path shape;
        shape.addRoundedRectangle (mount, 2.0f);
        juce::DropShadow (black (0.5f), 3, { 1, 2 }).drawForPath (g, shape);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff9a9a9e), 0.0f, mount.getY(), juce::Colour (0xff45454a), 0.0f, mount.getBottom(), false));
        g.fillPath (shape);
    }

    const auto handle = juce::Rectangle<float> (handleX, 62.0f, 12.0f, height - 124.0f);
    juce::Path handleShape;
    handleShape.addRoundedRectangle (handle, 6.0f);
    juce::DropShadow (black (0.5f), 9, { 5, 7 }).drawForPath (g, handleShape);
    juce::DropShadow (black (0.35f), 2, { 2, 2 }).drawForPath (g, handleShape);

    juce::ColourGradient chrome (juce::Colour (0xff4a4a4e), handle.getX(), 0.0f, juce::Colour (0xff3e3e42), handle.getRight(), 0.0f, false);
    chrome.addColour (0.2, juce::Colour (0xffcfcfd3));
    chrome.addColour (0.3, juce::Colours::white);
    chrome.addColour (0.46, juce::Colour (0xffb4b4b8));
    chrome.addColour (0.7, juce::Colour (0xff75757a));
    g.setGradientFill (chrome);
    g.fillPath (handleShape);
}

void PanelFace::paintBrand (juce::Graphics& g) const
{
    using namespace rack;

    // 電源ランプ
    drawLed (g, theme, juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ 79.5f, 467.0f }), true, true);

    g.setFont (silkscreen (11.0f, true, 0.32f));
    drawPrinted (g, theme, "PRITEZZA", 90, 471, juce::Justification::left, theme.muted);

    g.setFont (silkscreen (20.0f, true, 0.02f));
    drawPrinted (g, theme, "RESONANCE", 76, 518, juce::Justification::left, theme.ink);
    drawPrinted (g, theme, "SUPPRESSOR", 76, 538, juce::Justification::left, theme.ink);

    const auto modelFont = silkscreen (9.5f, false, 0.16f);
    g.setFont (modelFont);
    drawPrinted (g, theme, "MODEL", 76, 582, juce::Justification::left, theme.muted);

    g.setFont (silkscreen (15.0f, true, 0.08f));
    drawPrinted (g, theme, "RS-1", 76 + juce::roundToInt (juce::GlyphArrangement::getStringWidth (modelFont, "MODEL")) + 6, 582,
                 juce::Justification::left, theme.ink);
}

//==============================================================================
namespace
{
    // 表示の設定（テーマ、更新頻度、アナライザー）はパソコンに保存する（全インスタンス・全プロジェクト共通）
    std::unique_ptr<juce::PropertiesFile> openUserSettings()
    {
        juce::PropertiesFile::Options options;
        options.applicationName = "PRIResonanceSuppressor";
        options.folderName = "PRITEZZA";
        options.filenameSuffix = ".settings";
        options.osxLibrarySubFolder = "Application Support";
        return std::make_unique<juce::PropertiesFile> (options);
    }

    constexpr const char* themeKey = "theme";
    constexpr const char* frameRateKey = "frameRateIndex";
    constexpr const char* fallSpeedKey = "fallSpeedIndex";
    constexpr const char* rangeKey = "rangeIndex";
    constexpr const char* peakHoldKey = "peakHold";
    constexpr const char* showInputKey = "showInput";
    constexpr const char* showOutputKey = "showOutput";
    constexpr const char* lineGlowKey = "lineGlow";

    rack::DisplayOptions loadOptions (juce::PropertiesFile& file)
    {
        using rack::DisplayOptions;
        const DisplayOptions defaults;
        auto index = [&file] (const char* key, int fallback, int count) { return juce::jlimit (0, count - 1, file.getIntValue (key, fallback)); };

        DisplayOptions o;
        o.theme = index (themeKey, defaults.theme, rack::Theme::numThemes);
        o.frameRate = index (frameRateKey, defaults.frameRate, (int) std::size (DisplayOptions::frameRates));
        o.fallSpeed = index (fallSpeedKey, defaults.fallSpeed, (int) std::size (DisplayOptions::fallSpeeds));
        o.range = index (rangeKey, defaults.range, (int) std::size (DisplayOptions::ranges));
        o.peakHold = file.getBoolValue (peakHoldKey, defaults.peakHold);
        o.showInput = file.getBoolValue (showInputKey, defaults.showInput);
        o.showOutput = file.getBoolValue (showOutputKey, defaults.showOutput);
        o.lineGlow = file.getBoolValue (lineGlowKey, defaults.lineGlow);
        return o;
    }
}

PRIResonanceSuppressorEditor::PRIResonanceSuppressorEditor (PRIResonanceSuppressorProcessor& p)
    : AudioProcessorEditor (p), pluginProcessor (p), userSettings (openUserSettings()),
      options (loadOptions (*userSettings)),
      theme (rack::Theme::get (options.theme)),
      panel (p, theme)
{
    panel.setLookAndFeel (&lookAndFeel);
    panel.setBounds (0, 0, RackPanel::designWidth, RackPanel::designHeight);
    addAndMakeVisible (panel);

    panel.settings.setOptions (options);
    panel.settings.onOptionsChanged = [this] (const rack::DisplayOptions& newOptions) { applyOptions (newOptions, true); };
    panel.settings.onScaleChanged = [this] (float scale)
    {
        setSize (juce::roundToInt (RackPanel::designWidth * scale), juce::roundToInt (RackPanel::designHeight * scale));
    };

    // 縦横比を保って 50〜150% で拡大・縮小する。倍率は DAW のプロジェクトに保存する
    // （下の設定中にも resized() が呼ばれるので、保存してある倍率は先に読んでおく）
    const double scale = p.getUiScale();
    setResizeLimits (RackPanel::designWidth / 2, RackPanel::designHeight / 2,
                     RackPanel::designWidth * 3 / 2, RackPanel::designHeight * 3 / 2);
    getConstrainer()->setFixedAspectRatio ((double) RackPanel::designWidth / RackPanel::designHeight);
    setResizable (true, true);
    setSize (juce::roundToInt (RackPanel::designWidth * scale), juce::roundToInt (RackPanel::designHeight * scale));
    sizeRestored = true;

    applyOptions (options, false);
}

PRIResonanceSuppressorEditor::~PRIResonanceSuppressorEditor()
{
    panel.setLookAndFeel (nullptr);
}

void PRIResonanceSuppressorEditor::applyOptions (const rack::DisplayOptions& newOptions, bool save)
{
    using rack::DisplayOptions;
    options = newOptions;

    theme = rack::Theme::get (options.theme);
    panel.lcd.setDisplayOptions (DisplayOptions::fallSpeeds[options.fallSpeed], DisplayOptions::ranges[options.range],
                                 options.peakHold, options.showInput, options.showOutput, options.lineGlow);
    panel.refreshTheme();

    const int hz = DisplayOptions::frameRates[options.frameRate];

    if (getTimerInterval() != 1000 / hz)
        startTimerHz (hz);

    if (save)
    {
        userSettings->setValue (themeKey, options.theme);
        userSettings->setValue (frameRateKey, options.frameRate);
        userSettings->setValue (fallSpeedKey, options.fallSpeed);
        userSettings->setValue (rangeKey, options.range);
        userSettings->setValue (peakHoldKey, options.peakHold);
        userSettings->setValue (showInputKey, options.showInput);
        userSettings->setValue (showOutputKey, options.showOutput);
        userSettings->setValue (lineGlowKey, options.lineGlow);
        userSettings->saveIfNeeded();
    }
}

void PRIResonanceSuppressorEditor::resized()
{
    const double scale = (double) getWidth() / RackPanel::designWidth;
    panel.setTransform (juce::AffineTransform::scale ((float) scale));
    panel.settings.setScale (scale);

    if (sizeRestored)
        pluginProcessor.setUiScale ((float) scale);
}

// JUCE 8 の Direct2D 描画では、ウィンドウの枠をドラッグしてリサイズすると中身がずれることがある。
// ウィンドウ（ピア）ができたら、ソフトウェア描画に切り替える
void PRIResonanceSuppressorEditor::parentHierarchyChanged()
{
    if (auto* peer = getPeer())
    {
        const int software = peer->getAvailableRenderingEngines().indexOf ("Software Renderer");

        if (software >= 0 && peer->getCurrentRenderingEngine() != software)
            peer->setCurrentRenderingEngine (software);
    }
}

void PRIResonanceSuppressorEditor::timerCallback()
{
    // ホストが状態を戻して倍率が変わったら、画面の大きさを合わせる
    if (const float scale = pluginProcessor.getUiScale(); std::abs (scale - (float) getWidth() / RackPanel::designWidth) > 0.002f)
        setSize (juce::roundToInt (RackPanel::designWidth * scale), juce::roundToInt (RackPanel::designHeight * scale));

    // 設定画面を開いている間は、後ろの液晶とメーターを止める（重ねた設定画面ごと描き直しになるため）
    if (panel.settings.isVisible())
        return;

    const float seconds = (float) getTimerInterval() / 1000.0f;
    auto& state = pluginProcessor.getState();
    auto raw = [&state] (const char* id) { return state.getRawParameterValue (id)->load(); };

    rack::LcdDisplay::Info info;
    info.bypassed = pluginProcessor.isBypassed();
    info.listening = ! info.bypassed && raw (params::listen) >= 0.5f;
    info.minHz = juce::jmin (raw (params::minHz), raw (params::maxHz));
    info.maxHz = juce::jmax (raw (params::minHz), raw (params::maxHz));
    info.thresholdDb = raw (params::threshold);
    info.depthDb = raw (params::cut);
    info.fftOrder = pluginProcessor.currentFftOrder();

    // ステレオの扱いは 2 チャンネルのときだけ意味があるので、そのときだけ出す
    if (pluginProcessor.getTotalNumInputChannels() == 2)
    {
        const int stereoMode = juce::roundToInt (raw (params::stereo));
        info.stereoLabel = stereoMode == params::stereoDual ? "L/R" : (stereoMode == params::stereoMidSide ? "M/S" : "");
    }

    panel.lcd.update (info, seconds);

    // GR の LED は表示設定によらず 45 dB/秒 で落ちる
    panel.grMeter.setLevel (info.bypassed ? 0.0f : pluginProcessor.getSuppressor().getPeakReductionDb(), 45.0f * seconds);
}
