#include "core/document/Document.h"
#include "core/paint/StrokeController.h"
#include "core/paint/StrokeMemory.h"

#include <gtest/gtest.h>

using namespace doodlebold;

namespace {

InputSample at(double x, double y) {
    InputSample s;
    s.pos = QPointF(x, y);
    return s;
}

void paintLine(Document& doc, double y = 40) {
    StrokeController stroke(&doc);
    BrushSettings st = defaultSettings(ToolType::Pencil);
    st.size = 60;
    ASSERT_TRUE(stroke.begin(at(10, y), Qt::red, st));
    stroke.add(at(110, y));
    stroke.finish();
}

}

TEST(Document, NewDocumentHasOneEmptyLayerOverPaper) {
    Document doc(QSize(120, 80), Qt::white);
    EXPECT_EQ(doc.layers().layerCount(), 1);
    ASSERT_NE(doc.currentLayer(), nullptr);
    EXPECT_EQ(doc.currentLayer()->props.name, QStringLiteral("Layer1"));
    EXPECT_TRUE(doc.currentLayer()->pixels.isEmpty());
    EXPECT_EQ(doc.projection().pixel(5, 5), 0xffffffffu);
    EXPECT_FALSE(doc.isModified());
}

TEST(Document, StrokeUndoRedoRestoresPixelsAndModifiedFlag) {
    Document doc(QSize(120, 80), Qt::white);
    const QImage pristine = doc.projection().copy();
    paintLine(doc);
    const QImage painted = doc.projection().copy();

    EXPECT_TRUE(doc.isModified());
    EXPECT_NE(painted, pristine);
    ASSERT_TRUE(doc.undo());
    EXPECT_EQ(doc.projection(), pristine);
    EXPECT_TRUE(doc.currentLayer()->pixels.isEmpty());
    EXPECT_FALSE(doc.isModified());
    ASSERT_TRUE(doc.redo());
    EXPECT_EQ(doc.projection(), painted);
}

TEST(Document, EditsAndUndoAreRefusedWhileAStrokeRuns) {
    Document doc(QSize(64, 64), Qt::white);
    StrokeController stroke(&doc);
    ASSERT_TRUE(stroke.begin(at(5, 5), Qt::black, defaultSettings(ToolType::Pencil)));
    const auto added = doc.addLayer();
    ASSERT_FALSE(added);
    EXPECT_EQ(added.error(), EditError::StrokeInProgress);
    EXPECT_EQ(doc.undo().error(), EditError::StrokeInProgress);
    stroke.finish();
    EXPECT_TRUE(doc.addLayer());
}

TEST(Document, LayerOperationsAreUndoSteps) {
    Document doc(QSize(64, 64), Qt::white);
    const LayerId first = doc.currentLayerId();
    const LayerId second = doc.addLayer().value();
    EXPECT_EQ(doc.currentLayerId(), second);
    EXPECT_EQ(doc.layers().indexOf(second), 1);
    ASSERT_TRUE(doc.undo());
    EXPECT_EQ(doc.layers().layerCount(), 1);
    EXPECT_EQ(doc.currentLayerId(), first);
    ASSERT_TRUE(doc.redo());
    EXPECT_EQ(doc.currentLayerId(), second);
}

TEST(Document, RefusedEditRecordsNoUndoStep) {
    Document doc(QSize(64, 64), Qt::white);
    const int steps = doc.undoStack()->count();
    EXPECT_EQ(doc.deleteLayer(doc.currentLayerId()).error(), EditError::LastLayer);
    EXPECT_EQ(doc.clearLayer(doc.currentLayerId()).error(), EditError::NoChange);
    EXPECT_EQ(doc.undoStack()->count(), steps);
    EXPECT_EQ(doc.layers().layerCount(), 1);
}

TEST(Document, OpacityDragMergesIntoOneStep) {
    Document doc(QSize(64, 64), Qt::white);
    const LayerId id = doc.currentLayerId();
    const int before = doc.undoStack()->count();
    for (int o = 90; o >= 40; o -= 10) {
        LayerProps p = doc.layer(id)->props;
        p.opacity = o;
        ASSERT_TRUE(doc.setLayerProps(id, p, /*mergeKey=*/1));
    }
    EXPECT_EQ(doc.undoStack()->count(), before + 1);
    EXPECT_EQ(doc.layer(id)->props.opacity, 40);
    ASSERT_TRUE(doc.undo());
    EXPECT_EQ(doc.layer(id)->props.opacity, 100);
}

