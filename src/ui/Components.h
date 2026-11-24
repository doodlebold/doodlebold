#pragma once

#include <QRgb>
#include <QString>

class QAbstractButton;
class QFrame;
class QLabel;
class QPainter;
class QRect;
class QVBoxLayout;
class QWidget;

// The design system's shared pieces (Theme.h): screens build from these instead of styling their own.
// Buttons are styled by the style sheet: a QPushButton is Secondary, the dialog's default button or role="primary" is
// Primary, a QToolButton is the tool button; toolButton() gives it the one size.
namespace doodlebold::components {

// A section's title in a panel or an options card: small capitals in secondary text (TextRole::Overline).
QLabel* sectionHeading(const QString& text, QWidget* parent = nullptr);

// An options card: Surface, the large radius, comfortable padding and its title; `body` receives the card's layout for the
// rows under the title.
QFrame* sectionCard(const QString& title, QVBoxLayout*& body);

// Secondary text (TextMuted), wrapped: hints, help under an option, a dialog's subtitle.
QLabel* mutedLabel(const QString& text, QWidget* parent = nullptr);

// The tool button's one size: a compact control square with a medium icon.
void toolButton(QAbstractButton* button);

// A tip: the name in body-strong, then the shortcut in a caption in secondary text, then the explanation; any part may be
// empty except the name.
QString tipHtml(const QString& name, const QString& keys = {}, const QString& explanation = {});

// The transparency checkerboard under the user's colours and thumbnails, in `cell` px squares. Its greys are not themed.
void paintChecker(QPainter& painter, const QRect& area, int cell, QRgb light, QRgb dark);

}
