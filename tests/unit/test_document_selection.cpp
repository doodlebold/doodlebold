#include "core/document/Document.h"
#include "core/selection/MagicWand.h"

#include <gtest/gtest.h>

using namespace doodlebold;
using selection::CombineMode;

namespace {

constexpr int kOne = SelectionMask::kOne;

SelectionMask rect(const Document& doc, const QRect& r) {
    return selection::rectangle(doc.size(), QRectF(r), false);
}

}

TEST(DocumentSelection, StartsWithoutSelection) {
    Document doc(QSize(100, 80), Qt::white);
    EXPECT_FALSE(doc.selection().has_value());
}

TEST(DocumentSelection, SelectAllDeselectAndInvertAreUndoSteps) {
    Document doc(QSize(100, 80), Qt::white);
    ASSERT_TRUE(doc.selectAll());
    ASSERT_TRUE(doc.selection().has_value());
    EXPECT_EQ(doc.selection()->at(99, 79), kOne);
    EXPECT_TRUE(doc.isModified());

    ASSERT_TRUE(doc.deselect());
    EXPECT_FALSE(doc.selection().has_value());
    ASSERT_TRUE(doc.undo());
    ASSERT_TRUE(doc.selection().has_value());
    EXPECT_EQ(doc.selection()->at(5, 5), kOne);
    ASSERT_TRUE(doc.redo());
    EXPECT_FALSE(doc.selection().has_value());
    ASSERT_TRUE(doc.undo());

    ASSERT_TRUE(doc.setSelection(rect(doc, QRect(10, 10, 30, 30))));
    ASSERT_TRUE(doc.invertSelection());
    EXPECT_EQ(doc.selection()->at(20, 20), 0);
    EXPECT_EQ(doc.selection()->at(60, 60), kOne);
    ASSERT_TRUE(doc.undo());
    EXPECT_EQ(doc.selection()->at(20, 20), kOne);
    EXPECT_EQ(doc.selection()->at(60, 60), 0);
}

TEST(DocumentSelection, UndoTextsNameTheOperation) {
    Document doc(QSize(100, 80), Qt::white);
    ASSERT_TRUE(doc.selectAll());
    EXPECT_EQ(doc.undoStack()->undoText(), QStringLiteral("Select All"));
    ASSERT_TRUE(doc.setSelection(rect(doc, QRect(0, 0, 10, 10)), CombineMode::Subtract));
    EXPECT_EQ(doc.undoStack()->undoText(), QStringLiteral("Subtract from Selection"));
    ASSERT_TRUE(doc.setSelection(rect(doc, QRect(0, 0, 10, 10)), CombineMode::Add));
    EXPECT_EQ(doc.undoStack()->undoText(), QStringLiteral("Add to Selection"));
    ASSERT_TRUE(doc.setSelection(rect(doc, QRect(0, 0, 50, 10)), CombineMode::Intersect));
    EXPECT_EQ(doc.undoStack()->undoText(), QStringLiteral("Intersect Selection"));
    ASSERT_TRUE(doc.dilateSelection(2));
    EXPECT_EQ(doc.undoStack()->undoText(), QStringLiteral("Grow Selection"));
    ASSERT_TRUE(doc.erodeSelection(1));
    EXPECT_EQ(doc.undoStack()->undoText(), QStringLiteral("Shrink Selection"));
    ASSERT_TRUE(doc.invertSelection());
    EXPECT_EQ(doc.undoStack()->undoText(), QStringLiteral("Invert Selection"));
    ASSERT_TRUE(doc.deselect());
    EXPECT_EQ(doc.undoStack()->undoText(), QStringLiteral("Deselect"));
}

TEST(DocumentSelection, EditsThatChangeNothingAreRefusedWithoutAnUndoStep) {
    Document doc(QSize(100, 80), Qt::white);
    const int steps = doc.undoStack()->count();
    EXPECT_EQ(doc.deselect().error(), EditError::NoChange);
    EXPECT_EQ(doc.invertSelection().error(), EditError::NoChange);
    EXPECT_EQ(doc.dilateSelection(3).error(), EditError::NoChange);
    EXPECT_EQ(doc.erodeSelection(3).error(), EditError::NoChange);
    ASSERT_TRUE(doc.selectAll());
    EXPECT_EQ(doc.selectAll().error(), EditError::NoChange);
    EXPECT_EQ(doc.dilateSelection(0).error(), EditError::NoChange);
    EXPECT_EQ(doc.undoStack()->count(), steps + 1);
}

TEST(DocumentSelection, ASelectionThatSelectsNothingDeselects) {
    Document doc(QSize(100, 80), Qt::white);
    ASSERT_TRUE(doc.setSelection(rect(doc, QRect(10, 10, 30, 30))));
    ASSERT_TRUE(doc.setSelection(rect(doc, QRect(50, 50, 10, 10)), CombineMode::Intersect));
    EXPECT_FALSE(doc.selection().has_value());
    ASSERT_TRUE(doc.undo());
    EXPECT_TRUE(doc.selection().has_value());
    ASSERT_TRUE(doc.setSelection(rect(doc, QRect(20, 20, 4, 4))));
    ASSERT_TRUE(doc.erodeSelection(5));
    EXPECT_FALSE(doc.selection().has_value());
}

