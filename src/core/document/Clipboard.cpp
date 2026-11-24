#include "core/document/Clipboard.h"

#include "core/transform/Lift.h"

#include <algorithm>

namespace doodlebold {

ClipboardImage ClipboardImage::centred(const QImage& picture, const QSize& canvas) {
    return {picture.convertToFormat(QImage::Format_RGBA8888),
            QPoint((canvas.width() - picture.width()) / 2, (canvas.height() - picture.height()) / 2), QImage()};
}

namespace clipboard {

std::optional<ClipboardImage> copy(const TiledImage& pixels, const SelectionMask* selection) {
    TiledImage part = selection ? transform::liftSelection(pixels, *selection).part : pixels;
    const QRect bounds = part.contentBounds(true).intersected(part.rect());
    if (bounds.isEmpty())
        return std::nullopt;
    QImage image(bounds.size(), QImage::Format_RGBA8888);
    QImage onWhite(bounds.size(), QImage::Format_RGB888);
    for (int y = 0; y < bounds.height(); ++y) {
        std::uint8_t* out = image.scanLine(y);
        std::uint8_t* flat = onWhite.scanLine(y);
        for (int x = 0; x < bounds.width(); ++x, out += 4, flat += 3) {
            const auto p = part.pixel(bounds.left() + x, bounds.top() + y);
            const int a = p[3];
            for (int c = 0; c < 3; ++c) {
                out[c] = a == 0 ? 0 : static_cast<std::uint8_t>(std::min(255, (p[std::size_t(c)] * 255 + a / 2) / a));
                flat[c] = toUnorm8(p[std::size_t(c)] + (kChannelOne - a));
            }
            out[3] = toUnorm8(a);
        }
    }
    return ClipboardImage{std::move(image), bounds.topLeft(), std::move(onWhite)};
}

TiledImage toPixels(const ClipboardImage& clip, const QSize& canvas) {
    TiledImage out(canvas);
    const QImage img = clip.image.convertToFormat(QImage::Format_RGBA8888);
    const QRect area = QRect(clip.offset, img.size()).intersected(out.rect());
    for (int y = area.top(); y <= area.bottom(); ++y) {
        const std::uint8_t* in = img.constScanLine(y - clip.offset.y()) + (area.left() - clip.offset.x()) * 4;
        for (int x = area.left(); x <= area.right(); ++x, in += 4) {
            if (in[3] == 0)
                continue;
            const int a = fromUnorm8(in[3]);
            Channel* p = out.writableTile(x / Tile::kSize, y / Tile::kSize)->at(x % Tile::kSize, y % Tile::kSize);
            for (int c = 0; c < 3; ++c)
                p[c] = static_cast<Channel>(mulChannel(fromUnorm8(in[c]), a));
            p[3] = static_cast<Channel>(a);
        }
    }
    return out;
}

}
}
