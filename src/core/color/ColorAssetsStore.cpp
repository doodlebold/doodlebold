#include "core/color/ColorAssetsStore.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>

namespace doodlebold {

ColorAssetsStore::ColorAssetsStore(ColorAssets* assets, QString path, QObject* parent)
    : QObject(parent), m_assets(assets), m_path(std::move(path)) {
    m_timer.setSingleShot(true);
    m_timer.setInterval(kSaveDelayMs);
    connect(m_assets, &ColorAssets::modified, &m_timer, qOverload<>(&QTimer::start));
    connect(&m_timer, &QTimer::timeout, this, [this] {
        if (!save())
            qWarning().noquote() << "Color panel data not saved:" << m_path;
    });
}

QString ColorAssetsStore::defaultPath() {
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)).filePath(QStringLiteral("colors.json"));
}

bool ColorAssetsStore::load() {
    QFile file(m_path);
    if (!file.exists())
        return true;
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject())
        return false;
    m_assets->fromJson(doc.object());
    m_timer.stop();
    return true;
}

bool ColorAssetsStore::save() const {
    if (!QDir().mkpath(QFileInfo(m_path).absolutePath()))
        return false;
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(QJsonDocument(m_assets->toJson()).toJson(QJsonDocument::Indented));
    return file.commit();
}

void ColorAssetsStore::flush() {
    if (!m_timer.isActive())
        return;
    m_timer.stop();
    if (!save())
        qWarning().noquote() << "Color panel data not saved:" << m_path;
}

}
