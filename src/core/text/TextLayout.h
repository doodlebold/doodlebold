#pragma once

#include "core/raster/TiledImage.h"
#include "core/text/TextContent.h"

#include <QFont>
#include <QLineF>
#include <QPolygonF>
#include <QTransform>

#include <memory>
#include <vector>

class QTextLayout;

namespace doodlebold {

// One line per '\n' paragraph, no wrapping; Vertical turns the lines a quarter clockwise (columns right to left, origin at the
// first column's top-right). Needs a QGuiApplication (fonts).
class TextLayout {
public:
    explicit TextLayout(const TextContent& content);
    ~TextLayout();
    TextLayout(const TextLayout&) = delete;
    TextLayout& operator=(const TextLayout&) = delete;

    QRectF frame() const;
    QLineF caret(int pos) const;
    int positionAt(const QPointF& canvas) const;
    std::vector<QPolygonF> selection(int from, int to) const;
    int lineStart(int pos) const;
    int lineEnd(int pos) const;
    int lineAbove(int pos) const;
    int lineBelow(int pos) const;

    QRect render(TiledImage& target) const;

    static QFont fontFor(const TextStyle& style, bool fitToPixelGrid);

private:
    struct Line {
        std::unique_ptr<QTextLayout> layout;
        int start = 0;
        int length = 0;
        double top = 0.0;   // in layout coordinates (before the vertical turn)
        double height = 0.0;
    };
    const Line& lineOf(int pos) const;
    int indexOf(int pos) const;

    TextContent m_content;
    std::vector<Line> m_lines;
    QTransform m_toCanvas;
    QRectF m_bounds;        // layout coordinates
};

}
