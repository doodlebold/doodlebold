#include "core/document/NewCanvas.h"

#include "core/io/DocumentFormat.h"

#include <QCoreApplication>
#include <QJsonValue>

#include <algorithm>
#include <cmath>

namespace doodlebold {

namespace {

constexpr double kMmPerInch = 25.4;
constexpr double kCmPerInch = 2.54;
constexpr double kMinPpi = 1.0, kMaxPpi = 10000.0;

double unitsPerInch(SizeUnit unit) {
    switch (unit) {
    case SizeUnit::Inches: return 1.0;
    case SizeUnit::Centimeters: return kCmPerInch;
    case SizeUnit::Millimeters: return kMmPerInch;
    case SizeUnit::Pixels: break;
    }
    return 1.0;
}

int decimals(SizeUnit unit) {
    switch (unit) {
    case SizeUnit::Pixels: return 0;
    case SizeUnit::Inches: return 3;
    case SizeUnit::Centimeters: return 2;
    case SizeUnit::Millimeters: return 1;
    }
    return 0;
}

// "210.0" → "210", "84.70" → "84.7"
QString trimmed(double value, int digits) {
    QString s = QString::number(value, 'f', digits);
    if (digits > 0) {
        while (s.endsWith(QLatin1Char('0')))
            s.chop(1);
        if (s.endsWith(QLatin1Char('.')))
            s.chop(1);
    }
    return s;
}

QString unitSuffix(SizeUnit unit) {
    switch (unit) {
    case SizeUnit::Inches: return QStringLiteral("in");
    case SizeUnit::Centimeters: return QStringLiteral("cm");
    case SizeUnit::Pixels:
    case SizeUnit::Millimeters: break;
    }
    return QStringLiteral("mm");
}

}

QString sizeUnitLabel(SizeUnit unit) {
    switch (unit) {
    case SizeUnit::Pixels: return QStringLiteral("pixels");
    case SizeUnit::Inches: return QStringLiteral("inches");
    case SizeUnit::Centimeters: return QStringLiteral("cm");
    case SizeUnit::Millimeters: return QStringLiteral("mm");
    }
    return {};
}

QString resolutionUnitLabel(ResolutionUnit unit) {
    return unit == ResolutionUnit::PerInch ? QStringLiteral("pixels/inch") : QStringLiteral("pixels/cm");
}

QString sizeUnitKey(SizeUnit unit) {
    return sizeUnitLabel(unit);
}

QString resolutionUnitKey(ResolutionUnit unit) {
    return unit == ResolutionUnit::PerInch ? QStringLiteral("ppi") : QStringLiteral("ppcm");
}

std::optional<SizeUnit> sizeUnitFromKey(const QString& key) {
    for (const SizeUnit u : {SizeUnit::Pixels, SizeUnit::Inches, SizeUnit::Centimeters, SizeUnit::Millimeters})
        if (key == sizeUnitKey(u))
            return u;
    return std::nullopt;
}

std::optional<ResolutionUnit> resolutionUnitFromKey(const QString& key) {
    for (const ResolutionUnit u : {ResolutionUnit::PerInch, ResolutionUnit::PerCentimeter})
        if (key == resolutionUnitKey(u))
            return u;
    return std::nullopt;
}

CanvasPresetList defaultCanvasPresets() {
    const auto make = [](const char* name, double w, double h, SizeUnit unit, double ppi) {
        CanvasPreset p;
        p.name = QString::fromUtf8(name);
        p.width = w;
        p.height = h;
        p.sizeUnit = unit;
        p.resolution = ppi;
        return p;
    };
    return {make("512 × 512 - 72ppi", 512, 512, SizeUnit::Pixels, 72),
            make("1024 × 1024 - 72ppi", 1024, 1024, SizeUnit::Pixels, 72),
            CanvasPreset::makeSeparator(),
            make("A5 - 300ppi", 148, 210, SizeUnit::Millimeters, 300),
            make("A5 - 350ppi", 148, 210, SizeUnit::Millimeters, 350),
            CanvasPreset::makeSeparator(),
            make("A4 - 300ppi", 210, 297, SizeUnit::Millimeters, 300),
            make("A4 - 350ppi", 210, 297, SizeUnit::Millimeters, 350)};
}

QJsonArray presetsToJson(const CanvasPresetList& presets) {
    QJsonArray out;
    for (const CanvasPreset& p : presets) {
        QJsonObject o;
        if (p.separator) {
            o[QLatin1String("separator")] = true;
        } else {
            o[QLatin1String("name")] = p.name;
            o[QLatin1String("width")] = p.width;
            o[QLatin1String("height")] = p.height;
            o[QLatin1String("sizeUnit")] = sizeUnitKey(p.sizeUnit);
            o[QLatin1String("resolution")] = p.resolution;
            o[QLatin1String("resolutionUnit")] = resolutionUnitKey(p.resolutionUnit);
            if (p.background) {
                o[QLatin1String("background")] = backgroundModeKey(p.background->mode);
                o[QLatin1String("color")] = p.background->color.name(QColor::HexRgb);
            }
        }
        out.append(o);
    }
    return out;
}

std::optional<CanvasPresetList> presetsFromJson(const QJsonArray& json) {
    CanvasPresetList out;
    for (const auto& v : json) {
        if (!v.isObject())
            return std::nullopt;
        const QJsonObject o = v.toObject();
        if (o.value(QLatin1String("separator")).toBool(false)) {
            out.push_back(CanvasPreset::makeSeparator());
            continue;
        }
        CanvasPreset p;
        p.name = o.value(QLatin1String("name")).toString();
        p.width = o.value(QLatin1String("width")).toDouble(-1);
        p.height = o.value(QLatin1String("height")).toDouble(-1);
        p.resolution = o.value(QLatin1String("resolution")).toDouble(-1);
        const auto unit = sizeUnitFromKey(o.value(QLatin1String("sizeUnit")).toString());
        const auto resUnit = resolutionUnitFromKey(o.value(QLatin1String("resolutionUnit")).toString());
        if (p.name.isEmpty() || !unit || !resUnit || !(p.width > 0) || !(p.height > 0) || !(p.resolution > 0))
            return std::nullopt;
        p.sizeUnit = *unit;
        p.resolutionUnit = *resUnit;
        if (o.contains(QLatin1String("background"))) {
            const auto mode = backgroundModeFromKey(o.value(QLatin1String("background")).toString());
            const QColor color(o.value(QLatin1String("color")).toString());
            if (!mode || !color.isValid())
                return std::nullopt;
            p.background = CanvasBackground{*mode, color};
        }
        out.push_back(p);
    }
    return out;
}

int toPixels(double value, SizeUnit unit, double ppi) {
    const double px = unit == SizeUnit::Pixels ? value : value / unitsPerInch(unit) * ppi;
    if (!(px >= 1.0))
        return 1;
    return static_cast<int>(std::min(std::round(px), double(kMaxCanvasSide)));
}

double fromPixels(int pixels, SizeUnit unit, double ppi) {
    return unit == SizeUnit::Pixels ? pixels : pixels / ppi * unitsPerInch(unit);
}

QString formatLength(double value, SizeUnit unit) {
    return trimmed(value, decimals(unit));
}

QString formatResolution(double value) {
    return trimmed(value, 1);
}

QJsonObject newCanvasValuesToJson(const NewCanvasValues& v) {
    QJsonObject o;
    o[QLatin1String("width")] = v.pixels.width();
    o[QLatin1String("height")] = v.pixels.height();
    o[QLatin1String("resolution")] = v.resolution;
    o[QLatin1String("sizeUnit")] = sizeUnitKey(v.sizeUnit);
    o[QLatin1String("resolutionUnit")] = resolutionUnitKey(v.resolutionUnit);
    o[QLatin1String("background")] = backgroundModeKey(v.background.mode);
    o[QLatin1String("color")] = v.background.color.name(QColor::HexRgb);
    return o;
}

NewCanvasValues newCanvasValuesFromJson(const QJsonObject& o) {
    NewCanvasValues v;
    const int w = o.value(QLatin1String("width")).toInt(0), h = o.value(QLatin1String("height")).toInt(0);
    if (w >= 1 && w <= kMaxCanvasSide && h >= 1 && h <= kMaxCanvasSide)
        v.pixels = QSize(w, h);
    const double res = o.value(QLatin1String("resolution")).toDouble(0);
    if (res >= kMinPpi && res <= kMaxPpi)
        v.resolution = res;
    if (const auto u = sizeUnitFromKey(o.value(QLatin1String("sizeUnit")).toString()))
        v.sizeUnit = *u;
    if (const auto u = resolutionUnitFromKey(o.value(QLatin1String("resolutionUnit")).toString()))
        v.resolutionUnit = *u;
    if (const auto m = backgroundModeFromKey(o.value(QLatin1String("background")).toString()))
        v.background.mode = *m;
    if (const QColor c(o.value(QLatin1String("color")).toString()); c.isValid())
        v.background.color = QColor(c.red(), c.green(), c.blue());
    return v;
}

NewCanvasModel::NewCanvasModel(const NewCanvasValues& values)
    : m_pixelsW(std::clamp(values.pixels.width(), 1, kMaxCanvasSide)),
      m_pixelsH(std::clamp(values.pixels.height(), 1, kMaxCanvasSide)),
      m_ppi(std::clamp(values.resolution, kMinPpi, kMaxPpi)),
      m_sizeUnit(values.sizeUnit),
      m_resolutionUnit(values.resolutionUnit),
      m_background(values.background) {
    refreshFields();
}

NewCanvasValues NewCanvasModel::values() const {
    return {QSize(m_pixelsW, m_pixelsH), m_ppi, m_sizeUnit, m_resolutionUnit, m_background};
}

double NewCanvasModel::resolutionValue() const {
    return m_resolutionUnit == ResolutionUnit::PerInch ? m_ppi : m_ppi / kCmPerInch;
}

void NewCanvasModel::refreshFields() {
    m_width = fromPixels(m_pixelsW, m_sizeUnit, m_ppi);
    m_height = fromPixels(m_pixelsH, m_sizeUnit, m_ppi);
}

// What a size field shows for `value` once the pixel size is `pixels`: the typed value, unless the pixel size had to be
// limited.
double NewCanvasModel::fieldFor(double value, int pixels) const {
    const double raw = m_sizeUnit == SizeUnit::Pixels ? value : value / unitsPerInch(m_sizeUnit) * m_ppi;
    return raw >= 1.0 && raw < kMaxCanvasSide + 0.5 ? value : fromPixels(pixels, m_sizeUnit, m_ppi);
}

bool NewCanvasModel::beyondLimit(double value) const {
    const double raw = m_sizeUnit == SizeUnit::Pixels ? value : value / unitsPerInch(m_sizeUnit) * m_ppi;
    return raw >= kMaxCanvasSide + 0.5;
}

void NewCanvasModel::setWidth(double value) {
    m_activePreset = -1;
    m_widthLimited = beyondLimit(value);
    m_pixelsW = toPixels(value, m_sizeUnit, m_ppi);
    m_width = fieldFor(value, m_pixelsW);
}

void NewCanvasModel::setHeight(double value) {
    m_activePreset = -1;
    m_heightLimited = beyondLimit(value);
    m_pixelsH = toPixels(value, m_sizeUnit, m_ppi);
    m_height = fieldFor(value, m_pixelsH);
}

void NewCanvasModel::setSizeUnit(SizeUnit unit) {
    m_activePreset = -1;
    m_sizeUnit = unit;
    refreshFields();
    m_widthLimited = m_heightLimited = false;
}

void NewCanvasModel::setResolutionValue(double value) {
    m_activePreset = -1;
    const double ppi = std::clamp(m_resolutionUnit == ResolutionUnit::PerInch ? value : value * kCmPerInch, kMinPpi, kMaxPpi);
    if (m_sizeUnit == SizeUnit::Pixels) {
        m_ppi = ppi;
        return;
    }
    // A physical size is kept and the pixel size follows.
    const double w = m_width, h = m_height;
    m_ppi = ppi;
    m_widthLimited = beyondLimit(w);
    m_heightLimited = beyondLimit(h);
    m_pixelsW = toPixels(w, m_sizeUnit, m_ppi);
    m_pixelsH = toPixels(h, m_sizeUnit, m_ppi);
}

void NewCanvasModel::setResolutionUnit(ResolutionUnit unit) {
    m_activePreset = -1;
    m_resolutionUnit = unit;
}

void NewCanvasModel::setBackground(const CanvasBackground& background) {
    m_background = background;
}

void NewCanvasModel::applyPreset(const CanvasPreset& preset, int index) {
    if (preset.separator)
        return;
    m_sizeUnit = preset.sizeUnit;
    m_resolutionUnit = preset.resolutionUnit;
    m_ppi =
        std::clamp(preset.resolutionUnit == ResolutionUnit::PerInch ? preset.resolution : preset.resolution * kCmPerInch, kMinPpi, kMaxPpi);
    m_widthLimited = beyondLimit(preset.width);
    m_heightLimited = beyondLimit(preset.height);
    m_pixelsW = toPixels(preset.width, m_sizeUnit, m_ppi);
    m_pixelsH = toPixels(preset.height, m_sizeUnit, m_ppi);
    m_width = fieldFor(preset.width, m_pixelsW);
    m_height = fieldFor(preset.height, m_pixelsH);
    if (preset.background)
        m_background = *preset.background;
    m_activePreset = index;
}

QString NewCanvasModel::pixelsText() const {
    return QStringLiteral("%1 × %2").arg(m_pixelsW).arg(m_pixelsH);
}

QString NewCanvasModel::metricsText() const {
    const SizeUnit unit = m_sizeUnit == SizeUnit::Pixels ? SizeUnit::Millimeters : m_sizeUnit;
    return QStringLiteral("%1%2 × %3%2 (%4 %5)")
        .arg(formatLength(fromPixels(m_pixelsW, unit, m_ppi), unit), unitSuffix(unit),
             formatLength(fromPixels(m_pixelsH, unit, m_ppi), unit), formatResolution(resolutionValue()),
             resolutionUnitLabel(m_resolutionUnit));
}

}
