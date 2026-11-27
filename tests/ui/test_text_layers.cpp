#include "ScreenshotSupport.h"
#include "core/document/TransformSession.h"
#include "core/document/Document.h"
#include "core/text/TextLayout.h"
#include "core/tools/ToolBox.h"
#include "ui/CanvasView.h"
#include "ui/tools/TextTool.h"

#include <QApplication>
#include <QFontDatabase>
#include <QKeyEvent>
#include <QPainter>

#include <gtest/gtest.h>

using namespace doodlebold;

namespace {

TextStyle style(double size = 20.0, bool bold = false, QColor colour = Qt::black) {
    TextStyle s;
    s.font = QFontDatabase::systemFont(QFontDatabase::GeneralFont).family();
    s.sizePx = size;
    s.bold = bold;
    s.colour = colour;
    return s;
}

TextContent text(const QString& s, QPointF origin = {40, 30}, const TextStyle& st = style()) {
    TextContent t;
    t.origin = origin;
    t.emptyStyle = st;
    t.insert(0, s, st);
    return t;
}

QRect inkBounds(const TiledImage& img) {
    return img.contentBounds(true);
}

class Host : public ToolHost {
public:
    explicit Host(Document* doc) : m_doc(doc) {}
    Document* toolDocument() override { return m_doc; }
    ToolBox* toolBox() override { return &tools; }
    double docPerPixel() const override { return 1.0; }
    void toolChanged() override { ++changes; }
    void toolMessage(const QString& t) override { messages << t; }
    TransformSession* session() override { return nullptr; }
    bool startMoveSession() override { return false; }
    void finishSession(bool) override {}
    bool isCurrentCanvas() const override { return current; }

    ToolBox tools;
    bool current = true;
    int changes = 0;
    QStringList messages;

private:
    Document* m_doc;
};

ToolPointer at(QPointF doc, Qt::KeyboardModifiers mods = Qt::NoModifier) {
    ToolPointer p;
    p.doc = doc;
    p.widget = doc;
    p.modifiers = mods;
    return p;
}

void type(TextTool& tool, const QString& s) {
    for (QChar c : s) {
        QKeyEvent e(QEvent::KeyPress, c == QLatin1Char('\n') ? Qt::Key_Return : 0, Qt::NoModifier,
                    c == QLatin1Char('\n') ? QString() : QString(c));
        EXPECT_TRUE(tool.wantsKey(&e));
        EXPECT_TRUE(tool.keyPress(&e));
    }
}

void key(TextTool& tool, int k, Qt::KeyboardModifiers mods = Qt::NoModifier) {
    QKeyEvent e(QEvent::KeyPress, k, mods);
    tool.keyPress(&e);
}

}

TEST(TextLayout, TheFirstLineStartsAtTheOriginAndGrowsWithTheText) {
    const TextLayout one(text(QStringLiteral("Hi")));
    const TextLayout more(text(QStringLiteral("Hi there")));
    EXPECT_NEAR(one.frame().left(), 40, 0.5);
    EXPECT_NEAR(one.frame().top(), 30, 0.5);
    EXPECT_GT(more.frame().width(), one.frame().width());
    const TextLayout two(text(QStringLiteral("Hi\nthere")));
    EXPECT_GT(two.frame().height(), one.frame().height() * 1.8) << "a second line";
}

TEST(TextLayout, CaretAndHitTestingAgree) {
    const TextLayout l(text(QStringLiteral("Hello\nworld")));
    for (int pos : {0, 3, 5, 6, 9, 11}) {
        const QLineF c = l.caret(pos);
        EXPECT_EQ(l.positionAt(c.center() + QPointF(0.2, 0)), pos) << pos;
    }
    EXPECT_LT(l.caret(1).x1(), l.caret(4).x1());
    EXPECT_GT(l.caret(7).y1(), l.caret(1).y1()) << "the second line is lower";
    EXPECT_EQ(l.lineStart(8), 6);
    EXPECT_EQ(l.lineEnd(2), 5);
    EXPECT_EQ(l.lineBelow(2), 8);
    EXPECT_EQ(l.lineAbove(8), 2);
}

