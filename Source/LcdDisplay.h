#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "RackStyle.h"
#include "ResonanceSuppressor.h"

namespace rack
{
    // 液晶。入力スペクトル（左の目盛り: dBFS）、削る量（上から下がる塗り、右の目盛り: 高さの半分で 24 dB）、
    // 抑制後のスペクトル（明るい線）、検出範囲、最大の削減位置、下の行に状態と設定を出す。
    // 検出範囲の縦の破線はドラッグで動かせる（LOW は HIGH を、HIGH は LOW を越えない）。
    // 表示は設定画面で選んだ速さで落ちる（既定は毎秒 45 dB）。ピークホールドは入力スペクトルの最大値
    class LcdDisplay final : public juce::Component
    {
    public:
        struct Info
        {
            bool bypassed = false, listening = false;
            float minHz = 1000.0f, maxHz = 10000.0f, thresholdDb = 6.0f, depthDb = 9.0f;
            int fftOrder = ResonanceSuppressor::minFftOrder;
            juce::String stereoLabel;   // 左右共通以外のときに右下に出す（"L/R"、"M/S"）

            bool operator!= (const Info& o) const
            {
                return bypassed != o.bypassed || listening != o.listening || minHz != o.minHz || maxHz != o.maxHz
                       || thresholdDb != o.thresholdDb || depthDb != o.depthDb || fftOrder != o.fftOrder || stereoLabel != o.stereoLabel;
            }
        };

        LcdDisplay (const ResonanceSuppressor& suppressorToShow, const Theme& themeToUse,
                    juce::RangedAudioParameter& lowParameter, juce::RangedAudioParameter& highParameter)
            : suppressor (suppressorToShow), theme (themeToUse), lowParam (lowParameter), highParam (highParameter)
        {
            clearShown();
            points.reserve (ResonanceSuppressor::maxBins);
            smoothed.reserve (ResonanceSuppressor::maxBins * 2);
            setOpaque (true);
        }

        // 設定画面の表示設定。fallDbPerSecond: 表示の落ちる速さ、rangeDb: 縦軸（入力レベル）の範囲、
        // peakHold: 入力スペクトルの最大値を 2 秒残してから落とす、showInput / showOutput: 入力・抑制後の線の表示、
        // lineGlow: 抑制後の線を、太く薄い線 2 本を下に重ねてにじませる（false なら線 1 本だけで、描画が軽い）
        void setDisplayOptions (float newFallDbPerSecond, float newRangeDb, bool newPeakHold, bool newShowInput, bool newShowOutput, bool newLineGlow)
        {
            fallDbPerSecond = newFallDbPerSecond;
            rangeDb = newRangeDb;
            showInput = newShowInput;
            showOutput = newShowOutput;
            lineGlow = newLineGlow;

            if (newPeakHold != peakHold)
            {
                peakHold = newPeakHold;
                heldLevel = shownLevel;
                holdSeconds.fill (0.0f);
            }

            repaint();
        }

        // タイマーから呼ぶ。seconds は前回の呼び出しからの時間
        void update (const Info& newInfo, float seconds)
        {
            if (! isShowing())
                return;

            bool changed = newInfo != info;
            info = newInfo;

            // FFT サイズが変わるとビンの周波数が変わるので、表示中の値を捨てる
            const int numBins = suppressor.getDisplayNumBins();

            if (numBins != shownNumBins)
            {
                shownNumBins = numBins;
                clearShown();
                changed = true;
            }

            const float maxFallDb = fallDbPerSecond * seconds;

            for (int k = 0; k < numBins; ++k)
            {
                const float level = suppressor.getDisplayLevelDb (k);
                const float reduction = info.bypassed ? 0.0f : suppressor.getDisplayReductionDb (k);
                auto& lv = shownLevel[(size_t) k];
                auto& rd = shownReduction[(size_t) k];

                const float newLevel = level > lv ? level : juce::jmax (level, lv - maxFallDb);
                const float newReduction = reduction > rd ? reduction : juce::jmax (reduction, rd - maxFallDb);

                if (std::abs (newLevel - lv) > 0.1f || std::abs (newReduction - rd) > 0.05f)
                    changed = true;

                lv = newLevel;
                rd = newReduction;

                if (peakHold)
                {
                    auto& held = heldLevel[(size_t) k];
                    auto& remaining = holdSeconds[(size_t) k];

                    if (newLevel >= held)
                    {
                        held = newLevel;
                        remaining = holdTime;
                    }
                    else if ((remaining -= seconds) <= 0.0f)
                    {
                        held = juce::jmax (newLevel, held - maxFallDb);
                        changed = true;
                    }
                }
            }

            const auto newStatus = statusText();

            if (newStatus != shownStatus)
            {
                shownStatus = newStatus;
                changed = true;
            }

            if (changed)
                repaint();
        }

