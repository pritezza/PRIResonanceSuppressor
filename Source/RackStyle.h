#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <map>

// ラック実機風の画面のテーマ・フォント・光の表現・ノブの描画。
// 座標はすべてデザインの原寸（1280×608）で書き、エディター側で全体を拡大・縮小する。
// 光は左上から当たる想定
namespace rack
{
    // ランプとメインのノブの指針はテーマによらず同じ色
    inline const juce::Colour accent { 0xffff8c2e };
    inline const juce::Colour knobPointer { 0xffe4e0d6 };

    struct Theme
    {
        enum Id { black, silver, vintage, numThemes };

        // つまみの材質: ガンメタルの削り出し / アルミの削り出し / 黒いベークライトにクロームのスカート
        enum class KnobMaterial { gunmetal, aluminium, bakelite };

        int id = black;

        // パネル: 上・中・下の地の色、ヘアライン、光の強さ（白・黒の不透明度）
        juce::Colour panelTop, panelMid, panelBottom, hairline;
        float streak = 0.0f, sheen = 0.0f, glare = 0.0f, vignette = 0.0f, lightOverlay = 0.0f, darkOverlay = 0.0f;
        juce::Colour edgeTop, edgeBottom, earLine;
        float earHighlight = 0.0f;

        // 印刷（文字と線）。printShadow は文字の 1 px 下に置く影（明るいパネルでは光の照り返し）
        juce::Colour ink, muted, rule, ruleHighlight, printShadow, tick;
        juce::Colour ledOff, ledOffHighlight;

        // 液晶の枠のくぼみ（上が影、下の縁が光を受ける）と液晶
        juce::Colour recessTop, bezelFill;
        float recessBottom = 0.0f;
        juce::Colour lcdBackground, lcdOuter, lcdBright, lcdMid, lcdDim, lcdFill, lcdGrid;

        juce::Colour buttonTop, buttonBottom, buttonStroke, buttonText, buttonTextShadow;
        float buttonGloss = 0.0f;

        KnobMaterial knobMaterial = KnobMaterial::gunmetal;
        juce::Colour pointerSkirt, pointerTop;

        // パネルに落ちる影（遠い影と近い影）
        juce::Colour shadowFar, shadowNear;

        juce::Colour backdrop, card;

        static juce::StringArray getNames() { return { "BLACK", "SILVER", "VINTAGE" }; }

        static Theme get (int id)
        {
            switch (id)
            {
                case silver:  return makeSilver();
                case vintage: return makeVintage();
                default:      return makeBlack();
            }
        }

    private:
        static juce::Colour whiteA (float alpha) { return juce::Colours::white.withAlpha (alpha); }
        static juce::Colour blackA (float alpha) { return juce::Colours::black.withAlpha (alpha); }

        static Theme makeBlack()
        {
            Theme t;
            t.id = black;
            t.panelTop = juce::Colour (0xff2e2e31);
            t.panelMid = juce::Colour (0xff1f1f21);
            t.panelBottom = juce::Colour (0xff141416);
            t.hairline = whiteA (0.03f);
            t.streak = 0.025f;
            t.sheen = 0.12f;
            t.glare = 0.05f;
            t.vignette = 0.45f;
            t.lightOverlay = 0.05f;
            t.darkOverlay = 0.12f;
            t.edgeTop = whiteA (0.22f);
            t.edgeBottom = juce::Colour (0xff0b0b0c);
            t.earLine = juce::Colour (0xff0e0e0f);
            t.earHighlight = 0.06f;
            t.ink = juce::Colour (0xffe4e0d6);
            t.muted = juce::Colour (0xffa8a39a);
            t.rule = juce::Colour (0xff6b6862);
            t.ruleHighlight = whiteA (0.05f);
            t.printShadow = blackA (0.65f);
            t.tick = juce::Colour (0xffcfcac0);
            t.ledOff = juce::Colour (0xff3a2a1a);
            t.ledOffHighlight = juce::Colour (0xff4a3824);
            t.recessTop = blackA (0.8f);
            t.recessBottom = 0.16f;
            t.bezelFill = juce::Colour (0xff0a0a0b);
            t.lcdBackground = juce::Colour (0xff24190a);
            t.lcdOuter = juce::Colour (0xff110b03);
            t.lcdBright = juce::Colour (0xffffb347);
            t.lcdMid = juce::Colour (0xffc98a35);
            t.lcdDim = juce::Colour (0xff9c6a26);
            t.lcdFill = juce::Colour (0xff8a5a1c);
            t.lcdGrid = juce::Colour (0xff3a2a12);
            t.buttonTop = juce::Colour (0xff525258);
            t.buttonBottom = juce::Colour (0xff242427);
            t.buttonStroke = juce::Colour (0xff5c5c62);
            t.buttonText = juce::Colour (0xffe4e0d6);
            t.buttonTextShadow = blackA (0.6f);
            t.buttonGloss = 0.16f;
            t.knobMaterial = KnobMaterial::gunmetal;
            t.pointerSkirt = juce::Colour (0xffe4e0d6);
            t.pointerTop = juce::Colour (0xffe4e0d6);
            t.shadowFar = blackA (0.65f);
            t.shadowNear = blackA (0.6f);
            t.backdrop = blackA (0.6f);
            t.card = juce::Colour (0xff252528);
            return t;
        }

