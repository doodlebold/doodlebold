#include "core/stroke/Bristles.h"

#include <algorithm>

namespace doodlebold {

namespace {

// A pixel darker than this grey, of 255, is a hair. The reference's own maps hold 0 for each hair and 173 or lighter elsewhere; half
// way keeps an imported map's soft-edged dots one hair each at their middle pixel's darkness or more.
constexpr int kHairDarkerThan = 128;

}

std::shared_ptr<const Bristles> bristlesOf(const QImage& picture) {
    const QImage grey = picture.convertToFormat(QImage::Format_Grayscale8);
    const double half = std::max(grey.width(), grey.height()) / 2.0;
    auto bristles = std::make_shared<Bristles>();
    for (int y = 0; y < grey.height(); ++y) {
        const uchar* row = grey.constScanLine(y);
        for (int x = 0; x < grey.width(); ++x)
            if (row[x] < kHairDarkerThan)
                bristles->hairs.push_back({(x + 0.5 - grey.width() / 2.0) / half, (y + 0.5 - grey.height() / 2.0) / half});
    }
    if (bristles->hairs.empty())
        return nullptr;
    return bristles;
}

}
