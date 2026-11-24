#pragma once

#include "core/animation/Animation.h"

#include <QColor>
#include <QCoreApplication>
#include <QString>

#include <array>

namespace doodlebold::track_labels {

// The label colours a track can be given to sort tracks by (feedback: "similar to how after effects does it"): eight hues
// spread round the wheel, mid-light so a row's strip reads on the dark panels and a lane's faint tint doesn't hide the
// drawings. Chosen by eye; the names say what each is.
struct Label {
    QRgb colour;
    const char* name;
};
inline constexpr std::array<Label, AnimationTrack::kLabels> kLabels = {{{0xffe0524e, "Red"},
                                                                        {0xffe8c547, "Yellow"},
                                                                        {0xff7fd1a8, "Sea Foam"},
                                                                        {0xff49b6c9, "Aqua"},
                                                                        {0xff4a7de0, "Blue"},
                                                                        {0xffa58be0, "Lavender"},
                                                                        {0xffe88ab8, "Pink"},
                                                                        {0xffe8a06a, "Peach"}}};

// A track's label colour (1 … kLabels); invalid for none.
inline QColor colour(int label) {
    return label >= 1 && label <= AnimationTrack::kLabels ? QColor::fromRgba(kLabels[static_cast<std::size_t>(label - 1)].colour)
                                                          : QColor();
}

inline QString name(int label) {
    return label >= 1 && label <= AnimationTrack::kLabels
               ? QCoreApplication::translate("TrackLabels", kLabels[static_cast<std::size_t>(label - 1)].name)
               : QCoreApplication::translate("TrackLabels", "None");
}

}