        // アルミのヘアライン仕上げに黒の印刷。つまみもアルミの削り出し
        static Theme makeSilver()
        {
            Theme t = makeBlack();
            t.id = silver;
            t.panelTop = juce::Colour (0xffdfe0e3);
            t.panelMid = juce::Colour (0xffc5c6c9);
            t.panelBottom = juce::Colour (0xffa8a9ad);
            t.hairline = blackA (0.045f);
            t.streak = 0.16f;
            t.sheen = 0.6f;
            t.glare = 0.3f;
            t.vignette = 0.2f;
            t.lightOverlay = 0.12f;
            t.darkOverlay = 0.1f;
            t.edgeTop = whiteA (0.9f);
            t.edgeBottom = juce::Colour (0xff6e6e72);
            t.earLine = juce::Colour (0xff8a8a8e);
            t.earHighlight = 0.7f;
            t.ink = juce::Colour (0xff1c1c1e);
            t.muted = juce::Colour (0xff47474c);
            t.rule = juce::Colour (0xff6e6e73);
            t.ruleHighlight = whiteA (0.55f);
            t.printShadow = whiteA (0.45f);
            t.tick = juce::Colour (0xff2a2a2d);
            t.ledOff = juce::Colour (0xff4d3b2b);
            t.ledOffHighlight = juce::Colour (0xff5e4a37);
            t.recessTop = blackA (0.55f);
            t.recessBottom = 0.85f;
            t.buttonTop = juce::Colour (0xfff4f4f6);
            t.buttonBottom = juce::Colour (0xffaeafb3);
            t.buttonStroke = juce::Colour (0xff6e6e73);
            t.buttonText = juce::Colour (0xff1c1c1e);
            t.buttonTextShadow = whiteA (0.6f);
            t.buttonGloss = 0.6f;
            t.knobMaterial = KnobMaterial::aluminium;
            t.pointerSkirt = juce::Colour (0xff1c1c1e);
            t.pointerTop = juce::Colour (0xff1c1c1e);
            t.shadowFar = juce::Colour::fromRGBA (30, 30, 40, 115);
            t.shadowNear = blackA (0.35f);
            t.backdrop = blackA (0.45f);
            t.card = juce::Colour (0xffd4d5d8);
            return t;
        }

        // クリーム色の塗装パネルに茶色の印刷、緑の液晶。つまみは黒いベークライトにクロームのスカート
        static Theme makeVintage()
        {
            Theme t = makeBlack();
            t.id = vintage;
            t.panelTop = juce::Colour (0xfff1e9d4);
            t.panelMid = juce::Colour (0xffe4d9be);
            t.panelBottom = juce::Colour (0xffcec1a0);
            t.hairline = juce::Colour::fromRGBA (110, 85, 50, 6);
            t.streak = 0.12f;
            t.sheen = 0.55f;
            t.glare = 0.26f;
            t.vignette = 0.18f;
            t.lightOverlay = 0.11f;
            t.darkOverlay = 0.1f;
            t.edgeTop = juce::Colour::fromRGBA (255, 252, 240, 242);
            t.edgeBottom = juce::Colour (0xff8a7a5e);
            t.earLine = juce::Colour (0xffa8987a);
            t.earHighlight = 0.6f;
            t.ink = juce::Colour (0xff3a2817);
            t.muted = juce::Colour (0xff6a5640);
            t.rule = juce::Colour (0xff8a765c);
            t.ruleHighlight = juce::Colour::fromRGBA (255, 250, 235, 153);
            t.printShadow = juce::Colour::fromRGBA (255, 250, 235, 128);
            t.tick = juce::Colour (0xff3a2817);
            t.ledOff = juce::Colour (0xff5a4430);
            t.ledOffHighlight = juce::Colour (0xff6c5440);
            t.recessTop = juce::Colour::fromRGBA (40, 25, 10, 140);
            t.recessBottom = 0.8f;
            t.bezelFill = juce::Colour (0xff1a1510);
            t.lcdBackground = juce::Colour (0xff12230f);
            t.lcdOuter = juce::Colour (0xff081208);
            t.lcdBright = juce::Colour (0xff9cf59c);
            t.lcdMid = juce::Colour (0xff6ccf74);
            t.lcdDim = juce::Colour (0xff3f8a4a);
            t.lcdFill = juce::Colour (0xff2f6b3a);
            t.lcdGrid = juce::Colour (0xff1e3a22);
            t.buttonTop = juce::Colour (0xfff8f2e2);
            t.buttonBottom = juce::Colour (0xffc8bb99);
            t.buttonStroke = juce::Colour (0xff8a765c);
            t.buttonText = juce::Colour (0xff3a2817);
            t.buttonTextShadow = juce::Colour::fromRGBA (255, 250, 235, 153);
            t.buttonGloss = 0.55f;
            t.knobMaterial = KnobMaterial::bakelite;
            t.pointerSkirt = juce::Colour (0xff1a1612);
            t.pointerTop = juce::Colour (0xfff1e8d2);
            t.shadowFar = juce::Colour::fromRGBA (60, 40, 20, 115);
            t.shadowNear = juce::Colour::fromRGBA (40, 25, 10, 102);
            t.backdrop = juce::Colour (0xff1a1208).withAlpha (0.5f);
            t.card = juce::Colour (0xffeee5cf);
            return t;
        }
    };

