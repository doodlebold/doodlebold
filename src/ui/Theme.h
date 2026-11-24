#pragma once

#include <QColor>
#include <QString>

#include <optional>

class QApplication;
class QFont;
class QFormLayout;
class QGridLayout;
class QLayout;

// Doodlebold's UI design system: the one source of every colour, size, spacing, radius, font and duration the chrome uses.
// The canvas and the user's own colours (pickers, swatches, thumbnails) are not themed. tools/lint_ui_theme_tokens.py checks
// that the UI code takes its values from here.
namespace doodlebold::Theme {

// Colours by role. A light or dark theme changes only the values (builtIn(), cosmic()), never the code that uses them.
enum class Token {
    Window,         // background: the main window, the panel area behind the cards
    Base,           // surface-raised: fields, tool cells, value bars, a secondary button's resting fill
    AlternateBase,  // surface-alt: alternate rows, disabled fields, inset settings boxes
    Surface,        // surface: cards, menus, tooltips, toasts, pills
    Border,
    BorderStrong,   // check box frames, scroll bar handles, a floating pill's edge
    BorderDisabled,
    Text,           // text-primary
    TextMuted,      // text-secondary: only on Window, Surface, AlternateBase or Base (4.5:1 or better there)
    TextDisabled,
    Accent,         // the active tool, the selection, primary buttons, slider thumbs
    AccentHover,    // a primary button under the pointer; the playhead and current frame
    AccentText,     // on-accent: text and marks on Accent, AccentHover, AccentPressed or Selected
    Danger,
    Hover,          // control-hover: any control under the pointer
    Pressed,        // control-pressed: any control held down
    Selected,       // selection: the current item (layer row, tool cell, checked button)
    SelectedText,   // on-selection
    Fill,           // a value bar's filled part
    AntDark,        // canvas only: marching ants' dashes (black in every theme, so they read on any picture)
    AntLight,       // canvas only
    SelectionTint,  // canvas only: the selection shown as an area while the Selection Pen / Eraser is active
    TextAccent,     // text-accent: links and accent-coloured text (Accent itself is too dark for text: 3.5:1 on Window)
    AccentPressed,  // a primary button held down
    FocusRing,      // the 1 px outline of the control with keyboard focus (on a Selected fill, AccentText instead)
    Success,
    Warning,
    Shadow,         // the one shadow colour (alpha included): a slider thumb's, an overlay's
};
inline constexpr int kTokenCount = static_cast<int>(Token::Shadow) + 1;

// Spacing scale, px: a 4 px grid, with a 2 px hairline for space inside one control. Built from the values the chrome used
// most (4 ×33, 6 ×28, 2 ×24, 10 ×17, 8 ×16 sites at 8fde570); the strays snapped to the nearest step (approved).
inline constexpr int kSpaceHair = 2;  // inside a control: a card's title to its content, between chip parts
inline constexpr int kSpace1 = 4;     // between neighbouring controls in a bar; between stacked bars
inline constexpr int kSpace2 = 8;     // label to control in a panel; rows of settings; dialog field rows
inline constexpr int kSpace3 = 12;    // a panel's padding; label to field in a dialog; between option cards
inline constexpr int kSpace4 = 16;    // a dialog's padding and sections
inline constexpr int kSpace5 = 24;    // columns in a dialog; help text indented under its check box
inline constexpr int kSpace6 = 32;    // reserved: nothing uses it yet

// Corner radius scale, px. Nested corners: an element that fills its container's content box takes the container's radius
// minus the padding between them, never less than kRadiusXs.
inline constexpr int kRadiusXs = 4;   // check boxes, key caps, thumbnails, frame cells, badges, chips, slider grooves
inline constexpr int kRadiusSm = 6;   // fields, spin boxes, menu items, tooltips, list items, tabs in a tab group, tool buttons
inline constexpr int kRadiusMd = 10;  // buttons, tool tray cells, combo boxes, value bars, layer rows, menus, toasts
inline constexpr int kRadiusLg = 14;  // cards and panels, canvas window frames, the replay bar
// Pills, switches, thumbs and meters are fully round: their radius is half their height, worked out where they are drawn.

// Icon sizes, px.
inline constexpr int kIconSm = 12;  // inline marks: lock, link, share, signs
inline constexpr int kIconMd = 16;  // every icon on a button
inline constexpr int kIconLg = 20;  // reserved

// Border widths, px.
inline constexpr int kBorder = 1;        // edges and the focus ring
inline constexpr int kBorderStrong = 2;  // a selected ring, a drop target, the mask target, a colour swatch's ring
inline constexpr double kGlyphStroke = 1.5;  // the line width of icons drawn in code (1.3 to 1.6 before)

// The fill of a floating surface over the canvas (toasts, the replay bar), alpha 0-255: the canvas barely shows through.
inline constexpr int kFloatingAlpha = 245;

// The overlay elevation's shadow (the replay bar over the canvas): blur and downward offset in px, black at this alpha (50 %).
inline constexpr int kOverlayShadowBlur = kSpace6;
inline constexpr int kOverlayShadowOffset = kSpace3;
inline constexpr int kOverlayShadowAlpha = 128;

// Motion, ms. The toast's own durations, the only animation in the chrome, became the scale.
inline constexpr int kMotionFast = 160;  // a short move (a toast sliding to its place)
inline constexpr int kMotionBase = 200;  // fades
inline constexpr int kMotionSlow = 360;  // an emphasis (a toast's shake)

// The two densities. Compact: the menu and quick bars, the status bar, every dock card (timeline included), the canvas
// windows' headers, floating pills, menus, tooltips and toasts. Comfortable: every dialog. No panel picks its own values.
namespace compact {
inline constexpr int kPadding = kSpace3;     // a card's edge to its content
inline constexpr int kControlGap = kSpace1;  // between the controls of one row or bar
inline constexpr int kLabelGap = kSpace2;    // a label to its control
inline constexpr int kRowGap = kSpace2;      // between rows of settings
inline constexpr int kStackGap = kSpace1;    // between stacked bars (the timeline's)
inline constexpr int kSectionGap = kSpace3;  // between one section and the next, its heading included
inline constexpr int kControlSm = 20;        // small controls: inline toggles, chips (px tall)
inline constexpr int kControlMd = 24;        // buttons, tool buttons, value bars, fields, combo boxes
inline constexpr int kControlLg = 32;        // the floating bars' buttons (quick-shape pill, replay bar)
}
namespace comfortable {
inline constexpr int kPadding = kSpace4;
inline constexpr int kControlGap = kSpace3;
inline constexpr int kLabelGap = kSpace3;
inline constexpr int kRowGap = kSpace2;
inline constexpr int kSectionGap = kSpace4;
inline constexpr int kControlSm = 24;
inline constexpr int kControlMd = 28;
inline constexpr int kControlLg = 32;
}

// The names the panels used before the scale; kept so the panels read as before.
inline constexpr int kPanelRowGap = compact::kRowGap;
inline constexpr int kPanelColumnGap = compact::kLabelGap;
inline constexpr int kPanelSectionGap = compact::kSectionGap;
// Options' pages: each section a card with kCardPadding inside it, kCardTitleLift less above its title, its rows kCardRowGap
// apart, and the cards kCardGap apart.
inline constexpr int kCardPadding = comfortable::kPadding;
inline constexpr int kCardTitleLift = kSpace1;
inline constexpr int kCardRowGap = kSpace3;
inline constexpr int kCardGap = kSpace3;

// Type roles. Sizes are relative to the app font (the system size, at least 9 pt), so the chrome follows the system's
// text size; at 9 pt they are: caption and overline 8 pt, body, label and body-strong 9 pt, title 11 pt, heading 14 pt.
enum class TextRole {
    Caption,     // small secondary text: a tip's shortcut, a layer's blend mode, ruler labels, badges
    Overline,    // section and card titles: small capitals, letter-spaced
    Body,
    Label,       // values inside controls
    BodyStrong,  // primary buttons, dock titles, a tip's name, headlines
    Title,       // a title inside a dialog
    Heading,     // the About box's name
    Mono,        // diagnostics and licence text
};
inline constexpr double kCaptionDelta = -1.0;    // pt from the app font
inline constexpr double kTitleDelta = 2.0;       // pt
inline constexpr double kHeadingDelta = 5.0;     // pt
inline constexpr double kOverlineSpacing = 0.6;  // px of letter spacing in small capitals
// The fonts of the roles, made from the app font when the theme is applied: a painter reads one, it never builds it.
const QFont& font(TextRole role);

// Data colours: not themed and not chrome. They mark the user's own sorting (track labels) or a layer's state, and keep the
// same value in every theme (kept as they were).
inline constexpr QRgb kClipBar = 0xffe83f6f;    // a clipping layer's bar
inline constexpr QRgb kMaskFrame = 0xffe21e48;  // the mask being painted on
inline constexpr QRgb kDriveMeter = 0xff32a0f0;  // the drive usage meter (the memory meter is Success)
// The transparency checkerboard's two greys, by where it is drawn: still as each was (question 5's interim answer).
inline constexpr QRgb kCheckerLight = 0xfff2f3f5;
inline constexpr QRgb kCheckerDark = 0xff9a9da3;

enum class Metric {
    Radius,         // kRadiusMd
    RadiusSmall,    // kRadiusSm
    Gap,            // kSpace1: between neighbouring controls or cells
    Padding,        // kSpace1: between a panel's edge and its content
    CardRadius,     // kRadiusLg
    CardGap,        // kSpace1: half the gap between two cards, and between a card and the window's edge
    DialogPadding,  // a dialog's outer margins and the space between its sections
    FieldSpacing,   // between the rows of a dialog's fields
    FieldGap,       // between a field's label and the field, and between the controls of one row
};

// Where the values come from: Doodlebold's own (BuiltIn), or the desktop's COSMIC theme when DOODLEBOLD_THEME=cosmic asks for
// it and there is one (Auto).
enum class Source {
    Auto,
    BuiltIn
};

struct Values {
    QColor colors[kTokenCount];
    // Corners and spacing are Doodlebold's own in every theme (mixed radii looked strange).
    int radius = kRadiusMd;
    int radiusSmall = kRadiusSm;
    int cardRadius = kRadiusLg;
    int cardGap = kSpace1;
    int gap = kSpace1;
    int padding = kSpace1;
    int dialogPadding = comfortable::kPadding;
    int fieldSpacing = comfortable::kRowGap;
    int fieldGap = comfortable::kLabelGap;
    QString name;
};

QColor color(Token token);
int metric(Metric metric);
// Gives a dialog's outer layout the DialogPadding all round and between its sections. A dialog keeps the size of its content
// (it cannot be stretched) unless it is `resizable` (one whose list or log can use the room).
void padDialog(QLayout* layout, bool resizable = false);
void spaceFields(QGridLayout* grid);
void spaceFields(QFormLayout* form);
const Values& current();

Values builtIn();
// The COSMIC desktop's dark theme from its configuration under `configRoot` (normally $XDG_CONFIG_HOME/cosmic or
// ~/.config/cosmic, then /usr/share/cosmic), mapped onto Doodlebold's tokens; empty when none is found. The icons are
// drawn for a dark theme, so the dark entries are used even while COSMIC is in light mode.
std::optional<Values> cosmic(const QString& configRoot);
// Fills a style sheet's placeholders: @Name is the colour of Token::Name, @Name~A the same colour with alpha A (0-255), and
// @name the px value of a size token (radiusMd, space3, controlMd, …, listed in Theme.cpp); @captionSize and @titleSize are
// the type roles' sizes in pt.
QString styleSheet(const QString& source, const Values& values);

// Applies a theme. With Source::Auto and DOODLEBOLD_THEME=cosmic the COSMIC theme is used where there is one.
void apply(QApplication& app, Source source = Source::Auto);

}
