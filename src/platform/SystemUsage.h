#pragma once

#include <QString>

#include <cstdint>
#include <optional>

namespace doodlebold::platform::usage {

struct Memory {
    std::uint64_t privateBytes = 0;   // only this process uses (Linux RssAnon, Windows private bytes)
    std::uint64_t residentBytes = 0;  // all of the process in RAM (resident set / working set)
    std::uint64_t totalBytes = 0;
};

std::optional<Memory> memory();
std::optional<double> driveFullness(const QString& path);

// Whole percent: 1% for anything under 1.5 %, never 0 % while something is used.
int percent(std::uint64_t part, std::uint64_t whole);

}
