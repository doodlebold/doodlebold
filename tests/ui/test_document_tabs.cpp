#include "CrashStack.h"
#include "core/document/Document.h"
#include "core/input/TabletStats.h"
#include "ui/CanvasView.h"
#include "ui/DocumentTabs.h"
#include "ui/DoodleboldStyle.h"
#include "ui/ViewSelector.h"

#include <QAccessible>
#include <QApplication>
#include <QMouseEvent>
#include <QSettings>
#include <QTemporaryDir>
#include <QLayout>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QUndoCommand>

#include <optional>
#include <vector>

#include <gtest/gtest.h>

using namespace doodlebold;

namespace {

class TestTabs : public DocumentTabs {
public:
    using DocumentTabs::DocumentTabs;
    bool answer = false;
    std::optional<UnsavedChoice> choice;
    int asked = 0;

protected:
    UnsavedChoice askUnsaved(const Document*) override {
        ++asked;
        return choice ? *choice : (answer ? UnsavedChoice::Discard : UnsavedChoice::Cancel);
    }
};

Document* newDoc() {
    return new Document(QSize(32, 32), Qt::white);
}

void makeModified(Document* doc) {
    doc->undoStack()->push(new QUndoCommand(QStringLiteral("edit")));
}

QString title(const DocumentTabs& tabs, int i) {
    return tabs.windowAt(i)->windowTitle();
}

}

TEST(DocumentTabs, WindowTitlesAndTabsFollowTheDocument) {
    TestTabs tabs(nullptr);
    Document* a = newDoc();
    Document* b = newDoc();
    Document* c = newDoc();
    tabs.addDocument(a);
    tabs.addDocument(b);
    tabs.addDocument(c);
    ASSERT_EQ(tabs.count(), 3);
    EXPECT_EQ(tabs.currentDocument(), c);
    const ViewSelector& sel = *tabs.viewSelector();
    ASSERT_EQ(sel.count(), 3);
    EXPECT_EQ(sel.current(), 2);
    EXPECT_EQ(sel.item(1).name, b->displayName());
    EXPECT_EQ(sel.item(1).zoom, QStringLiteral("100%"));
    EXPECT_EQ(sel.item(1).toolTip, b->displayName());
    EXPECT_EQ(title(tabs, 1), b->displayName() + QStringLiteral(" (100%) (32 × 32 px / 72 dpi)"));

    makeModified(b);
    EXPECT_EQ(title(tabs, 1), b->displayName() + QStringLiteral(" (*) (100%) (32 × 32 px / 72 dpi)"));
    EXPECT_EQ(sel.item(1).name, b->displayName());
    EXPECT_EQ(title(tabs, 0), a->displayName() + QStringLiteral(" (100%) (32 × 32 px / 72 dpi)"));
    b->undoStack()->undo();
    EXPECT_EQ(title(tabs, 1), b->displayName() + QStringLiteral(" (100%) (32 × 32 px / 72 dpi)"));

    b->setFilePath(QStringLiteral("/tmp/some/dir/painting.doodle"));
    EXPECT_EQ(sel.item(1).name, QStringLiteral("painting.doodle"));
    EXPECT_EQ(sel.item(1).toolTip, QStringLiteral("/tmp/some/dir/painting.doodle"));

    tabs.canvasAt(1)->setZoom(0.25, QPointF(16, 16));
    EXPECT_EQ(title(tabs, 1), QStringLiteral("painting.doodle (25%) (32 × 32 px / 72 dpi)"));
    EXPECT_EQ(sel.item(1).zoom, QStringLiteral("25%"));
}

// The canvas's size and resolution in another set of brackets, kept up to date when the canvas's size changes.
TEST(DocumentTabs, AWindowTitleShowsTheCanvasSizeAndResolution) {
    TestTabs tabs(nullptr);
    auto* doc = new Document(QSize(40, 30), CanvasBackground::fromPaper(Qt::white), 350.0);
    tabs.addDocument(doc);
    EXPECT_EQ(title(tabs, 0), doc->displayName() + QStringLiteral(" (100%) (40 × 30 px / 350 dpi)"));
    ASSERT_TRUE(doc->rotateCanvas(true).ok());
    EXPECT_EQ(title(tabs, 0), doc->displayName() + QStringLiteral(" (*) (100%) (30 × 40 px / 350 dpi)")) << "rotating it is a change";
}

