#include "core/brush/BrushResources.h"
#include "core/brush/BrushSettings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QPainter>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTextStream>

#include <algorithm>

// The built-in pictures are in a resource file of this static library, which the linker would leave out unless it's named.
static void initBrushPictures() {
    Q_INIT_RESOURCE(brushes);
}

namespace doodlebold {

const QString BrushResourceLibrary::kSimpleCircle = QStringLiteral("Round");
const QString BrushResourceLibrary::kNoTexture = QStringLiteral("None");
const QString BrushResourceLibrary::kLayerPaper = QStringLiteral("Layer paper");

QString resourceFolder(ResourceKind kind) {
    switch (kind) {
    case ResourceKind::BlotMap: return QStringLiteral("forms/blots");
    case ResourceKind::Bristle: return QStringLiteral("forms/bristles");
    case ResourceKind::Shape: return QStringLiteral("forms/shapes");
    case ResourceKind::Texture: return QStringLiteral("textures");
    case ResourceKind::PaperTexture: return QStringLiteral("papers");
    }
    return {};
}

QString describe(ResourceError error) {
    switch (error) {
    case ResourceError::Unreadable: return QCoreApplication::translate("BrushResources", "The file is not an image that can be read.");
    case ResourceError::Empty: return QCoreApplication::translate("BrushResources", "The image is empty.");
    case ResourceError::WriteFailed: return QCoreApplication::translate("BrushResources", "The brush folder could not be written.");
    case ResourceError::BuiltIn:
        return QCoreApplication::translate("BrushResources", "This picture comes with Doodlebold and cannot be removed or renamed.");
    case ResourceError::NameTaken: return QCoreApplication::translate("BrushResources", "Another picture already has that name.");
    case ResourceError::NotFound: return QCoreApplication::translate("BrushResources", "The picture is no longer in the library.");
    }
    return {};
}

QImage toGreyscale(const QImage& image) {
    if (!image.hasAlphaChannel())
        return image.convertToFormat(QImage::Format_Grayscale8);
    QImage flat(image.size(), QImage::Format_RGB32);
    flat.fill(Qt::white);
    QPainter p(&flat);
    p.drawImage(0, 0, image);
    p.end();
    return flat.convertToFormat(QImage::Format_Grayscale8);
}

std::optional<ShapeSettings> readShapeSettings(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return std::nullopt;
    ShapeSettings shape;
    const std::pair<const char*, int*> numbers[] = {{"Density", &shape.density}, {"AngleControl", &shape.angleControl},
                                                    {"Angle", &shape.angle},     {"AngleJitter", &shape.angleJitter},
                                                    {"Scaling", &shape.scaling}, {"SizeJitter", &shape.sizeJitter},
                                                    {"Spacing", &shape.spacing}, {"Scattering", &shape.scattering}};
    QTextStream in(&file);
    while (!in.atEnd()) {
        const QString line = in.readLine().section(QLatin1Char(';'), 0, 0).trimmed();
        const qsizetype equals = line.indexOf(QLatin1Char('='));
        if (equals < 0)
            continue;
        const QString key = line.left(equals).trimmed();
        bool ok = false;
        const int value = line.mid(equals + 1).trimmed().toInt(&ok);
        if (!ok)
            continue;
        for (const auto& [name, field] : numbers)
            if (key == QLatin1String(name))
                *field = value;
        if (key == QLatin1String("HighQualitySampling"))
            shape.highQuality = value != 0;
        if (key == QLatin1String("OneStroke"))
            shape.oneStroke = value != 0;
    }
    return shape;
}

void applyShapeSettings(BrushSettings& settings, const ShapeSettings& shape) {
    settings.shapeDensity = shape.density;
    settings.shapeAngleControl = shape.angleControl;
    settings.shapeAngle = shape.angle;
    settings.shapeAngleJitter = shape.angleJitter;
    settings.shapeScaling = shape.scaling;
    settings.shapeSizeJitter = shape.sizeJitter;
    settings.shapeSpacing = shape.spacing;
    settings.shapeScattering = shape.scattering;
    settings.shapeHighQuality = shape.highQuality;
}

namespace {

// A brush shape's settings file, beside its picture.
std::optional<ShapeSettings> shapeSettingsBeside(const QFileInfo& picture, ResourceKind kind) {
    if (kind != ResourceKind::Shape)
        return std::nullopt;
    return readShapeSettings(picture.dir().filePath(picture.completeBaseName() + QStringLiteral(".ini")));
}

}

const std::vector<BrushResource>& builtInResources() {
    static const std::vector<BrushResource> pictures = [] {
        initBrushPictures();
        std::vector<BrushResource> out;
        for (ResourceKind kind : kResourceKinds) {
            const QDir dir(QStringLiteral(":/brushes/") + resourceFolder(kind));
            for (const QFileInfo& info : dir.entryInfoList({QStringLiteral("*.png")}, QDir::Files, QDir::Name)) {
                const QImage image(info.filePath());
                if (!image.isNull())
                    out.push_back(
                        {info.completeBaseName(), kind, toGreyscale(image), info.filePath(), true, shapeSettingsBeside(info, kind)});
            }
        }
        return out;
    }();
    return pictures;
}

BrushResourceLibrary::BrushResourceLibrary(QString folder, QObject* parent)
    : BrushResourceLibrary(std::move(folder), BuiltIns::Included, parent) {}

BrushResourceLibrary::BrushResourceLibrary(QString folder, BuiltIns builtIns, QObject* parent)
    : QObject(parent), m_folder(std::move(folder)), m_builtIns(builtIns) {
    reload();
}

BrushResourceLibrary::~BrushResourceLibrary() = default;

QString BrushResourceLibrary::defaultFolder() {
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QStringLiteral("brushes"));
}

