#include "core/tools/ToolBoxJson.h"

#include <QCoreApplication>
#include <QJsonArray>

#include <cmath>
#include <utility>

namespace doodlebold {

namespace {

constexpr int kFormatVersion = 10;
// A tool saved by older versions kept Hair on a scale this many times the new one: its 100, its start, laid the hair the new 50 lays.
constexpr double kOldHairPerHair = 2.0;

struct IntField {
    const char* key;
    int BrushSettings::* member;
};
struct BoolField {
    const char* key;
    bool BrushSettings::* member;
};

constexpr IntField kIntFields[] = {
    {"size", &BrushSettings::size},
    {"minSize", &BrushSettings::minSize},
    {"density", &BrushSettings::density},
    {"minDensity", &BrushSettings::minDensity},
    {"edge", &BrushSettings::edge},
    {"sharpness", &BrushSettings::sharpness},
    {"dilution", &BrushSettings::dilution},
    {"blending", &BrushSettings::blending},
    {"persistence", &BrushSettings::persistence},
    {"blurPressure", &BrushSettings::blurPressure},
    {"blurWidth", &BrushSettings::blurWidth},
    {"sensitivitySize", &BrushSettings::sensitivitySize},
    {"sensitivityDensity", &BrushSettings::sensitivityDensity},
    {"sensitivityBlending", &BrushSettings::sensitivityBlending},
    {"opacity", &BrushSettings::opacity},
    {"formStrength", &BrushSettings::formStrength},
    {"formScale", &BrushSettings::formScale},
    {"textureStrength", &BrushSettings::textureStrength},
    {"textureScale", &BrushSettings::textureScale},
    {"textureScratch", &BrushSettings::textureScratch},
    {"ampDensity", &BrushSettings::ampDensity},
    {"bristleMinimum", &BrushSettings::bristleMinimum},
    // Saved as "hair" now; a tool saved by older versions kept "bristleSize" on a scale whose 100 is the new 50 (kOldHairPerHair).
    {"hair", &BrushSettings::bristleSize},
    {"bristleRandomize", &BrushSettings::bristleRandomize},
    // Saved under a key of its own now: a tool saved by older versions holds "bristleDirection", 0 (None) as the setting was never read,
    // while the engine always laid hairs as Auto, so that key is left unread and the tool starts at Auto, once.
    {"hairDirection", &BrushSettings::bristleDirection},
    {"shapeDensity", &BrushSettings::shapeDensity},
    {"shapeAngleControl", &BrushSettings::shapeAngleControl},
    {"shapeAngle", &BrushSettings::shapeAngle},
    {"shapeAngleJitter", &BrushSettings::shapeAngleJitter},
    {"shapeScaling", &BrushSettings::shapeScaling},
    {"shapeSizeJitter", &BrushSettings::shapeSizeJitter},
    {"shapeSpacing", &BrushSettings::shapeSpacing},
    {"shapeScattering", &BrushSettings::shapeScattering},
};

constexpr BoolField kBoolFields[] = {
    {"pressureSize", &BrushSettings::pressureSize},
    {"pressureDensity", &BrushSettings::pressureDensity},
    {"pressureBlending", &BrushSettings::pressureBlending},
    {"pressureDilution", &BrushSettings::pressureDilution},
    {"cloneAligned", &BrushSettings::cloneAligned},
    {"cloneAllLayers", &BrushSettings::cloneAllLayers},
    {"formInvert", &BrushSettings::formInvert},
    {"formSharpen", &BrushSettings::formSharpen},
    {"shapeHighQuality", &BrushSettings::shapeHighQuality},
    {"curveInterpolation", &BrushSettings::curveInterpolation},
    {"disableDensityAssist", &BrushSettings::disableDensityAssist},
    {"antiRipple", &BrushSettings::antiRipple},
    {"formEraseLow", &BrushSettings::formEraseLow},
    {"textureInvert", &BrushSettings::textureInvert},
    {"textureEraseLow", &BrushSettings::textureEraseLow},
    {"hairDetectTurn", &BrushSettings::bristleDetectTurn},
};

struct StringField {
    const char* key;
    QString BrushSettings::* member;
};

constexpr StringField kStringFields[] = {
    {"form", &BrushSettings::form},
    {"texture", &BrushSettings::texture},
};

QJsonObject levelToJson(const StabilizerLevel& l) {
    return {{"value", l.value}};
}

std::optional<StabilizerLevel> levelFromJson(const QJsonValue& v) {
    const QJsonObject o = v.toObject();
    if (!o.value("value").isDouble())
        return std::nullopt;
    return StabilizerLevel{o.value("value").toInt()};
}

const char* wandModeKey(selection::WandMode m);

QJsonObject bucketToJson(const BucketSettings& b) {
    return {{"mode", wandModeKey(b.mode)},    {"colourThreshold", b.colourThreshold},
            {"gapTolerance", b.gapTolerance}, {"source", b.mergedSource ? "merged" : "current"},
            {"antialias", b.antialias},       {"ignoreSelection", b.ignoreSelection}};
}

// Starts from `b`: a damaged value keeps it.
BucketSettings bucketFromJson(const QJsonObject& o, BucketSettings b) {
    for (selection::WandMode mode :
         {selection::WandMode::EnclosedTransparent, selection::WandMode::SimilarColour, selection::WandMode::AllSimilar})
        if (o.value("mode").toString() == QLatin1String(wandModeKey(mode)))
            b.mode = mode;
    for (auto [key, member] :
         {std::pair{"colourThreshold", &BucketSettings::colourThreshold}, std::pair{"gapTolerance", &BucketSettings::gapTolerance}})
        if (o.value(QLatin1String(key)).isDouble())
            b.*member = o.value(QLatin1String(key)).toInt();
    if (o.value("source").isString())
        b.mergedSource = o.value("source").toString() == QLatin1String("merged");
    for (auto [key, member] :
         {std::pair{"antialias", &BucketSettings::antialias}, std::pair{"ignoreSelection", &BucketSettings::ignoreSelection}})
        if (o.value(QLatin1String(key)).isBool())
            b.*member = o.value(QLatin1String(key)).toBool();
    return b;
}

QJsonObject settingsToJson(const BrushSettings& s) {
    QJsonObject o;
    for (const IntField& f : kIntFields)
        o.insert(QLatin1String(f.key), s.*f.member);
    for (const BoolField& f : kBoolFields)
        o.insert(QLatin1String(f.key), s.*f.member);
    for (const StringField& f : kStringFields)
        o.insert(QLatin1String(f.key), s.*f.member);
    if (s.stabilizer)
        o.insert("stabilizer", levelToJson(*s.stabilizer));
    if (s.tool == ToolType::Bucket)
        o.insert("bucket", bucketToJson(s.bucket));
    return o;
}

// Starts from the tool's defaults: a damaged value keeps them.
BrushSettings settingsFromJson(ToolType tool, const QJsonObject& o) {
    BrushSettings s = defaultSettings(tool);
    for (const IntField& f : kIntFields)
        if (const QJsonValue v = o.value(QLatin1String(f.key)); v.isDouble())
            s.*f.member = int(std::lround(std::clamp(v.toDouble(), -1e9, 1e9)));  // in range of an int; setState clamps it
    for (const BoolField& f : kBoolFields)
        if (const QJsonValue v = o.value(QLatin1String(f.key)); v.isBool())
            s.*f.member = v.toBool();
    for (const StringField& f : kStringFields)
        if (const QJsonValue v = o.value(QLatin1String(f.key)); v.isString() && !v.toString().isEmpty())
            s.*f.member = v.toString();
    if (const QJsonValue v = o.value("bristleSize");
        !o.contains("hair") && v.isDouble())  // saved by an older version: its hairs as wide as then
        s.bristleSize = int(std::lround(std::clamp(v.toDouble(), -1e9, 1e9) / kOldHairPerHair));
    if (o.contains("stabilizer"))
        s.stabilizer = levelFromJson(o.value("stabilizer"));
    s.bucket = bucketFromJson(o.value("bucket").toObject(), s.bucket);
    return s;
}

QJsonObject slotToJson(const ToolSlot& slot) {
    QJsonObject o{{"tool", toolTypeKey(slot.type())}, {"name", slot.name}, {"settings", settingsToJson(slot.settings)}};
    if (slot.reserved)
        o.insert("reserved", settingsToJson(*slot.reserved));
    if (!slot.key.isEmpty())
        o.insert("key", slot.key);
    return o;
}

// A cell of an unknown tool type is empty.
std::optional<ToolSlot> slotFromJson(const QJsonValue& v) {
    if (!v.isObject())
        return std::nullopt;
    const QJsonObject o = v.toObject();
    const std::optional<ToolType> tool = toolTypeFromKey(o.value("tool").toString());
    if (!tool)
        return std::nullopt;
    ToolSlot slot;
    slot.name = o.value("name").toString();
    slot.settings = settingsFromJson(*tool, o.value("settings").toObject());
    if (o.value("reserved").isObject())
        slot.reserved = settingsFromJson(*tool, o.value("reserved").toObject());
    slot.key = o.value("key").toString();
    return slot;
}

// Only the global stabilizer level; the minimum pressure is an application option.
QJsonObject optionsToJson(const BrushOptions& options) {
    return {{"stabilizer", levelToJson(options.stabilizer)}};
}

BrushOptions optionsFromJson(const QJsonObject& o) {
    BrushOptions options;
    if (const auto level = levelFromJson(o.value("stabilizer")))
        options.stabilizer = *level;
    return options;
}

const char* interpolationKey(transform::Interpolation i) {
    switch (i) {
    case transform::Interpolation::Nearest: return "nearest";
    case transform::Interpolation::Bilinear: return "bilinear";
    case transform::Interpolation::Bicubic: return "bicubic";
    case transform::Interpolation::Lanczos2: return "lanczos2";
    case transform::Interpolation::Lanczos3: return "lanczos3";
    }
    return "bilinear";
}

transform::Interpolation interpolationFromJson(const QJsonValue& v, transform::Interpolation fallback) {
    const QString key = v.toString();
    for (transform::Interpolation i :
         {transform::Interpolation::Nearest, transform::Interpolation::Bilinear, transform::Interpolation::Bicubic,
          transform::Interpolation::Lanczos2, transform::Interpolation::Lanczos3})
        if (key == QLatin1String(interpolationKey(i)))
            return i;
    return fallback;
}

const char* wandModeKey(selection::WandMode m) {
    switch (m) {
    case selection::WandMode::EnclosedTransparent: return "enclosedTransparent";
    case selection::WandMode::SimilarColour: return "similarColour";
    case selection::WandMode::AllSimilar: return "allSimilar";
    }
    return "enclosedTransparent";
}

QJsonObject fixedToJson(const FixedToolOptions& f) {
    const MarqueeOptions& m = f.marquee;
    const WandToolOptions& w = f.wand;
    return {{"marquee", QJsonObject{{"cutAndMove", m.cutAndMove},
                                    {"transformMode", transformModeKey(m.transformMode)},
                                    {"perspective", m.perspective},
                                    {"dragging", interpolationKey(m.dragging)},
                                    {"preview", interpolationKey(m.preview)},
                                    {"final", interpolationKey(m.final)}}},
            {"lasso", QJsonObject{{"mode", lassoModeKey(f.lasso.mode)}, {"antialias", f.lasso.antialias}}},
            {"text", QJsonObject{{"font", f.text.font},
                                 {"size", f.text.sizePx},
                                 {"bold", f.text.bold},
                                 {"italic", f.text.italic},
                                 {"vertical", f.text.vertical},
                                 {"fitToPixelGrid", f.text.fitToPixelGrid}}},
            {"wand", QJsonObject{{"mode", wandModeKey(w.mode)},
                                 {"colourThreshold", w.colourThreshold},
                                 {"transparencyRange", w.transparencyRange},
                                 {"gapTolerance", w.gapTolerance},
                                 {"source", w.source == WandSource::MergedImage ? "merged" : "current"},
                                 {"antialias", w.antialias}}}};
}

// Keys that are missing or of the wrong type keep the defaults.
FixedToolOptions fixedFromJson(const QJsonObject& o) {
    FixedToolOptions f;
    const QJsonObject m = o.value("marquee").toObject();
    if (m.value("cutAndMove").isBool())
        f.marquee.cutAndMove = m.value("cutAndMove").toBool();
    if (const auto mode = transformModeFromKey(m.value("transformMode").toString()))
        f.marquee.transformMode = *mode;
    if (m.value("perspective").isDouble())
        f.marquee.perspective = m.value("perspective").toInt();
    f.marquee.dragging = interpolationFromJson(m.value("dragging"), f.marquee.dragging);
    f.marquee.preview = interpolationFromJson(m.value("preview"), f.marquee.preview);
    f.marquee.final = interpolationFromJson(m.value("final"), f.marquee.final);

    const QJsonObject l = o.value("lasso").toObject();
    if (const auto mode = lassoModeFromKey(l.value("mode").toString()))
        f.lasso.mode = *mode;
    if (l.value("antialias").isBool())
        f.lasso.antialias = l.value("antialias").toBool();

    const QJsonObject text = o.value("text").toObject();
    if (text.value("font").isString())
        f.text.font = text.value("font").toString();
    if (text.value("size").isDouble())
        f.text.sizePx = text.value("size").toDouble();
    for (auto [key, member] :
         {std::pair{"bold", &TextToolOptions::bold}, std::pair{"italic", &TextToolOptions::italic},
          std::pair{"vertical", &TextToolOptions::vertical}, std::pair{"fitToPixelGrid", &TextToolOptions::fitToPixelGrid}})
        if (text.value(QLatin1String(key)).isBool())
            f.text.*member = text.value(QLatin1String(key)).toBool();

    const QJsonObject w = o.value("wand").toObject();
    for (selection::WandMode mode :
         {selection::WandMode::EnclosedTransparent, selection::WandMode::SimilarColour, selection::WandMode::AllSimilar})
        if (w.value("mode").toString() == QLatin1String(wandModeKey(mode)))
            f.wand.mode = mode;
    if (w.value("colourThreshold").isDouble())
        f.wand.colourThreshold = w.value("colourThreshold").toInt();
    if (w.value("transparencyRange").isDouble())
        f.wand.transparencyRange = w.value("transparencyRange").toInt();
    if (w.value("gapTolerance").isDouble())
        f.wand.gapTolerance = w.value("gapTolerance").toInt();
    if (w.value("source").toString() == QLatin1String("merged"))
        f.wand.source = WandSource::MergedImage;
    if (w.value("antialias").isBool())
        f.wand.antialias = w.value("antialias").toBool();
    return f.clamped();
}

}

QString describe(ToolBoxFileError e) {
    const auto tr = [](const char* text) { return QCoreApplication::translate("ToolBoxFile", text); };
    switch (e) {
    case ToolBoxFileError::NoFile: return tr("There is no saved tool set.");
    case ToolBoxFileError::Unreadable: return tr("The saved tool set cannot be opened.");
    case ToolBoxFileError::Corrupt: return tr("The saved tool set is damaged.");
    case ToolBoxFileError::OtherVersion: return tr("The saved tool set was written by a different version of Doodlebold.");
    case ToolBoxFileError::WriteFailed: return tr("The tool set could not be saved.");
    }
    return {};
}

QJsonObject toJson(const ToolBoxState& state) {
    QJsonArray groups;
    for (const ToolGroup& g : state.groups) {
        QJsonArray cells;
        for (const auto& cell : g.cells)
            cells.append(cell ? QJsonValue(slotToJson(*cell)) : QJsonValue(QJsonValue::Null));
        groups.append(QJsonObject{{"name", g.name}, {"cells", cells}});
    }
    return {{"version", kFormatVersion},
            {"bucketPlaced", true},
            {"color", state.color.name(QColor::HexArgb)},
            {"secondaryColor", state.secondaryColor.name(QColor::HexArgb)},
            {"current", QJsonObject{{"group", state.current.group}, {"slot", state.current.index}}},
            {"options", optionsToJson(state.options)},
            {"fixedTools", fixedToJson(state.fixed)},
            {"groups", groups}};
}

Result<ToolBoxState, ToolBoxFileError> toolBoxStateFromJson(const QJsonObject& json) {
    using Fail = Result<ToolBoxState, ToolBoxFileError>;
    if (!json.value("version").isDouble() || !json.value("groups").isArray())
        return Fail::failure(ToolBoxFileError::Corrupt);
    if (json.value("version").toInt() != kFormatVersion)
        return Fail::failure(ToolBoxFileError::OtherVersion);

    ToolBoxState state;
    for (const auto& gv : json.value("groups").toArray()) {
        const QJsonObject go = gv.toObject();
        ToolGroup g;
        g.name = go.value("name").toString();
        for (const auto& cell : go.value("cells").toArray())
            g.cells.push_back(slotFromJson(cell));
        state.groups.push_back(std::move(g));
    }
    const QJsonObject current = json.value("current").toObject();
    state.current = {current.value("group").toInt(), current.value("slot").toInt()};
    if (const QColor c(json.value("color").toString()); c.isValid())
        state.color = c;
    if (const QColor c(json.value("secondaryColor").toString()); c.isValid())
        state.secondaryColor = c;
    state.options = optionsFromJson(json.value("options").toObject());
    state.fixed = fixedFromJson(json.value("fixedTools").toObject());
    if (!json.value("bucketPlaced").toBool())
        placeBucket(state);
    return state;
}

}
