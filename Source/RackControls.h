#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Parameters.h"
#include "RackStyle.h"

namespace rack
{
    // マウス操作（ドラッグ、ホイール、ダブルクリック）で決まる値を constrain で制限するスライダー。
    // DAW のオートメーションで来た値は制限しない（書き戻すとオートメーションと取り合いになるため）
    class ConstrainedSlider final : public juce::Slider
    {
    public:
        std::function<double (double)> constrain;

        double snapValue (double attemptedValue, DragMode) override
        {
            return constrain != nullptr ? constrain (attemptedValue) : attemptedValue;
        }

        void valueChanged() override
        {
            if (constrain == nullptr || ! isMouseButtonDown())
                return;

            const double limited = constrain (getValue());

            if (limited != getValue())
                setValue (limited, juce::sendNotificationSync);
        }
    };

    // ノブ＋その下の名前と値。値の文字列はパラメータの表示形式（Parameters.h）
    class Knob final : public juce::Component
    {
    public:
        Knob (juce::AudioProcessorValueTreeState& state, const juce::String& parameterId, const juce::String& labelText,
              int knobSize, juce::Colour pointerColour, const Theme& themeToUse)
            : theme (themeToUse), label (labelText), size (knobSize)
        {
            slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
            slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
            slider.setColour (juce::Slider::rotarySliderFillColourId, pointerColour);
            attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, parameterId, slider);