void BrushResourceLibrary::reload() {
    m_user.clear();
    QStringList patterns;
    for (const QByteArray& format : QImageReader::supportedImageFormats())
        patterns << QStringLiteral("*.") + QString::fromLatin1(format);
    for (ResourceKind kind : kResourceKinds) {
        const QDir dir(QDir(m_folder).filePath(resourceFolder(kind)));
        for (const QFileInfo& info : dir.entryInfoList(patterns, QDir::Files, QDir::Name)) {
            QImageReader reader(info.filePath());
            reader.setAutoTransform(true);
            const QImage image = reader.read();
            if (image.isNull() || image.size().isEmpty())
                continue;
            auto r = std::make_unique<BrushResource>();
            // A file may not take the name of a "none" entry: every tool that has no form would be using it.
            r->name = isReserved(info.completeBaseName()) ? info.completeBaseName() + QStringLiteral(" (file)") : info.completeBaseName();
            r->kind = kind;
            r->image = toGreyscale(image);
            r->file = info.filePath();
            r->shapeSettings = shapeSettingsBeside(info, kind);
            m_user.push_back(std::move(r));
        }
    }
    gather();
}

void BrushResourceLibrary::gather() {
    m_shown.clear();
    for (const auto& r : m_user)
        m_shown.push_back(r.get());
    if (m_builtIns == BuiltIns::Left)
        return;
    for (const BrushResource& b : builtInResources()) {
        const bool replaced = std::ranges::any_of(m_user, [&b](const auto& r) { return sameList(r->kind, b.kind) && r->name == b.name; });
        if (!replaced)
            m_shown.push_back(&b);
    }
}

QString BrushResourceLibrary::kindFolder(ResourceKind kind) const {
    return QDir(m_folder).filePath(resourceFolder(kind));
}

QString BrushResourceLibrary::safeName(const QString& name) {
    static const QRegularExpression unsafe(QStringLiteral(R"([\\/:*?"<>|])"));
    QString safe = name;
    safe.replace(unsafe, QStringLiteral("-"));
    return safe.trimmed().isEmpty() ? QStringLiteral("Imported") : safe.trimmed();
}

bool BrushResourceLibrary::isReserved(const QString& name) {
    return name == kSimpleCircle || name == kNoTexture || name == kLayerPaper;
}

const BrushResource* BrushResourceLibrary::lookup(ResourceKind kind, const QString& name) const {
    return isForm(kind) ? form(name) : find(kind, name);
}

const BrushResource* BrushResourceLibrary::find(ResourceKind kind, const QString& name) const {
    for (const BrushResource* r : m_shown)
        if (r->kind == kind && r->name == name)
            return r;
    return nullptr;
}

const BrushResource* BrushResourceLibrary::findUser(ResourceKind kind, const QString& name) const {
    for (const auto& r : m_user)
        if (sameList(r->kind, kind) && r->name == name)
            return r.get();
    return nullptr;
}

std::vector<const BrushResource*> BrushResourceLibrary::sorted(bool (*wanted)(ResourceKind)) const {
    std::vector<const BrushResource*> out;
    for (const BrushResource* r : m_shown)
        if (wanted(r->kind))
            out.push_back(r);
    std::ranges::sort(
        out, [](const BrushResource* a, const BrushResource* b) { return QString::compare(a->name, b->name, Qt::CaseInsensitive) < 0; });
    return out;
}

std::vector<const BrushResource*> BrushResourceLibrary::forms() const {
    return sorted(isForm);
}

std::vector<const BrushResource*> BrushResourceLibrary::textures() const {
    return sorted([](ResourceKind k) { return k == ResourceKind::Texture; });
}

std::vector<const BrushResource*> BrushResourceLibrary::paperTextures() const {
    return sorted([](ResourceKind k) { return k == ResourceKind::PaperTexture; });
}

std::vector<const BrushResource*> BrushResourceLibrary::ofKind(ResourceKind kind) const {
    std::vector<const BrushResource*> out;
    for (const BrushResource* r : sorted([](ResourceKind) { return true; }))
        if (r->kind == kind)
            out.push_back(r);
    return out;
}

const BrushResource* BrushResourceLibrary::form(const QString& name) const {
    // One list holds all three kinds of form.
    for (const BrushResource* r : m_shown)
        if (isForm(r->kind) && r->name == name)
            return r;
    return nullptr;
}

