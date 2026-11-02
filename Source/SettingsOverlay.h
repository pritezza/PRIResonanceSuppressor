#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Parameters.h"
#include "RackControls.h"
#include "RackStyle.h"

namespace rack
{
    // 表示の設定（パソコンに保存し、全インスタンス共通）。数値は各配列の番号
    struct DisplayOptions
    {
        static constexpr int frameRates[] = { 15, 30, 60 };
        static constexpr float fallSpeeds[] = { 15.0f, 45.0f, 120.0f }; // dB/秒
        static constexpr float ranges[] = { 90.0f, 60.0f };             // 液晶の縦軸（dB）

        int theme = Theme::vintage, frameRate = 1, fallSpeed = 1, range = 0;
        bool peakHold = false, showInput = true, showOutput = true;
        bool lineGlow = true;   // 抑制後の線: true は 3 本重ね（にじみあり）、false は 1 本（描画が軽い）
    };

    // 歯車ボタンで開く設定画面。パネル全体に重ね、中央のカードに項目を並べる。カードの外をクリックするか CLOSE で閉じる。
    // 左の列: DISPLAY（テーマ、画面の大きさ、更新頻度）と PROCESSING（Hi-Res FFT、ステレオの扱い、無音判定レベル）、右の列: ANALYZER。
    // 左下にバージョンとライセンス、右下に設定の保存先の説明
    class SettingsOverlay final : public juce::Component
    {
    public:
        static constexpr float uiScales[] = { 0.75f, 1.0f, 1.25f, 1.5f };

        std::function<void (const DisplayOptions&)> onOptionsChanged;
        std::function<void (float)> onScaleChanged;
        std::function<void()> onClose;

        SettingsOverlay (juce::AudioProcessorValueTreeState& state, const Theme& themeToUse)
            : theme (themeToUse),
              hiResAttachment (*state.getParameter (params::hiResFft),
                               [this] (float v) { select (*hiResRow, v >= 0.5f ? 1 : 0); }, nullptr),
              stereoAttachment (*state.getParameter (params::stereo),
                                [this] (float v) { select (*stereoRow, juce::jlimit (0, 2, juce::roundToInt (v))); }, nullptr),
              closeButton (themeToUse)
        {
            const auto notifyOptions = [this] { if (onOptionsChanged) onOptionsChanged (readOptions()); };

            themeRow = &addRow (0, 0, "THEME", "Panel colour", Theme::getNames(), notifyOptions);
            scaleRow = &addRow (0, 1, "UI SIZE", "Window scale", { "75%", "100%", "125%", "150%" }, [this]
            {
                if (onScaleChanged)
                    onScaleChanged (uiScales[juce::jmax (0, selectedIndex (*scaleRow))]);
            });
            rateRow = &addRow (0, 2, "DISPLAY RATE", "Higher uses more CPU", { "15 FPS", "30 FPS", "60 FPS" }, notifyOptions);
            hiResRow = &addRow (0, 3, "HI-RES FFT", "x2 at 88.2k, x4 at 176.4k", { "OFF", "ON" }, [this]
            {
                hiResAttachment.setValueAsCompleteGesture (selectedIndex (*hiResRow) == 1 ? 1.0f : 0.0f);
            });
            stereoRow = &addRow (0, 4, "STEREO", "Linked, per side or mid/side", { "LINKED", "L / R", "M / S" }, [this]
            {
                stereoAttachment.setValueAsCompleteGesture ((float) juce::jmax (0, selectedIndex (*stereoRow)));
            });

            fallRow = &addRow (1, 0, "FALL SPEED", "Analyzer decay", { "SLOW", "MEDIUM", "FAST" }, notifyOptions);
            holdRow = &addRow (1, 1, "PEAK HOLD", "Input peaks for 2 s", { "OFF", "ON" }, notifyOptions);
            rangeRow = &addRow (1, 2, "RANGE", "Level axis", { "90 dB", "60 dB" }, notifyOptions);
            linesRow = &addRow (1, 3, "LINES", "Spectrum lines shown", { "INPUT", "OUTPUT" }, notifyOptions, false);
            glowRow = &addRow (1, 4, "LINE GLOW", "1 line uses less CPU", { "3 LINES", "1 LINE" }, notifyOptions);

            gateSlider.setSliderStyle (juce::Slider::LinearHorizontal);
            gateSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            gateSlider.setMouseCursor (juce::MouseCursor::PointingHandCursor);
            gateAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, params::silence, gateSlider);