TEST(DocumentTabs, WindowsOpenMaximizedThenCascade) {
    TestTabs tabs(nullptr);
    tabs.resize(1200, 800);
    tabs.layout()->activate();  // laid out without being shown: the offscreen platform cannot draw the canvases
    tabs.addDocument(newDoc());
    EXPECT_TRUE(tabs.windowAt(0)->isMaximized());
    tabs.windowAt(0)->showNormal();
    QCoreApplication::processEvents();
    tabs.addDocument(newDoc());
    tabs.addDocument(newDoc());
    EXPECT_FALSE(tabs.windowAt(1)->isMaximized());
    EXPECT_FALSE(tabs.windowAt(2)->isMaximized());
    EXPECT_EQ(tabs.windowAt(2)->pos() - tabs.windowAt(1)->pos(), QPoint(DocumentTabs::kCascadeStep, DocumentTabs::kCascadeStep));
    tabs.windowAt(2)->showMaximized();
    QCoreApplication::processEvents();
    tabs.addDocument(newDoc());
    EXPECT_TRUE(tabs.windowAt(3)->isMaximized());
}

TEST(DocumentTabs, TabsWrapIntoRowsAndScroll) {
    TestTabs tabs(nullptr);
    tabs.resize(ViewSelector::kTabWidth * 6 + ViewSelector::kArrowWidth, 600);
    tabs.layout()->activate();
    for (int i = 0; i < 14; ++i)
        tabs.addDocument(newDoc());
    const ViewSelector& sel = *tabs.viewSelector();
    EXPECT_EQ(sel.tabsPerRow(), 6);
    EXPECT_EQ(sel.rowCount(), 3);
    EXPECT_EQ(sel.sizeHint().height(), 3 * ViewSelector::kRowHeight + 2);
    EXPECT_EQ(sel.itemAt(sel.itemRect(7).center()), 7);
    for (int i = 0; i < 10; ++i)
        tabs.addDocument(newDoc());
    EXPECT_EQ(sel.rowCount(), 4);
    EXPECT_EQ(sel.visibleRows(), ViewSelector::kMaxRows);
    EXPECT_FALSE(sel.itemRect(23).isEmpty());
    EXPECT_TRUE(sel.itemRect(0).isEmpty());
    tabs.setCurrentIndex(0);
    EXPECT_FALSE(sel.itemRect(0).isEmpty());
}

TEST(DocumentTabs, WindowCloseButtonAsksForAModifiedCanvas) {
    TestTabs tabs(nullptr);
    Document* a = newDoc();
    tabs.addDocument(a);
    makeModified(a);
    tabs.windowAt(0)->close();
    QCoreApplication::sendPostedEvents();
    EXPECT_EQ(tabs.asked, 1);
    EXPECT_EQ(tabs.count(), 1);
    tabs.answer = true;
    tabs.windowAt(0)->close();
    QCoreApplication::sendPostedEvents();
    EXPECT_EQ(tabs.count(), 0);
}

// Qt's accessibility cache forgets a window when the window's destroyed() reaches it. If it never does, a later window made at the
// same address is handed the dead one's interface, and Qt 6.8's setWindowTitle crashes reading its title.
TEST(DocumentTabs, AClosedWindowLeavesNoAccessibleInterfaceBehind) {
    TestTabs tabs(nullptr);
    tabs.addDocument(newDoc());
    const QAccessible::Id id = QAccessible::uniqueId(QAccessible::queryAccessibleInterface(tabs.windowAt(0)));
    ASSERT_TRUE(tabs.requestCloseTab(0));
    EXPECT_EQ(QAccessible::accessibleInterface(id), nullptr);
}

TEST(DocumentTabs, TabsGoingAwayLeaveNoAccessibleInterfaceBehind) {
    QAccessible::Id id = 0;
    {
        TestTabs tabs(nullptr);
        tabs.addDocument(newDoc());
        id = QAccessible::uniqueId(QAccessible::queryAccessibleInterface(tabs.windowAt(0)));
    }
    EXPECT_EQ(QAccessible::accessibleInterface(id), nullptr);
}

TEST(DocumentTabs, CloseModifiedTabAsksAndHonoursAnswer) {
    TestTabs tabs(nullptr);
    Document* a = newDoc();
    Document* b = newDoc();
    tabs.addDocument(a);
    tabs.addDocument(b);
    makeModified(b);

    tabs.answer = false;
    EXPECT_FALSE(tabs.requestCloseTab(1));
    EXPECT_EQ(tabs.asked, 1);
    EXPECT_EQ(tabs.count(), 2);

    tabs.answer = true;
    EXPECT_TRUE(tabs.requestCloseTab(1));
    EXPECT_EQ(tabs.asked, 2);
    ASSERT_EQ(tabs.count(), 1);
    EXPECT_EQ(tabs.currentDocument(), a);

    EXPECT_TRUE(tabs.requestCloseTab(0));
    EXPECT_EQ(tabs.asked, 2);
    EXPECT_EQ(tabs.count(), 0);
}