        void paint (juce::Graphics& g) override
        {
            drawBackground (g);

            const double sampleRate = suppressor.getSampleRate();
            const float binHz = suppressor.getDisplayBinHz();

            drawRange (g);
            drawSpectrum (g, binHz);
            drawScales (g);

            // 下の行: 状態と設定
            g.setColour (theme.lcdGrid);
            g.fillRect (8.0f, (float) getHeight() - 24.0f, (float) getWidth() - 16.0f, 1.0f);

            g.setFont (mono (15.0f));
            g.setColour (theme.lcdBright);
            g.drawSingleLineText (shownStatus, 10, getHeight() - 6);

            const int fftSize = 1 << info.fftOrder;
            juce::String right = (info.stereoLabel.isNotEmpty() ? info.stereoLabel + "  " : juce::String()) + "FFT " + juce::String (fftSize);

            if (sampleRate > 0.0)
            {
                const double kHz = sampleRate / 1000.0;
                right << "  " << juce::roundToInt (sampleRate / fftSize) << "Hz/" << juce::roundToInt (fftSize * 1000.0 / sampleRate) << "ms  "
                      << juce::String (kHz, std::abs (kHz - std::round (kHz)) < 0.01 ? 0 : 1) << "kHz";
            }

            g.setColour (theme.lcdMid);
            g.drawSingleLineText (right, getWidth() - 10, getHeight() - 6, juce::Justification::right);

            drawGlass (g);
        }

        //==============================================================================
        void mouseMove (const juce::MouseEvent& e) override
        {
            setHovered (edgeAt (e.position));
        }

        void mouseExit (const juce::MouseEvent&) override
        {
            if (dragging == Edge::none)
                setHovered (Edge::none);
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            dragging = edgeAt (e.position);
            gestureStarted = false;

            if (dragging == Edge::low || dragging == Edge::high)
                beginGesture();
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            // 2 本が重なっているときは、動かした向きで決める（右なら HIGH、左なら LOW）
            if (dragging == Edge::either)
            {
                const float dx = e.position.x - e.mouseDownPosition.x;

                if (std::abs (dx) < 1.0f)
                    return;

                dragging = dx > 0.0f ? Edge::high : Edge::low;
                beginGesture();
            }

            if (dragging != Edge::low && dragging != Edge::high)
                return;

            const float hz = hzForX (e.position.x);

            if (dragging == Edge::low)
            {
                const float value = juce::jmin (hz, valueOf (highParam));
                lowParam.setValueNotifyingHost (lowParam.convertTo0to1 (value));
                info.minHz = valueOf (lowParam);
            }
            else
            {
                const float value = juce::jmax (hz, valueOf (lowParam));
                highParam.setValueNotifyingHost (highParam.convertTo0to1 (value));
                info.maxHz = valueOf (highParam);
            }

            repaint();
        }

        void mouseUp (const juce::MouseEvent& e) override
        {
            if (gestureStarted)
                (dragging == Edge::low ? lowParam : highParam).endChangeGesture();

            gestureStarted = false;
            dragging = Edge::none;
            setHovered (contains (e.position.toInt()) ? edgeAt (e.position) : Edge::none);
        }

    private:
        enum class Edge { none, low, high, either };

        static constexpr float floorDb = -120.0f;
        static constexpr float holdTime = 2.0f;
        static constexpr float x0 = 36.0f, y0 = 8.0f;
        float x1() const { return (float) getWidth() - 42.0f; }
        float y1() const { return (float) getHeight() - 48.0f; }   // 下 48 px は周波数の目盛りと状態の行

        float xForHz (float hz) const        { return x0 + (x1() - x0) * std::log10 (hz / 20.0f) / 3.0f; }
        float hzForX (float x) const         { return 20.0f * std::pow (1000.0f, (x - x0) / (x1() - x0)); }
        float yForLevel (float db) const     { return y0 + (y1() - y0) * juce::jlimit (0.0f, rangeDb, -db) / rangeDb; }
        float yForReduction (float db) const { return y0 + (y1() - y0) * 0.5f * juce::jlimit (0.0f, 48.0f, db) / 24.0f; }

