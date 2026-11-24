#include "core/io/FormatRegistry.h"

#include "core/io/DoodleFormat.h"
#include "core/io/ImageFormat.h"
#include "core/io/PsdFormat.h"

#include <QCoreApplication>
#include <QFileInfo>

#include <algorithm>
#include <ranges>

namespace doodlebold {

FileResult<LoadedDocument> DocumentFormat::read(QIODevice&) const {
    return FileResult<LoadedDocument>::failure({FileError::Kind::UnknownFormat, name()});
}

FileResult<LoadedDocument> DocumentFormat::readScaledToFit(QIODevice&) const {
    return FileResult<LoadedDocument>::failure({FileError::Kind::Unsupported, name()});
}

FileResult<> DocumentFormat::write(const DocumentContent&, QIODevice&, const ExportOptions&) const {
    return FileResult<>::failure({FileError::Kind::UnknownFormat, name()});
}

FormatRegistry& FormatRegistry::instance() {
    static FormatRegistry registry = [] {
        FormatRegistry r;
        registerBuiltInFormats(r);
        return r;
    }();
    return registry;
}

void FormatRegistry::add(std::unique_ptr<DocumentFormat> format) {
    m_formats.push_back(std::move(format));
}

const DocumentFormat* FormatRegistry::byId(const QString& id) const {
    for (const auto& f : m_formats)
        if (f->id() == id)
            return f.get();
    return nullptr;
}

const DocumentFormat* FormatRegistry::forPath(const QString& path, bool forWriting) const {
    const QString ext = QFileInfo(path).suffix().toLower();
    if (ext.isEmpty())
        return nullptr;
    for (const auto& m_format : std::views::reverse(m_formats)) {  // the latest registration wins
        const DocumentFormat& f = *m_format;
        if ((forWriting ? f.canWrite() : f.canRead()) && f.extensions().contains(ext))
            return &f;
    }
    return nullptr;
}

std::vector<const DocumentFormat*> FormatRegistry::readable() const {
    std::vector<const DocumentFormat*> out;
    for (const auto& f : m_formats)
        if (f->canRead())
            out.push_back(f.get());
    return out;
}

std::vector<const DocumentFormat*> FormatRegistry::writable() const {
    std::vector<const DocumentFormat*> out;
    for (const auto& f : m_formats)
        if (f->canWrite())
            out.push_back(f.get());
    return out;
}

QString FormatRegistry::filterFor(const DocumentFormat& format) {
    QStringList patterns;
    for (const QString& e : format.extensions())
        patterns << QStringLiteral("*.") + e;
    return QStringLiteral("%1 (%2)").arg(format.name(), patterns.join(QLatin1Char(' ')));
}

QStringList FormatRegistry::readableExtensions() const {
    QStringList out;
    for (const DocumentFormat* f : readable())
        for (const QString& e : f->extensions())
            if (!out.contains(e))
                out << e;
    return out;
}

QString FormatRegistry::openFilter() const {
    QStringList all;
    for (const QString& e : readableExtensions())
        all << QStringLiteral("*.") + e;
    QStringList filters{QCoreApplication::translate("FormatRegistry", "All supported files (%1)").arg(all.join(QLatin1Char(' ')))};
    for (const DocumentFormat* f : readable())
        filters << filterFor(*f);
    filters << QCoreApplication::translate("FormatRegistry", "All files (*)");
    return filters.join(QStringLiteral(";;"));
}

void registerBuiltInFormats(FormatRegistry& registry) {
    // Earlier registrations are listed first. To add a format, add one line here (see FormatRegistry.h).
    registry.add(std::make_unique<DoodleFormat>());
    registry.add(std::make_unique<PsdFormat>());
    registry.add(std::make_unique<PsdFormat>(true));  // .psb
    for (auto& f : builtInImageFormats())
        registry.add(std::move(f));
}

}
