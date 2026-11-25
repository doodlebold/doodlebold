#pragma once

#include <QObject>
#include <QSettings>

#include <cstdint>

namespace doodlebold {

// The keys held on the canvas (Options › Keys). Zoom and Rotate are held with the pan key. Zoom and Rotate never share a
// modifier, nor do Pick and Move: choosing one for both swaps them (swappedHeldKey).
struct HeldKeys {
    int pan = Qt::Key_Space;  // a key without modifiers
    Qt::KeyboardModifier zoom = Qt::ControlModifier;
    Qt::KeyboardModifier rotate = Qt::AltModifier;
    Qt::KeyboardModifier pick = Qt::AltModifier;
    Qt::KeyboardModifier move = Qt::ControlModifier;
    bool operator==(const HeldKeys&) const = default;

    // The modifiers a held key may be: Ctrl, Alt and Shift.
    static bool isChoice(Qt::KeyboardModifier m);
    // The key that holds modifier `m` (Qt::Key_Control for Ctrl …).
    static int keyOf(Qt::KeyboardModifier m);
    // Every modifier used here.
    Qt::KeyboardModifiers modifiers() const { return zoom | rotate | pick | move; }
};

// `keys` with `member` set to `m`: the other of its pair (Zoom and Rotate, Pick and Move) takes the old one if it had `m`.
HeldKeys swappedHeldKey(HeldKeys keys, Qt::KeyboardModifier HeldKeys::* member, Qt::KeyboardModifier m);

// The options Doodlebold honours; the code an option steers listens to changed().
class AppOptions : public QObject {
    Q_OBJECT
public:
    // How the canvas is drawn zoomed out: Smooth or Fast as the user picked, or, until they pick, by the graphics: Fast on
    // software graphics, which have no card to smooth it quickly, Smooth on a graphics card.
    enum class ZoomedOut {
        ByGraphics,
        Smooth,
        Fast
    };

    struct Values {
        int minimumPressure = 0;  // %, 0 … 100: pen pressure below it is ignored
        // %, 1 … 100: pen pressure at it and above counts as full. It starts at 100, a straight response, as the reference's pen does:
        // at 80 the Brush on the recorded C laid 543,132 px through the window against 491,883 at 100 and the reference's
        // 493,787, since every pressure past 0.8 counted as full and the rest a quarter firmer.
        int fullPressure = 100;
        bool fitOnResetPosition = false;
        bool swapWheelZoom = false;
        bool wheelZoomAtCentre = false;
        bool quickShapes = true;  // a stroke held still turns into a shape
        int quickShapeHoldMs = 1000;  // how long it is held, 300 … 3000
        bool altPicksColour = true;  // held.pick is the eyedropper while held; off, it's nothing
        bool ctrlMovesLayers = true;  // held.move moves the selected layers while held; off, it's nothing
        HeldKeys held;
        bool recordNewCanvases = false;  // every new or opened picture starts a timelapse recording
        // Options > Performance. None of them changes how a stroke looks.
        bool frostedMenus = true;  // menus and popups blur what's behind them, where the desktop can
        ZoomedOut zoomedOut = ZoomedOut::ByGraphics;  // Fast: zoomed out, without the smoothing that weak graphics find slow
        int undoSteps = kDefaultUndoSteps;  // how many steps Undo keeps, kLeastUndoSteps … kMostUndoSteps
        bool brushOutline = true;  // the brush's outline is drawn under the pen
        bool strokeTiming = false;  // the canvas shows how long the pen events of a stroke took to paint
        // Options › Performance › Memory: what Doodlebold may use, the machine's amount (Automatic) or the user's own, and
        // whether a brush too large for it is kept to the largest size that fits.
        bool customMemory = false;
        double customMemoryGB = 0.0;  // 0 until one is set: the dialog starts it at Automatic's amount
        bool keepBrushesInMemory = true;
        bool checkForUpdatesAtStart = true;  // at most once a day, Doodlebold asks GitHub whether a newer release exists
        // Options › Recovery: copies of the canvases with unsaved changes, kept so a crash loses little work.
        bool saveRecoveryCopies = true;
        int recoveryMinutes = kDefaultRecoveryMinutes;  // how often, kLeastRecoveryMinutes … kMostRecoveryMinutes
        int recoveryCopiesKept = kDefaultRecoveryCopiesKept;  // of each canvas, kLeastRecoveryCopiesKept … kMostRecoveryCopiesKept
        QString recoveryFolder;  // empty until clamped, which gives defaultRecoveryFolder()
        bool operator==(const Values&) const = default;
    };

    static constexpr int kLeastUndoSteps = 10;
    static constexpr int kMostUndoSteps = 1000;
    static constexpr int kDefaultUndoSteps = 25;
    // Minutes between recovery copies. 5 loses at most five minutes of painting to a crash while writing a large canvas (a few
    // hundred MB) seldom enough not to be felt. The setting is in whole minutes, so 1 is its least; past 60 a crash could lose
    // a whole session's painting.
    static constexpr int kDefaultRecoveryMinutes = 5;
    static constexpr int kLeastRecoveryMinutes = 1;
    static constexpr int kMostRecoveryMinutes = 60;
    // Copies kept of each canvas. 3 leaves two older ones if the newest can't be read, for three times the canvas's size on the
    // drive; 20 reaches back 100 minutes at the default interval, and more would mostly fill the drive.
    static constexpr int kDefaultRecoveryCopiesKept = 3;
    static constexpr int kLeastRecoveryCopiesKept = 1;
    static constexpr int kMostRecoveryCopiesKept = 20;

    explicit AppOptions(QObject* parent = nullptr);

    const Values& values() const { return m_values; }
    void setValues(const Values& values);

    static QSettings settings();
    static Values clamped(Values values);
    // The machine's memory and what Automatic allows, in GB (of 2^30 bytes), and the amount `values` lets painting use, in bytes.
    static double machineMemoryGB();
    static double automaticMemoryGB();
    static std::int64_t memoryBytes(const Values& values);
    // A "recovery" folder in the app's local data folder.
    static QString defaultRecoveryFolder();

signals:
    void changed();

private:
    Values m_values;
};

}