        // 検出範囲の線の位置（表示範囲 20 Hz〜20 kHz の外は端に寄せる）
        float lowX() const  { return juce::jlimit (x0, x1(), xForHz (info.minHz)); }
        float highX() const { return juce::jlimit (x0, x1(), xForHz (info.maxHz)); }

        static float valueOf (const juce::RangedAudioParameter& p) { return p.convertFrom0to1 (p.getValue()); }

        void clearShown()
        {
            shownLevel.fill (floorDb);
            shownReduction.fill (0.0f);
            heldLevel.fill (floorDb);
            holdSeconds.fill (0.0f);
        }

        Edge edgeAt (juce::Point<float> p) const
        {
            if (p.y < y0 || p.y > y1() + 20.0f)
                return Edge::none;

            constexpr float grab = 6.0f;
            const float dl = std::abs (p.x - lowX()), dh = std::abs (p.x - highX());

            if (juce::jmin (dl, dh) > grab)
                return Edge::none;

            if (std::abs (lowX() - highX()) < 1.0f)
                return Edge::either;

            return dl <= dh ? Edge::low : Edge::high;
        }

        void setHovered (Edge edge)
        {
            setMouseCursor (edge == Edge::none ? juce::MouseCursor::NormalCursor : juce::MouseCursor::LeftRightResizeCursor);

            if (edge != hovered)
            {
                hovered = edge;
                repaint();
            }
        }

        void beginGesture()
        {
            (dragging == Edge::low ? lowParam : highParam).beginChangeGesture();
            gestureStarted = true;
        }

        bool isActive (Edge edge) const
        {
            const auto current = dragging != Edge::none ? dragging : hovered;
            return current == edge || current == Edge::either;
        }

        juce::String statusText() const
        {
            if (info.bypassed)
                return "BYPASS  (latency kept)";

            if (info.listening)
                return "DELTA LISTEN  -  removed signal only";

            // 設定値を先に出す。状態（GR など）は長さが変わるので後ろに置き、THR / DEPTH の位置が動かないようにする
            const juce::String settings = "THR " + juce::String (info.thresholdDb, 1) + "dB   DEPTH " + juce::String (info.depthDb, 1) + "dB   ";

            switch (suppressor.getState())
            {
                case ResonanceSuppressor::State::silent:
                    return settings + "SILENT";

                case ResonanceSuppressor::State::cutting:
                    return settings + "GR -" + juce::String (suppressor.getPeakReductionDb(), 1) + "dB @ "
                           + juce::String (juce::roundToInt (suppressor.getPeakReductionHz())) + "Hz";

                case ResonanceSuppressor::State::none:
                case ResonanceSuppressor::State::off:
                    break;
            }

            return settings + "NO CUT";
        }

        // 液晶の地: 中央が少し明るく、周りが暗い。目盛りの点線もここに入れる（点線は細かい線分の集まりで描くのが遅く、
        // 毎フレーム描くと液晶の描画時間の 3 割以上を占めたため）。絵として作り置きし、縦軸の範囲ごとに別の絵にする
        void drawBackground (juce::Graphics& g) const
        {
            const auto area = getLocalBounds().toFloat();

            drawCached (g, theme, "lcdBack" + juce::String (juce::roundToInt (rangeDb)), area, [this, area] (juce::Graphics& ig, juce::Image&, float)
            {
                ig.fillAll (theme.lcdOuter);
                fillRadialGlow (ig, circlePath (area.expanded (area.getWidth())), { area.getWidth() * 0.5f, area.getHeight() * 0.42f },
                                area.getWidth() * 0.75f, area.getHeight() * 0.7f, theme.lcdBackground);
                drawGrid (ig);
            });
        }

