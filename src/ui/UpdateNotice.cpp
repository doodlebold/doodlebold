#include "ui/UpdateNotice.h"

#include "core/options/AppOptions.h"
#include "core/util/BuildInfo.h"
#include "core/util/ProjectLinks.h"

#include <QLabel>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QPushButton>
#include <QWidget>

#include <utility>

namespace doodlebold {

namespace {

constexpr const char* kLastCheck = "updates/lastCheck";
// ms: a reply not in by then counts as none, so Check for Updates says it couldn't check instead of leaving the user waiting.
// GitHub answers within a second; this leaves room for a slow connection.
constexpr int kReplyTimeoutMs = 15000;

UpdateNotice::Fetcher& fetcher() {
    static UpdateNotice::Fetcher f;
    return f;
}

}

void UpdateNotice::setFetcher(Fetcher f) {
    fetcher() = std::move(f);
}

UpdateNotice::UpdateNotice(QWidget* window) : QObject(window), m_window(window) {}

QDateTime UpdateNotice::lastCheck() {
    return QDateTime::fromString(AppOptions::settings().value(QLatin1String(kLastCheck)).toString(), Qt::ISODate);
}

void UpdateNotice::setLastCheck(const QDateTime& when) {
    AppOptions::settings().setValue(QLatin1String(kLastCheck), when.toUTC().toString(Qt::ISODate));
}

// The time is kept as it asks, not as it hears: a computer offline at start asks again the next day, not at every start.
void UpdateNotice::checkAtStart() {
    const QDateTime now = QDateTime::currentDateTimeUtc();
    if (!update_check::isDue(lastCheck(), now))
        return;
    setLastCheck(now);
    fetch([this](const std::optional<update_check::Release>& release) {
        if (release && update_check::isNewer(release->version, build_info::version()))
            showNewer(*release, false);
    });
}

void UpdateNotice::checkNow() {
    fetch([this](const std::optional<update_check::Release>& release) { showAnswer(release); });
}

// A reply can come after the window has closed.
void UpdateNotice::fetch(Heard heard) {
    Done done = [self = QPointer<UpdateNotice>(this), heard = std::move(heard)](const std::optional<QByteArray>& body) {
        if (self)
            heard(body ? update_check::parseLatestRelease(*body) : std::nullopt);
    };
    if (fetcher())
        fetcher()(update_check::latestReleaseUrl(), std::move(done));
    else
        get(std::move(done));
}

void UpdateNotice::get(Done done) {
    if (!m_network)
        m_network = new QNetworkAccessManager(this);
    QNetworkRequest request(update_check::latestReleaseUrl());
    request.setHeader(QNetworkRequest::UserAgentHeader, update_check::userAgent(build_info::version()));
    // Qt would send the system's languages, which tell GitHub something of who asks; the reply is the same in any language.
    request.setRawHeader("Accept-Language", "en");
    request.setTransferTimeout(kReplyTimeoutMs);
    QNetworkReply* reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [reply, done = std::move(done)] {
        reply->deleteLater();
        done(reply->error() == QNetworkReply::NoError ? std::optional(reply->readAll()) : std::nullopt);
    });
}

// At start it doesn't take the keys or block the window: the user may already be painting when the reply comes.
void UpdateNotice::showNewer(const update_check::Release& release, bool modal) {
    QMessageBox* box = makeBox(tr("Update Available"), tr("Doodlebold %1 is available").arg(release.version),
                               tr("This is %1. The release page says what is new and has the downloads.").arg(build_info::version()));
    QPushButton* open = box->addButton(tr("Open Release Page"), QMessageBox::AcceptRole);
    open->setObjectName(QStringLiteral("updateNoticeOpen"));
    box->addButton(QMessageBox::Close);
    box->setDefaultButton(open);
    connect(open, &QPushButton::clicked, this, [page = release.page] { links::open(page); });
    if (modal) {
        box->open();
        return;
    }
    box->setWindowModality(Qt::NonModal);
    box->setAttribute(Qt::WA_ShowWithoutActivating);
    box->show();
}

void UpdateNotice::showAnswer(const std::optional<update_check::Release>& release) {
    if (release && update_check::isNewer(release->version, build_info::version())) {
        showNewer(*release, true);
        return;
    }
    QMessageBox* box = release ? makeBox(tr("Check for Updates"), tr("Doodlebold is up to date"),
                                         tr("The latest release is %1, and this is %2.").arg(release->version, build_info::version()))
                               : makeBox(tr("Check for Updates"), tr("Couldn't check for updates"),
                                         tr("The latest release couldn't be read from GitHub. Check the internet connection and "
                                            "try again."));
    box->addButton(QMessageBox::Ok);
    box->open();
}

QMessageBox* UpdateNotice::makeBox(const QString& title, const QString& headline, const QString& text) {
    // The headline is in the box too, where a window manager's title bar may not show the title.
    auto* box = new QMessageBox(QMessageBox::Information, title, headline, QMessageBox::NoButton, m_window);
    box->setObjectName(QStringLiteral("updateNotice"));
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setInformativeText(text);
    if (auto* label = box->findChild<QLabel*>(QStringLiteral("qt_msgbox_label")))
        label->setProperty("role", "strong");  // body-strong, from the style sheet
    return box;
}

}
