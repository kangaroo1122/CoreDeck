#include "device_file.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <optional>
#include <sstream>
#include <string_view>

#include "adb.h"
#include "process.h"
#include "utilities.h"

namespace CoreDeck {
    namespace {
        std::string TrimTrailingSlash(std::string value) {
            while (value.size() > 1 && value.back() == '/') {
                value.pop_back();
            }
            return value.empty() ? "/" : value;
        }

        std::vector<std::string> SplitTabs(const std::string &line) {
            std::vector<std::string> parts;
            std::size_t start = 0;
            for (int i = 0; i < 4; i++) {
                const auto pos = line.find('\t', start);
                if (pos == std::string::npos) {
                    return {};
                }
                parts.push_back(line.substr(start, pos - start));
                start = pos + 1;
            }
            parts.push_back(line.substr(start));
            return parts;
        }

        DeviceFileKind KindFromStatType(const std::string &type) {
            const std::string lower = LowerCopy(type);
            if (lower.find("directory") != std::string::npos) {
                return DeviceFileKind::Directory;
            }
            if (lower.find("symbolic link") != std::string::npos) {
                return DeviceFileKind::Symlink;
            }
            if (lower.find("regular file") != std::string::npos) {
                return DeviceFileKind::File;
            }
            return DeviceFileKind::Other;
        }

        std::uintmax_t ParseSize(const std::string &value) {
            char *end = nullptr;
            const auto parsed = std::strtoull(value.c_str(), &end, 10);
            return end == value.c_str() ? 0 : static_cast<std::uintmax_t>(parsed);
        }

        std::int64_t ParseInt64(const std::string &value) {
            char *end = nullptr;
            const auto parsed = std::strtoll(value.c_str(), &end, 10);
            return end == value.c_str() ? 0 : static_cast<std::int64_t>(parsed);
        }

        std::string ShellQuote(const std::string &value) {
            std::string quoted = "'";
            for (const char c: value) {
                if (c == '\'') {
                    quoted += "'\\''";
                } else {
                    quoted.push_back(c);
                }
            }
            quoted.push_back('\'');
            return quoted;
        }

        std::string UrlEncode(const std::string &value) {
            constexpr char HEX[] = "0123456789ABCDEF";
            std::string encoded;
            for (const unsigned char ch: value) {
                if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
                    encoded.push_back(static_cast<char>(ch));
                    continue;
                }
                encoded.push_back('%');
                encoded.push_back(HEX[ch >> 4]);
                encoded.push_back(HEX[ch & 0x0F]);
            }
            return encoded;
        }

        std::optional<std::string> ExternalStorageDocumentUri(const std::string &normalizedPath) {
            constexpr std::string_view sdcardPrefix = "/sdcard";
            constexpr std::string_view emulatedPrefix = "/storage/emulated/0";

            std::string relativePath;
            if (normalizedPath == sdcardPrefix || normalizedPath == emulatedPrefix) {
                return "content://com.android.externalstorage.documents/root/primary";
            }
            if (normalizedPath.starts_with(std::string(sdcardPrefix) + "/")) {
                relativePath = normalizedPath.substr(sdcardPrefix.size() + 1);
            } else if (normalizedPath.starts_with(std::string(emulatedPrefix) + "/")) {
                relativePath = normalizedPath.substr(emulatedPrefix.size() + 1);
            } else {
                return std::nullopt;
            }

            return "content://com.android.externalstorage.documents/document/" +
                   UrlEncode(StrConcat("primary:", relativePath));
        }

        DeviceFileOperationResult RunAdbCommand(
            const SdkInfo &sdk,
            const std::vector<std::string> &args,
            const std::function<bool()> &shouldCancel = {}
        ) {
            DeviceFileOperationResult result;
            if (!HasAdb(sdk)) {
                result.Output = "ADB was not found.";
                return result;
            }

            std::string output;
            result.Success = StreamCommandArgsWithEnvCancelable(
                sdk.AdbPath,
                args,
                "",
                BuildAndroidToolEnvironment(sdk),
                [&output](const std::string &line) {
                    output += line;
                    output.push_back('\n');
                },
                shouldCancel
            );
            result.Output = std::move(output);
            return result;
        }