    // シルク印刷の文字は Bahnschrift（Windows 10 以降に標準で入っている DIN 風の書体）の SemiCondensed。
    // 無い環境では既定のサンセリフにする。size は CSS の font-size と同じ意味（em の高さ）
    inline juce::Font silkscreen (float size, bool bold = true, float tracking = 0.0f)
    {
        static const auto styles = juce::Font::findAllTypefaceStyles ("Bahnschrift");

        auto pick = [] (bool wantBold)
        {
            for (const auto& style : styles)
            {
                const auto compact = style.removeCharacters (" ");

                if (style.contains ("Oblique") || ! compact.contains ("SemiCondensed"))
                    continue;

                if (wantBold ? compact.contains ("SemiBold") : compact == "SemiCondensed")
                    return style;
            }

            return juce::String();
        };

        static const juce::String boldStyle = pick (true), regularStyle = pick (false);
        const auto& style = bold ? boldStyle : regularStyle;

        auto options = style.isNotEmpty() ? juce::FontOptions ("Bahnschrift", style, 1.0f)
                                          : juce::FontOptions (1.0f, bold ? juce::Font::bold : juce::Font::plain);

        return juce::Font (options.withPointHeight (size).withKerningFactor (tracking));
    }

    // 液晶と数値の表示
    inline juce::Font mono (float size)
    {
        return juce::Font (juce::FontOptions ("Consolas", "Regular", 1.0f).withPointHeight (size));
    }

    //==============================================================================
    // 光の表現の部品

    inline juce::Colour white (float alpha) { return juce::Colours::white.withAlpha (juce::jlimit (0.0f, 1.0f, alpha)); }
    inline juce::Colour black (float alpha) { return juce::Colours::black.withAlpha (juce::jlimit (0.0f, 1.0f, alpha)); }

    // 実際の画素と論理座標の比（エディターの拡大率とディスプレイの倍率を含む）
    inline float physicalScale (juce::Graphics& g)
    {
        return juce::jmax (0.25f, g.getInternalContext().getPhysicalPixelScaleFactor());
    }

    // 位置（0〜1）と色の並び
    using Stops = std::vector<std::pair<float, juce::Colour>>;

    inline juce::Colour colourAt (const Stops& stops, float t)
    {
        if (t <= stops.front().first)
            return stops.front().second;

        for (size_t i = 1; i < stops.size(); ++i)
            if (t <= stops[i].first)
            {
                const auto& [p0, c0] = stops[i - 1];
                const auto& [p1, c1] = stops[i];
                return c0.interpolatedWith (c1, (t - p0) / juce::jmax (1.0e-6f, p1 - p0));
            }

        return stops.back().second;
    }

    inline Stops evenStops (std::initializer_list<juce::Colour> colours)
    {
        Stops stops;
        const int n = (int) colours.size();
        int i = 0;

        for (auto c : colours)
            stops.push_back ({ (float) i++ / (float) juce::jmax (1, n - 1), c });

        return stops;
    }

    // 円錐状のグラデーション（削り出しの金属の放射状の反射）を画素ごとに塗る。座標は画像の画素。
    // fromDegrees は 12 時から時計回り。縁は 1 px でなめらかにする
    inline void fillConicDisc (juce::Image& image, juce::Point<float> centre, float radius, float fromDegrees, const Stops& stops)
    {
        juce::Image::BitmapData data (image, juce::Image::BitmapData::readWrite);
        const int x0 = juce::jmax (0, (int) std::floor (centre.x - radius - 1.0f)), x1 = juce::jmin (image.getWidth(), (int) std::ceil (centre.x + radius + 1.0f));
        const int y0 = juce::jmax (0, (int) std::floor (centre.y - radius - 1.0f)), y1 = juce::jmin (image.getHeight(), (int) std::ceil (centre.y + radius + 1.0f));

        for (int y = y0; y < y1; ++y)
        {
            for (int x = x0; x < x1; ++x)
            {
                const float dx = (float) x + 0.5f - centre.x, dy = (float) y + 0.5f - centre.y;
                const float coverage = juce::jlimit (0.0f, 1.0f, radius - std::sqrt (dx * dx + dy * dy) + 0.5f);

                if (coverage <= 0.0f)
                    continue;

                float degrees = juce::radiansToDegrees (std::atan2 (dx, -dy)) - fromDegrees;
                degrees -= 360.0f * std::floor (degrees / 360.0f);
                const auto colour = colourAt (stops, degrees / 360.0f).withMultipliedAlpha (coverage);
                data.setPixelColour (x, y, data.getPixelColour (x, y).overlaidWith (colour));
            }
        }
    }

