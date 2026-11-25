#include "ui/MenuSpec.h"

#include <QAction>
#include <QActionGroup>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QStringList>
#include <QWidget>

#include <algorithm>
#include <vector>

namespace doodlebold::menu {

namespace {

constexpr Entry cmd(std::string_view id, std::string_view label, std::string_view keys = {}) {
    return {Kind::Command, id, label, keys, nullptr, 0};
}
constexpr Entry check(std::string_view id, std::string_view label, std::string_view keys = {}) {
    return {Kind::Check, id, label, keys, nullptr, 0};
}
constexpr Entry radio(std::string_view id, std::string_view label) {
    return {Kind::Radio, id, label, {}, nullptr, 0};
}
constexpr Entry sep() {
    return {Kind::Separator, {}, {}, {}, nullptr, 0};
}
template <std::size_t N> constexpr Entry sub(std::string_view id, std::string_view label, const Entry (&children)[N]) {
    return {Kind::Submenu, id, label, {}, children, N};
}

constexpr Entry kExport[] = {
    cmd("file.export.png", "PNG (.png)…"),       cmd("file.export.jpg", "JPEG (.jpg)…"),
    cmd("file.export.psd", "Photoshop (.psd)…"), cmd("file.export.psb", "Large Photoshop (.psb)…"),
    cmd("file.export.bmp", "Bitmap (.bmp)…"),    cmd("file.export.tga", "Targa (.tga)…"),
    cmd("file.exportVideo", "Video (.mp4)…"),    sep(),
    cmd("file.exportTimelapse", "Timelapse…"),
};

constexpr Entry kTimelapse[] = {
    check("file.recordTimelapse", "Record Timelapse"),
    cmd("file.timelapseReplay", "Replay"),
    sep(),
    cmd("file.restartRecording", "Restart Recording…"),
    cmd("file.deleteRecording", "Delete Recording…"),
};

constexpr Entry kFile[] = {
    cmd("file.new", "New…", "Ctrl+N"),
    cmd("file.canvasFromClipboard", "New from Clipboard", "Ctrl+B"),
    cmd("file.newFromVideo", "New from Video…"),
    sep(),
    cmd("file.open", "Open…", "Ctrl+O"),
    {Kind::Submenu, "file.recent", "Open Recent", {}, nullptr, 0},  // filled when it opens: the recent files, 1 … 9, 0
    sep(),
    cmd("file.save", "Save", "Ctrl+S"),
    cmd("file.saveAs", "Save As…", "Shift+Ctrl+S"),
    sub("file.export", "Export", kExport),
    sub("file.timelapse", "Timelapse", kTimelapse),
    sep(),
    cmd("file.close", "Close"),
    cmd("file.quit", "Quit"),
};

constexpr Entry kEdit[] = {
    cmd("edit.undo", "Undo", "Ctrl+Z"),
    cmd("edit.redo", "Redo", "Ctrl+Y"),
    sep(),
    cmd("edit.cut", "Cut", "Ctrl+X"),
    cmd("edit.copy", "Copy", "Ctrl+C"),
    cmd("edit.paste", "Paste", "Ctrl+V"),
    sep(),
    cmd("edit.copyBounds", "Copy Selection Rectangle"),
    cmd("edit.pasteKeep", "Paste and Keep Selection"),
    sep(),
    cmd("other.options", "Options…"),
};

constexpr Entry kBackground[] = {
    cmd("canvas.background.specify", "Background Color…"),
    sep(),
    radio("canvas.background.white", "White"),
    radio("canvas.background.black", "Black"),
    radio("canvas.background.color", "Color"),
    radio("canvas.background.transparencyWhite", "Transparent (White)"),
    radio("canvas.background.transparencyBlack", "Transparent (Black)"),
    radio("canvas.background.transparencyColor", "Transparent (Color)"),
    radio("canvas.background.transparencyBrightChecker", "Transparent (Light Checker)"),
    radio("canvas.background.transparencyDarkChecker", "Transparent (Dark Checker)"),
};

constexpr Entry kCanvas[] = {
    sub("canvas.background", "Background", kBackground),
    sep(),
    cmd("canvas.trim", "Trim to Selection"),
    sep(),
    cmd("canvas.flipH", "Flip Horizontal"),
    cmd("canvas.flipV", "Flip Vertical"),
    cmd("canvas.rotateCCW", "Rotate 90° Left"),
    cmd("canvas.rotateCW", "Rotate 90° Right"),
    sep(),
    check("canvas.showTimeline", "Show Timeline", "Shift+T"),
    cmd("canvas.newFrame", "New Frame", "Shift+N"),
    cmd("canvas.duplicateFrame", "Duplicate Frame"),
    cmd("canvas.deleteFrame", "Delete Frame"),
    sep(),
    cmd("canvas.addVideo", "Add Video…"),
    cmd("canvas.removeVideo", "Remove Video"),
    sep(),
    cmd("canvas.keepOnlyFrame", "Keep Only This Frame…"),
};

constexpr Entry kLayer[] = {
    cmd("layer.newNormal", "New Layer", "Shift+Ctrl+N"),
    cmd("layer.newFolder", "New Folder"),
    cmd("layer.copy", "Duplicate Layer"),
    cmd("layer.delete", "Delete Layer"),
    sep(),
    cmd("layer.fill", "Fill", "Alt+Delete"),
    cmd("layer.erase", "Clear", "Delete"),
    cmd("layer.rasterize", "Rasterize"),
    sep(),
    cmd("layer.transferDown", "Move Painting to Layer Below", "F"),
    cmd("layer.merge", "Merge Layers", "Ctrl+E"),
    cmd("layer.mergeVisible", "Merge Visible Layers"),
    cmd("layer.flatten", "Flatten Image"),
    sep(),
    cmd("layer.transform", "Transform", "Ctrl+T"),
    cmd("layer.flipH", "Flip Horizontal"),
    cmd("layer.flipV", "Flip Vertical"),
    cmd("layer.rotateCCW", "Rotate 90° Left"),
    cmd("layer.rotateCW", "Rotate 90° Right"),
};

// The entries and the gaps between groups as Doodlebold's Effects menu has them,
// with this menu bar's "…" (no space before it).
constexpr Entry kEffects[] = {
    cmd("effects.brightnessContrast", "Brightness/Contrast…"),
    cmd("effects.hueSaturation", "Hue/Saturation/Lightness…", "Ctrl+U"),
    cmd("effects.levels", "Levels…"),
    cmd("effects.colorBalance", "Color Balance…"),
    cmd("effects.invert", "Invert"),
    cmd("effects.desaturate", "Desaturate"),
    cmd("effects.posterize", "Posterize…"),
    sep(),
    cmd("effects.gaussianBlur", "Gaussian Blur…"),
    cmd("effects.motionBlur", "Motion Blur…"),
    cmd("effects.radialBlur", "Radial Blur…"),
    sep(),
    cmd("effects.unsharpMask", "Sharpen/Unsharp Mask…"),
    cmd("effects.addNoise", "Add Noise…"),
    cmd("effects.chromaticAberration", "Chromatic Aberration…"),
};

constexpr Entry kSelection[] = {
    cmd("selection.all", "Select All", "Ctrl+A"),
    cmd("selection.deselect", "Deselect", "Ctrl+D"),
    cmd("selection.invert", "Invert"),
    sep(),
    cmd("selection.dilate", "Grow…"),
    cmd("selection.erode", "Shrink…"),
    cmd("selection.dilate1", "Grow by 1 Pixel"),
    cmd("selection.erode1", "Shrink by 1 Pixel"),
    sep(),
    check("selection.show", "Show Selection", "Ctrl+H"),
};

// The list of open views ("1 NewCanvas3 (100%)", the current one checked) follows these entries; the main window adds it every
// time the menu opens.
constexpr Entry kView[] = {
    cmd("view.zoomIn", "Zoom In"),
    cmd("view.zoomOut", "Zoom Out"),
    cmd("view.scale100", "Actual Size", "Ctrl+Alt+0"),
    cmd("view.fit", "Fit to Window", "Ctrl+0"),
    sep(),
    cmd("view.rotateCCW", "Rotate Left", "Shift+PageUp"),
    cmd("view.rotateCW", "Rotate Right", "Shift+PageDown"),
    cmd("view.resetAngle", "Reset Rotation", "Shift+Home"),
    check("view.flip", "Flip View", "H"),
    sep(),
    cmd("view.resetPosition", "Reset View", "Home"),
    sep(),
    cmd("view.close", "Close View", "Ctrl+W"),
    cmd("view.closeAll", "Close All Views"),
};

constexpr Entry kShowPanel[] = {
    check("window.show.toolPanel", "Tool Panel"),
    check("window.show.colorPanel", "Color Panel"),
    check("window.show.layerPanel", "Layer Panel"),
    check("window.show.navigatorPanel", "Navigator Panel"),
    sep(),
    check("window.show.quickBar", "Quick Bar"),
    check("window.show.statusBar", "Status Bar"),
    check("window.show.viewSelector", "View Selector"),
    sep(),
    check("window.show.brushPreview", "Brush Preview"),
    check("window.show.colorCircle", "Color Circle"),
    check("window.show.hsvSlider", "HSV/HSL Sliders"),
    check("window.show.rgbSlider", "RGB Sliders"),
    check("window.show.swatches", "Swatches"),
};
constexpr Entry kSeparatePanel[] = {
    check("window.separate.toolPanel", "Tool Panel"),
    check("window.separate.colorPanel", "Color Panel"),
    check("window.separate.layerPanel", "Layer Panel"),
    check("window.separate.navigatorPanel", "Navigator Panel"),
};
constexpr Entry kHsvMode[] = {
    radio("window.hsv.hsv", "HSV"),
    radio("window.hsv.hsl", "HSL"),
};
constexpr Entry kSwatchesSize[] = {
    radio("window.swatches.small", "Small"),
    radio("window.swatches.middle", "Medium"),
    radio("window.swatches.large", "Large"),
};

constexpr Entry kWindow[] = {
    sub("window.showPanel", "Show Panels", kShowPanel),
    sub("window.separatePanel", "Separate Panels", kSeparatePanel),
    check("window.colorToolRight", "Color and Tool Panels on the Right"),
    check("window.navigatorLayerRight", "Navigator and Layer Panels on the Right"),
    sep(),
    sub("window.hsvMode", "Color Sliders", kHsvMode),
    sub("window.swatchesSize", "Swatch Size", kSwatchesSize),
    sep(),
    check("window.fullScreen", "Full Screen", "F11, Shift+Tab"),
    check("window.showAll", "Show All Panels", "Tab"),
};

constexpr Entry kHelp[] = {
    cmd("other.tabletDiagnostics", "Tablet Diagnostics…"),
    cmd("other.reportBug", "Report a Bug…"),
    cmd("other.crashReport", "Crash Report…"),
    sep(),
    cmd("other.checkForUpdates", "Check for Updates…"),
    cmd("other.aboutDoodlebold", "About Doodlebold…"),
};

constexpr Menu kMenus[] = {
    {"File", kFile},           {"Edit", kEdit}, {"Canvas", kCanvas}, {"Layer", kLayer}, {"Effects", kEffects},
    {"Selection", kSelection}, {"View", kView}, {"Window", kWindow}, {"Help", kHelp},
};

// Commands with a key that are in no menu.
constexpr KeyCommand kKeyCommands[] = {
    {"tool.pencil", "Pencil", "N"},
    {"tool.brush", "Brush", "V"},
    {"tool.waterColor", "Water Color", "C"},
    {"tool.eraser", "Eraser", "E"},
    {"tool.selectionPen", "Selection Pen", "A"},
    {"tool.selectionEraser", "Selection Eraser", "S"},
    {"tool.marquee", "Marquee", "M"},
    {"tool.lasso", "Lasso", "L"},
    {"tool.wand", "Magic Wand", "W"},
    {"color.exchange", "Exchange Foreground and Background Color", "X"},
    {"color.transparent", "Transparent Color", "-"},
    {"brush.sizePrevious", "Previous Size in Brush Size List", "["},
    {"brush.sizeNext", "Next Size in Brush Size List", "]"},
    {"brush.density10", "Brush Density 10%", "1"},
    {"brush.density20", "Brush Density 20%", "2"},
    {"brush.density30", "Brush Density 30%", "3"},
    {"brush.density40", "Brush Density 40%", "4"},
    {"brush.density50", "Brush Density 50%", "5"},
    {"brush.density60", "Brush Density 60%", "6"},
    {"brush.density70", "Brush Density 70%", "7"},
    {"brush.density80", "Brush Density 80%", "8"},
    {"brush.density90", "Brush Density 90%", "9"},
    {"brush.density100", "Brush Density 100%", "0"},
    {"view.zoomInAtCursor", "Zoom In at Cursor Position", "PageUp, Ctrl+="},
    {"view.zoomOutAtCursor", "Zoom Out at Cursor Position", "PageDown, Ctrl+-"},
    {"view.next", "Select Next View", "Ctrl+Tab"},
    {"view.previous", "Select Previous View", "Shift+Ctrl+Tab"},
    {"layer.forward", "Bring Layer Forward", "Ctrl+["},
    {"layer.backward", "Send Layer Backward", "Ctrl+]"},
    {"layer.toFront", "Bring Layer to Front", "Shift+Ctrl+["},
    {"layer.toBack", "Send Layer to Back", "Shift+Ctrl+]"},
    {"layer.selectForward", "Select Forward Layer as Current", "Alt+["},
    {"layer.selectBackward", "Select Backward Layer as Current", "Alt+]"},
    {"layer.selectMask", "Select/Deselect Layer Mask", ""},
    {"animation.previousFrame", "Previous Frame", ","},
    {"animation.nextFrame", "Next Frame", "."},
    {"animation.play", "Play / Stop Animation", ""},
    // Every tool has a command, so a key can reach it once one is bound.
    {"tool.cloneStamp", "Clone Stamp", ""},  // no key until the user binds one
    {"tool.marker", "Marker", ""},
    {"tool.pixelArtPen", "Pixel Art Pen", ""},
    {"tool.bucket", "Bucket", ""},
    {"tool.blur", "Blur", ""},
    {"tool.smudge", "Smudge", ""},
    {"tool.text", "Text", ""},
    {"tool.move", "Move", ""},
    {"tool.zoom", "Zoom", ""},
    {"tool.rotate", "Rotate", ""},
    {"tool.hand", "Hand", ""},
    {"tool.eyedropper", "Eyedropper", ""},
};

QString text(std::string_view s) {
    return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size()));
}