TEST(DocumentTabs, SaveAnswerSavesThenClosesAndAFailedSaveKeepsTheTab) {
    TestTabs tabs(nullptr);
    Document* a = newDoc();
    tabs.addDocument(a);
    makeModified(a);
    std::vector<Document*> saved;
    bool succeed = false;
    tabs.setSaveHandler([&](Document* d) {
        saved.push_back(d);
        return succeed;
    });
    tabs.choice = DocumentTabs::UnsavedChoice::Save;

    EXPECT_FALSE(tabs.requestCloseTab(0));
    EXPECT_EQ(tabs.count(), 1);
    ASSERT_EQ(saved.size(), 1u);
    EXPECT_EQ(saved[0], a);

    succeed = true;
    EXPECT_TRUE(tabs.requestCloseTab(0));
    EXPECT_EQ(saved.size(), 2u);
    EXPECT_EQ(tabs.count(), 0);
}

TEST(DocumentTabs, SaveWithoutAHandlerIsRefused) {
    TestTabs tabs(nullptr);
    tabs.addDocument(newDoc());
    makeModified(tabs.currentDocument());
    tabs.choice = DocumentTabs::UnsavedChoice::Save;
    EXPECT_FALSE(tabs.requestCloseTab(0));
    EXPECT_EQ(tabs.count(), 1);
}

TEST(DocumentTabs, CloseAllStopsAtFirstRefusal) {
    TestTabs tabs(nullptr);
    Document* a = newDoc();
    Document* b = newDoc();
    Document* c = newDoc();
    tabs.addDocument(a);
    tabs.addDocument(b);
    tabs.addDocument(c);
    makeModified(b);

    tabs.answer = false;
    EXPECT_FALSE(tabs.requestCloseAll());
    ASSERT_EQ(tabs.count(), 2);
    EXPECT_EQ(tabs.canvasAt(0)->document(), b);
    EXPECT_EQ(tabs.canvasAt(1)->document(), c);

    tabs.answer = true;
    EXPECT_TRUE(tabs.requestCloseAll());
    EXPECT_EQ(tabs.count(), 0);
}

TEST(DocumentTabs, NextAndPreviousWrapAround) {
    TestTabs tabs(nullptr);
    for (int i = 0; i < 3; ++i)
        tabs.addDocument(newDoc());
    tabs.setCurrentIndex(0);
    tabs.selectPreviousTab();
    EXPECT_EQ(tabs.currentIndex(), 2);
    tabs.selectNextTab();
    EXPECT_EQ(tabs.currentIndex(), 0);
    tabs.selectNextTab();
    EXPECT_EQ(tabs.currentIndex(), 1);
}

TEST(DocumentTabs, CurrentDocumentChangedIncludingNullAtEnd) {
    TestTabs tabs(nullptr);
    std::vector<Document*> seen;
    QObject::connect(&tabs, &DocumentTabs::currentDocumentChanged, &tabs, [&seen](Document* d) { seen.push_back(d); });
    Document* a = newDoc();
    Document* b = newDoc();
    tabs.addDocument(a);
    tabs.addDocument(b);
    ASSERT_EQ(seen.size(), 2u);
    EXPECT_EQ(seen[0], a);
    EXPECT_EQ(seen[1], b);

    tabs.selectNextTab();
    ASSERT_EQ(seen.size(), 3u);
    EXPECT_EQ(seen[2], a);

    tabs.requestCloseTab(0);
    ASSERT_EQ(seen.size(), 4u);
    EXPECT_EQ(seen[3], b);

    tabs.requestCloseTab(0);
    ASSERT_EQ(seen.size(), 5u);
    EXPECT_EQ(seen[4], nullptr);
    EXPECT_EQ(tabs.currentDocument(), nullptr);
}

