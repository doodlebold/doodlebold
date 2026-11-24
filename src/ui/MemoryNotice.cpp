#include "ui/MemoryNotice.h"

#include "core/document/Document.h"
#include "core/options/AppOptions.h"
#include "core/paint/StrokeMemory.h"
#include "core/tools/ToolBox.h"

#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>

namespace doodlebold {

namespace {

constexpr const char* kShown = "notices/strokeMemoryShown";

QString px(int tenths) {
    return QString::number(tenths / 10.0);
}

}

MemoryNotice::MemoryNotice(ToolBox* tools, QObject* parent) : QObject(parent), m_tools(tools) {}

void MemoryNotice::watch(Document* doc) {
    if (m_doc)
        disconnect(m_doc, nullptr, this, nullptr);
    m_doc = doc;
    m_pending.reset();
    if (!doc)
        return;
    connect(doc, &Document::strokeLimited, this, [this](int chosen, int used) { heard(false, chosen, used); });
    connect(doc, &Document::strokeStopped, this, [this](int size) { heard(true, size, size); });
    connect(doc, &Document::paintingChanged, this, [this](bool painting) {
        // The stroke finishes ending after it says so: the notice comes once it has.
        if (!painting && m_pending)
            QTimer::singleShot(0, this, &MemoryNotice::strokeEnded);
    });
}

bool MemoryNotice::wasShown() {
    return AppOptions::settings().value(QLatin1String(kShown), false).toBool();
}

void MemoryNotice::setShown(bool shown) {
    AppOptions::settings().setValue(QLatin1String(kShown), shown);
}

// Whichever comes first, a brush limited as the stroke starts or a stroke stopping, is what the notice tells.
void MemoryNotice::heard(bool stopped, int chosen, int used) {
    if (!stroke_memory::policy().keepBrushesWithin) {
        if (stopped)
            emit noteWanted(stoppedNote());
        return;
    }
    if (m_pending || wasShown() || !m_doc)
        return;
    Event event;
    event.stopped = stopped;
    event.chosen = chosen;
    event.used = used;
    event.tool = m_tools ? m_tools->settings().tool : ToolType::Brush;
    m_pending = event;
    m_canvas = m_doc->size();
}

void MemoryNotice::strokeEnded() {
    if (!m_pending || wasShown())
        return;
    Event event = *std::exchange(m_pending, std::nullopt);
    BrushSettings widest = m_tools ? m_tools->settings() : defaultSettings(event.tool);
    widest.size = BrushSettings::kMaxSize;
    event.largestThatFits = stroke_memory::sizeLimit(widest, m_canvas).value_or(BrushSettings::kMaxSize);
    setShown(true);
    emit noticeWanted(event);
}

QString MemoryNotice::title() {
    return tr("Brush too large for this computer's memory");
}

QString MemoryNotice::body(const Event& event) {
    const QString first = event.stopped
                              ? tr("This stroke stopped partway: at %1 px it needs more memory than there is. What you painted is "
                                   "kept.")
                                    .arg(px(event.chosen))
                              : tr("The brush was kept to %1 px instead of %2 px: at %2 px it needs more memory than there is.")
                                    .arg(px(event.used), px(event.chosen));
    const QString limit = tr("From now on, brushes are kept to the largest size that fits. On this canvas the %1 goes up to %2 px.")
                              .arg(toolTypeName(event.tool), px(event.largestThatFits));
    const QString last = tr("This message won't show again. The limit can be turned off in Options › Performance › Memory.");
    return first + QStringLiteral("\n\n") + limit + QStringLiteral("\n\n") + last;
}

QString MemoryNotice::stoppedNote() {
    return tr("This stroke stopped partway: it needed more memory than there is. What you painted is kept.");
}

QMessageBox* MemoryNotice::makeBox(const Event& event, QWidget* parent) {
    // The title is in the box too, as its headline, where a window manager's title bar may not show it.
    auto* box = new QMessageBox(QMessageBox::Information, title(), title(), QMessageBox::NoButton, parent);
    box->setObjectName(QStringLiteral("memoryNotice"));
    box->setInformativeText(body(event));
    if (auto* headline = box->findChild<QLabel*>(QStringLiteral("qt_msgbox_label")))
        headline->setProperty("role", "strong");  // body-strong, from the style sheet
    QPushButton* options = box->addButton(tr("Open Performance options"), QMessageBox::ActionRole);
    options->setObjectName(QStringLiteral("memoryNoticeOptions"));
    QPushButton* ok = box->addButton(QMessageBox::Ok);
    box->setDefaultButton(ok);
    return box;
}

}
