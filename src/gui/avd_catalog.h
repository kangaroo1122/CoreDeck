#pragma once

#include <string>
#include <vector>
#include "../core/options.h"

namespace CoreDeck {
    struct Context;
    void RefreshAvds(Context &context);
    void PollAvdRefresh(Context &context);
    void LoadAvdOptions(Context &context, const std::string &avdName);
    void SaveAvdOptions(Context &context, const std::string &avdName);
    std::vector<EmulatorOption> &GetDefaultAvdOptions(Context &context);
}