// The tables write "PageUp"; Qt's portable name is "PgUp".
QList<QKeySequence> keys(std::string_view shortcuts) {
    QList<QKeySequence> out;
    for (QString one : text(shortcuts).split(QStringLiteral(", "), Qt::SkipEmptyParts)) {
        one.replace(QStringLiteral("PageUp"), QStringLiteral("PgUp")).replace(QStringLiteral("PageDown"), QStringLiteral("PgDown"));
        out << QKeySequence::fromString(one, QKeySequence::PortableText);
    }
    return out;
}

// One menu level. Separators are written only once something follows them and something came before, so a group whose
// entries Doodlebold lacks leaves no empty section behind.
void fill(QMenu* menu, std::span<const Entry> entries, const Binder& binder) {
    bool pendingSeparator = false;
    QActionGroup* radios = nullptr;
    const auto add = [&](QAction* a) {
        if (pendingSeparator && !menu->isEmpty())
            menu->addSeparator();
        pendingSeparator = false;
        menu->addAction(a);
    };
    for (const Entry& e : entries) {
        switch (e.kind) {
        case Kind::Separator:
            pendingSeparator = true;
            radios = nullptr;
            break;
        case Kind::Submenu: {
            auto* child = new QMenu(text(e.label), menu);
            const bool own = binder.populate && binder.populate(e, child);
            fill(child, e.submenu(), binder);
            if (!own && child->isEmpty()) {
                delete child;
                break;
            }
            if (pendingSeparator && !menu->isEmpty())
                menu->addSeparator();
            pendingSeparator = false;
            menu->addMenu(child);
            child->menuAction()->setObjectName(QString::fromUtf8(e.id.data(), static_cast<qsizetype>(e.id.size())));
            radios = nullptr;
            break;
        }
        default: {
            QAction* a = binder.action(e);
            if (!a)
                break;
            // The part of the text after the tab is what the menu shows as the shortcut: the table's words ("Shift+PageUp",
            // "F11, Shift+Tab"), where Qt would print "Shift+PgUp" and only the first key.
            a->setText(e.shortcuts.empty() ? text(e.label) : text(e.label) + QLatin1Char('\t') + text(e.shortcuts));
            a->setShortcuts(keys(e.shortcuts));
            a->setCheckable(e.kind != Kind::Command);
            a->setIconVisibleInMenu(false);  // the menus have no icons; the quick bar shares these actions
            if (e.kind == Kind::Radio) {
                if (!radios)
                    radios = new QActionGroup(menu);
                radios->addAction(a);
            }
            add(a);
            break;
        }
        }
    }
}

}

