#include "ui/tools/TextTool.h"

#include "core/document/Document.h"
#include "core/text/TextLayout.h"
#include "core/tools/ToolBox.h"

#include <QClipboard>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QPainter>

#include <algorithm>

namespace doodlebold {

namespace {

constexpr int kMergeBase = 0x54580000;  // "TX": text edits merge with each other only
constexpr double kFramePad = 4.0;

QString firstFontFamily() {
    for (const QString& family : QFontDatabase::families())
        if (!QFontDatabase::isPrivateFamily(family))
            return family;
    return {};
}

}

TextTool::TextTool(ToolHost& host) : CanvasTool(host) {
    ToolBox* tools = host.toolBox();
    m_typing = styleFromTools();
    m_connections.push_back(QObject::connect(tools, &ToolBox::fixedOptionsChanged, [this] { onOptionsChanged(); }));
    m_connections.push_back(QObject::connect(tools, &ToolBox::colorChanged, [this] { onColourChanged(); }));
}

TextTool::~TextTool() {
    for (const QMetaObject::Connection& c : m_connections)
        QObject::disconnect(c);
}

const TextContent* TextTool::content() const {
    const Document* doc = const_cast<TextTool*>(this)->host().toolDocument();
    const LayerNode* n = doc && m_layer.isValid() ? doc->layer(m_layer) : nullptr;
    return n && n->text ? &*n->text : nullptr;
}

TextStyle TextTool::styleFromTools() const {
    ToolBox* tools = host().toolBox();
    const TextToolOptions& o = tools->fixedOptions().text;
    TextStyle s;
    s.font = o.font.isEmpty() ? firstFontFamily() : o.font;
    s.sizePx = o.sizePx;
    s.bold = o.bold;
    s.italic = o.italic;
    s.colour = tools->color();
    return s;
}

LayerId TextTool::textLayerAt(const QPointF& doc) const {
    const Document* d = const_cast<TextTool*>(this)->host().toolDocument();
    if (!d)
        return {};
    const auto hits = [&](const LayerNode* n) {
        return n && n->text && n->props.visible &&
               TextLayout(*n->text).frame().adjusted(-kFramePad, -kFramePad, kFramePad, kFramePad).contains(doc);
    };
    if (hits(d->currentLayer()))
        return d->currentLayerId();
    for (const LayerNode* n : d->layers().displayOrder())
        if (hits(n))
            return n->props.id;
    return {};
}

void TextTool::press(const ToolPointer& p) {
    Document* doc = host().toolDocument();
    if (!doc)
        return;
    ++m_mergeKey;
    const bool extend = (p.modifiers & Qt::ShiftModifier) && content();
    if (const LayerId hit = extend ? m_layer : textLayerAt(p.doc); hit.isValid()) {
        if (hit != doc->currentLayerId())
            doc->setCurrentLayer(hit);
        m_layer = hit;
        setCaret(TextLayout(*content()).positionAt(p.doc), extend);
        m_dragging = true;
    } else {
        TextContent c;
        c.origin = p.doc;
        c.vertical = host().toolBox()->fixedOptions().text.vertical;
        c.fitToPixelGrid = host().toolBox()->fixedOptions().text.fitToPixelGrid;
        c.emptyStyle = styleFromTools();
        const auto added = doc->addTextLayer(c);
        if (!added) {
            host().toolMessage(describe(added.error()));
            return;
        }
        m_layer = added.value();
        m_caret = m_anchor = 0;
        m_typing = c.emptyStyle;
    }
    host().toolChanged();
}

void TextTool::move(const ToolPointer& p) {
    if (!m_dragging || !content())
        return;
    setCaret(TextLayout(*content()).positionAt(p.doc), true);
    host().toolChanged();
}

void TextTool::release(const ToolPointer&) {
    if (!m_dragging)
        return;
    m_dragging = false;
    showStyleInPanel();
}

void TextTool::setCaret(int pos, bool extend) {
    const TextContent* c = content();
    const int length = c ? c->length() : 0;
    m_caret = std::clamp(pos, 0, length);
    if (!extend)
        m_anchor = m_caret;
    m_anchor = std::clamp(m_anchor, 0, length);
}

bool TextTool::wantsKey(const QKeyEvent* e) const {
    if (!content())
        return false;
    const Qt::KeyboardModifiers mods = e->modifiers() & ~(Qt::ShiftModifier | Qt::KeypadModifier);
    if (mods == Qt::ControlModifier)
        return e->key() == Qt::Key_A || e->key() == Qt::Key_C || e->key() == Qt::Key_X || e->key() == Qt::Key_V;
    if (mods != Qt::NoModifier)
        return false;
    switch (e->key()) {
    case Qt::Key_Backspace:
    case Qt::Key_Delete:
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Escape:
    case Qt::Key_Left:
    case Qt::Key_Right:
    case Qt::Key_Up:
    case Qt::Key_Down:
    case Qt::Key_Home:
    case Qt::Key_End: return true;
    default: break;
    }
    return !e->text().isEmpty() && e->text().front().isPrint();
}

bool TextTool::keyPress(QKeyEvent* e) {
    const TextContent* c = content();
    if (!c || !wantsKey(e))
        return false;
    const bool shift = e->modifiers() & Qt::ShiftModifier;
    const int from = std::min(m_caret, m_anchor), to = std::max(m_caret, m_anchor);
    if (e->modifiers() & Qt::ControlModifier) {
        QClipboard* clip = QGuiApplication::clipboard();
        switch (e->key()) {
        case Qt::Key_A: m_anchor = 0, m_caret = c->length(); break;  // Select All on a text layer selects its text
        case Qt::Key_C:
            if (from < to)
                clip->setText(c->plainText().mid(from, to - from));
            break;
        case Qt::Key_X:
            if (from < to) {
                clip->setText(c->plainText().mid(from, to - from));
                ++m_mergeKey;
                replaceSelection(QString());
            }
            break;
        case Qt::Key_V: {
            QString text = clip->text();
            text.remove(QLatin1Char('\r'));
            ++m_mergeKey;
            replaceSelection(text);
            break;
        }
        default: break;
        }
        host().toolChanged();
        return true;
    }
    const TextLayout layout(*c);
    switch (e->key()) {
    case Qt::Key_Escape: m_layer = {}; break;
    case Qt::Key_Return:
    case Qt::Key_Enter: replaceSelection(QStringLiteral("\n")); break;
    case Qt::Key_Backspace:
        if (from < to) {
            replaceSelection(QString());
        } else if (m_caret > 0) {
            TextContent edited = *c;
            edited.remove(m_caret - 1, m_caret);
            m_caret = m_anchor = m_caret - 1;
            commit(edited, false);
        }
        break;
    case Qt::Key_Delete:
        if (from < to) {
            replaceSelection(QString());
        } else if (m_caret < c->length()) {
            TextContent edited = *c;
            edited.remove(m_caret, m_caret + 1);
            commit(edited, false);
        }
        break;
    case Qt::Key_Left:
        ++m_mergeKey;
        setCaret(!shift && from < to ? from : m_caret - 1, shift);
        showStyleInPanel();
        break;
    case Qt::Key_Right:
        ++m_mergeKey;
        setCaret(!shift && from < to ? to : m_caret + 1, shift);
        showStyleInPanel();
        break;
    case Qt::Key_Up: ++m_mergeKey, setCaret(layout.lineAbove(m_caret), shift), showStyleInPanel(); break;
    case Qt::Key_Down: ++m_mergeKey, setCaret(layout.lineBelow(m_caret), shift), showStyleInPanel(); break;
    case Qt::Key_Home: ++m_mergeKey, setCaret(layout.lineStart(m_caret), shift), showStyleInPanel(); break;
    case Qt::Key_End: ++m_mergeKey, setCaret(layout.lineEnd(m_caret), shift), showStyleInPanel(); break;
    default: replaceSelection(e->text()); break;
    }
    host().toolChanged();
    return true;
}

void TextTool::replaceSelection(const QString& text) {
    const TextContent* c = content();
    if (!c)
        return;
    const int from = std::min(m_caret, m_anchor), to = std::max(m_caret, m_anchor);
    TextContent edited = *c;
    edited.remove(from, to);
    edited.insert(from, text, m_typing);
    m_caret = m_anchor = from + int(text.size());
    commit(edited, false);
}

void TextTool::commit(const TextContent& edited, bool newStep) {
    Document* doc = host().toolDocument();
    if (!doc || !m_layer.isValid())
        return;
    if (newStep)
        ++m_mergeKey;
    if (const auto done = doc->setText(m_layer, edited, kMergeBase + (m_mergeKey & 0xFFFF)); !done && done.error() != EditError::NoChange)
        host().toolMessage(describe(done.error()));
    setCaret(m_caret, true);
    host().toolChanged();
}

void TextTool::showStyleInPanel() {
    const TextContent* c = content();
    if (!c)
        return;
    const int from = std::min(m_caret, m_anchor), to = std::max(m_caret, m_anchor);
    m_typing = from < to ? c->styleOf(from, to) : c->styleAt(m_caret);
    ToolBox* tools = host().toolBox();
    FixedToolOptions o = tools->fixedOptions();
    o.text.font = m_typing.font;
    o.text.sizePx = m_typing.sizePx;
    o.text.bold = m_typing.bold;
    o.text.italic = m_typing.italic;
    o.text.vertical = c->vertical;
    o.text.fitToPixelGrid = c->fitToPixelGrid;
    m_syncingPanel = true;
    tools->setFixedOptions(o);
    m_syncingPanel = false;
}

void TextTool::onOptionsChanged() {
    if (m_syncingPanel)
        return;
    const TextToolOptions& o = host().toolBox()->fixedOptions().text;
    const QColor colour = m_typing.colour;
    m_typing = styleFromTools();
    m_typing.colour = colour;
    const TextContent* c = content();
    if (!c)
        return;
    TextContent edited = *c;
    edited.vertical = o.vertical;
    edited.fitToPixelGrid = o.fitToPixelGrid;
    const int from = std::min(m_caret, m_anchor), to = std::max(m_caret, m_anchor);
    const TextStyle typed = m_typing;
    edited.restyle(from, to, [&typed](TextStyle& s) {
        s.font = typed.font;
        s.sizePx = typed.sizePx;
        s.bold = typed.bold;
        s.italic = typed.italic;
    });
    if (c->isEmpty())
        edited.emptyStyle = m_typing;
    commit(edited, true);
}

void TextTool::onColourChanged() {
    m_typing.colour = host().toolBox()->color();
    const TextContent* c = content();
    const int from = std::min(m_caret, m_anchor), to = std::max(m_caret, m_anchor);
    if (!c || from == to)
        return;
    TextContent edited = *c;
    const QColor colour = m_typing.colour;
    edited.restyle(from, to, [colour](TextStyle& s) { s.colour = colour; });
    commit(edited, true);
}

void TextTool::paintOverlay(QPainter& p, const QTransform& docToWidget) {
    Document* doc = host().toolDocument();
    if (!doc)
        return;
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    for (const LayerNode* n : doc->layers().displayOrder()) {
        if (!n->text || !n->props.visible)
            continue;
        const TextLayout layout(*n->text);
        const QRectF frame = layout.frame().adjusted(-kFramePad, -kFramePad, kFramePad, kFramePad);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(128, 128, 128), 1.0));
        p.drawPolygon(docToWidget.map(QPolygonF(frame)));
        if (n->props.id != m_layer)
            continue;
        const int from = std::min(m_caret, m_anchor), to = std::max(m_caret, m_anchor);
        p.setPen(QPen(QColor(64, 96, 255), 1.0));
        p.setBrush(QColor(64, 96, 255, 50));
        for (const QPolygonF& box : layout.selection(from, to))
            p.drawPolygon(docToWidget.map(box));
        const QLineF caret = docToWidget.map(layout.caret(m_caret));
        p.setPen(QPen(Qt::white, 3.0));
        p.drawLine(caret);
        p.setPen(QPen(Qt::black, 1.5));
        p.drawLine(caret);
    }
    p.restore();
}

}