TEST(DocumentTabs, MiddleClickClosesTab) {
    TestTabs tabs(nullptr);
    tabs.resize(600, 300);
    tabs.layout()->activate();
    tabs.addDocument(newDoc());
    tabs.addDocument(newDoc());
    ViewSelector* sel = tabs.viewSelector();
    const QPoint pos = sel->itemRect(0).center();
    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(pos), QPointF(sel->mapToGlobal(pos)), Qt::MiddleButton, Qt::NoButton,
                        Qt::NoModifier);
    QCoreApplication::sendEvent(sel, &release);
    QCoreApplication::sendPostedEvents();
    EXPECT_EQ(tabs.count(), 1);
}

TEST(DocumentTabs, ClickingATabMakesItsCanvasCurrent) {
    TestTabs tabs(nullptr);
    tabs.resize(800, 400);
    tabs.layout()->activate();
    Document* a = newDoc();
    tabs.addDocument(a);
    tabs.addDocument(newDoc());
    ViewSelector* sel = tabs.viewSelector();
    const QPoint pos = sel->itemRect(0).center();
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(pos), QPointF(sel->mapToGlobal(pos)), Qt::LeftButton, Qt::LeftButton,
                      Qt::NoModifier);
    QCoreApplication::sendEvent(sel, &press);
    EXPECT_EQ(tabs.currentDocument(), a);
    EXPECT_EQ(sel->current(), 0);
}