std::span<const Menu> allMenus() {
    return kMenus;
}

std::span<const KeyCommand> allKeyCommands() {
    return kKeyCommands;
}

namespace {

QString keyGroup(std::string_view id) {
    const QString prefix = text(id).section(QLatin1Char('.'), 0, 0);
    if (prefix == QLatin1String("tool"))
        return QStringLiteral("Tools");
    if (prefix == QLatin1String("color"))
        return QStringLiteral("Color");
    if (prefix == QLatin1String("brush"))
        return QStringLiteral("Brush");
    if (prefix == QLatin1String("layer"))
        return QStringLiteral("Layer");
    return QStringLiteral("View");
}

QString plainLabel(std::string_view label) {
    QString t = text(label);
    if (t.endsWith(QChar(0x2026)))
        t.chop(1);
    return t;
}

bool listed(const std::vector<CommandInfo>& out, std::string_view id) {
    const QString key = text(id);
    return std::ranges::any_of(out, [&key](const CommandInfo& c) { return c.id == key; });
}

void collect(std::span<const Entry> entries, const QString& group, const QString& prefix, std::vector<CommandInfo>& out) {
    for (const Entry& e : entries) {
        if (e.kind == Kind::Separator || e.id.empty())
            continue;
        if (e.kind == Kind::Submenu) {
            collect(e.submenu(), group, prefix + plainLabel(e.label) + QStringLiteral(": "), out);
            continue;
        }
        if (!listed(out, e.id))
            out.push_back({text(e.id), group, prefix + plainLabel(e.label), keys(e.shortcuts)});
    }
}

const Entry* findEntry(std::span<const Entry> entries, const QString& id) {
    for (const Entry& e : entries) {
        if (text(e.id) == id && e.kind != Kind::Submenu)
            return &e;
        if (const Entry* inside = e.kind == Kind::Submenu ? findEntry(e.submenu(), id) : nullptr)
            return inside;
    }
    return nullptr;
}

std::string_view tableKeys(const QString& id) {
    for (const Menu& m : kMenus)
        if (const Entry* e = findEntry(m.entries, id))
            return e->shortcuts;
    for (const KeyCommand& k : kKeyCommands)
        if (text(k.id) == id)
            return k.shortcuts;
    return {};
}

}