const BrushResource* BrushResourceLibrary::texture(const QString& name) const {
    return find(ResourceKind::Texture, name);
}

const BrushResource* BrushResourceLibrary::paperTexture(const QString& name) const {
    return find(ResourceKind::PaperTexture, name);
}

QString BrushResourceLibrary::uniqueName(ResourceKind kind, const QString& base) const {
    const auto taken = [&](const QString& name) { return isReserved(name) || lookup(kind, name) != nullptr; };
    if (!taken(base))
        return base;
    for (int i = 2;; ++i)
        if (const QString name = QStringLiteral("%1 %2").arg(base).arg(i); !taken(name))
            return name;
}

Result<QString, ResourceError> BrushResourceLibrary::importImage(ResourceKind kind, const QString& path, SameName sameName) {
    QImageReader reader(path);
    reader.setAutoTransform(true);
    QImage image = reader.read();
    if (image.isNull())
        return Result<QString, ResourceError>::failure(ResourceError::Unreadable);
    if (image.size().isEmpty())
        return Result<QString, ResourceError>::failure(ResourceError::Empty);
    if (image.width() > kMaxImportSide || image.height() > kMaxImportSide)
        image = image.scaled(kMaxImportSide, kMaxImportSide, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    const QString base = safeName(QFileInfo(path).completeBaseName());
    const bool replace = sameName == SameName::Replace && !isReserved(base);
    const BrushResource* old = replace ? findUser(kind, base) : nullptr;
    const QString replacedFile = old ? old->file : QString();
    const QString name = replace ? base : uniqueName(kind, base);
    // A brush shape's settings file comes with it.
    const QString settingsFile = QFileInfo(path).dir().filePath(QFileInfo(path).completeBaseName() + QStringLiteral(".ini"));
    if (kind == ResourceKind::Shape && QFile::exists(settingsFile)) {
        const QString to = QDir(kindFolder(kind)).filePath(name + QStringLiteral(".ini"));
        QFile::remove(to);
        if (!QDir().mkpath(kindFolder(kind)) || !QFile::copy(settingsFile, to))
            return Result<QString, ResourceError>::failure(ResourceError::WriteFailed);
    }
    if (!add({{kind, name, toGreyscale(image)}}).ok())
        return Result<QString, ResourceError>::failure(ResourceError::WriteFailed);
    // The picture replaced goes only now the new one is written, so a failed write keeps it. A PNG of the same name was written over.
    const QString written = QDir(kindFolder(kind)).filePath(name + QStringLiteral(".png"));
    if (!replacedFile.isEmpty() && QFileInfo(replacedFile) != QFileInfo(written)) {
        const bool removed = QFile::remove(replacedFile);
        reload();
        emit changed();
        if (!removed)
            return Result<QString, ResourceError>::failure(ResourceError::WriteFailed);
    }
    return name;
}

Result<void, ResourceError> BrushResourceLibrary::add(const std::vector<NewResource>& resources) {
    bool failed = false;
    for (const NewResource& r : resources) {
        const QDir dir(QDir(m_folder).filePath(resourceFolder(r.kind)));
        if (!QDir().mkpath(dir.path()) || !r.image.save(dir.filePath(r.name) + QStringLiteral(".png"), "PNG")) {
            failed = true;
            break;
        }
    }
    if (!resources.empty()) {
        reload();
        emit changed();
    }
    return failed ? Result<void, ResourceError>::failure(ResourceError::WriteFailed) : Result<void, ResourceError>();
}

Result<QString, ResourceError> BrushResourceLibrary::rename(ResourceKind kind, const QString& name, const QString& newName) {
    using Out = Result<QString, ResourceError>;
    const BrushResource* r = nullptr;
    for (const auto& u : m_user)
        if (u->kind == kind && u->name == name)
            r = u.get();
    if (!r)
        return Out::failure(find(kind, name) ? ResourceError::BuiltIn : ResourceError::NotFound);
    const QString to = safeName(newName);
    if (to == name)
        return to;
    if (isReserved(to) || findUser(kind, to))
        return Out::failure(ResourceError::NameTaken);
    const QFileInfo info(r->file);
    const QString file = info.dir().filePath(to + QLatin1Char('.') + info.suffix());
    if (!QFile::rename(r->file, file))
        return Out::failure(ResourceError::WriteFailed);
    if (const QString settingsFile = info.dir().filePath(info.completeBaseName() + QStringLiteral(".ini")); QFile::exists(settingsFile))
        QFile::rename(settingsFile, info.dir().filePath(to + QStringLiteral(".ini")));
    reload();
    emit renamed(kind, name, to);
    emit changed();
    return to;
}

bool BrushResourceLibrary::remove(ResourceKind kind, const QString& name) {
    for (const auto& r : m_user)
        if (r->kind == kind && r->name == name) {
            if (!QFile::remove(r->file))
                return false;
            const QFileInfo info(r->file);
            QFile::remove(info.dir().filePath(info.completeBaseName() + QStringLiteral(".ini")));
            reload();
            emit changed();
            return true;
        }
    return false;
}

}