        std::vector<std::string> PathSegments(const std::string &path) {
            std::vector<std::string> segments;
            std::size_t start = 0;
            while (start < path.size()) {
                const auto slash = path.find('/', start);
                const std::string segment = path.substr(
                    start,
                    slash == std::string::npos ? std::string::npos : slash - start
                );
                if (!segment.empty()) {
                    segments.push_back(segment);
                }
                if (slash == std::string::npos) {
                    break;
                }
                start = slash + 1;
            }
            return segments;
        }

        std::string JoinSegments(const std::vector<std::string> &segments) {
            std::string result = "/";
            for (std::size_t i = 0; i < segments.size(); i++) {
                if (i > 0) {
                    result.push_back('/');
                }
                result += segments[i];
            }
            return result;
        }
    }

    std::string JoinDevicePath(const std::string &parent, const std::string &name) {
        if (name.empty()) {
            return TrimTrailingSlash(parent);
        }
        const std::string cleanParent = TrimTrailingSlash(parent.empty() ? "/" : parent);
        if (cleanParent == "/") {
            return "/" + name;
        }
        return cleanParent + "/" + name;
    }

    std::string ParentDevicePath(const std::string &path) {
        const std::string clean = TrimTrailingSlash(path.empty() ? "/" : path);
        if (clean == "/") {
            return "/";
        }
        const auto slash = clean.find_last_of('/');
        if (slash == std::string::npos || slash == 0) {
            return "/";
        }
        return clean.substr(0, slash);
    }

    bool IsValidDevicePathSegment(const std::string &name) {
        if (name.empty() || name == "." || name == ".." || name.find('/') != std::string::npos) {
            return false;
        }
        return std::ranges::any_of(name, [](const unsigned char ch) {
            return !std::isspace(ch);
        });
    }

    std::string NormalizeDevicePath(const std::string &path, const std::string &defaultDirectory) {
        const std::string normalizedDefault = TrimTrailingSlash(defaultDirectory.empty() ? "/sdcard" : defaultDirectory);
        std::vector<std::string> segments = PathSegments(path.empty() || path.front() != '/' ? normalizedDefault : "");
        for (const auto &segment: PathSegments(path)) {
            if (segment == ".") {
                continue;
            }
            if (segment == "..") {
                if (!segments.empty()) {
                    segments.pop_back();
                }
                continue;
            }
            segments.push_back(segment);
        }

        return JoinSegments(segments);
    }

    std::vector<DeviceFileEntry> ParseDeviceFileStatList(const std::string &output, const std::string &directory) {
        std::vector<DeviceFileEntry> entries;
        std::istringstream lines(output);
        std::string line;
        while (std::getline(lines, line)) {
            if (line.empty()) {
                continue;
            }
            const auto parts = SplitTabs(line);
            if (parts.size() != 5 || parts[4].empty() || parts[4] == "." || parts[4] == "..") {
                continue;
            }

            DeviceFileEntry entry;
            entry.Permissions = parts[0];
            entry.Kind = KindFromStatType(parts[1]);
            entry.SizeBytes = ParseSize(parts[2]);
            entry.ModifiedEpochSeconds = ParseInt64(parts[3]);
            entry.Name = parts[4];
            entry.Path = JoinDevicePath(directory, entry.Name);
            entries.push_back(std::move(entry));
        }

        std::ranges::sort(entries, [](const DeviceFileEntry &a, const DeviceFileEntry &b) {
            if (a.Kind == DeviceFileKind::Directory && b.Kind != DeviceFileKind::Directory) {
                return true;
            }
            if (a.Kind != DeviceFileKind::Directory && b.Kind == DeviceFileKind::Directory) {
                return false;
            }
            return LowerCopy(a.Name) < LowerCopy(b.Name);
        });
        return entries;
    }

    DeviceFileListResult ListDeviceFiles(
        const SdkInfo &sdk,
        const std::string &serial,
        const std::string &path,
        const std::function<bool()> &shouldCancel
    ) {
        DeviceFileListResult result;
        result.Path = NormalizeDevicePath(path);
        if (serial.empty()) {
            result.Error = "No device selected.";
            return result;
        }

        const std::string quotedPath = ShellQuote(result.Path);
        const std::string statFormat = "%A\t%F\t%s\t%Y\t%n";
        const std::string script = StrConcat(
            "dir=", quotedPath, "; ",
            "if [ ! -d \"$dir\" ]; then echo \"Not a directory: $dir\"; exit 1; fi; ",
            "cd \"$dir\" || exit 1; ",
            "for f in .* *; do ",
            "[ \"$f\" = \".\" ] && continue; ",
            "[ \"$f\" = \"..\" ] && continue; ",
            "[ -e \"$f\" ] || [ -L \"$f\" ] || continue; ",
            "stat -c ", ShellQuote(statFormat), " -- \"$f\"; ",
            "done"
        );
        const auto operation = RunAdbCommand(sdk, {"-s", serial, "shell", script}, shouldCancel);
        if (!operation.Success) {
            result.Error = operation.Output.empty() ? "Could not list device files." : operation.Output;
            return result;
        }

        result.Entries = ParseDeviceFileStatList(operation.Output, result.Path);
        result.Success = true;
        return result;
    }

    DeviceFileOperationResult PullDevicePath(
        const SdkInfo &sdk,
        const std::string &serial,
        const std::string &remotePath,
        const std::string &localPath,
        const std::function<bool()> &shouldCancel
    ) {
        if (serial.empty()) {
            return {.Success = false, .Output = "No device selected."};
        }
        const std::string normalized = NormalizeDevicePath(remotePath);
        return RunAdbCommand(sdk, {"-s", serial, "pull", normalized, localPath}, shouldCancel);
    }

    DeviceFileOperationResult PushLocalPath(
        const SdkInfo &sdk,
        const std::string &serial,
        const std::string &localPath,
        const std::string &remoteDirectory,
        const std::function<bool()> &shouldCancel
    ) {
        if (serial.empty()) {
            return {.Success = false, .Output = "No device selected."};
        }
        const std::string normalized = NormalizeDevicePath(remoteDirectory);
        return RunAdbCommand(sdk, {"-s", serial, "push", localPath, normalized}, shouldCancel);
    }

    DeviceFileOperationResult DeleteDevicePath(
        const SdkInfo &sdk,
        const std::string &serial,
        const std::string &remotePath,
        const std::function<bool()> &shouldCancel
    ) {
        if (serial.empty()) {
            return {.Success = false, .Output = "No device selected."};
        }
        if (remotePath.empty()) {
            return {.Success = false, .Output = "No device path selected."};
        }
        const std::string normalized = NormalizeDevicePath(remotePath);
        return RunAdbCommand(sdk, {"-s", serial, "shell", StrConcat("rm -rf -- ", ShellQuote(normalized))}, shouldCancel);
    }

    DeviceFileOperationResult CreateDeviceDirectory(
        const SdkInfo &sdk,
        const std::string &serial,
        const std::string &remotePath,
        const std::function<bool()> &shouldCancel
    ) {
        if (serial.empty()) {
            return {.Success = false, .Output = "No device selected."};
        }
        const std::string normalized = NormalizeDevicePath(remotePath);
        return RunAdbCommand(sdk, {"-s", serial, "shell", StrConcat("mkdir -p -- ", ShellQuote(normalized))}, shouldCancel);
    }

    std::vector<std::string> BuildOpenDeviceDirectoryInEmulatorArgs(
        const std::string &serial,
        const std::string &remotePath
    ) {
        const std::string normalized = NormalizeDevicePath(remotePath);
        if (const auto uri = ExternalStorageDocumentUri(normalized)) {
            return {
                "-s",
                serial,
                "shell",
                "am",
                "start",
                "-a",
                "android.intent.action.VIEW",
                "-d",
                *uri,
                "-t",
                "vnd.android.document/directory",
            };
        }

        return {
            "-s",
            serial,
            "shell",
            "am",
            "start",
            "-a",
            "android.intent.action.VIEW",
            "-d",
            StrConcat("file://", normalized),
            "-t",
            "resource/folder",
        };
    }

    DeviceFileOperationResult OpenDeviceDirectoryInEmulator(
        const SdkInfo &sdk,
        const std::string &serial,
        const std::string &remotePath,
        const std::function<bool()> &shouldCancel
    ) {
        if (serial.empty()) {
            return {.Success = false, .Output = "No device selected."};
        }
        if (remotePath.empty()) {
            return {.Success = false, .Output = "No device path selected."};
        }

        const std::string normalized = NormalizeDevicePath(remotePath);
        const auto created = CreateDeviceDirectory(sdk, serial, normalized, shouldCancel);
        if (!created.Success) {
            return created;
        }
        return RunAdbCommand(sdk, BuildOpenDeviceDirectoryInEmulatorArgs(serial, normalized), shouldCancel);
    }
}
