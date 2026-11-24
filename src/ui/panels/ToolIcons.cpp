#include "ui/panels/ToolIcons.h"

namespace doodlebold {

QIcon toolTypeIcon(ToolType type) {
    const char* file = "pencil";
    switch (type) {
    case ToolType::Pencil: file = "pencil"; break;
    case ToolType::Eraser: file = "eraser"; break;
    case ToolType::Brush: file = "brush"; break;
    case ToolType::WaterColor: file = "watercolor"; break;
    case ToolType::Marker: file = "marker"; break;
    case ToolType::PixelArtPen: file = "pixelart"; break;
    case ToolType::Blur: file = "blur"; break;
    case ToolType::Smudge: file = "smudge"; break;
    case ToolType::SelectionPen: file = "select"; break;
    case ToolType::SelectionEraser: file = "deselect"; break;
    case ToolType::Clone: file = "clone"; break;
    case ToolType::Bucket: file = "bucket"; break;
    }
    return QIcon(QStringLiteral(":/icons/tool-%1.svg").arg(QLatin1String(file)));
}

QIcon fixedToolIcon(FixedTool tool) {
    const char* file = "select-marquee";
    switch (tool) {
    case FixedTool::Marquee: file = "select-marquee"; break;
    case FixedTool::Lasso: file = "select-lasso"; break;
    case FixedTool::MagicWand: file = "select-wand"; break;
    case FixedTool::Move: file = "tool-move"; break;
    case FixedTool::Zoom: file = "tool-zoom"; break;
    case FixedTool::Rotate: file = "rotate-cw"; break;
    case FixedTool::Hand: file = "hand"; break;
    case FixedTool::Eyedropper: file = "eyedropper"; break;
    case FixedTool::Text: file = "tool-text"; break;
    }
    return QIcon(QStringLiteral(":/icons/%1.svg").arg(QLatin1String(file)));
}

}