TEST(Document, MergeDownKeepsThePicture) {
    Document doc(QSize(64, 64), Qt::white);
    const LayerId lower = doc.currentLayerId();
    ASSERT_TRUE(doc.fillLayer(lower, QColor(0, 0, 255)));
    const LayerId upper = doc.addLayer().value();
    ASSERT_TRUE(doc.fillLayer(upper, QColor(255, 0, 0)));
    LayerProps p = doc.layer(upper)->props;
    p.opacity = 50;
    ASSERT_TRUE(doc.setLayerProps(upper, p));
    const QImage composite = doc.projection().copy();
    ASSERT_TRUE(doc.mergeDown(upper));
    EXPECT_EQ(doc.layers().layerCount(), 1);
    EXPECT_EQ(doc.currentLayerId(), lower);
    EXPECT_EQ(doc.projection(), composite);
}

TEST(Document, PaintTargetExplainsRefusals) {
    Document doc(QSize(64, 64), Qt::white);
    const LayerId id = doc.currentLayerId();
    LayerProps p = doc.layer(id)->props;
    p.visible = false;
    ASSERT_TRUE(doc.setLayerProps(id, p));
    EXPECT_EQ(doc.paintTarget(id).error(), EditError::LayerHidden);
    p.visible = true;
    p.locked = true;
    ASSERT_TRUE(doc.setLayerProps(id, p));
    EXPECT_EQ(doc.paintTarget(id).error(), EditError::LayerLocked);
    EXPECT_EQ(doc.paintTarget(doc.addFolder().value()).error(), EditError::NotARasterLayer);
}

TEST(Document, DocumentsHaveIndependentHistoryAndView) {
    Document a(QSize(10, 10), Qt::white), b(QSize(10, 10), Qt::white);
    a.view().zoom = 2.0;
    EXPECT_DOUBLE_EQ(b.view().zoom, 1.0);
    EXPECT_NE(a.undoStack(), b.undoStack());
    EXPECT_NE(a.displayName(), b.displayName());
}

TEST(StrokeController, CancelRestoresPixelsWithoutUndoStep) {
    Document doc(QSize(64, 64), Qt::white);
    const int steps = doc.undoStack()->count();
    StrokeController stroke(&doc);
    ASSERT_TRUE(stroke.begin(at(5, 30), Qt::black, defaultSettings(ToolType::Pencil)));
    stroke.add(at(30, 30));
    stroke.add(at(45, 30));
    stroke.add(at(60, 30));  // the path holds back the last event, and a sample is laid once its stamps are ready: paint shows from here
    EXPECT_FALSE(doc.currentLayer()->pixels.isEmpty());
    stroke.cancel();
    EXPECT_FALSE(stroke.isActive());
    EXPECT_FALSE(doc.isPainting());
    EXPECT_TRUE(doc.currentLayer()->pixels.isEmpty());
    EXPECT_EQ(doc.undoStack()->count(), steps);
}

TEST(StrokeController, RefusesLockedLayerWithReason) {
    Document doc(QSize(64, 64), Qt::white);
    LayerProps p = doc.currentLayer()->props;
    p.locked = true;
    ASSERT_TRUE(doc.setLayerProps(doc.currentLayerId(), p));
    StrokeController stroke(&doc);
    const auto started = stroke.begin(at(5, 5), Qt::black, defaultSettings(ToolType::Pencil));
    ASSERT_FALSE(started);
    EXPECT_EQ(started.error(), EditError::LayerLocked);
    EXPECT_FALSE(doc.isPainting());
}

namespace {

struct MemorySetTo {
    explicit MemorySetTo(std::int64_t bytes) { stroke_memory::setPolicy({bytes, false}); }
    ~MemorySetTo() { stroke_memory::setPolicy({}); }
    MemorySetTo(const MemorySetTo&) = delete;
    MemorySetTo& operator=(const MemorySetTo&) = delete;
};

}

TEST(Document, PastItsShareOfMemoryUndoLetsGoOfTheOldestStepsPixels) {
    const MemorySetTo oneByte(1);  // no step's pixels fit, so only the newest step's are kept
    Document doc(QSize(120, 80), Qt::white);
    for (int i = 0; i < 3; ++i)
        paintLine(doc);  // each over the last, so each keeps the tiles it painted over
    EXPECT_TRUE(doc.undo()) << "the newest step is always kept";
    const auto refused = doc.undo();
    ASSERT_FALSE(refused);
    EXPECT_EQ(refused.error(), EditError::UndoLetGo);
    EXPECT_TRUE(doc.redo());
}

TEST(Document, WithinItsShareOfMemoryEveryStepIsKept) {
    Document doc(QSize(120, 80), Qt::white);
    for (int i = 0; i < 3; ++i)
        paintLine(doc);
    for (int i = 0; i < 3; ++i)
        EXPECT_TRUE(doc.undo());
    EXPECT_TRUE(doc.currentLayer()->pixels.isEmpty());
}

TEST(Document, ACanvasKeeps25UndoStepsUnlessTheOptionsSayOtherwise) {
    EXPECT_EQ(AppOptions::Values{}.undoSteps, 25);
    Document doc(QSize(64, 64), Qt::white);
    EXPECT_EQ(doc.undoStack()->undoLimit(), 25);
}