        // 液晶の上のガラス: 細かい横筋、左上の映り込み、斜めの照り返し、周りの暗がり、上から落ちる内側の影（絵として作り置き）
        void drawGlass (juce::Graphics& g) const
        {
            const auto area = getLocalBounds().toFloat();

            drawCached (g, theme, "lcdGlass", area, [area] (juce::Graphics& ig, juce::Image&, float)
            {
                const float w = area.getWidth(), h = area.getHeight();
                const auto everything = circlePath (area.expanded (w));

                ig.setColour (black (0.13f));

                for (float y = 0.0f; y < h; y += 3.0f)
                    ig.fillRect (0.0f, y, w, 1.0f);

                fillRadialGlow (ig, everything, { w * 0.16f, h * 0.06f }, w * 0.38f * 0.7f, h * 0.3f * 0.7f, white (0.08f));

                ig.setGradientFill (cssLinearGradient (area, 162.0f, { { 0.0f, white (0.1f) }, { 0.4f, white (0.035f) }, { 0.404f, white (0.0f) }, { 1.0f, white (0.0f) } }));
                ig.fillRect (area);

                {
                    juce::Graphics::ScopedSaveState save (ig);
                    const juce::Point<float> c { w * 0.5f, h * 0.45f };
                    ig.addTransform (juce::AffineTransform::scale (1.0f, h / w, c.x, c.y));
                    juce::ColourGradient vignette (black (0.0f), c, black (0.35f), c.translated (w * 1.2f, 0.0f), true);
                    vignette.addColour (0.55, black (0.0f));
                    ig.setGradientFill (vignette);
                    ig.fillRect (juce::Rectangle<float> (w * 4.0f, w * 4.0f).withCentre (c));
                }

                ig.setGradientFill (juce::ColourGradient (black (0.7f), 0.0f, 0.0f, black (0.0f), 0.0f, 18.0f, false));
                ig.fillRect (0.0f, 0.0f, w, 18.0f);
                ig.setColour (black (0.75f));
                ig.drawRect (area, 1.0f);
                ig.setColour (white (0.06f));
                ig.fillRect (1.0f, h - 1.0f, w - 2.0f, 1.0f);
            });
        }

        void drawGrid (juce::Graphics& g) const
        {
            const float dots[] = { 1.0f, 3.0f };
            g.setColour (theme.lcdGrid);

            for (auto hz : gridHz)
            {
                const float x = xForHz (hz);
                g.drawDashedLine ({ x, y0, x, y1() }, dots, 2, 1.0f);
            }

            for (float db = -20.0f; db > -rangeDb; db -= 20.0f)
            {
                const float y = yForLevel (db);
                g.drawDashedLine ({ x0, y, x1(), y }, dots, 2, 1.0f);
            }
        }

        void drawRange (juce::Graphics& g) const
        {
            const float left = lowX(), right = highX();
            const float dashes[] = { 3.0f, 2.0f };

            g.setColour (theme.lcdBright.withAlpha (0.06f));
            g.fillRect (left, y0, right - left, y1() - y0);

            auto edge = [&] (float x, bool active)
            {
                g.setColour (active ? theme.lcdBright : theme.lcdDim);
                g.drawDashedLine ({ x, y0, x, y1() }, dashes, 2, active ? 1.6f : 1.0f);

                // つまみ（線の下端の小さな三角）
                juce::Path grip;
                grip.addTriangle (x - 4.0f, y1() + 6.0f, x + 4.0f, y1() + 6.0f, x, y1() + 1.0f);
                g.fillPath (grip);
            };

            edge (left, isActive (Edge::low));
            edge (right, isActive (Edge::high));

            auto text = [] (float hz)
            {
                if (hz < 1000.0f)
                    return juce::String (juce::roundToInt (hz));

                return juce::String (hz / 1000.0f, 2).trimCharactersAtEnd ("0").trimCharactersAtEnd (".") + "k";
            };

            g.setFont (mono (11.0f));
            g.setColour (isActive (Edge::low) ? theme.lcdBright : theme.lcdMid);
            g.drawSingleLineText (text (info.minHz), juce::roundToInt (left - 5.0f), 22, juce::Justification::right);
            g.setColour (isActive (Edge::high) ? theme.lcdBright : theme.lcdMid);
            g.drawSingleLineText (text (info.maxHz), juce::roundToInt (right + 5.0f), 22);
        }

