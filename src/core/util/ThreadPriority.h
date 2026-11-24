#pragma once

namespace doodlebold {

// Makes the calling thread give way to the app's other threads, such as the one running the interface. Where a system offers no way
// to do this, it does nothing.
void lowerThisThreadsPriority();

}
