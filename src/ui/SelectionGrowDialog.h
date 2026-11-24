#pragma once

#include <QDialog>

class QSpinBox;

namespace doodlebold {

// The Grow / Shrink Selection dialog, reduced to its width; the shape options are not offered, the default shape is
// used.
class SelectionGrowDialog : public QDialog {
    Q_OBJECT
public:
    static constexpr int kMaxWidth =
        500;  // the radius is capped at 500 px (ui-tokens: allow: not a size on screen: the largest radius, in canvas pixels)

    explicit SelectionGrowDialog(bool dilate, QWidget* parent = nullptr);

    bool isDilate() const { return m_dilate; }
    int growWidth() const;
    void setGrowWidth(int pixels);

private:
    bool m_dilate;
    QSpinBox* m_width = nullptr;
};

}
