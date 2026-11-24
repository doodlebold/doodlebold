#include "core/text/TextLayout.h"

#include <QFontDatabase>
#include <QImage>
#include <QPainter>
#include <QTextLayout>

#include <algorithm>
#include <cmath>

namespace doodlebold {

QFont TextLayout::fontFor(const TextStyle& style, bool fitToPixelGrid) {
    QFont f = style.font.isEmpty() ? QFontDatabase::systemFont(QFontDatabase::GeneralFont) : QFont(style.font);
    f.setPixelSize(std::max(1, int(std::lround(style.sizePx))));
    if (std::abs(style.sizePx - std::round(style.sizePx)) > 1e-6)
        f.setPointSizeF(style.sizePx * 0.75);  // fractional pixel sizes: points at 96 dpi (QFont has no fractional pixel size)
    f.setBold(style.bold);
    f.setItalic(style.italic);
    f.setHintingPreference(fitToPixelGrid ? QFont::PreferFullHinting : QFont::PreferNoHinting);
    f.setStyleStrategy(QFont::PreferAntialias);
    return f;
}

TextLayout::TextLayout(const TextContent& content) : m_content(content) {
    const QString text = content.plainText();
    const QStringList paragraphs = text.split(QLatin1Char('\n'));
    int start = 0;
    double y = 0.0;
    for (const QString& para : paragraphs) {
        Line line;
        line.start = start;
        line.length = int(para.size());
        line.layout = std::make_unique<QTextLayout>(para);
        // The paragraph's own font: its first character's (an empty line: the style typed there).
        line.layout->setFont(fontFor(content.styleAt(start + (para.isEmpty() ? 0 : 1)), content.fitToPixelGrid));
        QList<QTextLayout::FormatRange> formats;
        int runStart = 0;
        for (const TextRun& r : content.runs) {
            const int runEnd = runStart + int(r.text.size());
            const int a = std::max(runStart, start), b = std::min(runEnd, start + line.length);
            if (a < b) {
                QTextLayout::FormatRange f;
                f.start = a - start;
                f.length = b - a;
                f.format.setFont(fontFor(r.style, content.fitToPixelGrid));
                f.format.setForeground(r.style.colour);
                formats << f;
            }
            runStart = runEnd;
        }
        line.layout->setFormats(formats);
        QTextOption option;
        option.setWrapMode(QTextOption::NoWrap);
        option.setUseDesignMetrics(!content.fitToPixelGrid);
        line.layout->setTextOption(option);
        line.layout->beginLayout();
        QTextLine tl = line.layout->createLine();
        if (tl.isValid()) {
            tl.setLineWidth(1e6);
            tl.setPosition(QPointF(0, 0));
        }
        line.layout->endLayout();
        line.top = content.fitToPixelGrid ? std::round(y) : y;
        line.height = tl.isValid() ? tl.height() : QFontMetricsF(line.layout->font()).height();
        y = line.top + line.height;
        m_bounds |= QRectF(0, line.top, std::max(1.0, tl.isValid() ? tl.naturalTextWidth() : 1.0), line.height);
        start += line.length + 1;
        m_lines.push_back(std::move(line));
    }
    QPointF origin = content.origin;
    if (content.fitToPixelGrid)
        origin = QPointF(std::round(origin.x()), std::round(origin.y()));
    m_toCanvas = QTransform::fromTranslate(origin.x(), origin.y());
    if (content.vertical)
        m_toCanvas.rotate(90);  // (x, y) → (−y, x): the first line is the rightmost column
}

TextLayout::~TextLayout() = default;

int TextLayout::indexOf(int pos) const {
    for (std::size_t i = 0; i < m_lines.size(); ++i)
        if (pos <= m_lines[i].start + m_lines[i].length)
            return int(i);
    return int(m_lines.size()) - 1;
}

const TextLayout::Line& TextLayout::lineOf(int pos) const {
    return m_lines[std::size_t(indexOf(pos))];
}

QRectF TextLayout::frame() const {
    return m_toCanvas.mapRect(m_bounds);
}

QLineF TextLayout::caret(int pos) const {
    pos = std::clamp(pos, 0, m_content.length());
    const Line& l = lineOf(pos);
    const QTextLine tl = l.layout->lineCount() ? l.layout->lineAt(0) : QTextLine();
    const double x = tl.isValid() ? tl.cursorToX(pos - l.start) : 0.0;
    return m_toCanvas.map(QLineF(x, l.top, x, l.top + l.height));
}

int TextLayout::positionAt(const QPointF& canvas) const {
    bool invertible = false;
    const QPointF p = m_toCanvas.inverted(&invertible).map(canvas);
    std::size_t i = 0;
    while (i + 1 < m_lines.size() && p.y() >= m_lines[i].top + m_lines[i].height)
        ++i;
    const Line& l = m_lines[i];
    const QTextLine tl = l.layout->lineCount() ? l.layout->lineAt(0) : QTextLine();
    return l.start + (tl.isValid() ? tl.xToCursor(p.x()) : 0);
}

std::vector<QPolygonF> TextLayout::selection(int from, int to) const {
    std::vector<QPolygonF> out;
    if (from >= to)
        return out;
    for (const Line& l : m_lines) {
        const int a = std::max(from, l.start), b = std::min(to, l.start + l.length);
        const bool newlineSelected = to > l.start + l.length && from <= l.start + l.length;
        if (a > b || (a == b && !newlineSelected))
            continue;
        const QTextLine tl = l.layout->lineCount() ? l.layout->lineAt(0) : QTextLine();
        const double x0 = tl.isValid() ? tl.cursorToX(a - l.start) : 0.0;
        double x1 = tl.isValid() ? tl.cursorToX(b - l.start) : 0.0;
        if (newlineSelected)
            x1 += QFontMetricsF(l.layout->font()).averageCharWidth() / 2;  // the line break shows as a little space
        out.push_back(m_toCanvas.map(QPolygonF(QRectF(x0, l.top, std::max(1.0, x1 - x0), l.height))));
    }
    return out;
}

int TextLayout::lineStart(int pos) const {
    return lineOf(pos).start;
}

int TextLayout::lineEnd(int pos) const {
    const Line& l = lineOf(pos);
    return l.start + l.length;
}

int TextLayout::lineAbove(int pos) const {
    const int i = indexOf(pos);
    if (i == 0)
        return 0;
    const Line& l = lineOf(pos);
    const QTextLine tl = l.layout->lineAt(0);
    const double x = tl.isValid() ? tl.cursorToX(pos - l.start) : 0.0;
    const Line& up = m_lines[std::size_t(i - 1)];
    const QTextLine ut = up.layout->lineAt(0);
    return up.start + (ut.isValid() ? ut.xToCursor(x) : 0);
}

int TextLayout::lineBelow(int pos) const {
    const int i = indexOf(pos);
    if (i + 1 >= int(m_lines.size()))
        return m_content.length();
    const Line& l = lineOf(pos);
    const QTextLine tl = l.layout->lineAt(0);
    const double x = tl.isValid() ? tl.cursorToX(pos - l.start) : 0.0;
    const Line& down = m_lines[std::size_t(i + 1)];
    const QTextLine dt = down.layout->lineAt(0);
    return down.start + (dt.isValid() ? dt.xToCursor(x) : 0);
}

QRect TextLayout::render(TiledImage& target) const {
    target.clear();
    if (m_content.isEmpty())
        return {};
    const QRect area = frame().toAlignedRect().adjusted(-4, -4, 4, 4) & target.paintArea();
    if (area.isEmpty())
        return {};
    QImage img(area.size(), QImage::Format_RGBA8888_Premultiplied);
    img.fill(Qt::transparent);
    {
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::TextAntialiasing);
        p.translate(-area.topLeft());
        p.setTransform(m_toCanvas, true);
        for (const Line& l : m_lines)
            l.layout->draw(&p, QPointF(0, l.top));
    }
    target.writeImage(img, area.topLeft());
    return area;
}

}
