#pragma once

#include "core/io/DoodleFormat.h"
#include "core/io/FileError.h"
#include "core/timelapse/Timelapse.h"

#include <QIODevice>

namespace doodlebold::timelapse_file {

// The chunks of a recording in a .doodle file, between the selection and END: TLPS (step count, paused, start
// time) and one TLST per step (kind, flags, painting time, canvas size, then each tile as x, y, length, compressed pixels).
inline constexpr char kHeaderChunk[] = "TLPS";
inline constexpr char kStepChunk[] = "TLST";
inline constexpr std::uint32_t kMaxSteps = 20'000'000;

bool write(QIODevice& device, const Timelapse& recording);

// Every length is checked against the file and every tile against its step's canvas before anything is allocated. A recording
// that does not end at the size of the picture (`canvas`) is dropped.
FileResult<Timelapse> read(QIODevice& device, const doodle_file::Chunk& header, const QSize& canvas);

}
