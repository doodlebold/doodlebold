#include "ui/CrashReportDialog.h"

#include "core/util/ProjectLinks.h"
#include "ui/Components.h"
#include "ui/Theme.h"

#include <QClipboard>
#include <QDialogButtonBox>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace doodlebold {

namespace {

// How long Copy says "Copied", as Tablet Diagnostics' Copy report does.
constexpr int kCopiedMs = 1500;
constexpr int kTextColumns = 90;  // the report's width in characters, so its frames fit on one line (component metric)
constexpr int kTextHeight = 360;  // px: the report box's height, about twenty lines (component metric)

}

CrashReportDialog::CrashReportDialog(const QString& report, bool afterCrash, QWidget* parent) : QDialog(parent), m_report(report) {
    setObjectName(QStringLiteral("crashReportDialog"));
    setWindowTitle(afterCrash ? tr("Doodlebold closed unexpectedly") : tr("Crash Report"));

    const QString intro =
        report.isEmpty()
            ? tr("No crash has been recorded.")
            : afterCrash ? tr("Doodlebold crashed the last time it ran. Please copy this report into a bug report on GitHub: it says "
                              "where in the program the crash happened.")
                         : tr("The report from the last time Doodlebold crashed. Copy it into a bug report on GitHub.");
    auto* label = components::mutedLabel(intro, this);
    label->setWordWrap(true);

    auto* text = new QPlainTextEdit(this);
    text->setObjectName(QStringLiteral("crashReportText"));
    text->setReadOnly(true);
    text->setFont(Theme::font(Theme::TextRole::Mono));
    text->setPlainText(report);
    text->setMinimumSize(QFontMetrics(text->font()).averageCharWidth() * kTextColumns, kTextHeight);
    text->setVisible(!report.isEmpty());

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    m_copy = buttons->addButton(tr("Copy report"), QDialogButtonBox::ActionRole);
    m_copy->setObjectName(QStringLiteral("crashReportCopy"));
    m_copy->setDefault(true);
    m_copy->setEnabled(!report.isEmpty());
    connect(m_copy, &QPushButton::clicked, this, &CrashReportDialog::copyReport);
    auto* reportBug = buttons->addButton(tr("Report a Bug…"), QDialogButtonBox::ActionRole);
    reportBug->setObjectName(QStringLiteral("crashReportBug"));
    connect(reportBug, &QPushButton::clicked, this, [] { links::open(links::bugReportUrl(links::systemFacts())); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    Theme::padDialog(layout, true);
    layout->addWidget(label);
    layout->addWidget(text, 1);
    layout->addWidget(buttons);
}

void CrashReportDialog::copyReport() {
    QGuiApplication::clipboard()->setText(m_report);
    m_copy->setText(tr("Copied"));
    QTimer::singleShot(kCopiedMs, m_copy, [this] { m_copy->setText(tr("Copy report")); });
}
}