        void drawSpectrum (juce::Graphics& g, float binHz)
        {
            // ビンを横位置にまとめる（同じ画素に入るビンは最大値）
            points.clear();

            for (int k = 1; k < shownNumBins; ++k)
            {
                const float hz = (float) k * binHz;

                if (hz < 20.0f)
                    continue;

                if (hz > 20000.0f)
                    break;

                const float x = xForHz (hz);
                const float level = shownLevel[(size_t) k], reduction = shownReduction[(size_t) k], held = heldLevel[(size_t) k];

                if (! points.empty() && (int) x == (int) points.back().x)
                {
                    auto& last = points.back();
                    last.level = juce::jmax (last.level, level);
                    last.reduction = juce::jmax (last.reduction, reduction);
                    last.output = juce::jmax (last.output, level - reduction);
                    last.held = juce::jmax (last.held, held);
                }
                else
                {
                    points.push_back ({ x, level, reduction, level - reduction, held });
                }
            }

            if (points.size() < 2)
                return;

            // 20 Hz より上の最初のビンが左端から離れているとき（FFT 1024 点では 47 Hz）は、20 Hz の値を補って左端まで描く。
            // 20 Hz の値は前後のビンから直線で補う。前が直流のビンなら、直流の値が大きくても小さいほうにする
            if (points.front().x > x0 + 0.5f && shownNumBins > 1 && binHz > 0.0f)
            {
                const int first = juce::jlimit (1, shownNumBins - 1, (int) std::ceil (20.0f / binHz));
                auto at20 = [&] (const std::array<float, ResonanceSuppressor::maxBins>& values)
                {
                    const float below = values[(size_t) first - 1], above = values[(size_t) first];

                    if (first == 1)
                        return juce::jmin (below, above);

                    const float t = (20.0f - (float) (first - 1) * binHz) / binHz;
                    return below + (above - below) * t;
                };

                const float level = at20 (shownLevel), reduction = juce::jmax (0.0f, at20 (shownReduction));
                points.insert (points.begin(), { x0, level, reduction, level - reduction, at20 (heldLevel) });
            }

            // 低い周波数はビンの間隔が画面上で広いので、なめらかな曲線（エルミート補間）でつなぐ
            smoothed.clear();

            for (size_t i = 0; i < points.size(); ++i)
            {
                const auto& a = points[i];
                smoothed.push_back (a);

                if (i + 1 >= points.size() || points[i + 1].x - a.x <= 3.0f)
                    continue;

                const auto& b = points[i + 1];
                const auto& before = points[i > 0 ? i - 1 : i];
                const auto& after = points[juce::jmin (i + 2, points.size() - 1)];
                const float gap = b.x - a.x;
                const int steps = (int) (gap / 2.0f);

                auto curve = [&] (float Point::* value, float t)
                {
                    const float slopeA = (b.*value - before.*value) / juce::jmax (1.0e-3f, b.x - before.x) * gap;
                    const float slopeB = (after.*value - a.*value) / juce::jmax (1.0e-3f, after.x - a.x) * gap;
                    const float t2 = t * t, t3 = t2 * t;
                    return (2.0f * t3 - 3.0f * t2 + 1.0f) * a.*value + (t3 - 2.0f * t2 + t) * slopeA
                           + (-2.0f * t3 + 3.0f * t2) * b.*value + (t3 - t2) * slopeB;
                };

                for (int s = 1; s < steps; ++s)
                {
                    const float t = (float) s / (float) steps;
                    smoothed.push_back ({ a.x + gap * t, curve (&Point::level, t), juce::jmax (0.0f, curve (&Point::reduction, t)),
                                          curve (&Point::output, t), curve (&Point::held, t) });
                }
            }

            juce::Path inputFill, inputLine, outputLine, heldLine, reductionFill;
            inputFill.startNewSubPath (smoothed.front().x, y1());
            reductionFill.startNewSubPath (smoothed.front().x, y0);

            for (size_t i = 0; i < smoothed.size(); ++i)
            {
                const auto& p = smoothed[i];
                const float yIn = yForLevel (p.level), yOut = yForLevel (p.output), yHeld = yForLevel (p.held);

                inputFill.lineTo (p.x, yIn);
                reductionFill.lineTo (p.x, yForReduction (p.reduction));

                if (i == 0)
                {
                    inputLine.startNewSubPath (p.x, yIn);
                    outputLine.startNewSubPath (p.x, yOut);
                    heldLine.startNewSubPath (p.x, yHeld);
                }
                else
                {
                    inputLine.lineTo (p.x, yIn);
                    outputLine.lineTo (p.x, yOut);
                    heldLine.lineTo (p.x, yHeld);
                }
            }

            inputFill.lineTo (smoothed.back().x, y1());
            inputFill.closeSubPath();
            reductionFill.lineTo (smoothed.back().x, y0);
            reductionFill.closeSubPath();

            if (showInput)
            {
                g.setColour (theme.lcdFill.withAlpha (0.35f));
                g.fillPath (inputFill);
                g.setColour (theme.lcdDim.withAlpha (info.listening ? 0.4f : 0.8f));
                g.strokePath (inputLine, juce::PathStrokeType (1.0f));
            }

            if (peakHold)
            {
                g.setColour (theme.lcdMid.withAlpha (0.7f));
                g.strokePath (heldLine, juce::PathStrokeType (1.0f));
            }

            // 明るい線は、太く薄い線を下に重ねて光がにじんで見えるようにする（設定で切ると線 1 本だけ）
            if (showOutput)
            {
                const float alpha = info.listening ? 0.2f : 1.0f;

                if (lineGlow)
                {
                    g.setColour (theme.lcdBright.withAlpha (0.08f * alpha));
                    g.strokePath (outputLine, juce::PathStrokeType (7.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                    g.setColour (theme.lcdBright.withAlpha (0.18f * alpha));
                    g.strokePath (outputLine, juce::PathStrokeType (3.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                }

                g.setColour (theme.lcdBright.withAlpha (alpha));
                g.strokePath (outputLine, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }

            g.setColour (theme.lcdBright);
            g.fillPath (reductionFill);

            // 最大の削減位置に引き出し線と値
            const float peakDb = suppressor.getPeakReductionDb();

            if (! info.bypassed && peakDb >= 0.5f && suppressor.getState() == ResonanceSuppressor::State::cutting)
            {
                const float px = xForHz (juce::jmax (20.0f, suppressor.getPeakReductionHz()));
                const float py = yForReduction (peakDb);
                const juce::String text = "-" + juce::String (peakDb, 1) + " dB @ " + juce::String (juce::roundToInt (suppressor.getPeakReductionHz())) + " Hz";
                const auto font = mono (13.0f);
                const float textWidth = juce::GlyphArrangement::getStringWidth (font, text);
                const bool toLeft = px + 44.0f + textWidth > x1();
                const float endX = toLeft ? px - 40.0f : px + 40.0f;

                g.setColour (theme.lcdBright);
                g.drawLine (px, py, endX, 46.0f, 1.0f);
                g.setFont (font);
                g.drawSingleLineText (text, juce::roundToInt (toLeft ? endX - 4.0f : endX + 4.0f), 50,
                                      toLeft ? juce::Justification::right : juce::Justification::left);
            }
        }

        void drawScales (juce::Graphics& g) const
        {
            g.setFont (mono (11.0f));
            g.setColour (theme.lcdDim);

            const std::pair<float, const char*> labels[] = { { 50.0f, "50" }, { 100.0f, "100" }, { 200.0f, "200" }, { 500.0f, "500" },
                                                             { 1000.0f, "1k" }, { 2000.0f, "2k" }, { 5000.0f, "5k" },
                                                             { 10000.0f, "10k" }, { 20000.0f, "20k" } };

            for (const auto& [hz, text] : labels)
                g.drawSingleLineText (text, juce::roundToInt (juce::jmin (xForHz (hz), x1() - 8.0f)), getHeight() - 32, juce::Justification::horizontallyCentred);

            for (int db = 0; (float) db > -rangeDb; db -= 20)
                g.drawSingleLineText (juce::String (db), 30, juce::roundToInt (yForLevel ((float) db) + 4.0f), juce::Justification::right);

            g.setColour (theme.lcdMid);

            for (auto db : { 0, 6, 12, 24 })
                g.drawSingleLineText (db == 0 ? juce::String ("0") : "-" + juce::String (db), getWidth() - 8,
                                      juce::roundToInt (yForReduction ((float) db) + 4.0f), juce::Justification::right);
        }

        static constexpr float gridHz[] = { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f, 20000.0f };

        struct Point
        {
            float x, level, reduction, output, held;
        };

        const ResonanceSuppressor& suppressor;
        const Theme& theme;
        juce::RangedAudioParameter& lowParam;
        juce::RangedAudioParameter& highParam;
        Info info;
        Edge hovered = Edge::none, dragging = Edge::none;
        bool gestureStarted = false;
        int shownNumBins = 0;
        juce::String shownStatus;
        std::array<float, ResonanceSuppressor::maxBins> shownLevel {}, shownReduction {}, heldLevel {}, holdSeconds {};
        std::vector<Point> points, smoothed;

        float fallDbPerSecond = 45.0f, rangeDb = 90.0f;
        bool peakHold = false, showInput = true, showOutput = true, lineGlow = true;
    };
}
