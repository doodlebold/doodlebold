#pragma once

namespace doodlebold::timing_log {

// Debugging aid: DOODLEBOLD_TIMING_LOG=<file> appends a JSON line for each stroke, with the time from pen event to finished
// canvas frame. It stops at the app handing the frame on, so it leaves out the GPU, the desktop and the display.
struct StrokeContext {
    int tool = 0;
    double sizePx = 0.0;
    double zoom = 1.0;
    int canvasWidth = 0;
    int canvasHeight = 0;
    long long catchUpUs = 0;  // how long the stroke went on painting after the pen lifted
    long long finishUs = 0;  // how long the main thread took to finish it off after that
};

// Times handling one pen event. `counts` is false for an event that's not part of a stroke.
class EventScope {
public:
    explicit EventScope(bool counts);
    ~EventScope();
    EventScope(const EventScope&) = delete;
    EventScope& operator=(const EventScope&) = delete;

private:
    long long m_startedUs = -1;
};

bool isOn();
void eventReceived();
void mainThreadTick();
void lifted();
void eventStamped(unsigned long long sentMs);
void pixelsChanged();
// The canvas took new paint from the stroke onto the layer, to show in its next frame.
void paintShown();
// With DOODLEBOLD_TIMING_UPDATES, prints `what` and the time now on stderr, and how long it took if given.
void note(const char* what, long long tookUs = 0);
void frameStarted();
void frameFinished();
void frameSwapped();
// A frame is about to show what a pen event queued at `queuedUs` led to. It's on the same clock as here.
void inkShown(long long queuedUs);
void strokeFinished(const StrokeContext& context);

}
