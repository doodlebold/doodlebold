#pragma once

#include <QFrame>

#include <functional>
#include <vector>

class QListWidget;
class QToolButton;

namespace doodlebold {

class AssetListPopup : public QFrame {
    Q_OBJECT
public:
    struct Actions {
        std::function<void(int)> choose;
        std::function<void()> add;
        std::function<void(int)> remove;
        std::function<void(int, QString)> rename;
    };

    AssetListPopup(const QString& kind, const std::vector<QString>& names, int current, Actions actions, QWidget* parent = nullptr);

    QListWidget* list() const { return m_list; }

private:
    QString m_kind;
    Actions m_actions;
    QListWidget* m_list;
};

}
