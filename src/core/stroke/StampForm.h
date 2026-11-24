#pragma once

#include <QImage>

#include <array>
#include <limits>
#include <vector>

namespace doodlebold {

// A form's picture as a stamp's outline: centred on the stamp, its longest side Scale times the stamp's diameter, read as
// darkness (Invert: lightness), bilinear between its pixels' centres, and nothing past its edge. Strength is how much of the outline
// it is, the round stamp's the rest.
class StampForm {
public:
    // `fixedToCanvas`: a blot map, which the reference lays over the canvas as a second paper rather than on each stamp.
    // `stamped`: a brush shape (ResourceKind::Shape), which the reference stamps whole (StampTip::pictureStamps).
    // `highQuality`: a brush shape's HQ Down Scaling. `turnedAtLightest`: Invert for Transparency, erasing, turns a blot map
    // over about its top and bottom (TurnedRange), not as the Invert switch does. `oneStroke`: a brush shape whose
    // settings file asks for it (ShapeSettings::oneStroke), its stamps laid as one continuous stroke. `groundEmpty`: one of
    // Doodlebold's own blot maps, whose white ground lays nothing from Mess. 25 up (groundLaysNothing).
    StampForm(const QImage& picture, double scale, double strength, bool invert, bool fixedToCanvas = false, bool sharpen = false,
              bool stamped = false, bool highQuality = false, bool turnedAtLightest = false, bool oneStroke = false,
              bool groundEmpty = false);

    float strength() const { return m_strength; }
    bool fixedToCanvas() const { return m_fixed; }
    bool stamped() const { return m_stamped; }
    // A stamped shape laid as one stroke: a pixel takes the most any one stamp lays there, not their sum.
    bool oneStroke() const { return m_oneStroke; }
    // A blot map's outline at canvas pixel (x, y), `distance` px from the centre of a stamp of `radius`, on edge Shape `edge` (0 the
    // softest): the stamp is a cone, the picture's light parts eat into it Strength deep, and what's left passes a window the Shape sets.
    // `window`: a mixing tool's dab, which the reference gathers into one mask a pen event, takes a window of its own (BlotWindow).
    // A blot map's window: each Shape's for the stamp tools, which lay its share as paint; the mixing tools' narrower one, their outline;
    // the Brush's 5th Shape's, its outline too.
    enum class BlotWindow {
        Stamp,
        Mixing,
        BrushFifth,
        MarkerSoft
    };  // MarkerSoft: the Marker's 1st to 4th Shapes
    float blotShape(float distance, float radius, int x, int y, int edge, BlotWindow window = BlotWindow::Stamp) const;
    // How far from the centre of a stamp of `radius` the picture can reach: its corners.
    float reachFor(float radius) const;
    // The picture at (dx, dy) px from the centre of a stamp of `radius`, the stamp turned by the angle whose cosine and sine are given:
    // bilinear between its pixels' centres, 0 past its edge. Its darkness, or for a brush shape its lightness.
    float at(float dx, float dy, float radius, float turnCos = 1.0f, float turnSin = 0.0f) const;

private:
    QImage m_picture;  // 8-bit grey
    int m_width = 1, m_height = 1;
    float m_scale = 1.0f;
    float m_strength = 1.0f;
    bool m_invert = false;
    float m_darkest = 0.0f;  // with Invert, the picture's darkest grey, its anchor
    bool m_fixed = false;
    bool m_sharpen = false;  // Sharpen: a blot map's picture laid unsoftened
    bool m_groundEmpty = false;  // a blot map whose white ground lays nothing
    float groundEaten(float eaten, float light) const;
    bool m_stamped = false;  // a brush shape, stamped whole
    bool m_oneStroke = false;
    // A brush shape's picture and its halved copies, each half the last's size, lightness 0 to 1 by rows, down to a pixel.
    struct Level {
        int width = 1, height = 1;
        std::vector<float> light;
        float at(float x, float y) const;  // bilinear between its pixels' centres, held at its edge
        float averageOver(float x0, float y0, float x1, float y1) const;  // over that box of its pixels, each whole across its square
    };
    std::vector<Level> m_levels;
    bool m_highQuality = false;
    void buildLevels();
    float stampedLight(float px, float py, float picturePerPixel) const;
    // A brush shape's lit pixels' bounds in its own pixels, none lit leaving them crossed so every point is past them.
    void findLit();
    bool pastLit(float px, float py, float picturePerPixel) const;
    float m_litLeft = std::numeric_limits<float>::max(), m_litTop = std::numeric_limits<float>::max();
    float m_litRight = std::numeric_limits<float>::lowest(), m_litBottom = std::numeric_limits<float>::lowest();
    // A blot map's lightness, 0 to 1, at Scale, repeated over the canvas from its top left.
    std::vector<float> m_lightness;
    int m_tileWidth = 1, m_tileHeight = 1;
};

}
