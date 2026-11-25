#pragma once

#include <QString>

// Written as the program crashes and shown when it starts again, as a crashing program can't show anything itself.
namespace doodlebold::crash_report {

// Its file is opened here, not at the crash, as a crash may come with the heap broken. After QApplication, which names the app's
// data folder.
void install();
void uninstall();

// Taken, so a crash's report is shown at one start only; it's kept as lastReport().
QString takeNewReport();
QString lastReport();

[[noreturn]] void crashForTest();

}
