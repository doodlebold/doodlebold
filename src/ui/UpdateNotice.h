#pragma once

#include "core/util/UpdateCheck.h"

#include <QByteArray>
#include <QDateTime>
#include <QObject>
#include <QUrl>

#include <functional>
#include <optional>

class QMessageBox;
class QNetworkAccessManager;
class QWidget;

namespace doodlebold {

// Tells the user a newer Doodlebold has been released, with a link to its page (core/util/UpdateCheck); it never downloads it. The
// check at start says nothing unless there is one, not even that GitHub couldn't be reached; Help › Check for Updates… always
// answers.
class UpdateNotice : public QObject {
    Q_OBJECT
public:
    // Gets `url` and calls `done` with the reply's body, or with nullopt when there is none.
    using Done = std::function<void(std::optional<QByteArray>)>;
    using Fetcher = std::function<void(const QUrl& url, Done done)>;
    // Tests answer in place of GitHub; empty, the network is asked.
    static void setFetcher(Fetcher fetcher);

    explicit UpdateNotice(QWidget* window);

    // At most once a day, and the notice of a newer release leaves the window free to paint on.
    void checkAtStart();
    void checkNow();

    // When the check at start last asked (kept in the settings); invalid if it never has.
    static QDateTime lastCheck();
    static void setLastCheck(const QDateTime& when);

private:
    using Heard = std::function<void(const std::optional<update_check::Release>&)>;
    void fetch(Heard heard);
    void get(Done done);
    void showNewer(const update_check::Release& release, bool modal);
    void showAnswer(const std::optional<update_check::Release>& release);
    QMessageBox* makeBox(const QString& title, const QString& headline, const QString& text);

    QWidget* m_window;
    QNetworkAccessManager* m_network = nullptr;
};

}
