#pragma once

#include <QWidget>

class QLabel;

namespace doodlebold {

// The main window puts one in each dock, round the panel it holds; the panel itself is unchanged.
class PanelCard : public QWidget {
    Q_OBJECT
public:
    PanelCard(const QString& title, QWidget* content, QWidget* parent = nullptr);

    QWidget* content() const { return m_content; }
    QRect cardRect() const;
    QSize chrome() const;
    // Extra room above / below the card, for a card at the edge of the window's panel area: the gap there is as wide as
    // the gap between two cards. The card and its content move in; the widget keeps its size.
    void setEdgeGap(int top, int bottom);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QWidget* m_content;
    QLabel* m_title;
    int m_edgeTop = 0, m_edgeBottom = 0;
};

}
