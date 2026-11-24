#pragma once

#include "core/layers/CanvasBackground.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QSize>
#include <QString>

#include <optional>
#include <vector>

namespace doodlebold {

// The numbers behind the File → New dialog: units, printing resolution, the "Pixels" and "Printing Metrics" read-outs, the
// preset list. No widgets.

enum class SizeUnit {
    Pixels,
    Inches,
    Centimeters,
    Millimeters
};
enum class ResolutionUnit {
    PerInch,
    PerCentimeter
};

QString sizeUnitLabel(SizeUnit unit);
QString resolutionUnitLabel(ResolutionUnit unit);
QString sizeUnitKey(SizeUnit unit);
QString resolutionUnitKey(ResolutionUnit unit);
std::optional<SizeUnit> sizeUnitFromKey(const QString& key);
std::optional<ResolutionUnit> resolutionUnitFromKey(const QString& key);

// One preset-list entry: a canvas size with unit and resolution, optionally a background (the "[None]" entry of Register New
// Canvas Preset leaves the dialog's background alone), or a separator.
struct CanvasPreset {
    QString name;
    bool separator = false;
    double width = 0, height = 0;  // in `sizeUnit`
    SizeUnit sizeUnit = SizeUnit::Pixels;
    double resolution = 72.0;      // in `resolutionUnit`
    ResolutionUnit resolutionUnit = ResolutionUnit::PerInch;
    std::optional<CanvasBackground> background;

    bool operator==(const CanvasPreset&) const = default;
    static CanvasPreset makeSeparator() {
        CanvasPreset p;
        p.separator = true;
        return p;
    }
};
using CanvasPresetList = std::vector<CanvasPreset>;

CanvasPresetList defaultCanvasPresets();

QJsonArray presetsToJson(const CanvasPresetList& presets);
// Empty on anything that is not a list of valid presets (a damaged settings file falls back to the defaults).
std::optional<CanvasPresetList> presetsFromJson(const QJsonArray& json);

// What the dialog remembers between uses (size, units, resolution and background come back as they were left).
struct NewCanvasValues {
    QSize pixels{2000, 1500};  // first-run default
    double resolution = 72.0;
    SizeUnit sizeUnit = SizeUnit::Pixels;
    ResolutionUnit resolutionUnit = ResolutionUnit::PerInch;
    CanvasBackground background;

    bool operator==(const NewCanvasValues&) const = default;
};

struct NewCanvasAnswer {
    QString fileName;
    NewCanvasValues values;
};
QJsonObject newCanvasValuesToJson(const NewCanvasValues& values);
NewCanvasValues newCanvasValuesFromJson(const QJsonObject& o);  // anything missing or invalid keeps its default

// Pixels ↔ units: mm is value / 25.4 · ppi, rounded to the nearest pixel (182 mm at 300 ppi
// is 2150, not 2149) and kept within 1…kMaxCanvasSide.
int toPixels(double value, SizeUnit unit, double ppi);
double fromPixels(int pixels, SizeUnit unit, double ppi);
// A value as the dialog shows it: 0 decimals for pixels, 3 for inches, 2 for cm, 1 for mm, trailing zeros dropped ("210",
// "84.7").
QString formatLength(double value, SizeUnit unit);
QString formatResolution(double value);  // one decimal, trailing zero dropped: "72", "137.8"

class NewCanvasModel {
public:
    explicit NewCanvasModel(const NewCanvasValues& values = {});

    NewCanvasValues values() const;
    QSize pixels() const { return {m_pixelsW, m_pixelsH}; }
    double resolutionPpi() const { return m_ppi; }
    SizeUnit sizeUnit() const { return m_sizeUnit; }
    ResolutionUnit resolutionUnit() const { return m_resolutionUnit; }
    const CanvasBackground& background() const { return m_background; }

    double width() const { return m_width; }
    double height() const { return m_height; }
    double resolutionValue() const;

    // Every edit resets the preset combo to "----" (activePreset() = -1). Width / height: the pixel size follows.
    // Size unit: the fields show the same pixel size in the new unit. Resolution value: with pixels as the size unit the pixel
    // size is kept; with a physical unit the printing size is kept and the pixel size follows. Resolution unit: the value is
    // converted, nothing else changes.
    void setWidth(double value);
    void setHeight(double value);
    void setSizeUnit(SizeUnit unit);
    void setResolutionValue(double value);
    void setResolutionUnit(ResolutionUnit unit);
    void setBackground(const CanvasBackground& background);  // the Background combo and the swatch; not a preset edit

    void applyPreset(const CanvasPreset& preset, int index);
    int activePreset() const { return m_activePreset; }

    // The width or height asked for, in pixels, was beyond kMaxCanvasSide, and the pixel size was limited to it.
    bool limited() const { return m_widthLimited || m_heightLimited; }

    QString pixelsText() const;
    // "45.2mm × 33.9mm (72 pixels/inch)": the printing size in the chosen unit (millimetres while the unit is pixels).
    QString metricsText() const;

private:
    void refreshFields();
    double fieldFor(double value, int pixels) const;
    bool beyondLimit(double value) const;

    int m_pixelsW = 1, m_pixelsH = 1;
    double m_ppi = 72.0;
    SizeUnit m_sizeUnit = SizeUnit::Pixels;
    ResolutionUnit m_resolutionUnit = ResolutionUnit::PerInch;
    CanvasBackground m_background;
    double m_width = 1, m_height = 1;
    int m_activePreset = -1;
    bool m_widthLimited = false, m_heightLimited = false;
};

}
