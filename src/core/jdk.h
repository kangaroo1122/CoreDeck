//
// Created by AbdulMuaz Aqeel on 19/04/2026.
//

#ifndef COREDECK_JDK_H
#define COREDECK_JDK_H

#include <string>

namespace CoreDeck {
    struct JavaHomeStatus {
        std::string Path;
        std::string Text;
        bool HasJava = false;
        int MajorVersion = 0;
    };

    std::string JavaExecutablePath(const std::string &path);

    std::string NormalizeJavaHomePath(const std::string &path);

    bool LooksLikeJavaHome(const std::string &path);

    JavaHomeStatus ReadJavaHomeStatus(const std::string &javaHomePath);

    void RefreshJavaHomeStatus(JavaHomeStatus &state, const std::string &javaHomePath);

    int JavaMajorVersionFromText(const std::string &text);
}

#endif // COREDECK_JDK_H
