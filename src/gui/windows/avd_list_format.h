#ifndef COREDECK_AVD_LIST_FORMAT_H
#define COREDECK_AVD_LIST_FORMAT_H

#include <string>

namespace CoreDeck {
    inline std::string FormatAvdListMetadata(
        const std::string &apiLevel,
        const std::string &type,
        const std::string &status
    ) {
        if (apiLevel.empty()) {
            return type + " - " + status;
        }
        return "API " + apiLevel + " - " + type + " - " + status;
    }
}

#endif // COREDECK_AVD_LIST_FORMAT_H
