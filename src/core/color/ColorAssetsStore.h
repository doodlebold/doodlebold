#pragma once

#include "core/color/ColorAssets.h"

#include <QObject>
#include <QString>
#include <QTimer>

namespace doodlebold {

// A damaged colors.json is reported and not overwritten until something changes.
class ColorAssetsStore : public QObject {
    Q_OBJECT
public:
    static constexpr int kSaveDelayMs = 500;

    ColorAssetsStore(ColorAssets* assets, QString path, QObject* parent = nullptr);

    static QString defaultPath();
    const QString& path() const { return m_path; }

    bool load();
    bool save() const;
    void flush();
    bool savePending() const { return m_timer.isActive(); }

private:
    ColorAssets* m_assets;
    QString m_path;
    QTimer m_timer;
};

}