TEST(DocumentTabs, ClickingAnotherCanvasMakesItCurrentWithoutPainting) {
    TabletStats stats;
    TestTabs tabs(&stats);
    tabs.resize(800, 600);
    tabs.layout()->activate();
    Document* a = newDoc();
    Document* b = newDoc();
    tabs.addDocument(a);
    tabs.addDocument(b);
    ASSERT_EQ(tabs.currentDocument(), b);
    CanvasView* view = tabs.canvasAt(0);
    view->resize(200, 200);
    const QPointF pos(100, 100);
    QMouseEvent press(QEvent::MouseButtonPress, pos, view->mapToGlobal(pos), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(view, &press);
    QMouseEvent release(QEvent::MouseButtonRelease, pos, view->mapToGlobal(pos), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(view, &release);
    EXPECT_EQ(tabs.currentDocument(), a);
    EXPECT_EQ(a->undoStack()->count(), 0) << "the activating click painted";

    QCoreApplication::sendEvent(view, &press);
    QCoreApplication::sendEvent(view, &release);
    EXPECT_EQ(a->undoStack()->count(), 1);
}

TEST(DocumentTabs, DocumentsKeepIndependentUndoAndView) {
    TestTabs tabs(nullptr);
    Document* a = newDoc();
    Document* b = newDoc();
    tabs.addDocument(a);
    tabs.addDocument(b);
    a->view().zoom = 3.0;
    makeModified(a);
    EXPECT_DOUBLE_EQ(b->view().zoom, 1.0);
    EXPECT_FALSE(b->isModified());
    EXPECT_NE(a->undoStack(), b->undoStack());
}

int main(int argc, char** argv) {
    printCrashStacks();
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    // Settings go to an ini file in a folder of their own: Qt's native format is the registry on Windows and the preferences
    // on macOS, which setPath() does not move, so the tests would share (and leave behind) the machine's settings.
    // (The code under test opens QSettings with QSettings::defaultFormat().)
    QTemporaryDir settingsDir;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

namespace {

// A shown area with one canvas window in its normal state, so moves and resizes reach the window as they do on screen.
struct LooseWindow {
    TestTabs tabs{nullptr};
    QMdiSubWindow* window = nullptr;

    LooseWindow() {
        tabs.resize(900, 700);
        tabs.show();
        tabs.addDocument(newDoc());
        window = tabs.windowAt(0);
        window->showNormal();
        window->setGeometry(100, 100, 400, 300);
        QCoreApplication::processEvents();
    }
    QSize room() const { return tabs.area()->viewport()->size(); }
    // The title bar as it shows, in the area's coordinates.
    QRect titleBar() const {
        const int margin = DoodleboldStyle::kFrameMargin;
        return QRect(window->pos() + QPoint(margin, margin),
                     QSize(window->width() - 2 * margin, DoodleboldStyle::kTitleBarHeight - margin));
    }
    // Up to half of a window may go behind the panels, "as long as we can see the canvas window controls":
    // the title bar's right end, with the buttons, is in, as is its height and at least half of its width.
    bool controlsShow() const {
        const QRect bar = titleBar();
        return bar.top() >= 0 && bar.bottom() < room().height() && bar.right() < room().width() && bar.left() >= -bar.width() / 2;
    }
};

}

// Feedback: "I can resize a canvas window very small, and move it behind the color / navigation panel, where i can no
// longer move or close the window."
TEST(DocumentTabs, ACanvasWindowKeepsItsTitleAndALittleCanvas) {
    LooseWindow w;
    w.window->resize(10, 10);
    QCoreApplication::processEvents();
    EXPECT_EQ(w.window->size(), DocumentTabs::minimumWindowSize());
    EXPECT_GE(w.window->widget()->height(), DocumentTabs::kMinimumCanvasHeight - 2) << "a little of the canvas shows";
    EXPECT_GE(w.window->width(), 2 * DoodleboldStyle::kFrameMargin + DocumentTabs::kMinimumTitleWidth + 3 * DoodleboldStyle::kButtonSize);
}

TEST(DocumentTabs, ACanvasWindowGoesHalfBehindThePanelsButKeepsItsButtonsIn) {
    LooseWindow w;
    w.window->move(-600, -600);
    QCoreApplication::processEvents();
    EXPECT_TRUE(w.controlsShow());
    EXPECT_EQ(w.titleBar().left(), -w.titleBar().width() / 2) << "half of it behind the panels on the left";
    EXPECT_EQ(w.titleBar().top(), 0);
    for (const QPoint to : {QPoint(5000, 40), QPoint(40, 5000), QPoint(5000, 5000)}) {
        w.window->move(to);
        QCoreApplication::processEvents();
        EXPECT_TRUE(w.controlsShow()) << to.x() << ", " << to.y();
    }
    EXPECT_EQ(w.titleBar().right(), w.room().width() - 1) << "the buttons stop at the right edge";
    w.window->move(w.room().width() - w.window->width(), w.room().height() - 60);
    QCoreApplication::processEvents();
    EXPECT_GT(w.window->geometry().bottom(), w.room().height()) << "only the title bar has to stay in: the rest may hang below";

    const auto dragBy = [&](QPoint by) {
        // Near the title's start: the timeline buttons and the pill sit further right in the title bar.
        const QPoint grab = QPoint(w.titleBar().left() + 24, w.titleBar().center().y()) - w.window->pos();
        // The window chooses what a press does from where the pointer hovers, as on screen.
        QMouseEvent hover(QEvent::MouseMove, grab, w.window->mapToGlobal(grab), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(w.window, &hover);
        QMouseEvent press(QEvent::MouseButtonPress, grab, w.window->mapToGlobal(grab), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(w.window, &press);
        const QPoint to = grab + by;
        QMouseEvent move(QEvent::MouseMove, to, w.window->mapToGlobal(to), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(w.window, &move);
        QMouseEvent release(QEvent::MouseButtonRelease, to, w.window->mapToGlobal(to), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(w.window, &release);
        QCoreApplication::processEvents();
    };
    w.window->move(200, 200);
    QCoreApplication::processEvents();
    dragBy({40, 30});
    ASSERT_EQ(w.window->pos(), QPoint(240, 230)) << "the drag reaches the window";
    dragBy({3000, 3000});
    EXPECT_TRUE(w.controlsShow()) << "dragged by its title bar";
    dragBy({-5000, -3000});
    EXPECT_TRUE(w.controlsShow()) << "dragged by its title bar";
    EXPECT_LT(w.titleBar().left(), 0) << "partly behind the panels (how far a drag goes, Qt bounds by the pointer)";
}

TEST(DocumentTabs, ACanvasWindowMovesBackInWhenTheAreaShrinks) {
    LooseWindow w;
    w.window->move(w.room().width() - w.window->width(), w.room().height() - 80);
    QCoreApplication::processEvents();
    const QSize size = w.window->size();
    w.tabs.resize(500, 300);  // a panel opened or widened, or the main window got smaller
    QCoreApplication::processEvents();
    EXPECT_TRUE(w.controlsShow());
    EXPECT_EQ(w.window->size(), size) << "moved, not resized";

    w.tabs.resize(300, 300);
    QCoreApplication::processEvents();
    EXPECT_EQ(w.titleBar().right(), w.room().width() - 1) << "wider than the area: its buttons' end stays in";
    EXPECT_EQ(w.window->size(), size);
}

TEST(DocumentTabs, ACanvasWindowWiderThanTheAreaKeepsItsWidthAndItsButtonsIn) {
    LooseWindow w;
    w.window->resize(3000, 300);
    QCoreApplication::processEvents();
    EXPECT_EQ(w.window->width(), 3000);
    EXPECT_EQ(w.titleBar().right(), w.room().width() - 1);
}
