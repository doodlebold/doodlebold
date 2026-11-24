#include "core/tools/ToolBoxStore.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>

namespace doodlebold {

ToolBoxStore::ToolBoxStore(ToolBox* tools, QString path, QObject* parent) : QObject(parent), m_tools(tools), m_path(std::move(path)) {
    m_timer.setSingleShot(true);
    m_timer.setInterval(kSaveDelayMs);
    connect(m_tools, &ToolBox::modified, &m_timer, qOverload<>(&QTimer::start));  // restarts: waits for a pause
    connect(&m_timer, &QTimer::timeout, this, &ToolBoxStore::saveAndReport);
}

QString ToolBoxStore::defaultPath() {
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)).filePath(QStringLiteral("tools.json"));
}

Result<void, ToolBoxFileError> ToolBoxStore::load() {
    using Fail = Result<void, ToolBoxFileError>;
    QFile file(m_path);
    if (!file.exists())
        return Fail::failure(ToolBoxFileError::NoFile);
    if (!file.open(QIODevice::ReadOnly))
        return Fail::failure(ToolBoxFileError::Unreadable);
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    const auto state =
        doc.isObject() ? toolBoxStateFromJson(doc.object()) : Result<ToolBoxState, ToolBoxFileError>::failure(ToolBoxFileError::Corrupt);
    if (!state) {
        keepRefusedFile();
        return Fail::failure(state.error());
    }
    m_tools->setState(state.value());
    m_timer.stop();
    return {};
}

void ToolBoxStore::keepRefusedFile() const {
    QString aside = m_path + QStringLiteral(".refused");
    for (int i = 2; QFile::exists(aside); ++i)
        aside = m_path + QStringLiteral(".refused-%1").arg(i);
    if (!QFile::rename(m_path, aside))
        qWarning().noquote() << "Tool set not moved aside:" << m_path;
}

Result<void, ToolBoxFileError> ToolBoxStore::save() const {
    using Fail = Result<void, ToolBoxFileError>;
    if (!QDir().mkpath(QFileInfo(m_path).absolutePath()))
        return Fail::failure(ToolBoxFileError::WriteFailed);
    QSaveFile file(m_path);  // written aside and renamed, so a crash never leaves half a file
    if (!file.open(QIODevice::WriteOnly))
        return Fail::failure(ToolBoxFileError::WriteFailed);
    file.write(QJsonDocument(toJson(m_tools->state())).toJson(QJsonDocument::Indented));
    if (!file.commit())
        return Fail::failure(ToolBoxFileError::WriteFailed);
    return {};
}

void ToolBoxStore::flush() {
    if (!m_timer.isActive())
        return;
    m_timer.stop();
    saveAndReport();
}

void ToolBoxStore::saveAndReport() {
    if (const auto saved = save(); !saved)
        qWarning().noquote() << "Tool set not saved:" << describe(saved.error()) << m_path;
}

}