std::vector<CommandInfo> allCommands() {
    std::vector<CommandInfo> out;
    for (const Menu& m : kMenus)
        collect(m.entries, text(m.title), {}, out);
    for (const KeyCommand& k : kKeyCommands)
        if (!listed(out, k.id))
            out.push_back({text(k.id), keyGroup(k.id), plainLabel(k.label), keys(k.shortcuts)});
    return out;
}

void setKeys(QAction* action, const QString& id, const QList<QKeySequence>& shortcuts) {
    const std::string_view table = tableKeys(id);
    const QString label = action->text().section(QLatin1Char('\t'), 0, 0);
    const bool asTable = shortcuts == keys(table);
    QStringList shown;
    for (const QKeySequence& k : shortcuts)
        shown << k.toString(QKeySequence::NativeText);
    const QString keyText = asTable ? text(table) : shown.join(QStringLiteral(", "));
    action->setShortcuts(shortcuts);
    action->setText(keyText.isEmpty() ? label : label + QLatin1Char('\t') + keyText);
}

void build(QMenuBar* bar, QWidget* window, const Binder& binder) {
    for (const Menu& m : kMenus) {
        auto* menu = new QMenu(text(m.title), bar);
        fill(menu, m.entries, binder);
        if (menu->isEmpty()) {
            delete menu;  // a menu with nothing bound is left out
            continue;
        }
        menu->menuAction()->setObjectName(text(m.title));
        bar->addMenu(menu);
    }
    for (const KeyCommand& k : kKeyCommands)
        if (QAction* a = binder.keyAction(k)) {
            a->setText(text(k.label));
            a->setShortcuts(keys(k.shortcuts));
            window->addAction(a);
        }
}

}
