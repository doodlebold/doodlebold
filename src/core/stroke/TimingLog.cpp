#include "core/stroke/TimingLog.h"

#include "core/stroke/PaintTimer.h"

#include <QFile>
#include <QString>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>

namespace doodlebold::timing_log {

namespace {

// bytes: one summary line: its names take 362, the size, zoom and canvas at most 24, and its 22 counts fit while each is under 1e18;
// the types' widest line is 826 (7bbe3a5).
constexpr std::size_t kSummaryLineBytes = 800;

struct Log {
    QFile file;
    PaintTimer timer;
    int handling = 0;  // pen events being handled right now
    // A stroke that has ended, written after the frame that shows its last paint (so that frame counts in its own line, not the next
    // stroke's), or when the next stroke's first event comes, or when the app quits, whichever is first.
    std::optional<StrokeContext> finished;
    void writeFinished();
    ~Log() { writeFinished(); }
};

Log* log() {
    static const std::unique_ptr<Log> instance = []() -> std::unique_ptr<Log> {
        const QString path = qEnvironmentVariable("DOODLEBOLD_TIMING_LOG");
        if (path.isEmpty())
            return nullptr;
        auto opened = std::make_unique<Log>();
        opened->file.setFileName(path);
        if (opened->file.open(QIODevice::Append | QIODevice::Text))
            return opened;
        std::fprintf(stderr, "DOODLEBOLD_TIMING_LOG: cannot open %s\n", qPrintable(path));
        return nullptr;
    }();
    return instance.get();
}

std::int64_t nowUs() {
    const auto sinceStart = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::microseconds>(sinceStart).count();
}

void writeSummary(QFile& file, const PaintTimer::Summary& summary, const StrokeContext& context) {
    char line[kSummaryLineBytes];
    const int length = std::snprintf(
        line, sizeof line,
        "{\"tool\":%d,\"sizePx\":%.1f,\"zoom\":%.3f,\"canvas\":\"%dx%d\",\"catchUpUs\":%lld,\"finishUs\":%lld,\"events\":%zu,\"frames\":%"
        "zu,\"medianUs\":%lld,\"p99Us\":%lld,"
        "\"p999Us\":%lld,\"worstUs\":%lld,\"drawMedianUs\":%lld,\"drawWorstUs\":%lld,\"handleMedianUs\":%lld,\"handleWorstUs\":%lld,"
        "\"swapMedianUs\":%lld,\"swapWorstUs\":%lld,\"inkMedianUs\":%lld,\"inkWorstUs\":%lld,\"mainStallWorstUs\":%lld,"
        "\"lastLiftStallWorstUs\":%lld,\"backlogWorstUs\":%lld,\"updates\":%zu,\"updateGapWorstUs\":%lld,\"afterLiftUs\":%lld}\n",
        context.tool, context.sizePx, context.zoom, context.canvasWidth, context.canvasHeight, context.catchUpUs, context.finishUs,
        summary.events, summary.frames, static_cast<long long>(summary.medianUs), static_cast<long long>(summary.p99Us),
        static_cast<long long>(summary.p999Us), static_cast<long long>(summary.worstUs), static_cast<long long>(summary.drawMedianUs),
        static_cast<long long>(summary.drawWorstUs), static_cast<long long>(summary.handleMedianUs),
        static_cast<long long>(summary.handleWorstUs), static_cast<long long>(summary.swapMedianUs),
        static_cast<long long>(summary.swapWorstUs), static_cast<long long>(summary.inkMedianUs),
        static_cast<long long>(summary.inkWorstUs), static_cast<long long>(summary.mainStallWorstUs),
        static_cast<long long>(summary.lastLiftStallWorstUs), static_cast<long long>(summary.backlogWorstUs), summary.updates,
        static_cast<long long>(summary.updateGapWorstUs), static_cast<long long>(summary.afterLiftUs));
    // snprintf cuts a line that doesn't fit and returns the length it would have had. Every count under 1e18 fits; the widest line the
    // app's own bounds allow, every count at its type's widest, is 826 bytes. A line that doesn't fit is left out, not written cut: it
    // would end without its closing brace and newline, and the next line would run into it.
    if (length < 0 || static_cast<std::size_t>(length) >= sizeof line) {
        std::fprintf(stderr, "DOODLEBOLD_TIMING_LOG: a summary line of %d bytes doesn't fit in %zu; left out\n", length, sizeof line);
        return;
    }
    file.write(line, length);
    file.flush();
}

void Log::writeFinished() {
    if (!finished)
        return;
    writeSummary(file, timer.takeSummary(), *finished);
    finished.reset();
}

}

EventScope::EventScope(bool counts) {
    Log* opened = counts ? log() : nullptr;
    if (!opened)
        return;
    m_startedUs = nowUs();
    ++opened->handling;
}

EventScope::~EventScope() {
    if (m_startedUs < 0)
        return;
    Log* opened = log();
    opened->timer.eventHandled(nowUs() - m_startedUs);
    --opened->handling;
}

bool isOn() {
    return log() != nullptr;
}

void mainThreadTick() {
    if (Log* opened = log())
        opened->timer.mainThreadTick(nowUs());
}

void lifted() {
    if (Log* opened = log())
        opened->timer.lifted(nowUs());
    note("lift");
}

// DOODLEBOLD_TIMING_UPDATES=1 also prints each lift and update, and what finishing a stroke took, on stderr, with the time in us.
void note(const char* what, long long tookUs) {
    static const bool on = std::getenv("DOODLEBOLD_TIMING_UPDATES") != nullptr;
    if (on)
        std::fprintf(stderr, "timing %s at %lld took %lld\n", what, static_cast<long long>(nowUs()), tookUs);
}

void eventStamped(unsigned long long sentMs) {
    if (Log* opened = log())
        opened->timer.eventStamped(sentMs, nowUs());
}

void inkShown(long long queuedUs) {
    if (Log* opened = log())
        opened->timer.inkShown(nowUs() - queuedUs);
}

void frameSwapped() {
    if (Log* opened = log())
        opened->timer.frameSwapped(nowUs());
}

void eventReceived() {
    if (Log* opened = log()) {
        opened->writeFinished();  // the stroke before is over, whether or not a frame came
        opened->timer.eventReceived(nowUs());
    }
}

void pixelsChanged() {
    if (Log* opened = log())
        opened->timer.pixelsChanged();
}

void paintShown() {
    if (Log* opened = log())
        opened->timer.paintShown(nowUs());
    note("update");
}

void frameStarted() {
    if (Log* opened = log())
        opened->timer.frameStarted(nowUs());
}

void frameFinished() {
    if (Log* opened = log()) {
        opened->timer.frameFinished(nowUs());
        if (opened->handling == 0)
            opened->writeFinished();
    }
}

void strokeFinished(const StrokeContext& context) {
    Log* opened = log();
    if (!opened)
        return;
    opened->writeFinished();  // one still waiting for its frame
    opened->finished = context;
}

}
