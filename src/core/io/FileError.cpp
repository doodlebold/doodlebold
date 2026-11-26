#include "core/io/FileError.h"

#include "core/io/DocumentFormat.h"

#include <QCoreApplication>

namespace doodlebold {

FileError FileError::canvasTooLarge(QSize size, bool scalable) {
    FileError e{Kind::TooLarge, {}};
    e.size = size;
    e.scalable = scalable;
    return e;
}

QString FileError::describe() const {
    const auto tr = [](const char* s) { return QCoreApplication::translate("FileError", s); };
    QString text;
    switch (kind) {
    case Kind::CannotOpen: text = tr("The file could not be opened."); break;
    case Kind::CannotWrite: text = tr("The file could not be written."); break;
    case Kind::UnknownFormat: text = tr("This kind of file is not supported."); break;
    case Kind::NotADoodleFile: text = tr("This is not an Doodlebold file."); break;
    case Kind::UnsupportedVersion: text = tr("The file was saved by a different version of Doodlebold and cannot be opened."); break;
    case Kind::Truncated: text = tr("The file is incomplete (it ends too early)."); break;
    case Kind::Corrupt: text = tr("The file is damaged."); break;
    case Kind::TooLarge:
        text = size.isValid() ? tr("The picture is %1 × %2 pixels; canvases are at most %3 × %3.")
                                    .arg(size.width())
                                    .arg(size.height())
                                    .arg(kMaxCanvasSide)
                              : tr("The image is too large.");
        break;
    case Kind::Unsupported: text = tr("The file uses a feature that is not supported."); break;
    case Kind::PluginFailed: text = tr("The plugin for this kind of file stopped working; Options > Plugins can turn it off."); break;
    }
    return detail.isEmpty() ? text : text + QLatin1Char(' ') + detail;
}

}