TEST(DocumentSelection, CombineModesBuildOnTheCurrentSelection) {
    Document doc(QSize(100, 80), Qt::white);
    ASSERT_TRUE(doc.setSelection(rect(doc, QRect(0, 0, 40, 40))));
    ASSERT_TRUE(doc.setSelection(rect(doc, QRect(30, 30, 40, 40)), CombineMode::Add));
    EXPECT_EQ(doc.selection()->at(10, 10), kOne);
    EXPECT_EQ(doc.selection()->at(60, 60), kOne);
    ASSERT_TRUE(doc.setSelection(rect(doc, QRect(0, 0, 20, 100)), CombineMode::Subtract));
    EXPECT_EQ(doc.selection()->at(10, 10), 0);
    EXPECT_EQ(doc.selection()->at(25, 25), kOne);
    ASSERT_TRUE(doc.setSelection(rect(doc, QRect(0, 0, 100, 35)), CombineMode::Intersect));
    EXPECT_EQ(doc.selection()->at(25, 25), kOne);
    EXPECT_EQ(doc.selection()->at(60, 60), 0);
}

TEST(DocumentSelection, ChangedAreaIsSignalled) {
    Document doc(QSize(200, 200), Qt::white);
    std::vector<QRect> areas;
    QObject::connect(&doc, &Document::selectionChanged, &doc, [&](const QRect& r) { areas.push_back(r); });
    ASSERT_TRUE(doc.setSelection(rect(doc, QRect(70, 70, 10, 10))));
    ASSERT_EQ(areas.size(), 1u);
    EXPECT_EQ(areas[0], QRect(64, 64, 64, 64));
    ASSERT_TRUE(doc.undo());
    EXPECT_EQ(areas.size(), 2u);
    ASSERT_TRUE(doc.redo());
    EXPECT_EQ(areas.size(), 3u);
    ASSERT_TRUE(doc.fillLayer(doc.currentLayerId(), Qt::red));
    EXPECT_EQ(areas.size(), 3u);
}

TEST(DocumentSelection, AnEmptyLayerSelectsNothing) {
    Document doc(QSize(100, 80), Qt::white);
    EXPECT_EQ(doc.selectionFromLayer(doc.currentLayerId()).error(), EditError::NoChange);
}

TEST(DocumentSelection, SelectionFromLayerUsesAlphaAndCombines) {
    Document doc(QSize(100, 80), Qt::white);
    const LayerId a = doc.currentLayerId();
    ASSERT_TRUE(doc.setSelection(rect(doc, QRect(10, 10, 20, 20))));
    ASSERT_TRUE(doc.fillLayer(a, Qt::black));
    const LayerId b = doc.addLayer().value();
    ASSERT_TRUE(doc.setSelection(rect(doc, QRect(50, 40, 10, 10))));
    ASSERT_TRUE(doc.fillLayer(b, Qt::black));
    ASSERT_TRUE(doc.deselect());

    ASSERT_TRUE(doc.selectionFromLayer(a));
    EXPECT_EQ(doc.selection()->bounds(true), QRect(10, 10, 20, 20));
    EXPECT_EQ(doc.selection()->at(15, 15), kOne);
    ASSERT_TRUE(doc.selectionFromLayer(b, CombineMode::Add));
    EXPECT_EQ(doc.selection()->at(15, 15), kOne);
    EXPECT_EQ(doc.selection()->at(55, 45), kOne);
    ASSERT_TRUE(doc.selectionFromLayer(b, CombineMode::Subtract));
    EXPECT_EQ(doc.selection()->at(55, 45), 0);
    EXPECT_EQ(doc.selection()->at(15, 15), kOne);

    EXPECT_EQ(doc.selectionFromLayer(LayerId(9999)).error(), EditError::NoSuchLayer);
}

TEST(DocumentSelection, SelectionFromFolderUsesItsComposite) {
    Document doc(QSize(100, 80), Qt::white);
    const LayerId a = doc.currentLayerId();
    ASSERT_TRUE(doc.setSelection(rect(doc, QRect(10, 10, 20, 20))));
    ASSERT_TRUE(doc.fillLayer(a, Qt::black));
    ASSERT_TRUE(doc.deselect());
    const LayerId folder = doc.addFolder().value();
    ASSERT_TRUE(doc.moveLayer(a, folder, 0));
    ASSERT_TRUE(doc.selectionFromLayer(folder));
    EXPECT_EQ(doc.selection()->bounds(true), QRect(10, 10, 20, 20));
}

TEST(DocumentSelection, MismatchedMaskSizeIsRefused) {
    Document doc(QSize(100, 80), Qt::white);
    const auto r = doc.setSelection(selection::all(QSize(50, 50)));
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error(), EditError::SizeMismatch);
}

TEST(DocumentSelection, SelectionEditsAreRefusedDuringAStroke) {
    Document doc(QSize(100, 80), Qt::white);
    doc.setPainting(true);
    EXPECT_EQ(doc.selectAll().error(), EditError::StrokeInProgress);
    doc.setPainting(false);
    EXPECT_TRUE(doc.selectAll());
}
