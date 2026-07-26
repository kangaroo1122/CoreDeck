//
// Created by AbdulMuaz Aqeel on 19/04/2026.
//

#include "jdk.h"

#include <filesystem>
#include <sstream>

#include "paths.h"
#include "process.h"

namespace CoreDeck {
    namespace {
        std::string FirstNonEmptyLine(const std::string &text) {
            std::istringstream stream(text);
            std::string line;
            while (std::getline(stream, line)) {
                while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) {
                    line.pop_back();
                }
                const auto start = line.find_first_not_of(" \t");
                if (start != std::string::npos) {
                    return line.substr(start);
                }
            }
            return "";
        }
    }

    std::string JavaExecutablePath(const std::string &path) {
#if defined(_WIN32)
        return Paths::JoinPaths({path, "bin", "java.exe"});
#else
        return Paths::JoinPaths({path, "bin", "java"});
#endif
    }

    std::string NormalizeJavaHomePath(const std::string &path) {
        if (path.empty() || std::filesystem::exists(JavaExecutablePath(path))) {
            return path;
        }

        const std::string bundleHome = Paths::JoinPaths({path, "Contents", "Home"});
        if (std::filesystem::exists(JavaExecutablePath(bundleHome))) {
            return bundleHome;
        }
        return path;
    }

    bool LooksLikeJavaHome(const std::string &path) {
        return path.empty() || std::filesystem::exists(JavaExecutablePath(NormalizeJavaHomePath(path)));
    }

    int JavaMajorVersionFromText(const std::string &text) {
        const std::size_t firstDigit = text.find_first_of("0123456789");
        if (firstDigit == std::string::npos) {
            return 0;
        }

        auto parseNumber = [&](std::size_t &pos) {
            int value = 0;
            while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') {
                value = (value * 10) + (text[pos] - '0');
                pos++;
            }
            return value;
        };

        std::size_t pos = firstDigit;
        const int first = parseNumber(pos);
        if (first == 1 && pos < text.size() && text[pos] == '.') {
            pos++;
            return parseNumber(pos);
        }
        return first;
    }

    JavaHomeStatus ReadJavaHomeStatus(const std::string &javaHomePath) {
        JavaHomeStatus status;
        status.Path = NormalizeJavaHomePath(javaHomePath);

        if (status.Path.empty()) {
#if defined(__APPLE__)
            const std::string javaHome = FirstNonEmptyLine(RunCommandArgs("/usr/libexec/java_home", {}));
            const std::string output = javaHome.empty() ? "" : RunCommandArgs(JavaExecutablePath(javaHome), {"-version"});
#else
            const std::string output = RunCommandArgs("java", {"-version"});
#endif
            const std::string firstLine = FirstNonEmptyLine(output);
            if (firstLine.empty()) {
                status.Text = "No Java environment detected.";
                return status;
            }
            status.HasJava = true;
            status.Text = firstLine;
            status.MajorVersion = JavaMajorVersionFromText(status.Text);
            return status;
        }

        const std::string javaPath = JavaExecutablePath(status.Path);
        if (!std::filesystem::exists(javaPath)) {
            status.Text = "No java executable found under bin.";
            return status;
        }

        status.HasJava = true;
        const std::string output = RunCommandArgs(javaPath, {"-version"});
        const std::string firstLine = FirstNonEmptyLine(output);
        status.Text = firstLine.empty() ? "Unable to read Java version." : firstLine;
        status.MajorVersion = JavaMajorVersionFromText(status.Text);
        return status;
    }

    void RefreshJavaHomeStatus(JavaHomeStatus &state, const std::string &javaHomePath) {
        const std::string normalizedPath = NormalizeJavaHomePath(javaHomePath);
        if (state.Path == normalizedPath && !state.Text.empty()) {
            return;
        }
        state = ReadJavaHomeStatus(normalizedPath);
    }
}
