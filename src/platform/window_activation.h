#ifndef COREDECK_WINDOW_ACTIVATION_H
#define COREDECK_WINDOW_ACTIVATION_H

#include "../core/process.h"

namespace CoreDeck {
    // Brings the visible top-level window owned by the process to the foreground.
    // The operation is best-effort because desktop environments may reject focus
    // changes that are not associated with recent user input.
    bool ActivateProcessWindow(ProcessId pid);
}

#endif // COREDECK_WINDOW_ACTIVATION_H
