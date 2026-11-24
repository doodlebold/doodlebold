#include "ui/AboutDialog.h"

#include "ui/Theme.h"

#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QFile>
#include <QFont>
#include <QFontMetrics>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace doodlebold {

namespace {

constexpr int kIconSize = 93;  // 45 % larger than the 64 px it was, a design choice (component metric)

// A scrollable, read-only window with the third-party notices embedded in the program (THIRD-PARTY-NOTICES.txt).
void showNotices(QWidget* parent) {
    QFile file(QStringLiteral(":/THIRD-PARTY-NOTICES.txt"));
    const QString notices = file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();

    auto* dialog = new QDialog(parent);
    dialog->setObjectName(QStringLiteral("noticesDialog"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(QObject::tr("Third-party licences"));
    auto* view = new QPlainTextEdit(dialog);
    view->setObjectName(QStringLiteral("noticesText"));
    view->setReadOnly(true);
    view->setFont(Theme::font(Theme::TextRole::Mono));
    view->setPlainText(notices);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    auto* layout = new QVBoxLayout(dialog);
    Theme::padDialog(layout, true);
    layout->addWidget(view);
    layout->addWidget(buttons);
    dialog->resize(QFontMetrics(view->font()).averageCharWidth() * 90, 480);
    dialog->open();
}

}

AboutDialog::AboutDialog(QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("aboutDialog"));
    setWindowTitle(tr("About Doodlebold"));

    auto* icon = new QLabel(this);
    icon->setObjectName(QStringLiteral("aboutIcon"));
    icon->setPixmap(QIcon(QStringLiteral(":/icons/doodlebold.png")).pixmap(kIconSize));
    icon->setAlignment(Qt::AlignCenter);  // in the middle of its column, from the top of the name to the end of the text

    auto* name = new QLabel(QStringLiteral("Doodlebold"), this);
    name->setFont(Theme::font(Theme::TextRole::Heading));
    auto* version = new QLabel(tr("Version %1").arg(QCoreApplication::applicationVersion()), this);
    version->setObjectName(QStringLiteral("aboutVersion"));
    auto* built = new QLabel(tr("Built with Qt %1 (LGPLv3)").arg(QString::fromLatin1(qVersion())), this);
    built->setObjectName(QStringLiteral("aboutQt"));
    auto* about = new QLabel(tr("A free, open-source digital-painting program."), this);
    about->setWordWrap(true);

    auto* text = new QVBoxLayout;
    text->setSpacing(Theme::metric(Theme::Metric::FieldSpacing));
    text->addWidget(name);
    text->addWidget(version);
    text->addWidget(built);
    text->addWidget(about);
    auto* header = new QHBoxLayout;
    header->setSpacing(Theme::metric(Theme::Metric::DialogPadding));
    header->addWidget(icon);
    header->addLayout(text);

    auto* form = new QFormLayout;
    Theme::spaceFields(form);
    form->setLabelAlignment(Qt::AlignRight);
    form->addRow(tr("Author :"), new QLabel(QStringLiteral("doodlebold"), this));
    form->addRow(tr("Licence :"), new QLabel(tr("MIT"), this));
    // No repository or issue links: Doodlebold's repository was renamed, and this version's isn't public yet.

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto* licences = buttons->addButton(tr("Third-party licences"), QDialogButtonBox::ActionRole);
    licences->setObjectName(QStringLiteral("aboutLicences"));
    connect(licences, &QPushButton::clicked, this, [this] { showNotices(this); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    Theme::padDialog(layout);
    layout->addLayout(header);
    layout->addLayout(form);
    layout->addWidget(buttons);
}

}
