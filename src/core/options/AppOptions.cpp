#include "core/options/AppOptions.h"

#include "core/paint/StrokeMemory.h"
#include "core/util/AppIdentity.h"

#include <algorithm>
#include <cmath>

namespace doodlebold {

namespace {

constexpr const char* kMinimumPressure = "options/minimumPressure";
constexpr const char* kFullPressure = "options/fullPressure";
// The processor-cores option, now removed: its saved values are deleted.
constexpr const char* kLimitCpuCores = "options/limitCpuCores";
constexpr const char* kCpuCores = "options/cpuCores";
constexpr const char* kFitOnResetPosition = "options/fitOnResetPosition";
constexpr const char* kSwapWheelZoom = "options/swapWheelZoom";
constexpr const char* kWheelZoomAtCentre = "options/wheelZoomAtCentre";
constexpr const char* kRecordNewCanvases = "options/recordNewCanvases";
constexpr const char* kQuickShapes = "options/quickShapes";
constexpr const char* kQuickShapeHoldMs = "options/quickShapeHoldMs";
constexpr const char* kAltPicksColour = "options/altPicksColour";
constexpr const char* kCtrlMovesLayers = "options/ctrlMovesLayers";
constexpr const char* kFrostedMenus = "options/frostedMenus";
constexpr const char* kFastZoomedOut = "options/fastZoomedOut";
constexpr const char* kUndoSteps = "options/undoSteps";
constexpr const char* kBrushOutline = "options/brushOutline";
constexpr const char* kStrokeTiming = "options/strokeTiming";
constexpr const char* kPanKey = "options/panKey";
constexpr const char* kCustomMemory = "options/customMemory";
constexpr const char* kCustomMemoryGB = "options/customMemoryGB";
constexpr const char* kKeepBrushesInMemory = "options/keepBrushesInMemory";
constexpr const char* kCheckForUpdatesAtStart = "options/checkForUpdatesAtStart";
constexpr double kBytesPerGB = 1024.0 * 1024.0 * 1024.0;
constexpr double kLeastMemoryGB = 1.0;
constexpr double kMemoryStepGB = 0.5;

struct HeldModifierKey {
    const char* key;
    Qt::KeyboardModifier HeldKeys::* member;
};
constexpr HeldModifierKey kHeldModifiers[] = {
    {"options/zoomKey", &HeldKeys::zoom},
    {"options/rotateKey", &HeldKeys::rotate},
    {"options/pickKey", &HeldKeys::pick},
    {"options/moveKey", &HeldKeys::move},
};

// Saved by name, so the file reads as the dialog does.
QString modifierName(Qt::KeyboardModifier m) {
    return m == Qt::ControlModifier ? QStringLiteral("Ctrl") : m == Qt::AltModifier ? QStringLiteral("Alt") : QStringLiteral("Shift");
}

Qt::KeyboardModifier modifierNamed(const QString& name, Qt::KeyboardModifier fallback) {
    for (const Qt::KeyboardModifier m : {Qt::ControlModifier, Qt::AltModifier, Qt::ShiftModifier})
        if (name == modifierName(m))
            return m;
    return fallback;
}

Qt::KeyboardModifier HeldKeys::* partnerOf(Qt::KeyboardModifier HeldKeys::* member) {
    if (member == &HeldKeys::zoom)
        return &HeldKeys::rotate;
    if (member == &HeldKeys::rotate)
        return &HeldKeys::zoom;
    return member == &HeldKeys::pick ? &HeldKeys::move : &HeldKeys::pick;
}

// A pan key with modifiers, or a modifier key itself, can't be held on its own; a pair sharing a modifier is set apart.
HeldKeys clampedHeld(HeldKeys h) {
    const HeldKeys defaults;
    if (h.pan == 0 || h.pan == Qt::Key_Control || h.pan == Qt::Key_Alt || h.pan == Qt::Key_Shift || h.pan == Qt::Key_Meta ||
        (h.pan & int(Qt::KeyboardModifierMask)))
        h.pan = defaults.pan;
    for (const HeldModifierKey& f : kHeldModifiers)
        if (!HeldKeys::isChoice(h.*f.member))
            h.*f.member = defaults.*f.member;
    if (h.zoom == h.rotate)
        h.zoom = h.rotate == defaults.zoom ? defaults.rotate : defaults.zoom;
    if (h.pick == h.move)
        h.pick = h.move == defaults.pick ? defaults.move : defaults.pick;
    return h;
}

}

bool HeldKeys::isChoice(Qt::KeyboardModifier m) {
    return m == Qt::ControlModifier || m == Qt::AltModifier || m == Qt::ShiftModifier;
}

int HeldKeys::keyOf(Qt::KeyboardModifier m) {
    return m == Qt::ControlModifier ? Qt::Key_Control : m == Qt::AltModifier ? Qt::Key_Alt : Qt::Key_Shift;
}

HeldKeys swappedHeldKey(HeldKeys keys, Qt::KeyboardModifier HeldKeys::* member, Qt::KeyboardModifier m) {
    Qt::KeyboardModifier HeldKeys::* partner = partnerOf(member);
    if (keys.*partner == m)
        keys.*partner = keys.*member;
    keys.*member = m;
    return keys;
}

QSettings AppOptions::settings() {
    return app_identity::settings();
}

AppOptions::Values AppOptions::clamped(Values v) {
    v.minimumPressure = std::clamp(v.minimumPressure, 0, 100);
    v.fullPressure = std::clamp(v.fullPressure, 1, 100);
    v.quickShapeHoldMs = std::clamp(v.quickShapeHoldMs, 300, 3000);
    v.undoSteps = std::clamp(v.undoSteps, kLeastUndoSteps, kMostUndoSteps);
    v.held = clampedHeld(v.held);
    if (v.customMemoryGB > 0.0) {
        const double most = std::max(kLeastMemoryGB, std::floor(machineMemoryGB() / kMemoryStepGB) * kMemoryStepGB);
        v.customMemoryGB = std::clamp(std::round(v.customMemoryGB / kMemoryStepGB) * kMemoryStepGB, kLeastMemoryGB, most);
    }
    return v;
}

double AppOptions::machineMemoryGB() {
    return double(stroke_memory::machineBytes()) / kBytesPerGB;
}

double AppOptions::automaticMemoryGB() {
    return double(stroke_memory::automaticBytes()) / kBytesPerGB;
}

std::int64_t AppOptions::memoryBytes(const Values& v) {
    return v.customMemory && v.customMemoryGB > 0.0 ? std::int64_t(v.customMemoryGB * kBytesPerGB) : 0;
}

AppOptions::AppOptions(QObject* parent) : QObject(parent) {
    QSettings s = settings();
    s.remove(QLatin1String(kLimitCpuCores));
    s.remove(QLatin1String(kCpuCores));
    Values v;
    v.minimumPressure = s.value(QLatin1String(kMinimumPressure), v.minimumPressure).toInt();
    v.fullPressure = s.value(QLatin1String(kFullPressure), v.fullPressure).toInt();
    v.fitOnResetPosition = s.value(QLatin1String(kFitOnResetPosition), v.fitOnResetPosition).toBool();
    v.swapWheelZoom = s.value(QLatin1String(kSwapWheelZoom), v.swapWheelZoom).toBool();
    v.wheelZoomAtCentre = s.value(QLatin1String(kWheelZoomAtCentre), v.wheelZoomAtCentre).toBool();
    v.recordNewCanvases = s.value(QLatin1String(kRecordNewCanvases), v.recordNewCanvases).toBool();
    v.quickShapes = s.value(QLatin1String(kQuickShapes), v.quickShapes).toBool();
    v.quickShapeHoldMs = s.value(QLatin1String(kQuickShapeHoldMs), v.quickShapeHoldMs).toInt();
    v.altPicksColour = s.value(QLatin1String(kAltPicksColour), v.altPicksColour).toBool();
    v.ctrlMovesLayers = s.value(QLatin1String(kCtrlMovesLayers), v.ctrlMovesLayers).toBool();
    v.frostedMenus = s.value(QLatin1String(kFrostedMenus), v.frostedMenus).toBool();
    if (s.contains(QLatin1String(kFastZoomedOut)))
        v.zoomedOut = s.value(QLatin1String(kFastZoomedOut)).toBool() ? ZoomedOut::Fast : ZoomedOut::Smooth;
    v.undoSteps = s.value(QLatin1String(kUndoSteps), v.undoSteps).toInt();
    v.brushOutline = s.value(QLatin1String(kBrushOutline), v.brushOutline).toBool();
    v.strokeTiming = s.value(QLatin1String(kStrokeTiming), v.strokeTiming).toBool();
    v.customMemory = s.value(QLatin1String(kCustomMemory), v.customMemory).toBool();
    v.customMemoryGB = s.value(QLatin1String(kCustomMemoryGB), v.customMemoryGB).toDouble();
    v.keepBrushesInMemory = s.value(QLatin1String(kKeepBrushesInMemory), v.keepBrushesInMemory).toBool();
    v.checkForUpdatesAtStart = s.value(QLatin1String(kCheckForUpdatesAtStart), v.checkForUpdatesAtStart).toBool();
    v.held.pan = s.value(QLatin1String(kPanKey), v.held.pan).toInt();
    for (const HeldModifierKey& f : kHeldModifiers)
        v.held.*f.member = modifierNamed(s.value(QLatin1String(f.key)).toString(), v.held.*f.member);
    m_values = clamped(v);
}

void AppOptions::setValues(const Values& values) {
    const Values v = clamped(values);
    if (v == m_values)
        return;
    m_values = v;
    QSettings s = settings();
    s.setValue(QLatin1String(kMinimumPressure), v.minimumPressure);
    s.setValue(QLatin1String(kFullPressure), v.fullPressure);
    s.setValue(QLatin1String(kFitOnResetPosition), v.fitOnResetPosition);
    s.setValue(QLatin1String(kSwapWheelZoom), v.swapWheelZoom);
    s.setValue(QLatin1String(kWheelZoomAtCentre), v.wheelZoomAtCentre);
    s.setValue(QLatin1String(kRecordNewCanvases), v.recordNewCanvases);
    s.setValue(QLatin1String(kQuickShapes), v.quickShapes);
    s.setValue(QLatin1String(kQuickShapeHoldMs), v.quickShapeHoldMs);
    s.setValue(QLatin1String(kAltPicksColour), v.altPicksColour);
    s.setValue(QLatin1String(kCtrlMovesLayers), v.ctrlMovesLayers);
    s.setValue(QLatin1String(kFrostedMenus), v.frostedMenus);
    if (v.zoomedOut == ZoomedOut::ByGraphics)
        s.remove(QLatin1String(kFastZoomedOut));  // nothing picked: the graphics found next time decide
    else
        s.setValue(QLatin1String(kFastZoomedOut), v.zoomedOut == ZoomedOut::Fast);
    s.setValue(QLatin1String(kUndoSteps), v.undoSteps);
    s.setValue(QLatin1String(kBrushOutline), v.brushOutline);
    s.setValue(QLatin1String(kStrokeTiming), v.strokeTiming);
    s.setValue(QLatin1String(kCustomMemory), v.customMemory);
    s.setValue(QLatin1String(kCustomMemoryGB), v.customMemoryGB);
    s.setValue(QLatin1String(kKeepBrushesInMemory), v.keepBrushesInMemory);
    s.setValue(QLatin1String(kCheckForUpdatesAtStart), v.checkForUpdatesAtStart);
    s.setValue(QLatin1String(kPanKey), v.held.pan);
    for (const HeldModifierKey& f : kHeldModifiers)
        s.setValue(QLatin1String(f.key), modifierName(v.held.*f.member));
    emit changed();
}

}