            if (auto* p = state.getParameter (params::silence))
                gateSlider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));

            gateSlider.onValueChange = [this] { repaint (gateValueArea); };
            addAndMakeVisible (gateSlider);

            closeButton.onClick = [this] { if (onClose) onClose(); };
            addAndMakeVisible (closeButton);

            hiResAttachment.sendInitialUpdate();
            stereoAttachment.sendInitialUpdate();
        }

        // 保存してある表示設定をボタンに反映する（通知はしない）
        void setOptions (const DisplayOptions& options)
        {
            select (*themeRow, options.theme);
            select (*rateRow, options.frameRate);
            select (*fallRow, options.fallSpeed);
            select (*holdRow, options.peakHold ? 1 : 0);
            select (*rangeRow, options.range);
            linesRow->buttons[0]->setToggleState (options.showInput, juce::dontSendNotification);
            linesRow->buttons[1]->setToggleState (options.showOutput, juce::dontSendNotification);
            select (*glowRow, options.lineGlow ? 0 : 1);
        }

        // 今の倍率に合うボタンを押し込む（角のドラッグで半端な倍率になったときはどれも押し込まない）
        void setScale (double scale)
        {
            for (int i = 0; i < (int) std::size (uiScales); ++i)
                scaleRow->buttons[i]->setToggleState (std::abs (scale - uiScales[i]) < 0.01, juce::dontSendNotification);
        }

        void resized() override
        {
            card = juce::Rectangle<int> (cardWidth, cardHeight).withCentre (getLocalBounds().getCentre());

            for (auto* row : rows)
            {
                const int controlsX = columnX (row->column) + labelWidth;
                const int count = row->buttons.size();
                const int buttonWidth = juce::jmin (100, (controlsWidth - buttonGap * (count - 1)) / count);
                int x = controlsX;

                for (auto* b : row->buttons)
                {
                    b->setBounds (x, rowY (row->column, row->line), buttonWidth, 37);
                    x += buttonWidth + buttonGap;
                }
            }

            // 無音判定レベルは PROCESSING の 3 行目（左の列の 6 行目）
            const int gateY = rowY (0, 5);
            gateSlider.setBounds (columnX (0) + labelWidth, gateY + 5, controlsWidth - 92, 26);
            gateValueArea = juce::Rectangle<int> (columnX (0) + labelWidth + controlsWidth - 86, gateY + 5, 86, 26);

            closeButton.setBounds (card.getRight() - padding - 96, card.getBottom() - padding - 37, 96, 37);
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (theme.backdrop);

            const auto cardArea = card.toFloat();
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.fillRoundedRectangle (cardArea.translated (0.0f, 4.0f).expanded (2.0f), 8.0f);
            g.setColour (theme.card);
            g.fillRoundedRectangle (cardArea, 6.0f);
            g.setColour (theme.rule);
            g.drawRoundedRectangle (cardArea.reduced (0.5f), 6.0f, 1.0f);

            g.setColour (theme.ink);
            g.setFont (silkscreen (13.0f, true, 0.3f));
            g.drawText ("SETUP", cardArea.withHeight (44.0f), juce::Justification::centred, false);

            auto header = [&] (const juce::String& title, int column, int y)
            {
                drawGroupHeader (g, theme, title, juce::Rectangle<float> ((float) columnX (column), (float) y, (float) columnWidth, 14.0f));
            };

            header ("DISPLAY", 0, card.getY() + 48);
            header ("PROCESSING", 0, rowY (0, 3) - 26);
            header ("ANALYZER", 1, card.getY() + 48);

            auto rowLabel = [&] (const juce::String& title, const juce::String& note, int column, int y)
            {
                const float x = (float) columnX (column);
                g.setColour (theme.ink);
                g.setFont (silkscreen (11.0f, true, 0.14f));
                g.drawText (title, juce::Rectangle<float> (x, (float) y + 2.0f, (float) labelWidth, 16.0f), juce::Justification::centredLeft, false);
                g.setColour (theme.muted);
                g.setFont (silkscreen (9.5f, false, 0.04f));
                g.drawText (note, juce::Rectangle<float> (x, (float) y + 18.0f, (float) labelWidth, 14.0f), juce::Justification::centredLeft, false);
            };

            for (auto* row : rows)
                rowLabel (row->title, row->note, row->column, rowY (row->column, row->line));

            rowLabel ("SILENCE GATE", "Skip quieter frames", 0, rowY (0, 5));

            g.setColour (theme.ink);
            g.setFont (mono (13.0f));
            g.drawText (gateSlider.getTextFromValue (gateSlider.getValue()), gateValueArea, juce::Justification::centredRight, false);

            g.setColour (theme.muted);
            g.setFont (silkscreen (9.5f, false, 0.04f));
            g.drawFittedText ("Theme, display rate and analyzer settings apply to all instances.\n"
                              "UI size, Hi-Res FFT, stereo and silence gate are saved with the project.",
                              juce::Rectangle<int> (columnX (1), card.getBottom() - padding - 37, columnWidth - 110, 37),
                              juce::Justification::centredLeft, 2);

            // 左下: 名前とバージョン、著作権とライセンス
            g.drawFittedText (juce::String (JucePlugin_Name) + " " + JucePlugin_VersionString + "\n"
                                  + juce::String::fromUTF8 ("\xc2\xa9 2026 PRITEZZA \xc2\xb7 GNU AGPL v3 \xc2\xb7 No warranty"),
                              juce::Rectangle<int> (columnX (0), card.getBottom() - padding - 37, columnWidth, 37),
                              juce::Justification::centredLeft, 2);
        }

        // カードの外をクリックしたら閉じる
        void mouseDown (const juce::MouseEvent& e) override
        {
            if (! card.contains (e.getPosition()) && onClose)
                onClose();
        }

    private:
        static constexpr int cardWidth = 1000, cardHeight = 462, padding = 24, columnGap = 40;
        static constexpr int columnWidth = (cardWidth - padding * 2 - columnGap) / 2;
        static constexpr int labelWidth = 150, controlsWidth = columnWidth - labelWidth, buttonGap = 8;

        struct Row
        {
            int column = 0, line = 0;
            juce::String title, note;
            juce::OwnedArray<SegmentButton> buttons;
        };

        int columnX (int column) const { return card.getX() + padding + column * (columnWidth + columnGap); }

        // 行の上端。左の列は 3 行目のあとに PROCESSING の見出しの分だけ空ける
        int rowY (int column, int line) const
        {
            return card.getY() + 72 + line * 50 + (column == 0 && line >= 3 ? 34 : 0);
        }

        // radio が true なら同じ行で 1 つだけ押し込まれる。false なら各ボタンが独立した ON/OFF
        Row& addRow (int column, int line, const juce::String& title, const juce::String& note, const juce::StringArray& options,
                     std::function<void()> onChange, bool radio = true)
        {
            auto* row = rows.add (new Row());
            row->column = column;
            row->line = line;
            row->title = title;
            row->note = note;

            for (const auto& option : options)
            {
                auto* b = row->buttons.add (new SegmentButton (option, theme));

                if (radio)
                    b->setRadioGroupId (rows.size());

                b->onClick = [b, onChange, radio] { if (b->getToggleState() || ! radio) onChange(); };
                addAndMakeVisible (b);
            }

            return *row;
        }

        static void select (Row& row, int index)
        {
            if (auto* b = row.buttons[index])
                b->setToggleState (true, juce::dontSendNotification);
        }

        static int selectedIndex (const Row& row)
        {
            for (int i = 0; i < row.buttons.size(); ++i)
                if (row.buttons[i]->getToggleState())
                    return i;

            return -1;
        }

        DisplayOptions readOptions() const
        {
            DisplayOptions o;
            o.theme = juce::jmax (0, selectedIndex (*themeRow));
            o.frameRate = juce::jmax (0, selectedIndex (*rateRow));
            o.fallSpeed = juce::jmax (0, selectedIndex (*fallRow));
            o.peakHold = selectedIndex (*holdRow) == 1;
            o.range = juce::jmax (0, selectedIndex (*rangeRow));
            o.showInput = linesRow->buttons[0]->getToggleState();
            o.showOutput = linesRow->buttons[1]->getToggleState();
            o.lineGlow = selectedIndex (*glowRow) != 1;
            return o;
        }

        // CLOSE ボタン（ランプなしの押しボタン）
        class CapButton final : public juce::Button
        {
        public:
            explicit CapButton (const Theme& t) : juce::Button ("CLOSE"), theme (t)
            {
                setMouseCursor (juce::MouseCursor::PointingHandCursor);
            }

            void paintButton (juce::Graphics& g, bool isHighlighted, bool isButtonDown) override
            {
                auto cap = getLocalBounds().toFloat().reduced (1.0f).withTrimmedBottom (3.0f);
                drawButtonCap (g, theme, cap, isButtonDown, isHighlighted);

                if (isButtonDown)
                    cap = cap.translated (0.0f, 1.5f);

                g.setColour (theme.buttonText);
                g.setFont (silkscreen (10.0f, true, 0.12f));
                g.drawText (getButtonText(), cap, juce::Justification::centred, false);
            }

        private:
            const Theme& theme;
        };

        const Theme& theme;
        juce::OwnedArray<Row> rows;
        Row* themeRow = nullptr;
        Row* scaleRow = nullptr;
        Row* rateRow = nullptr;
        Row* hiResRow = nullptr;
        Row* stereoRow = nullptr;
        Row* fallRow = nullptr;
        Row* holdRow = nullptr;
        Row* rangeRow = nullptr;
        Row* linesRow = nullptr;
        Row* glowRow = nullptr;
        juce::ParameterAttachment hiResAttachment, stereoAttachment;
        juce::Slider gateSlider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gateAttachment;
        CapButton closeButton;
        juce::Rectangle<int> card, gateValueArea;
    };
}
