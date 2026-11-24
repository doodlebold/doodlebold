#pragma once

#include <QColor>
#include <QPointF>
#include <QString>

#include <functional>
#include <vector>

namespace doodlebold {

struct TextStyle {
    QString font;           // family name; empty = the first font of the system's list
    double sizePx = 20.0;
    bool bold = false;
    bool italic = false;
    QColor colour = Qt::black;
    bool operator==(const TextStyle&) const = default;
};

struct TextRun {
    QString text;
    TextStyle style;
    bool operator==(const TextRun&) const = default;
};

// A text layer's content as styled runs; its pixels are drawn from it (TextLayout). Positions are UTF-16 code unit offsets.
struct TextContent {
    QPointF origin;              // the click point in canvas px: the top-left of the first line (Vertical: top-right)
    bool vertical = false;       // Layout → Vertical: lines are columns from right to left
    bool fitToPixelGrid = false; // Fit Outline to Pixel Grid: glyph outlines hinted to whole pixels
    std::vector<TextRun> runs;   // never empty runs; neighbours with equal styles are merged
    TextStyle emptyStyle;        // the style of a layer without text (where the caret types first)

    bool operator==(const TextContent&) const = default;

    int length() const;
    QString plainText() const;
    bool isEmpty() const { return runs.empty(); }
    TextStyle styleAt(int pos) const;
    // The style of the characters in [from, to), if they all share one (for the panel); else the first's.
    TextStyle styleOf(int from, int to) const;

    void insert(int pos, const QString& text, const TextStyle& style);
    void remove(int from, int to);
    void restyle(int from, int to, const std::function<void(TextStyle&)>& change);

private:
    void normalize();
    // Splits runs so that `pos` is a run boundary; returns the index of the run starting at `pos` (runs.size() at the end).
    std::size_t splitAt(int pos);
};

}
