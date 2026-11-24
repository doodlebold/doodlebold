#pragma once

#include "core/document/DocumentState.h"
#include "core/timelapse/Timelapse.h"
#include "core/view/ViewState.h"

#include <QColor>

#include <memory>
#include <optional>

namespace doodlebold {

// Everything a file stores about a canvas apart from the undo history; a value type whose pixels are shared tiles, so taking it
// is cheap and a writer can run on it in another thread.
inline constexpr double kDefaultResolution = 72.0;  // what New Canvas starts from, and files without one mean

struct DocumentContent {
    DocumentState state;
    double resolution = kDefaultResolution;  // pixels per inch
    std::optional<ViewState> view;
    // The timelapse recording, shared with the document that made it; null: none. Only .doodle files keep it.
    std::shared_ptr<const Timelapse> timelapse;

    QSize size() const { return state.layers.size(); }
    const CanvasBackground& background() const { return state.background; }
};

}