TEST(TextLayout, VerticalTurnsLinesIntoColumnsFromTheRight) {
    TextContent t = text(QStringLiteral("Hello\nworld"));
    t.vertical = true;
    const TextLayout l(t);
    EXPECT_NEAR(l.frame().right(), 40, 0.5) << "the first column's right edge is at the origin";
    EXPECT_NEAR(l.frame().top(), 30, 0.5);
    const QRectF across = TextLayout(text(QStringLiteral("Hello\nworld"))).frame();
    EXPECT_NEAR(l.frame().width(), across.height(), 0.5) << "the horizontal frame, turned";
    EXPECT_NEAR(l.frame().height(), across.width(), 0.5);
    EXPECT_LT(l.caret(7).x1(), l.caret(1).x1()) << "the second line is the column to the left";
    EXPECT_GT(l.caret(4).y1(), l.caret(1).y1());
}

TEST(TextLayout, DrawsAntialiasedTextInItsColours) {
    // An outline font: a headless platform's system font can be a bitmap font, which has no partial pixels.
    for (const char* weight : {"Regular", "Bold"})
        QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/Inter-%1.ttf").arg(QLatin1String(weight)));
    ASSERT_TRUE(QFontDatabase::hasFamily(QStringLiteral("Inter")));
    TextStyle redStyle = style(40, false, Qt::red), blueStyle = style(40, true, Qt::blue);
    redStyle.font = blueStyle.font = QStringLiteral("Inter");
    TiledImage img(QSize(300, 200));
    TextContent t = text(QStringLiteral("Ab"), {20, 20}, redStyle);
    t.insert(2, QStringLiteral("cd"), blueStyle);
    const QRect area = TextLayout(t).render(img);
    EXPECT_FALSE(area.isEmpty());
    const QRect ink = inkBounds(img);
    EXPECT_GE(ink.left(), 18);
    EXPECT_GE(ink.top(), 18);
    int red = 0, blue = 0, partial = 0;
    for (int y = ink.top(); y <= ink.bottom(); ++y)
        for (int x = ink.left(); x <= ink.right(); ++x) {
            const auto p = img.pixel(x, y);
            if (p[3] == 0)
                continue;
            red += p[0] > p[2];
            blue += p[2] > p[0];
            partial += p[3] > 0 && p[3] < kChannelOne;
        }
    EXPECT_GT(red, 50);
    EXPECT_GT(blue, 50);
    EXPECT_GT(partial, 20) << "antialiased edges";
}

TEST(TextLayers, AddEditUndoAndName) {
    Document doc(QSize(300, 200), Qt::white);
    const auto id = doc.addTextLayer(text(QString()));
    ASSERT_TRUE(id);
    const LayerNode* n = doc.layer(id.value());
    ASSERT_TRUE(n && n->isText());
    EXPECT_EQ(n->props.name, "Text1");
    EXPECT_EQ(doc.currentLayerId(), id.value());
    EXPECT_TRUE(inkBounds(n->pixels).isEmpty());
    ASSERT_TRUE(doc.setText(id.value(), text(QStringLiteral("H")), 7));
    ASSERT_TRUE(doc.setText(id.value(), text(QStringLiteral("Hi")), 7));
    EXPECT_FALSE(inkBounds(doc.layer(id.value())->pixels).isEmpty()) << "drawn";
    const int steps = doc.undoStack()->count();
    ASSERT_TRUE(doc.undo());
    EXPECT_TRUE(doc.layer(id.value())->text->isEmpty()) << "both keystrokes are one step";
    ASSERT_TRUE(doc.redo());
    EXPECT_EQ(doc.layer(id.value())->text->plainText(), "Hi");
    EXPECT_EQ(doc.undoStack()->count(), steps);
}

TEST(TextLayers, PixelEditsAreRefusedAndMovingMovesTheText) {
    Document doc(QSize(300, 200), Qt::white);
    const LayerId id = doc.addTextLayer(text(QStringLiteral("Hi"))).value();
    EXPECT_EQ(doc.paintTarget(id).error(), EditError::NotForThisLayer);
    EXPECT_EQ(doc.fillLayer(id, Qt::red).error(), EditError::NotForThisLayer);
    EXPECT_EQ(doc.clearLayer(id).error(), EditError::NotForThisLayer);
    EXPECT_EQ(doc.cut(id).error(), EditError::NotForThisLayer);
    EXPECT_EQ(doc.moveLayerExactly(id, transform::Turn::Clockwise).error(), EditError::NotForThisLayer);
    const QRect before = inkBounds(doc.layer(id)->pixels);
    ASSERT_TRUE(doc.moveLayerExactly(id, transform::Turn::None, QPoint(10, 5)));
    EXPECT_EQ(doc.layer(id)->text->origin, QPointF(50, 35));
    EXPECT_EQ(inkBounds(doc.layer(id)->pixels), before.translated(10, 5));
    const QRect box = doc.transformBounds(id);
    EXPECT_TRUE(doc.transformLayer(id, transform::Quad::of(QRectF(box).translated(3, 0)), transform::Interpolation::Bilinear));
    EXPECT_EQ(doc.layer(id)->text->origin, QPointF(53, 35));
    EXPECT_EQ(doc.transformLayer(id, transform::Quad::of(QRectF(box).adjusted(0, 0, 20, 0)), transform::Interpolation::Bilinear).error(),
              EditError::NotForThisLayer);
}

