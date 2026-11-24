#pragma once

#include <cstdint>

namespace doodlebold::selection {

// How the Magic Wand and the Bucket find the area around the clicked point.
enum class WandMode : std::uint8_t {
    EnclosedTransparent,  // the transparent area around the clicked point, closed in by lines
    SimilarColour,        // the connected area of pixels similar to the clicked one
    AllSimilar,           // every pixel of the image similar to the clicked one
};

}
