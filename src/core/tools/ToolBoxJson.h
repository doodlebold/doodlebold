#pragma once

#include "core/tools/ToolBox.h"
#include "core/util/Result.h"

#include <QJsonObject>
#include <QString>

namespace doodlebold {

enum class ToolBoxFileError {
    NoFile,       // there is no file (first start): not a problem
    Unreadable,
    Corrupt,
    OtherVersion, // written in another version of the format: only the current one is read
    WriteFailed,
};

QString describe(ToolBoxFileError e);

// The file format (tools.json). Only this version is read: a file of any other gives OtherVersion.
//   { "version": 10, "color": "#aarrggbb", "secondaryColor": "#aarrggbb", "current": {"group": g, "slot": i},
//     "options": {"stabilizer": {"value": n}},
//     "fixedTools": {"marquee": {…}, "lasso": {…}, "text": {…}, "wand": {…}},
//     "groups": [{"name": …, "cells": [null | {"tool": key, "name": …, "settings": {…}, "reserved": {…}, "key": …}, …]}] }
// A Bucket's settings hold "bucket": {"mode", "colourThreshold", "gapTolerance", "source", "antialias", "ignoreSelection"};
// a slot's "key" is the key that picks it, as QKeySequence's portable text. Both are left out when not used.
// "bucketPlaced": true marks a tray that has had the Bucket put on it; a file without it gets placeBucket once.
QJsonObject toJson(const ToolBoxState& state);
// The state in `json`, not yet repaired (ToolBox::setState does that). Corrupt when the layout is not recognisable.
Result<ToolBoxState, ToolBoxFileError> toolBoxStateFromJson(const QJsonObject& json);

}
