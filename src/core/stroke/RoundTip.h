#pragma once

#include "core/stroke/DabTip.h"
#include "core/stroke/StrokeBuffer.h"
#include "core/stroke/StrokeMerge.h"
#include "core/stroke/StampTip.h"
#include "core/stroke/StrokePath.h"
#include "core/stroke/StrokeReplay.h"

#include <QPointF>
#include <QRect>

#include <array>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace doodlebold {

struct RoundTip {
    double radius = 10.0;  // px, at full pressure; each tool sets its own (RebuiltStroke)
    bool followsPressure = true;
    double leastWidth = 0.0;  // the share of the radius left at no pressure
    int sizeSensitivity = 100;  // %, how width follows pressure: see pressureResponse()
    bool densityFollowsPressure = false;
    double leastDensity = 0.0;  // the share of the coverage left at no pressure
    int densitySensitivity = 100;  // %, how coverage follows pressure
    TipMixing mixing{};
    float fullAt = 1.0f;  // the merge's full point, for a plain tool's stamps on a grid (mixing.onGrid)
    // For stamps.
    double coverage = 1.0;  // what a long line reaches along its centre, 0 to 1: Density, times the coverage the merge counts as full
    int edge = 0;  // which of the five rims, from the softest
    double rimShare = 0.0;  // how much of the radius is soft rim: 1 - Sharpness
    bool followsCurve = false;  // see StampTip
    bool layDirect = false;  // see StampTip
    double solidFrom = 0.0;  // see StampTip
    bool insetOutline = false;
    double rimReach = 1e9;
    double rimHardensFrom = -1.0;
    double rimHardensTo = 1.0;
    double edgeBlur = 0.0;
    double rampWidth = 1.0;
    double softShare = 0.0;
    double softInset = 0.0;
    double edgeShift = 0.0;
    double evenSideFrom = 0.0;  // see StampTip
    double evenSideTo = 1.0;
    double gatherWidth = 0.0;  // see StampTip
    double startFuller = 0.0;  // see StampTip
    double startReach = 1.0;
    int skirtNeighbours = 0;  // see StampTip
    double mostOpacity = 1.0;
    int profile = -1;  // see StampTip
    bool densityAssist = true;  // see StampTip
    double spacingShare = 0.05;  // see StampTip
    double firmPower = 0.0;  // see StampTip
    bool penLaysFirstTwice = false;
    bool softSpacedWide = false;
    double thinnestRadius = 0.5;
    double leastSpacing = -1.0;  // see StampTip
    double tapShare = 0.0;
    double tapFullRadius = 1e9;
    double tapFalloff = 0.0;
    bool tapIsWalked = false;
    double clickShare = 0.0;  // see StampTip
    double clickPower = 1.0;
    bool hard = false;  // see StampTip
    std::shared_ptr<const StampForm> form{};  // see StampTip
    bool pictureStamps = false;  // see StampTip
    double pictureAngle = 0.0;
    bool pictureAlongStroke = false;
    double pictureSpacing = 0.25;  // see StampTip
    double pictureAngleJitter = 0.0;
    double pictureSizeJitter = 0.0;
    double pictureScatter = 0.0;
    double pictureFullAt = 1.0;  // see StampTip
    std::shared_ptr<const class PaperGrain> paperPerStamp{};  // see StampTip
    std::shared_ptr<const Bristles> bristles{};  // see StampTip
    double bristleSpread = 0.0;
    double hairRadius = 0.0;  // see StampTip
    double hairLeast = 1.0;  // see StampTip
    bool hairFollowsPressure = false;
    int hairSensitivity = 100;
    StampTip::HairWay hairWay = StampTip::HairWay::AlongStroke;  // see StampTip
    double hairWander = 0.0;  // see StampTip
    bool hairDetectTurn = false;  // see StampTip
};

// What pressure comes to for this tip's width and coverage.
PressureShares sharesFor(double pressure, const RoundTip& tip);

// A tip that mixes paints dab by dab, on stamps (stage 5). Every other tip lays stamps into the stroke's buffer.
inline bool laysStamps(const RoundTip& tip) {
    return !tip.mixing.isOn();
}

// Paints a round tip from a stroke's samples: as stamps, how every tool that doesn't mix paints, or once readFrom has said what
// it reads from, as dabs that mix.
class RoundTipEvaluator : public StrokeEvaluator {
public:
    enum class Threads {
        Workers,
        OneThread
    };  // the pixels are the same

    explicit RoundTipEvaluator(const RoundTip& tip, Threads threads = Threads::Workers);
    // What a mixing tip paints on, a copy of which it takes, and the paint it lays.
    void readFrom(const TiledImage* layer, const StrokePaint& paint);
    // For a mixing tip: the layer's tile as it's painted so far, which the stroke shows in place of merging. Otherwise null.
    TilePtr paintedTile(TileCoord coord) const;
    // And its outline at each pixel there (DabPainter::outline), or null.
    std::function<bool(float*)> paintedOutline(TileCoord coord) const;
    void begin(const InputSample& first, StrokeBuffer& buffer) override;
    void add(const InputSample& sample, StrokeBuffer& buffer) override;
    // The same as adding them one at a time, in fewer goes for the workers.
    void addAll(const std::vector<InputSample>& samples, StrokeBuffer& buffer, bool groupDabs = false);
    // Called between a pen event's dabs, on a tip that mixes (after readFrom).
    void setBetweenDabs(std::function<void()> betweenDabs);
    void setMemoryPressed(std::function<bool()> pressed);
    void end(StrokeBuffer& buffer) override;
    // The area painted since the last call.
    QRect takeDirty();
    // The tiles that took paint since the last call. Others under the tip were already solid.
    std::vector<TileCoord> takeChangedTiles();

private:
    void takeSample(const InputSample& sample);
    void layNewPoints(StrokeBuffer& buffer);

    RoundTip m_tip;
    Threads m_threads;
    std::optional<StampLayer> m_stamps;
    std::optional<DabPainter> m_dabs;
};

}
