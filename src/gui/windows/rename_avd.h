//
// Created by AbdulMuaz Aqeel on 24/07/2026.
//

#ifndef COREDECK_RENAME_AVD_H
#define COREDECK_RENAME_AVD_H

#include "../context.h"

namespace CoreDeck {
    void OpenRenameAvdDialog(Context &context, const AvdInfo &avd);

    void BuildRenameAvdWindow(Context &context);
}

#endif // COREDECK_RENAME_AVD_H
