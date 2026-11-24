#include "core/text/TextContent.h"

#include <gtest/gtest.h>

#include <cstring>
#include <vector>

using namespace doodlebold;

namespace {

TextStyle style(double size, bool bold = false, QColor colour = Qt::black) {
    TextStyle s;
    s.font = QStringLiteral("Sans");
    s.sizePx = size;
    s.bold = bold;
    s.colour = colour;
    return s;
}

}

TEST(TextContent, InsertsMergesAndSplitsRuns) {
    TextContent t;
    t.insert(0, QStringLiteral("Hello"), style(20));
    t.insert(5, QStringLiteral(" Ink"), style(20));
    ASSERT_EQ(t.runs.size(), 1u) << "equal styles merge";
    EXPECT_EQ(t.plainText(), "Hello Ink");
    t.insert(5, QStringLiteral("!"), style(40, true));
    ASSERT_EQ(t.runs.size(), 3u);
    EXPECT_EQ(t.runs[0].text, "Hello");
    EXPECT_EQ(t.runs[1].text, "!");
    EXPECT_EQ(t.runs[2].text, " Ink");
    EXPECT_EQ(t.length(), 10);
}

TEST(TextContent, StyleAtIsTheCharacterBefore) {
    TextContent t;
    t.emptyStyle = style(33);
    EXPECT_EQ(t.styleAt(0).sizePx, 33) << "an empty layer types in its own style";
    t.insert(0, QStringLiteral("ab"), style(20));
    t.insert(2, QStringLiteral("cd"), style(40));
    EXPECT_EQ(t.styleAt(0).sizePx, 20);
    EXPECT_EQ(t.styleAt(2).sizePx, 20);
    EXPECT_EQ(t.styleAt(3).sizePx, 40);
    EXPECT_EQ(t.styleOf(1, 3).sizePx, 20);
}

TEST(TextContent, RemoveAndRestyle) {
    TextContent t;
    t.insert(0, QStringLiteral("Hello Ink"), style(20));
    t.restyle(6, 9, [](TextStyle& s) { s.bold = true; });
    ASSERT_EQ(t.runs.size(), 2u);
    EXPECT_TRUE(t.runs[1].style.bold);
    EXPECT_EQ(t.runs[1].text, "Ink");
    t.remove(4, 7);
    EXPECT_EQ(t.plainText(), "Hellnk");
    t.restyle(0, 6, [](TextStyle& s) { s.bold = true; });
    EXPECT_EQ(t.runs.size(), 1u) << "all bold now: one run";
    t.remove(0, 6);
    EXPECT_TRUE(t.isEmpty());
    EXPECT_TRUE(t.emptyStyle.bold) << "typing continues in the removed text's style";
}
