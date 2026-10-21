//
// Created by AbdulMuaz Aqeel on 18/04/2026.
//

#ifndef COREDECK_PREFERENCES_H
#define COREDECK_PREFERENCES_H

#include "../context.h"

namespace CoreDeck {
    // Call after cancellation has been requested for all modules.
    void ShutdownSdkPreferencesWork(Context &context);

    bool BuildPreferencesWindow(Context &context);
}

#endif // COREDECK_PREFERENCES_H