TEST(TextLayers, TextPastTheCanvasShowsWhileItIsDraggedBack) {
    Document doc(QSize(300, 200), Qt::white);
    const LayerId id = doc.addTextLayer(text(QStringLiteral("Hi"), QPointF(-200, 30))).value();
    const QRect ink = inkBounds(doc.layer(id)->pixels);
    ASSERT_FALSE(ink.isEmpty()) << "its pixels exist off the canvas";
    EXPECT_LT(ink.right(), 0);
    EXPECT_EQ(doc.projection().pixelColor(5, 40), QColor(Qt::white));

    auto session = TransformSession::beginMove(doc, id, MarqueeOptions{});
    ASSERT_TRUE(session.ok());
    auto& move = *session.value();
    EXPECT_EQ(move.sourceBounds(), ink);
    move.setGpuPreview(true);
    const QImage& source = move.floatingPreview().source;
    int strongest = 0;
    for (int y = 0; y < source.height(); ++y)
        for (int x = 0; x < source.width(); ++x)
            strongest = std::max(strongest, source.pixelColor(x, y).alpha());
    EXPECT_GT(strongest, 0) << "the picture the drag draws has the text in it";

    move.setGpuPreview(false);
    const QPointF start = ink.center();
    move.beginDrag(TransformSession::Handle::Inside, start);
    move.dragTo(start + QPointF(250, 0), Qt::NoModifier);
    const QImage shown = move.image();
    bool changed = false;
    for (int y = ink.top(); y <= ink.bottom(); ++y)
        for (int x = ink.left() + 250; x <= ink.right() + 250; ++x)
            changed = changed || (QRect(QPoint(0, 0), shown.size()).contains(x, y) && shown.pixelColor(x, y) != QColor(Qt::white));
    EXPECT_TRUE(changed) << "the text shows during the drag";
    move.endDrag();
    ASSERT_TRUE(move.commit().ok());
    EXPECT_EQ(doc.layer(id)->text->origin, QPointF(50, 30));
}

TEST(TextLayers, RasterizeKeepsThePixels) {
    Document doc(QSize(300, 200), Qt::white);
    const LayerId id = doc.addTextLayer(text(QStringLiteral("Hi"))).value();
    const QRect ink = inkBounds(doc.layer(id)->pixels);
    ASSERT_TRUE(doc.rasterizeLayer(id));
    EXPECT_FALSE(doc.layer(id)->isText());
    EXPECT_FALSE(doc.layer(id)->text);
    EXPECT_EQ(inkBounds(doc.layer(id)->pixels), ink);
    EXPECT_TRUE(doc.fillLayer(id, Qt::red)) << "an ordinary layer now";
}

TEST(TextTool, ClickTypeAndEdit) {
    Document doc(QSize(400, 300), Qt::white);
    Host host(&doc);
    TextTool tool(host);
    tool.press(at({50, 60}));
    tool.release(at({50, 60}));
    const LayerId id = tool.editing();
    ASSERT_TRUE(id.isValid());
    EXPECT_TRUE(doc.layer(id)->isText());
    type(tool, QStringLiteral("Hello"));
    EXPECT_EQ(doc.layer(id)->text->plainText(), "Hello");
    key(tool, Qt::Key_Backspace);
    type(tool, QStringLiteral("p\nInk"));
    EXPECT_EQ(doc.layer(id)->text->plainText(), "Hellp\nInk");
    key(tool, Qt::Key_Home);
    key(tool, Qt::Key_End, Qt::ShiftModifier);
    EXPECT_EQ(tool.anchor(), 6);
    EXPECT_EQ(tool.caret(), 9);
    FixedToolOptions o = host.tools.fixedOptions();
    o.text.bold = true;
    host.tools.setFixedOptions(o);
    const TextContent& t = *doc.layer(id)->text;
    EXPECT_FALSE(t.styleAt(3).bold);
    EXPECT_TRUE(t.styleAt(8).bold);
    host.tools.setColor(Qt::red);
    EXPECT_EQ(doc.layer(id)->text->styleAt(8).colour, QColor(Qt::red));
    EXPECT_EQ(doc.layer(id)->text->styleAt(3).colour, QColor(Qt::black));
    key(tool, Qt::Key_A, Qt::ControlModifier);
    EXPECT_EQ(tool.anchor(), 0);
    EXPECT_EQ(tool.caret(), 9);
}

