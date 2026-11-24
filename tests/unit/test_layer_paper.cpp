#include "core/document/Document.h"
#include "core/layers/Compositor.h"
#include "core/layers/LayerPaper.h"

#include <gtest/gtest.h>

using namespace doodlebold;

namespace {

struct TwoLayers {
    TwoLayers() : doc(QSize(70, 70), Qt::white) {
        top = doc.currentLayerId();
        LayerProps p = doc.layer(top)->props;
        p.paper.texture = QStringLiteral("t0");
        p.paper.intensity = 50;
        EXPECT_TRUE(doc.setLayerProps(top, p).ok());
        doc.undoStack()->clear();
    }
    Document doc;
    LayerId top;
};

}

TEST(LayerPaper, SettingsAreClampedToWhatTheControlsHold) {
    EXPECT_EQ(LayerPaper::clampIntensity(-3), LayerPaper::kMinIntensity);
    EXPECT_EQ(LayerPaper::clampIntensity(400), LayerPaper::kMaxIntensity);
    EXPECT_EQ(LayerPaper::clampScale(1), LayerPaper::kMinScale);
    EXPECT_EQ(LayerPaper::clampScale(9999), LayerPaper::kMaxScale);
    EXPECT_EQ(LayerPaper::clampScale(37), 37);
}

TEST(LayerPaper, TheEffectDoesNotChangeTheCanvasYet) {
    Document plain(QSize(70, 70), Qt::white), withPaper(QSize(70, 70), Qt::white);
    for (Document* d : {&plain, &withPaper})
        d->currentLayer();
    LayerProps p = withPaper.layer(withPaper.currentLayerId())->props;
    p.paper.texture = QStringLiteral("t0");
    p.paper.intensity = 100;
    ASSERT_TRUE(withPaper.setLayerProps(p.id, p).ok());
    EXPECT_EQ(withPaper.projection(), plain.projection());
}

TEST(LayerPaperDocument, ChangingThePaperIsOneUndoStep) {
    TwoLayers s;
    LayerProps p = s.doc.layer(s.top)->props;
    p.paper.intensity = 100;
    ASSERT_TRUE(s.doc.setLayerProps(s.top, p).ok());
    EXPECT_EQ(s.doc.undoStack()->count(), 1);
    s.doc.undoStack()->undo();
    EXPECT_EQ(s.doc.layer(s.top)->props.paper.intensity, 50);
    s.doc.undoStack()->redo();
    EXPECT_EQ(s.doc.layer(s.top)->props.paper.intensity, 100);
}

TEST(LayerPaperDocument, FoldersHaveNoPaper) {
    Document doc(QSize(70, 70), Qt::white);
    const LayerId folder = doc.addFolder().value();
    LayerProps p = doc.layer(folder)->props;
    p.paper.texture = QStringLiteral("t0");
    EXPECT_FALSE(doc.setLayerProps(folder, p).ok()) << "nothing to change: a folder has no paper";
    EXPECT_FALSE(doc.layer(folder)->props.paper.isSet());
}

TEST(LayerPaperDocument, CopyLayerCopiesThePaper) {
    TwoLayers s;
    const LayerId copy = s.doc.duplicateLayer(s.top).value();
    EXPECT_EQ(s.doc.layer(copy)->props.paper, s.doc.layer(s.top)->props.paper);
}
