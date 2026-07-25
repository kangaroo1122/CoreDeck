//
// Created by AbdulMuaz Aqeel on 15/04/2026.
//

#include "tinyfiledialogs.h"

#include "file_dialog.h"

namespace CoreDeck::FileDialog {
    std::optional<std::string> PickFolder(const std::string &title, const std::string &defaultPath) {
        const char *result = tinyfd_selectFolderDialog(
            title.c_str(),
            defaultPath.empty() ? nullptr : defaultPath.c_str()
        );

        if (result == nullptr) {
            return std::nullopt;
        }
        return std::string(result);
    }

    std::optional<std::string> PickFile(
        const std::string &title,
        const char *const *filters,
        const int filterCount,
        const std::string &filterDescription,
        const std::string &defaultPath
    ) {
        const char *result = tinyfd_openFileDialog(
            title.c_str(),
            defaultPath.empty() ? nullptr : defaultPath.c_str(),
            filterCount,
            filters,
            filterDescription.empty() ? nullptr : filterDescription.c_str(),
            0
        );

        if (result == nullptr) {
            return std::nullopt;
        }
        return std::string(result);
    }
}