// Every open canvas's Text tool hears the one ToolBox. Picking a colour or a style for another canvas, or clicking one of its text
// layers (which shows that layer's style in the panel), restyled the text still selected here, as an undo step out of sight.
TEST(TextTool, OnlyTheCurrentCanvasTakesTheToolBoxsChanges) {
    Document doc(QSize(400, 300), Qt::white);
    Host host(&doc);
    TextTool tool(host);
    tool.press(at({50, 60}));
    tool.release(at({50, 60}));
    const LayerId id = tool.editing();
    type(tool, QStringLiteral("Hello"));
    key(tool, Qt::Key_A, Qt::ControlModifier);
    const int steps = doc.undoStack()->count();
    host.current = false;
    host.tools.setColor(Qt::red);
    FixedToolOptions o = host.tools.fixedOptions();
    o.text.bold = true;
    o.text.vertical = true;
    host.tools.setFixedOptions(o);
    const TextContent& t = *doc.layer(id)->text;
    EXPECT_EQ(t.styleAt(2).colour, QColor(Qt::black));
    EXPECT_FALSE(t.styleAt(2).bold);
    EXPECT_FALSE(t.vertical);
    EXPECT_EQ(doc.undoStack()->count(), steps);
}

TEST(TextTool, ClickIntoAFrameEditsThatLayerAndOutsideStartsAnother) {
    Document doc(QSize(400, 300), Qt::white);
    Host host(&doc);
    TextTool tool(host);
    const LayerId first = doc.addTextLayer(text(QStringLiteral("Hello"), {50, 50})).value();
    doc.setCurrentLayer(doc.layers().root().children.front().props.id);
    const QLineF afterE = TextLayout(*doc.layer(first)->text).caret(2);
    tool.press(at(afterE.center()));
    tool.release(at(afterE.center()));
    EXPECT_EQ(tool.editing(), first);
    EXPECT_EQ(doc.currentLayerId(), first);
    EXPECT_EQ(tool.caret(), 2);
    type(tool, QStringLiteral("!"));
    EXPECT_EQ(doc.layer(first)->text->plainText(), "He!llo");
    tool.press(at({300, 250}));
    EXPECT_NE(tool.editing(), first);
    EXPECT_EQ(doc.layer(tool.editing())->props.name, "Text2");
}

TEST(TextTool, TypingKeysAreTheToolsNotShortcuts) {
    Document doc(QSize(400, 300), Qt::white);
    Host host(&doc);
    TextTool tool(host);
    QKeyEvent e(QEvent::KeyPress, Qt::Key_E, Qt::NoModifier, QStringLiteral("e"));
    EXPECT_FALSE(tool.wantsKey(&e)) << "not editing: E is the Eraser";
    tool.press(at({10, 10}));
    EXPECT_TRUE(tool.wantsKey(&e));
    QKeyEvent space(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier, QStringLiteral(" "));
    EXPECT_TRUE(tool.wantsKey(&space)) << "a space, not the pan key";
    QKeyEvent undo(QEvent::KeyPress, Qt::Key_Z, Qt::ControlModifier);
    EXPECT_FALSE(tool.wantsKey(&undo)) << "Undo stays the window's";
    key(tool, Qt::Key_Escape);
    EXPECT_FALSE(tool.editing().isValid());
}

TEST(TextTool, Screenshot) {
    Document doc(QSize(320, 160), Qt::white);
    Host host(&doc);
    TextTool tool(host);
    TextContent t = text(QStringLiteral("Hello Ink\n"), {20, 20}, style(24));
    t.insert(t.length(), QStringLiteral("second line"), style(36, true, QColor(200, 40, 40)));
    ASSERT_TRUE(doc.addTextLayer(t));
    QImage shot = doc.projection().copy();
    {
        QPainter p(&shot);
        tool.paintOverlay(p, QTransform());
    }
    EXPECT_TRUE(test::saveScreenshot(QPixmap::fromImage(shot), QStringLiteral("text_layer")));
}