            // ダブルクリックで既定値に戻す
            if (auto* p = state.getParameter (parameterId))
                slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));

            slider.onValueChange = [this] { repaint(); };
            addAndMakeVisible (slider);
        }

        void setConstraint (std::function<double (double)> constrain) { slider.constrain = std::move (constrain); }

        bool isLarge() const { return size >= 80; }

        // 名前と値の行を含めた高さ
        int getPreferredHeight() const { return size + (isLarge() ? 31 : 28); }

        void resized() override
        {
            slider.setBounds (getLocalBounds().removeFromTop (size).withSizeKeepingCentre (size, size));
        }

        void paint (juce::Graphics& g) override
        {
            const bool large = isLarge();
            auto area = getLocalBounds().toFloat().withTrimmedTop ((float) size);

            g.setFont (silkscreen (large ? 11.0f : 9.5f, true, 0.14f));
            drawPrinted (g, theme, label, area.removeFromTop (large ? 15.0f : 13.0f), juce::Justification::centred, theme.ink);

            g.setFont (mono (large ? 12.0f : 11.0f));
            drawPrinted (g, theme, slider.getTextFromValue (slider.getValue()), area, juce::Justification::centredTop, theme.muted);
        }

    private:
        const Theme& theme;
        ConstrainedSlider slider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
        juce::String label;
        int size;
    };

    // 分解能の 3 段切り替えノブ（1024 / 2048 / 4096）。上下ドラッグ・ホイールで 1 段ずつ、数字の近くをクリックでその段、
    // ノブをクリックで次の段。88.2 kHz 以上で Hi-Res FFT が ON のときは、実際の点数はこの 2 倍・4 倍（液晶の右下に出る）
    class ResolutionSwitch final : public juce::Component
    {
    public:
        ResolutionSwitch (juce::AudioProcessorValueTreeState& state, const Theme& themeToUse)
            : theme (themeToUse),
              attachment (*state.getParameter (params::resolution), [this] (float v) { setIndex (juce::roundToInt (v)); }, nullptr)
        {
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
            setTitle ("Resolution");
            attachment.sendInitialUpdate();
        }

        void paint (juce::Graphics& g) override
        {
            g.setColour (theme.tick);

            for (auto angle : angles)
                g.drawLine ({ point (19.0f, angle), point (25.0f, angle) }, 1.4f);

            g.setFont (mono (11.0f));
            const char* labels[] = { "1024", "2048", "4096" };
            const juce::Point<float> labelPositions[] = { { 28.0f, 38.0f }, { 59.0f, 12.0f }, { 90.0f, 38.0f } };
            const juce::Justification justifications[] = { juce::Justification::right, juce::Justification::horizontallyCentred,
                                                           juce::Justification::left };

            for (int i = 0; i < 3; ++i)
                drawPrinted (g, theme, labels[i], juce::roundToInt (labelPositions[i].x), juce::roundToInt (labelPositions[i].y),
                             justifications[i], i == index ? theme.ink : theme.muted);

            // ほかのノブと同じ材質（スカートの直径 32 px）。角度は 12 時から時計回りに直す
            drawKnobBody (g, theme, centre, knobSize);
            drawKnobPointer (g, theme, centre, knobSize, juce::degreesToRadians (angles[index] + 90.0f), false);

            g.setFont (silkscreen (9.5f, true, 0.14f));
            drawPrinted (g, theme, "RESOLUTION", getLocalBounds().toFloat().withTop (70.0f), juce::Justification::centredTop, theme.ink);
        }

        // 上下ドラッグ: 上へ 24 px ごとに 1 段上がる（1024 → 2048 → 4096）
        void mouseDown (const juce::MouseEvent&) override
        {
            indexAtMouseDown = index;
            dragging = false;
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (! dragging && std::abs (e.getDistanceFromDragStartY()) < 4)
                return;

            if (! dragging)
            {
                dragging = true;
                attachment.beginGesture();
            }

            const int next = juce::jlimit (0, 2, indexAtMouseDown - juce::roundToInt ((float) e.getDistanceFromDragStartY() / dragStep));

            if (next != index)
                attachment.setValueAsPartOfGesture ((float) next);
        }

        // ドラッグしなかったときはクリック: 数字の近くならその段、ノブなら次の段
        void mouseUp (const juce::MouseEvent& e) override
        {
            if (dragging)
            {
                attachment.endGesture();
                dragging = false;
                return;
            }

            const auto p = e.position;
            int next = (index + 1) % 3;

            if (p.getDistanceFrom (centre) > 18.0f)
            {
                float best = std::numeric_limits<float>::max();

                for (int i = 0; i < 3; ++i)
                {
                    const float d = p.getDistanceFrom (point (30.0f, angles[i]));

                    if (d < best)
                    {
                        best = d;
                        next = i;
                    }
                }
            }

            attachment.setValueAsCompleteGesture ((float) next);
        }

        // ホイール: 上へ回すと 1 段上がる。トラックパッドのように細かく来るものは溜めてから動かす
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override
        {
            wheelAccumulator += wheel.isReversed ? -wheel.deltaY : wheel.deltaY;

            if (std::abs (wheelAccumulator) < (wheel.isSmooth ? 0.25f : 0.01f))
                return;

            const int next = juce::jlimit (0, 2, index + (wheelAccumulator > 0.0f ? 1 : -1));
            wheelAccumulator = 0.0f;

            if (next != index)
                attachment.setValueAsCompleteGesture ((float) next);
        }

    private:
        static constexpr float dragStep = 24.0f;
        void setIndex (int newIndex)
        {
            index = juce::jlimit (0, 2, newIndex);
            repaint();
        }

        // 角度はデザインの SVG と同じく、右が 0° で時計回り
        juce::Point<float> point (float radius, float degrees) const
        {
            const float a = juce::degreesToRadians (degrees);
            return centre + juce::Point<float> (radius * std::cos (a), radius * std::sin (a));
        }

        static constexpr float angles[] = { 210.0f, 270.0f, 330.0f };
        static constexpr float knobSize = 46.0f;
        const juce::Point<float> centre { 59.0f, 48.0f };
        const Theme& theme;
        juce::ParameterAttachment attachment;
        int index = 0, indexAtMouseDown = 0;
        bool dragging = false;
        float wheelAccumulator = 0.0f;
    };

    // レバー式のトグルスイッチ（上が ON）
    class LeverSwitch final : public juce::ToggleButton
    {
    public:
        LeverSwitch (const juce::String& labelText, const Theme& themeToUse) : juce::ToggleButton (labelText), theme (themeToUse)
        {
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
        }

        void paintButton (juce::Graphics& g, bool, bool) override
        {
            const float cx = (float) getWidth() / 2.0f;
            const juce::Point<float> nut { cx, 30.0f };

            g.setFont (silkscreen (8.0f, false));
            drawPrinted (g, theme, "ON", juce::roundToInt (cx), 8, juce::Justification::horizontallyCentred, theme.muted);
            drawPrinted (g, theme, "OFF", juce::roundToInt (cx), 55, juce::Justification::horizontallyCentred, theme.muted);

            // 座金（クロームの削り出し）とナット
            const auto washer = juce::Rectangle<float> (20.0f, 20.0f).withCentre (nut);
            drawCached (g, theme, "washer", washer.expanded (6.0f), [] (juce::Graphics& ig, juce::Image& image, float)
            {
                const auto area = juce::Rectangle<float> (6.0f, 6.0f, 20.0f, 20.0f);
                const float pixels = (float) image.getWidth() / 32.0f;
                juce::DropShadow (black (0.5f), 4, { 1, 3 }).drawForPath (ig, circlePath (area));
                fillConicDisc (image, area.getCentre() * pixels, 10.0f * pixels, 20.0f,
                               evenStops ({ juce::Colour (0xff6a6a6e), juce::Colour (0xfffafafa), juce::Colour (0xff8a8a8e), juce::Colour (0xffe6e6e8),
                                            juce::Colour (0xff646468), juce::Colour (0xfffafafa), juce::Colour (0xff8a8a8e), juce::Colour (0xffe6e6e8),
                                            juce::Colour (0xff6a6a6e) }));
                drawRim (ig, area, 0.4f, 0.4f);
            });

            const auto nutArea = juce::Rectangle<float> (13.0f, 13.0f).withCentre (nut);
            g.setColour (black (0.6f));
            g.fillEllipse (nutArea.translated (0.0f, 1.0f));
            g.setGradientFill (juce::ColourGradient (juce::Colours::white, nutArea.getX() + nutArea.getWidth() * 0.35f, nutArea.getY() + nutArea.getHeight() * 0.3f,
                                                     juce::Colour (0xff55555a), nutArea.getRight(), nutArea.getBottom(), true));
            g.fillEllipse (nutArea);

            // レバー（円柱のクローム）と先の玉。影は右下へ
            const float tipY = getToggleState() ? 15.0f : 45.0f;
            const auto lever = juce::Rectangle<float> (cx - 3.0f, juce::jmin (nut.y, tipY), 6.0f, std::abs (tipY - nut.y));
            const auto ball = juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ cx, tipY });
            juce::Path leverShape;
            leverShape.addRoundedRectangle (lever, 3.0f);
            leverShape.addEllipse (ball);
            juce::DropShadow (black (0.45f), 3, { 2, 3 }).drawForPath (g, leverShape);

            juce::ColourGradient chrome (juce::Colour (0xff5a5a5e), lever.getX(), 0.0f, juce::Colour (0xff4a4a4e), lever.getRight(), 0.0f, false);
            chrome.addColour (0.35, juce::Colours::white);
            chrome.addColour (0.62, juce::Colour (0xffa8a8ac));
            g.setGradientFill (chrome);
            g.fillRoundedRectangle (lever, 3.0f);

            g.setGradientFill (juce::ColourGradient (juce::Colours::white, ball.getX() + ball.getWidth() * 0.35f, ball.getY() + ball.getHeight() * 0.3f,
                                                     juce::Colour (0xff5a5a5e), ball.getRight(), ball.getBottom(), true));
            g.fillEllipse (ball);

            g.setFont (silkscreen (9.5f, true, 0.14f));
            drawPrinted (g, theme, getButtonText(), getLocalBounds().toFloat().withTop (58.0f), juce::Justification::centredTop, theme.ink);
        }

    private:
        const Theme& theme;
    };

    // バイパスの押しボタン。文字は表面に印刷し、押し込まれている（バイパス中）ときは文字が光る。
    // 上の ACTIVE ランプは処理中（バイパスでない）ときに点く
    class BypassButton final : public juce::ToggleButton
    {
    public:
        explicit BypassButton (const Theme& themeToUse) : juce::ToggleButton ("BYPASS"), theme (themeToUse)
        {
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
        }

        void paintButton (juce::Graphics& g, bool isHighlighted, bool isButtonDown) override
        {
            const bool bypassed = getToggleState();
            const float cx = (float) getWidth() / 2.0f;

            // ランプと ACTIVE の文字（まとめて中央に置く）
            const auto smallFont = silkscreen (8.0f, false, 0.1f);
            const float textWidth = juce::GlyphArrangement::getStringWidth (smallFont, "ACTIVE");
            const float rowLeft = cx - (7.0f + 4.0f + textWidth) / 2.0f;
            const auto led = juce::Rectangle<float> (rowLeft, 1.0f, 7.0f, 7.0f);

            drawLed (g, theme, led, ! bypassed, true);
            g.setFont (smallFont);
            drawPrinted (g, theme, "ACTIVE", juce::Rectangle<float> (led.getRight() + 4.0f, 0.0f, textWidth + 4.0f, 9.0f),
                         juce::Justification::centredLeft, theme.muted);

            const bool pressed = bypassed || isButtonDown;
            auto cap = juce::Rectangle<float> (50.0f, 32.0f).withCentre ({ cx, 31.0f });
            drawButtonCap (g, theme, cap, pressed, isHighlighted);

            if (pressed)
                cap = cap.translated (0.0f, 1.5f);

            g.setFont (silkscreen (10.0f, true, 0.12f));

            // バイパス中は文字がオレンジに光る
            if (bypassed)
            {
                g.setColour (accent.withAlpha (0.3f));
                for (const auto offset : { juce::Point<float> (-1.0f, 0.0f), juce::Point<float> (1.0f, 0.0f), juce::Point<float> (0.0f, -1.0f), juce::Point<float> (0.0f, 1.0f) })
                    g.drawText (getButtonText(), cap.translated (offset.x, offset.y), juce::Justification::centred, false);
            }

            drawButtonLegend (g, theme, getButtonText(), cap, bypassed ? accent : theme.buttonText);
        }

    private:
        const Theme& theme;
    };

    // 設定画面を開く丸い押しボタン（歯車の印）
    class GearButton final : public juce::Button
    {
    public:
        explicit GearButton (const Theme& themeToUse) : juce::Button ("Settings"), theme (themeToUse)
        {
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
            setTooltip ("Settings");
        }

        void paintButton (juce::Graphics& g, bool isHighlighted, bool isButtonDown) override
        {
            // 下は影の分を空ける
            const float diameter = (float) getWidth() - 2.0f;
            auto cap = juce::Rectangle<float> (1.0f, 1.0f, diameter, diameter);
            drawButtonCap (g, theme, cap, isButtonDown, isHighlighted, diameter / 2.0f);

            if (isButtonDown)
                cap = cap.translated (0.0f, 1.5f);

            const auto gear = makeGear (cap.getCentre(), diameter * 0.3f);
            g.setColour (theme.buttonTextShadow);
            g.fillPath (gear, juce::AffineTransform::translation (0.0f, 1.0f));
            g.setColour (theme.buttonText);
            g.fillPath (gear);
        }

    private:
        static juce::Path makeGear (juce::Point<float> centre, float radius)
        {
            constexpr int teeth = 8;
            juce::Path gear;

            for (int i = 0; i < teeth * 4; ++i)
            {
                // 歯の先（2 点）と根元（2 点）を交互に
                const bool tip = (i % 4) == 1 || (i % 4) == 2;
                const float angle = juce::MathConstants<float>::twoPi * ((float) i - 0.5f) / (float) (teeth * 4);
                const auto p = centre.getPointOnCircumference (tip ? radius : radius * 0.74f, angle);

                if (i == 0)
                    gear.startNewSubPath (p);
                else
                    gear.lineTo (p);
            }

            gear.closeSubPath();
            gear.addEllipse (juce::Rectangle<float> (radius * 0.7f, radius * 0.7f).withCentre (centre));
            gear.setUsingNonZeroWinding (false);
            return gear;
        }

        const Theme& theme;
    };

    // 設定画面の選択肢ボタン（同じグループで 1 つだけ押し込まれる）。選ばれているものはランプが点く
    class SegmentButton final : public juce::Button
    {
    public:
        SegmentButton (const juce::String& text, const Theme& themeToUse) : juce::Button (text), theme (themeToUse)
        {
            setClickingTogglesState (true);
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
        }

        void paintButton (juce::Graphics& g, bool isHighlighted, bool isButtonDown) override
        {
            const bool on = getToggleState();
            auto cap = getLocalBounds().toFloat().reduced (1.0f).withTrimmedBottom (3.0f);
            drawButtonCap (g, theme, cap, on || isButtonDown, isHighlighted);

            if (on || isButtonDown)
                cap = cap.translated (0.0f, 1.5f);

            const auto led = juce::Rectangle<float> (6.0f, 6.0f).withCentre ({ cap.getX() + 10.0f, cap.getCentreY() });
            drawLed (g, theme, led, on, true);

            g.setFont (silkscreen (10.0f, true, 0.1f));
            drawButtonLegend (g, theme, getButtonText(), cap.withTrimmedLeft (16.0f), theme.buttonText);
        }

    private:
        const Theme& theme;
    };

    // 削減量の LED メーター（上から 1, 2, 3 ... 24 dB）
    class GainReductionMeter final : public juce::Component
    {
    public:
        explicit GainReductionMeter (const Theme& themeToUse) : theme (themeToUse) {}

        // maxFallDb はこの呼び出しで表示が下がってよい最大量
        void setLevel (float reductionDb, float maxFallDb)
        {
            const float next = reductionDb > shown ? reductionDb : juce::jmax (reductionDb, shown - maxFallDb);
            int lit = 0;

            // 削減量は目標（DEPTH）へ指数的に近づき、フレームごとに少し揺れるので、最大の 24 dB ちょうどにはならない
            // （23.9 dB 前後）。目盛りは 1 dB 単位なので、四捨五入してその値になる 0.5 dB 手前から点ける
            for (auto db : steps)
                if (next >= db - 0.5f)
                    ++lit;

            shown = next;

            if (lit != litCount)
            {
                litCount = lit;
                repaint();
            }
        }

        // 点いている LED の数（確認用）
        int getLitCount() const { return litCount; }

        void paint (juce::Graphics& g) override
        {
            // 右の glowMargin は点いた LED の光のにじみの分。目盛りと LED は左の meterWidth に収める
            const auto bounds = getLocalBounds().toFloat().withTrimmedRight (glowMargin);

            g.setFont (silkscreen (10.0f, false, 0.24f));
            drawPrinted (g, theme, "GR", bounds.withHeight (12.0f), juce::Justification::centred, theme.ink);

            g.setFont (mono (10.0f));

            // LED は「GR」の下から「dB」の上までに等間隔で並べる
            const float pitch = (bounds.getHeight() - 53.0f) / (float) (std::size (steps) - 1);

            for (int i = 0; i < (int) std::size (steps); ++i)
            {
                const float y = 22.0f + (float) i * pitch;
                const auto led = juce::Rectangle<float> (bounds.getRight() - 18.0f, y, 18.0f, 11.0f);

                drawPrinted (g, theme, juce::String (juce::roundToInt (steps[i])), juce::Rectangle<float> (0.0f, y - 1.0f, led.getX() - 5.0f, 13.0f),
                             juce::Justification::centredRight, theme.muted);

                // LED のはまる穴（下の縁が光を受ける）
                const auto well = led.expanded (1.0f);
                g.setColour (white (theme.earHighlight * 0.4f));
                g.fillRoundedRectangle (well.translated (0.0f, 1.0f), 3.0f);
                g.setColour (black (0.55f));
                g.fillRoundedRectangle (well, 3.0f);
            }

            for (int i = 0; i < (int) std::size (steps); ++i)
                drawLed (g, theme, juce::Rectangle<float> (bounds.getRight() - 18.0f, 22.0f + (float) i * pitch, 18.0f, 11.0f), i < litCount, false);

            g.setFont (silkscreen (8.0f, false, 0.1f));
            drawPrinted (g, theme, "dB", bounds.withTop (bounds.getBottom() - 10.0f), juce::Justification::centred, theme.muted);
        }

        static constexpr int meterWidth = 46, glowMargin = 10;

    private:
        static constexpr float steps[] = { 1.0f, 2.0f, 3.0f, 4.0f, 6.0f, 9.0f, 12.0f, 15.0f, 18.0f, 24.0f };
        const Theme& theme;
        float shown = 0.0f;
        int litCount = 0;
    };
}
