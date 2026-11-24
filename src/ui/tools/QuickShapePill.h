#pragma once

#include "core/paint/QuickShape.h"

#include <QWidget>

#include <array>

class QAbstractButton;
class QLabel;

namespace doodlebold {

class PillButton;

// The bar at the top of the canvas while a snapped stroke is adjusted: the shapes, the corners of a Polygon, cancel and done.
class QuickShapePill : public QWidget {
    Q_OBJECT
public:
    explicit QuickShapePill(QWidget* parent = nullptr);

    void setShape(QuickShapeKind kind, int corners);
    // Only the icons, for a canvas narrower than the pill.
    void setCompact(bool compact);

    QAbstractButton* shapeButton(QuickShapeKind kind) const;
    QAbstractButton* fewerButton() const;
    QAbstractButton* moreButton() const;
    QAbstractButton* cancelButton() const;
    QAbstractButton* doneButton() const;

signals:
    void kindChosen(doodlebold::QuickShapeKind kind);
    void cornersChosen(int corners);
    void cancelled();
    void accepted();

protected:
    void paintEvent(QPaintEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;

private:
    std::array<PillButton*, 6> m_shapes{};
    QWidget* m_stepper = nullptr;
    PillButton* m_fewer = nullptr;
    PillButton* m_more = nullptr;
    QLabel* m_count = nullptr;
    PillButton* m_cancel = nullptr;
    PillButton* m_done = nullptr;
    int m_corners = QuickShapeEdit::kMinimumCorners;
};

}