    // 楕円の放射グラデーション（中心の色 → 透明）。clip の形の中だけに塗る
    inline void fillRadialGlow (juce::Graphics& g, const juce::Path& clip, juce::Point<float> centre, float radiusX, float radiusY,
                                juce::Colour colour)
    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (clip);
        g.addTransform (juce::AffineTransform::scale (1.0f, radiusY / radiusX, centre.x, centre.y));
        g.setGradientFill (juce::ColourGradient (colour, centre, colour.withAlpha (0.0f), centre.translated (radiusX, 0.0f), true));
        const float extent = clip.getBounds().getWidth() + clip.getBounds().getHeight() + radiusX * 2.0f;
        g.fillRect (juce::Rectangle<float> (extent * 2.0f, extent * 2.0f * radiusX / radiusY).withCentre (centre));
    }

    // CSS の linear-gradient(角度) と同じ向き・長さのグラデーションを area に作る（0° が上向き、時計回り）
    inline juce::ColourGradient cssLinearGradient (juce::Rectangle<float> area, float degrees, const Stops& stops)
    {
        const float a = juce::degreesToRadians (degrees);
        const juce::Point<float> dir { std::sin (a), -std::cos (a) };
        const float length = std::abs (area.getWidth() * dir.x) + std::abs (area.getHeight() * dir.y);
        const auto c = area.getCentre();
        const auto start = c - dir * (length * 0.5f), end = c + dir * (length * 0.5f);
        const float t0 = stops.front().first, t1 = stops.back().first;

        juce::ColourGradient gradient (stops.front().second, start + (end - start) * t0, stops.back().second, start + (end - start) * t1, false);

        for (size_t i = 1; i + 1 < stops.size(); ++i)
            gradient.addColour ((stops[i].first - t0) / juce::jmax (1.0e-6f, t1 - t0), stops[i].second);

        return gradient;
    }

    // 縁の照り返し: 上の縁が光り、下の縁は影になる
    inline void drawRim (juce::Graphics& g, juce::Rectangle<float> circle, float topWhite, float bottomBlack)
    {
        const auto ring = circle.reduced (0.5f);
        g.setGradientFill (juce::ColourGradient (white (topWhite), ring.getCentreX(), ring.getY(),
                                                 white (0.0f), ring.getCentreX(), ring.getCentreY(), false));
        g.drawEllipse (ring, 1.0f);
        g.setGradientFill (juce::ColourGradient (black (0.0f), ring.getCentreX(), ring.getCentreY(),
                                                 black (bottomBlack), ring.getCentreX(), ring.getBottom(), false));
        g.drawEllipse (ring, 1.0f);
    }

    inline juce::Path circlePath (juce::Rectangle<float> area)
    {
        juce::Path p;
        p.addEllipse (area);
        return p;
    }

    // 作るのに時間がかかる絵（つまみの本体など）の置き場。絵の種類とテーマごとに、今の大きさの 1 枚だけを持つ
    // （大きさが変わったら作り直す。角のドラッグで大きさを変えても溜まらない）。
    // エディターが ImageCache の SharedResourcePointer を持ち、最後のエディターが閉じたら絵も消える
    struct ImageCache
    {
        std::map<juce::String, juce::Image> images;
    };

    inline juce::Image cachedImage (const juce::String& key, int width, int height, const std::function<void (juce::Image&)>& render)
    {
        juce::SharedResourcePointer<ImageCache> cache;
        width = juce::jmax (1, width);
        height = juce::jmax (1, height);

        if (auto it = cache->images.find (key); it != cache->images.end() && it->second.getWidth() == width && it->second.getHeight() == height)
            return it->second;

        // 画素を直接書くのでソフトウェアの画像で作る（エディターはソフトウェア描画にしているので、そのまま描ける）
        juce::Image image (juce::Image::ARGB, width, height, true, juce::SoftwareImageType());
        render (image);
        return cache->images[key] = image;
    }

    // 論理座標 area に、実際の画素の倍率で作った絵を描く。key には絵の種類を入れる（テーマはここで足す）
    inline void drawCached (juce::Graphics& g, const Theme& theme, const juce::String& key, juce::Rectangle<float> area,
                            const std::function<void (juce::Graphics&, juce::Image&, float scale)>& render)
    {
        const float scale = physicalScale (g);
        const int width = (int) std::ceil (area.getWidth() * scale), height = (int) std::ceil (area.getHeight() * scale);
        const auto fullKey = key + "/" + juce::String (theme.id);

        const auto image = cachedImage (fullKey, width, height, [&] (juce::Image& target)
        {
            juce::Graphics ig (target);
            ig.addTransform (juce::AffineTransform::scale ((float) width / area.getWidth(), (float) height / area.getHeight()));
            render (ig, target, scale);
        });

        g.drawImage (image, area, juce::RectanglePlacement::stretchToFit);
    }

    //==============================================================================
    // つまみ

    struct KnobLook
    {
        Stops skirt, face;
        float skirtFrom = 15.0f, faceFrom = 25.0f, skirtSpec = 0.1f, faceSpec = 0.3f;
        Stops capSide;
        bool glossyFace = false;   // ベークライト: 削り出しの反射ではなく、つやのある面
    };

    inline KnobLook knobLook (Theme::KnobMaterial material)
    {
        using C = juce::Colour;
        KnobLook look;

        switch (material)
        {
            case Theme::KnobMaterial::aluminium:
                look.skirt = { { 0.0f, C (0xff8a8b8f) }, { 0.1f, C (0xfff6f6f8) }, { 0.22f, C (0xff96979b) }, { 0.36f, C (0xffe2e3e6) }, { 0.5f, C (0xff808185) },
                               { 0.62f, C (0xfff6f6f8) }, { 0.74f, C (0xff96979b) }, { 0.87f, C (0xffe2e3e6) }, { 1.0f, C (0xff8a8b8f) } };
                look.face = { { 0.0f, C (0xffb4b5b8) }, { 0.09f, C (0xffffffff) }, { 0.2f, C (0xffa9aaae) }, { 0.33f, C (0xffeeeef0) }, { 0.46f, C (0xffa0a1a5) },
                              { 0.59f, C (0xffffffff) }, { 0.71f, C (0xffa9aaae) }, { 0.84f, C (0xffeeeef0) }, { 1.0f, C (0xffb4b5b8) } };
                look.capSide = { { 0.0f, C (0xffffffff) }, { 0.45f, C (0xffc9cacd) }, { 1.0f, C (0xff75767a) } };
                look.skirtSpec = 0.35f;
                look.faceSpec = 0.6f;
                break;

            case Theme::KnobMaterial::bakelite:
                look.skirt = { { 0.0f, C (0xff6a6a6e) }, { 0.09f, C (0xfffbfbfc) }, { 0.2f, C (0xff7c7c80) }, { 0.34f, C (0xffe9e9ec) }, { 0.48f, C (0xff606064) },
                               { 0.6f, C (0xfffbfbfc) }, { 0.72f, C (0xff7c7c80) }, { 0.86f, C (0xffe9e9ec) }, { 1.0f, C (0xff6a6a6e) } };
                look.capSide = { { 0.0f, C (0xff4e463e) }, { 0.45f, C (0xff1b1714) }, { 1.0f, C (0xff0a0807) } };
                look.skirtSpec = 0.4f;
                look.glossyFace = true;
                break;

            case Theme::KnobMaterial::gunmetal:
            default:
                look.skirt = { { 0.0f, C (0xff0d0d0e) }, { 0.1f, C (0xff3c3c40) }, { 0.22f, C (0xff0d0d0e) }, { 0.36f, C (0xff303034) }, { 0.5f, C (0xff0b0b0c) },
                               { 0.62f, C (0xff3c3c40) }, { 0.74f, C (0xff0d0d0e) }, { 0.87f, C (0xff303034) }, { 1.0f, C (0xff0d0d0e) } };
                look.face = { { 0.0f, C (0xff2e2e32) }, { 0.09f, C (0xff7a7a80) }, { 0.2f, C (0xff2a2a2e) }, { 0.33f, C (0xff66666c) }, { 0.46f, C (0xff28282c) },
                              { 0.59f, C (0xff7a7a80) }, { 0.71f, C (0xff2a2a2e) }, { 0.84f, C (0xff66666c) }, { 1.0f, C (0xff2e2e32) } };
                look.capSide = { { 0.0f, C (0xff5a5a5f) }, { 0.45f, C (0xff2a2a2d) }, { 1.0f, C (0xff131315) } };
                break;
        }

        return look;
    }

    // つまみの半径（大きさ size に対する比）: スカート、側面、上面
    inline float knobSkirtRadius (float size) { return size * 0.35f; }
    inline float knobCapRadius (float size)   { return size * 0.27f; }
    inline float knobTopRadius (float size)   { return size * 0.215f; }

    // つまみの本体（影、スカート、側面、上面）。削り出しの反射は光の向きで決まり、回しても動かないので、絵として作り置きする
    inline void drawKnobBody (juce::Graphics& g, const Theme& theme, juce::Point<float> centre, float size)
    {
        constexpr float margin = 14.0f;   // 影の分
        const auto area = juce::Rectangle<float> (size + margin * 2.0f, size + margin * 2.0f).withCentre (centre);

        drawCached (g, theme, "knob" + juce::String (size, 1), area, [&theme, size] (juce::Graphics& ig, juce::Image& image, float)
        {
            const auto look = knobLook (theme.knobMaterial);
            const juce::Point<float> c { size / 2.0f + margin, size / 2.0f + margin };
            const float skirt = knobSkirtRadius (size), cap = knobCapRadius (size), top = knobTopRadius (size);
            const auto skirtArea = juce::Rectangle<float> (skirt * 2.0f, skirt * 2.0f).withCentre (c);
            const auto capArea = juce::Rectangle<float> (cap * 2.0f, cap * 2.0f).withCentre (c);
            const auto topArea = juce::Rectangle<float> (top * 2.0f, top * 2.0f).withCentre (c);
            const float pixels = (float) image.getWidth() / (size + margin * 2.0f);

            // パネルに落ちる影（右下へ）
            juce::DropShadow (theme.shadowFar, 10, { 3, 7 }).drawForPath (ig, circlePath (skirtArea));
            juce::DropShadow (theme.shadowNear, 3, { 1, 2 }).drawForPath (ig, circlePath (skirtArea));

            // スカート: 削り出しの放射状の反射と、左上の照り
            fillConicDisc (image, c * pixels, skirt * pixels, look.skirtFrom, look.skirt);
            fillRadialGlow (ig, circlePath (skirtArea), { skirtArea.getX() + skirtArea.getWidth() * 0.34f, skirtArea.getY() + skirtArea.getHeight() * 0.28f },
                            skirtArea.getWidth() * 0.51f, skirtArea.getWidth() * 0.51f, white (look.skirtSpec));
            drawRim (ig, skirtArea, 0.3f, 0.45f);

            // 側面（円柱）: 左上から右下へ暗くなる
            juce::DropShadow (black (0.55f), 3, { 0, 2 }).drawForPath (ig, circlePath (capArea));
            ig.setGradientFill (cssLinearGradient (capArea, 160.0f, look.capSide));
            ig.fillEllipse (capArea);
            drawRim (ig, capArea, 0.35f, 0.0f);

            // 上面
            if (look.glossyFace)
            {
                const juce::Point<float> base { topArea.getCentreX(), topArea.getY() + topArea.getHeight() * 0.62f };
                ig.setGradientFill (juce::ColourGradient (juce::Colour (0xff231d18), base, juce::Colour (0xff0c0a08),
                                                          base.translated (topArea.getWidth() * 0.8f, 0.0f), true));
                ig.fillEllipse (topArea);
                fillRadialGlow (ig, circlePath (topArea), { topArea.getX() + topArea.getWidth() * 0.36f, topArea.getY() + topArea.getHeight() * 0.27f },
                                topArea.getWidth() * 0.62f * 0.72f, topArea.getHeight() * 0.46f * 0.72f, juce::Colour::fromRGBA (255, 250, 238, 153));
            }
            else
            {
                fillConicDisc (image, c * pixels, top * pixels, look.faceFrom, look.face);
                fillRadialGlow (ig, circlePath (topArea), { topArea.getX() + topArea.getWidth() * 0.34f, topArea.getY() + topArea.getHeight() * 0.28f },
                                topArea.getWidth() * 0.51f, topArea.getWidth() * 0.51f, white (look.faceSpec));
            }

            drawRim (ig, topArea, 0.4f, 0.5f);
        });
    }

    // 指針: スカートの線と上面の線。angle は 12 時から時計回り（ラジアン）。accented はメインのノブ（オレンジ、少し光る）
    inline void drawKnobPointer (juce::Graphics& g, const Theme& theme, juce::Point<float> centre, float size, float angle, bool accented)
    {
        const bool large = size >= 80.0f;
        const float skirt = knobSkirtRadius (size), cap = knobCapRadius (size), top = knobTopRadius (size);
        auto point = [centre] (float radius, float a) { return centre.getPointOnCircumference (radius, a); };

        g.setColour (accented ? accent : theme.pointerSkirt);
        g.drawLine ({ point (cap + 1.0f, angle), point (skirt - 1.0f, angle) }, large ? 2.5f : 2.0f);

        juce::Path pointer;
        pointer.startNewSubPath (point (top * 0.25f, angle));
        pointer.lineTo (point (top * 0.92f, angle));
        const float width = large ? 3.5f : 2.5f;

        if (accented)
        {
            g.setColour (accent.withAlpha (0.25f));
            g.strokePath (pointer, juce::PathStrokeType (width + 4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        g.setColour (accented ? accent : theme.pointerTop);
        g.strokePath (pointer, juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    //==============================================================================
    // LED。点いているときはにじんだ光と、表面の照り。消えているときは暗いガラスに小さな照り
    inline void drawLed (juce::Graphics& g, const Theme& theme, juce::Rectangle<float> area, bool lit, bool round)
    {
        auto shape = [round] (juce::Rectangle<float> a)
        {
            juce::Path p;

            if (round)
                p.addEllipse (a);
            else
                p.addRoundedRectangle (a, juce::jmin (2.0f, a.getHeight() * 0.25f));

            return p;
        };

        const auto centre = area.getCentre();
        const float radius = juce::jmax (area.getWidth(), area.getHeight()) * 0.5f;

        if (lit)
        {
            const float glow = radius + 9.0f;
            g.setGradientFill (juce::ColourGradient (accent.withAlpha (0.5f), centre, accent.withAlpha (0.0f), centre.translated (glow, 0.0f), true));
            g.fillEllipse (juce::Rectangle<float> (glow * 2.0f, glow * 2.0f).withCentre (centre));

            juce::ColourGradient body (juce::Colour (0xffffe2bc), centre.x, area.getY() + area.getHeight() * 0.4f,
                                       juce::Colour (0xffe0670f), centre.x + radius * 1.2f, area.getY() + area.getHeight() * 0.4f, true);
            body.addColour (0.45, juce::Colour (0xffff9a40));
            g.setGradientFill (body);
            g.fillPath (shape (area));
        }
        else
        {
            g.setGradientFill (juce::ColourGradient (theme.ledOffHighlight, centre.x, area.getY(), theme.ledOff, centre.x, area.getBottom(), false));
            g.fillPath (shape (area));
            g.setColour (black (0.3f));
            g.strokePath (shape (area.reduced (0.5f)), juce::PathStrokeType (1.0f));
        }

        // 表面の照り（上の細い帯）
        const auto spec = round ? juce::Rectangle<float> (area.getWidth() * 0.45f, area.getHeight() * 0.3f).withCentre ({ centre.x - area.getWidth() * 0.08f, area.getY() + area.getHeight() * 0.28f })
                                : juce::Rectangle<float> (area.getX() + 2.0f, area.getY() + 1.0f, area.getWidth() - 4.0f, juce::jmin (3.0f, area.getHeight() * 0.3f));
        g.setColour (white (lit ? 0.55f : 0.16f));
        g.fillRoundedRectangle (spec, spec.getHeight() * 0.5f);
    }

    // 印刷の文字: 1 px 下に影（明るいパネルでは照り返し）を置いてから文字を書く
    inline void drawPrinted (juce::Graphics& g, const Theme& theme, const juce::String& text, juce::Rectangle<float> area,
                             juce::Justification justification, juce::Colour colour)
    {
        g.setColour (theme.printShadow);
        g.drawText (text, area.translated (0.0f, 1.0f), justification, false);
        g.setColour (colour);
        g.drawText (text, area, justification, false);
    }

    inline void drawPrinted (juce::Graphics& g, const Theme& theme, const juce::String& text, int x, int baseline,
                             juce::Justification justification, juce::Colour colour)
    {
        g.setColour (theme.printShadow);
        g.drawSingleLineText (text, x, baseline + 1, justification);
        g.setColour (colour);
        g.drawSingleLineText (text, x, baseline, justification);
    }

    // 「—— DETECT ——」のような見出し（文字の左右に線。線は刻まれたように下の縁が光る）
    inline void drawGroupHeader (juce::Graphics& g, const Theme& theme, const juce::String& title, juce::Rectangle<float> area)
    {
        const auto font = silkscreen (10.0f, false, 0.24f);
        const float textWidth = juce::GlyphArrangement::getStringWidth (font, title);
        const float lineY = area.getCentreY();
        const float gap = 6.0f;
        const float leftWidth = area.getWidth() / 2.0f - textWidth / 2.0f - gap;
        const float rightX = area.getCentreX() + textWidth / 2.0f + gap;

        for (const auto& [colour, offset] : { std::pair { theme.ruleHighlight, 1.0f }, std::pair { theme.rule, 0.0f } })
        {
            g.setColour (colour);
            g.fillRect (area.getX(), lineY + offset, leftWidth, 1.0f);
            g.fillRect (rightX, lineY + offset, area.getRight() - rightX, 1.0f);
        }

        g.setFont (font);
        drawPrinted (g, theme, title, area, juce::Justification::centred, theme.ink);
    }

    // 盛り上がった押しボタンの表面。押し込まれているときは 1.5 px 下げて影を内側に。
    // 上半分に照り返しの帯、下に厚みと、パネルに落ちるやわらかい影
    inline void drawButtonCap (juce::Graphics& g, const Theme& theme, juce::Rectangle<float> cap, bool pressed, bool highlighted,
                               float corner = 4.0f)
    {
        if (! pressed)
        {
            for (int i = 2; i >= 1; --i)
            {
                g.setColour (theme.shadowFar.withMultipliedAlpha (0.22f));
                g.fillRoundedRectangle (cap.translated (0.0f, 1.5f + (float) i).expanded ((float) i), corner + (float) i);
            }

            g.setColour (black (0.5f));
            g.fillRoundedRectangle (cap.translated (0.0f, 2.5f), corner);
        }
        else
        {
            cap = cap.translated (0.0f, 1.5f);
        }

        auto top = theme.buttonTop, bottom = theme.buttonBottom;

        if (pressed)
        {
            top = top.darker (0.25f);
            bottom = bottom.darker (0.25f);
        }
        else if (highlighted)
        {
            top = top.brighter (0.12f);
            bottom = bottom.brighter (0.12f);
        }

        g.setGradientFill (juce::ColourGradient (pressed ? bottom : top, cap.getX(), cap.getY(),
                                                 pressed ? top : bottom, cap.getX(), cap.getBottom(), false));
        g.fillRoundedRectangle (cap, corner);

        // 照り返しは表面の形の中だけに描く（丸いボタンで円の外にはみ出さないように）
        {
            juce::Graphics::ScopedSaveState save (g);
            juce::Path surface;
            surface.addRoundedRectangle (cap, corner);
            g.reduceClipRegion (surface);

            // 上半分の照り返し（押し込むと弱くなる）
            const float gloss = theme.buttonGloss * (pressed ? 0.35f : 1.0f);
            juce::ColourGradient band (white (gloss), cap.getX(), cap.getY(), white (0.0f), cap.getX(), cap.getCentreY(), false);
            band.addColour (0.92, white (gloss * 0.25f));
            g.setGradientFill (band);
            g.fillRect (cap.withHeight (cap.getHeight() * 0.5f));

            // 上の縁の照り返し（押し込むと消えて、上側に影が出る）
            g.setColour (pressed ? black (0.35f) : white (0.4f));
            g.fillRect (cap.reduced (2.0f, 0.0f).withTrimmedTop (1.0f).withHeight (1.2f));
        }

        g.setColour (theme.buttonStroke);
        g.drawRoundedRectangle (cap.reduced (0.5f), corner, 1.0f);
    }

    // 押しボタンの表面の文字（影付き）
    inline void drawButtonLegend (juce::Graphics& g, const Theme& theme, const juce::String& text, juce::Rectangle<float> area, juce::Colour colour)
    {
        g.setColour (theme.buttonTextShadow);
        g.drawText (text, area.translated (0.0f, 1.0f), juce::Justification::centred, false);
        g.setColour (colour);
        g.drawText (text, area, juce::Justification::centred, false);
    }

    // スカート付きのノブ（メインのノブは Slider::rotarySliderFillColourId に accent を入れる）と、設定画面の横スライダー
    class LookAndFeel final : public juce::LookAndFeel_V4
    {
    public:
        explicit LookAndFeel (const Theme& themeToUse) : theme (themeToUse) {}

        void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                               float startAngle, float endAngle, juce::Slider& slider) override
        {
            const float size = (float) juce::jmin (width, height);
            const auto centre = juce::Rectangle<int> (x, y, width, height).toFloat().getCentre();
            const bool large = size >= 80.0f;
            const float skirt = knobSkirtRadius (size);
            const bool accented = slider.findColour (juce::Slider::rotarySliderFillColourId) == accent;

            // 目盛り: 11 本（両端と中央は長い）
            g.setColour (theme.tick);

            for (int i = 0; i <= 10; ++i)
            {
                const float angle = startAngle + (endAngle - startAngle) * (float) i / 10.0f;
                const float outer = juce::jmin (skirt + (i % 5 == 0 ? 10.0f : 7.0f), size * 0.5f - 0.8f);
                g.drawLine ({ centre.getPointOnCircumference (skirt + 3.0f, angle), centre.getPointOnCircumference (outer, angle) }, large ? 1.4f : 1.2f);
            }

            drawKnobBody (g, theme, centre, size);
            drawKnobPointer (g, theme, centre, size, startAngle + sliderPos * (endAngle - startAngle), accented);
        }

        void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos, float, float,
                               juce::Slider::SliderStyle, juce::Slider& slider) override
        {
            const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat();
            const float trackY = bounds.getCentreY();

            g.setColour (theme.rule.withAlpha (0.6f));
            g.fillRoundedRectangle (bounds.getX(), trackY - 2.0f, bounds.getWidth(), 4.0f, 2.0f);
            g.setColour (accent);
            g.fillRoundedRectangle (bounds.getX(), trackY - 2.0f, sliderPos - bounds.getX(), 4.0f, 2.0f);

            drawButtonCap (g, theme, juce::Rectangle<float> (14.0f, 20.0f).withCentre ({ sliderPos, trackY }),
                           slider.isMouseButtonDown(), slider.isMouseOverOrDragging(), 3.0f);
        }

    private:
        const Theme& theme;
    };
}
