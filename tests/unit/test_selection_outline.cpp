#include "core/selection/SelectionOps.h"
#include "core/selection/SelectionOutline.h"

#include <algorithm>

#include <gtest/gtest.h>

using namespace doodlebold;
using namespace doodlebold::selection;

namespace {

const QSize kCanvas(150, 100);

std::vector<QLine> lines(const QPainterPath& p) {
    std::vector<QLine> out;
    QPointF start, previous;
    for (int i = 0; i < p.elementCount(); ++i) {
        const QPainterPath::Element e = p.elementAt(i);
        const QPointF pt(e.x, e.y);
        if (e.type == QPainterPath::MoveToElement) {
            if (i > 0 && previous != start)
                out.emplace_back(previous.toPoint(), start.toPoint());
            start = previous = pt;
            continue;
        }
        EXPECT_EQ(e.type, QPainterPath::LineToElement);
        if (pt != previous)
            out.emplace_back(previous.toPoint(), pt.toPoint());
        previous = pt;
    }
    if (p.elementCount() > 0 && previous != start)
        out.emplace_back(previous.toPoint(), start.toPoint());
    return out;
}

bool has(const std::vector<QLine>& v, QLine l) {
    return std::ranges::any_of(v, [&](const QLine& x) { return x == l || x == QLine(l.p2(), l.p1()); });
}

}

TEST(SelectionOutline, EmptyMaskHasNoOutline) {
    EXPECT_TRUE(outline(SelectionMask(kCanvas)).isEmpty());
}

TEST(SelectionOutline, RectangleIsFourLinesOnTheOutermostPixelRing) {
    const SelectionMask m = marquee(kCanvas, QPoint(10, 20), QPoint(29, 39));
    const std::vector<QLine> l = lines(outline(m));
    ASSERT_EQ(l.size(), 4u);
    EXPECT_TRUE(has(l, QLine(10, 20, 30, 20)));
    EXPECT_TRUE(has(l, QLine(10, 40, 30, 40)));
    EXPECT_TRUE(has(l, QLine(10, 20, 10, 40)));
    EXPECT_TRUE(has(l, QLine(30, 20, 30, 40)));
}

TEST(SelectionOutline, SelectAllIsTheCanvasBorder) {
    const std::vector<QLine> l = lines(outline(all(kCanvas)));
    ASSERT_EQ(l.size(), 4u);
    EXPECT_TRUE(has(l, QLine(0, 0, 150, 0)));
    EXPECT_TRUE(has(l, QLine(0, 100, 150, 100)));
    EXPECT_TRUE(has(l, QLine(0, 0, 0, 100)));
    EXPECT_TRUE(has(l, QLine(150, 0, 150, 100)));
}

TEST(SelectionOutline, AHoleAddsItsOwnRing) {
    SelectionMask m = marquee(kCanvas, QPoint(10, 10), QPoint(59, 59));
    const SelectionMask hole = marquee(kCanvas, QPoint(20, 20), QPoint(29, 29));
    m = combine(m, hole, CombineMode::Subtract);
    EXPECT_EQ(lines(outline(m)).size(), 8u);
}

TEST(SelectionOutline, SoftEdgesFollowTheHalfCoverageLine) {
    SelectionMask m = marquee(kCanvas, QPoint(10, 10), QPoint(19, 19));
    std::vector<std::uint16_t> row(12, 0);
    for (int x = 0; x < 12; ++x)
        row[std::size_t(x)] =
            (x == 0 || x == 11) ? std::uint16_t(SelectionMask::kOne * 4 / 10) : std::uint16_t(SelectionMask::kOne * 6 / 10);
    m.writeRow(10, 9, 12, row.data());
    m.squeeze();
    const std::vector<QLine> l = lines(outline(m));
    EXPECT_TRUE(has(l, QLine(10, 10, 20, 10)));
}

TEST(SelectionOutline, LargeScatteredMaskIsFast) {
    SelectionMask m = all(QSize(2000, 1500));
    const QPainterPath p = outline(m);
    EXPECT_EQ(lines(p).size(), 4u);
}

TEST(SelectionOutline, EachBoundaryIsOneClosedLoop) {
    SelectionMask m = polygon(kCanvas, QPolygonF({QPointF(20, 20), QPointF(60, 25), QPointF(45, 70), QPointF(15, 55)}));
    m = combine(m, marquee(kCanvas, QPoint(100, 10), QPoint(120, 30)), CombineMode::Add);
    const QPainterPath p = outline(m);
    int loops = 0;
    for (int i = 0; i < p.elementCount(); ++i)
        loops += p.elementAt(i).type == QPainterPath::MoveToElement ? 1 : 0;
    EXPECT_EQ(loops, 2);
}
