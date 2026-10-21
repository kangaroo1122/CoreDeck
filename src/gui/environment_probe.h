#pragma once

#include <string>

namespace CoreDeck {
    struct Context;
    struct JavaHomeStatus;

    bool RefreshJavaHomeStatusAsync(Context &context, JavaHomeStatus &state, const std::string &path);
    bool RefreshSdkPathStatusAsync(Context &context, const std::string &path);
}
