#include "shared_folder.h"

#include <cerrno>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <map>
#include <set>
#include <string_view>
#include <system_error>
#include <utility>

#include "adb.h"
#include "device_file.h"
#include "paths.h"
#include "process.h"

namespace CoreDeck {
    namespace {
        constexpr const char *UNSUPPORTED_SYMLINK_ERROR = "Symbolic links are not supported in shared folder.";

        std::string TrimCopy(const std::string &value) {
            const auto start = value.find_first_not_of(" \t\r\n");
            if (start == std::string::npos) {
                return "";
            }
            const auto end = value.find_last_not_of(" \t\r\n");
            return value.substr(start, end - start + 1);
        }

        std::string SanitizeFolderName(const std::string &value) {
            std::string result;
            for (const unsigned char ch: TrimCopy(value)) {
                if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.') {
                    result.push_back(static_cast<char>(ch));
                } else {
                    result.push_back('_');
                }
            }
            return result == "." || result == ".." ? "" : result;
        }

        SharedFolderSyncResult Failure(std::string message) {
            return {.Success = false, .Output = std::move(message)};
        }

        struct PullSharedFolderResult : SharedFolderSyncResult {
            std::filesystem::path StagingPath;
            std::string HostPath;
        };

        PullSharedFolderResult PullFailure(std::string message) {
            PullSharedFolderResult result;
            result.Success = false;
            result.Output = std::move(message);
            return result;
        }

        SharedFolderSyncResult RunSharedFolderAdbCommand(
            const SdkInfo &sdk,
            const std::vector<std::string> &args,
            const std::function<bool()> &shouldCancel
        ) {
            if (!HasAdb(sdk)) {
                return Failure("ADB was not found.");
            }

            std::string output;
            const bool ok = StreamCommandArgsWithEnvCancelable(
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
            return {.Success = ok, .Output = std::move(output)};
        }

        std::string BuildAvdStateFolderName(const std::string &avdName) {
            std::string folderName = SanitizeFolderName(avdName);
            if (folderName.empty()) {
                folderName = "default";
            }
            return folderName;
        }

        std::filesystem::path BuildStagingPath(const std::string &avdName) {
            const std::string folderName = BuildAvdStateFolderName(avdName);
            return Paths::JoinPaths({Paths::GetAppConfigPath("shared_staging"), folderName});
        }

        std::filesystem::path BuildSnapshotPath(const std::string &avdName) {
            return Paths::JoinPaths({Paths::GetAppConfigPath("shared_state"), BuildAvdStateFolderName(avdName) + ".txt"});
        }

        bool IsSafeSnapshotRelativePath(const std::string &relativePath) {
            if (relativePath.empty()) {
                return false;
            }

            const std::filesystem::path path(relativePath);
            if (path.is_absolute() || path.has_root_name()) {
                return false;
            }

            for (const auto &part: path) {
                const std::string segment = part.string();
                if (segment.empty() || segment == "." || segment == "..") {
                    return false;
                }
            }
            return true;
        }

        struct FileState {
            std::uintmax_t SizeBytes = 0;
            std::int64_t ModifiedTick = 0;
            std::uint64_t Hash = 0;
            bool HasMetadata = false;
        };

        using FileStateMap = std::map<std::string, FileState>;

        bool ParseUintmax(const std::string &value, std::uintmax_t *out) {
            if (out == nullptr || value.empty()) {
                return false;
            }

            errno = 0;
            char *end = nullptr;
            const unsigned long long parsed = std::strtoull(value.c_str(), &end, 10);
            if (errno != 0 || end == value.c_str() || *end != '\0') {
                return false;
            }
            *out = static_cast<std::uintmax_t>(parsed);
            return true;
        }

        bool ParseInt64(const std::string &value, std::int64_t *out) {
            if (out == nullptr || value.empty()) {
                return false;
            }

            errno = 0;
            char *end = nullptr;
            const long long parsed = std::strtoll(value.c_str(), &end, 10);
            if (errno != 0 || end == value.c_str() || *end != '\0') {
                return false;
            }
            *out = static_cast<std::int64_t>(parsed);
            return true;
        }

        bool ParseUint64(const std::string &value, std::uint64_t *out) {
            if (out == nullptr || value.empty()) {
                return false;
            }

            errno = 0;
            char *end = nullptr;
            const unsigned long long parsed = std::strtoull(value.c_str(), &end, 10);
            if (errno != 0 || end == value.c_str() || *end != '\0') {
                return false;
            }
            *out = static_cast<std::uint64_t>(parsed);
            return true;
        }

        bool HashFile(const std::filesystem::path &path, std::uint64_t *hash, std::string *error) {
            if (hash == nullptr) {
                return false;
            }

            std::ifstream file(path, std::ios::binary);
            if (!file.is_open()) {
                if (error != nullptr) {
                    *error = "Could not read shared folder file.";
                }
                return false;
            }

            std::uint64_t value = 14695981039346656037ULL;
            char buffer[65536] = {};
            while (file.read(buffer, sizeof(buffer)) || file.gcount() > 0) {
                for (std::streamsize i = 0; i < file.gcount(); i++) {
                    value ^= static_cast<unsigned char>(buffer[i]);
                    value *= 1099511628211ULL;
                }
            }
            if (file.bad()) {
                if (error != nullptr) {
                    *error = "Could not read shared folder file.";
                }
                return false;
            }

            *hash = value;
            return true;
        }

        bool CollectRelativeFileStates(
            const std::filesystem::path &rootPath,
            FileStateMap *files,
            std::string *error
        ) {
            if (files == nullptr) {
                return false;
            }
            files->clear();

            std::error_code ec;
            if (!std::filesystem::is_directory(rootPath, ec) || ec) {
                if (error != nullptr) {
                    *error = ec ? ec.message() : "Shared folder path is not a directory.";
                }
                return false;
            }

            std::filesystem::recursive_directory_iterator it(
                rootPath,
                std::filesystem::directory_options::skip_permission_denied,
                ec
            );
            if (ec) {
                if (error != nullptr) {
                    *error = ec.message();
                }
                return false;
            }

            const std::filesystem::recursive_directory_iterator end;
            while (it != end) {
                const auto status = it->symlink_status(ec);
                if (ec) {
                    if (error != nullptr) {
                        *error = ec.message();
                    }
                    return false;
                }
                if (std::filesystem::is_symlink(status)) {
                    if (error != nullptr) {
                        *error = UNSUPPORTED_SYMLINK_ERROR;
                    }
                    return false;
                }
                if (!std::filesystem::is_regular_file(status)) {
                    it.increment(ec);
                    if (ec) {
                        if (error != nullptr) {
                            *error = ec.message();
                        }
                        return false;
                    }
                    continue;
                }

                std::filesystem::path relative = std::filesystem::relative(it->path(), rootPath, ec);
                if (ec) {
                    if (error != nullptr) {
                        *error = ec.message();
                    }
                    return false;
                }

                FileState state;
                state.HasMetadata = true;
                state.SizeBytes = it->file_size(ec);
                if (ec) {
                    if (error != nullptr) {
                        *error = ec.message();
                    }
                    return false;
                }

                const auto modified = it->last_write_time(ec);
                if (ec) {
                    if (error != nullptr) {
                        *error = ec.message();
                    }
                    return false;
                }
                state.ModifiedTick = modified.time_since_epoch().count();
                if (!HashFile(it->path(), &state.Hash, error)) {
                    return false;
                }
                (*files)[relative.generic_string()] = state;

                it.increment(ec);
                if (ec) {
                    if (error != nullptr) {
                        *error = ec.message();
                    }
                    return false;
                }
            }
            return true;
        }

        bool ParseSnapshotV2Line(const std::string &line, std::string *relativePath, FileState *state) {
            static constexpr std::string_view PREFIX = "v2\t";
            if (!line.starts_with(PREFIX)) {
                return false;
            }

            std::size_t start = PREFIX.size();
            const auto sizeEnd = line.find('\t', start);
            if (sizeEnd == std::string::npos) {
                return false;
            }
            const auto timeEnd = line.find('\t', sizeEnd + 1);
            if (timeEnd == std::string::npos) {
                return false;
            }
            const auto hashEnd = line.find('\t', timeEnd + 1);
            if (hashEnd == std::string::npos) {
                return false;
            }

            FileState parsed;
            if (!ParseUintmax(line.substr(start, sizeEnd - start), &parsed.SizeBytes) ||
                !ParseInt64(line.substr(sizeEnd + 1, timeEnd - sizeEnd - 1), &parsed.ModifiedTick) ||
                !ParseUint64(line.substr(timeEnd + 1, hashEnd - timeEnd - 1), &parsed.Hash)) {
                return false;
            }

            std::string parsedPath = line.substr(hashEnd + 1);
            if (!IsSafeSnapshotRelativePath(parsedPath)) {
                return false;
            }

            parsed.HasMetadata = true;
            if (relativePath != nullptr) {
                *relativePath = std::move(parsedPath);
            }
            if (state != nullptr) {
                *state = parsed;
            }
            return true;
        }

        bool LoadSharedFolderSnapshot(
            const std::string &snapshotPath,
            FileStateMap *files,
            std::string *error
        ) {
            if (files == nullptr) {
                return false;
            }
            files->clear();

            std::error_code ec;
            if (snapshotPath.empty() || !std::filesystem::exists(snapshotPath, ec)) {
                return !ec;
            }
            if (ec) {
                if (error != nullptr) {
                    *error = ec.message();
                }
                return false;
            }

            std::ifstream file(snapshotPath);
            if (!file.is_open()) {
                if (error != nullptr) {
                    *error = "Could not read shared folder sync state.";
                }
                return false;
            }

            std::string line;
            while (std::getline(file, line)) {
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }

                if (line.starts_with("v2\t")) {
                    std::string relativePath;
                    FileState state;
                    if (!ParseSnapshotV2Line(line, &relativePath, &state)) {
                        if (error != nullptr) {
                            *error = "Could not read shared folder sync state.";
                        }
                        return false;
                    }
                    (*files)[relativePath] = state;
                    continue;
                }

                if (IsSafeSnapshotRelativePath(line)) {
                    (*files)[line] = FileState{};
                }
            }
            return true;
        }

        bool CopyFilePreservingTime(
            const std::filesystem::path &source,
            const std::filesystem::path &target,
            std::string *error
        ) {
            std::error_code ec;
            const auto sourceTime = std::filesystem::last_write_time(source, ec);
            if (ec) {
                if (error != nullptr) {
                    *error = ec.message();
                }
                return false;
            }

            const std::filesystem::path parent = target.parent_path();
            if (!parent.empty() && std::filesystem::exists(parent, ec) && !ec && !std::filesystem::is_directory(parent, ec)) {
                return true;
            }
            if (ec) {
                if (error != nullptr) {
                    *error = ec.message();
                }
                return false;
            }

            std::filesystem::create_directories(target.parent_path(), ec);
            if (ec) {
                if (error != nullptr) {
                    *error = ec.message();
                }
                return false;
            }

            std::filesystem::copy_file(source, target, std::filesystem::copy_options::overwrite_existing, ec);
            if (ec) {
                if (error != nullptr) {
                    *error = ec.message();
                }
                return false;
            }
            std::filesystem::last_write_time(target, sourceTime, ec);
            if (ec) {
                if (error != nullptr) {
                    *error = ec.message();
                }
                return false;
            }
            return true;
        }

        bool CopySharedFolderFile(
            const std::filesystem::path &sourceRoot,
            const std::filesystem::path &targetRoot,
            const std::string &relativePath,
            std::string *error
        ) {
            if (!IsSafeSnapshotRelativePath(relativePath)) {
                return true;
            }

            const std::filesystem::path target = targetRoot / std::filesystem::path(relativePath);
            std::error_code ec;
            if (std::filesystem::exists(target, ec) && !ec) {
                const auto status = std::filesystem::symlink_status(target, ec);
                if (ec) {
                    if (error != nullptr) {
                        *error = ec.message();
                    }
                    return false;
                }
                if (std::filesystem::is_symlink(status)) {
                    if (error != nullptr) {
                        *error = UNSUPPORTED_SYMLINK_ERROR;
                    }
                    return false;
                }
                if (!std::filesystem::is_regular_file(status)) {
                    return true;
                }
            } else if (ec) {
                if (error != nullptr) {
                    *error = ec.message();
                }
                return false;
            }

            return CopyFilePreservingTime(
                sourceRoot / std::filesystem::path(relativePath),
                target,
                error
            );
        }

        bool RemoveSharedFolderFile(
            const std::filesystem::path &root,
            const std::string &relativePath,
            std::string *error
        ) {
            if (!IsSafeSnapshotRelativePath(relativePath)) {
                return true;
            }

            std::error_code ec;
            const std::filesystem::path target = root / std::filesystem::path(relativePath);
            const auto status = std::filesystem::symlink_status(target, ec);
            if (ec) {
                if (error != nullptr) {
                    *error = ec.message();
                }
                return false;
            }
            if (!std::filesystem::exists(status)) {
                return true;
            }
            if (!std::filesystem::is_regular_file(status) && !std::filesystem::is_symlink(status)) {
                return true;
            }

            std::filesystem::remove(target, ec);
            if (ec) {
                if (error != nullptr) {
                    *error = ec.message();
                }
                return false;
            }
            return true;
        }

        std::filesystem::path BuildConflictPath(
            const std::filesystem::path &hostRoot,
            const std::string &relativePath,
            const char *side,
            std::string *conflictRelativePath
        ) {
            const std::filesystem::path relative(relativePath);
            const std::filesystem::path parent = relative.parent_path();
            const std::string stem = relative.stem().string();
            const std::string extension = relative.extension().string();
            const std::string sideLabel = side == nullptr ? "copy" : side;

            for (int suffix = 0; suffix < 1000; suffix++) {
                std::string name = stem + ".conflict-" + sideLabel;
                if (suffix > 0) {
                    name += "-" + std::to_string(suffix);
                }
                name += extension;

                const std::filesystem::path candidateRelative = parent / name;
                const std::filesystem::path candidate = hostRoot / candidateRelative;

                std::error_code ec;
                if (!std::filesystem::exists(candidate, ec) && !ec) {
                    if (conflictRelativePath != nullptr) {
                        *conflictRelativePath = candidateRelative.generic_string();
                    }
                    return candidate;
                }
            }

            const std::filesystem::path fallbackRelative =
                parent / (stem + ".conflict-" + sideLabel + "-copy" + extension);
            if (conflictRelativePath != nullptr) {
                *conflictRelativePath = fallbackRelative.generic_string();
            }
            return hostRoot / fallbackRelative;
        }

        bool CopyConflictFile(
            const std::filesystem::path &source,
            const std::filesystem::path &hostRoot,
            const std::string &relativePath,
            const char *side,
            SharedFolderReconcileChanges *changes,
            std::string *error
        ) {
            std::string conflictRelativePath;
            const std::filesystem::path conflictPath = BuildConflictPath(hostRoot, relativePath, side, &conflictRelativePath);
            if (!CopyFilePreservingTime(source, conflictPath, error)) {
                return false;
            }
            if (changes != nullptr) {
                changes->ConflictPaths.push_back(std::move(conflictRelativePath));
            }
            return true;
        }

        const FileState *FindFileState(const FileStateMap &files, const std::string &relativePath) {
            const auto it = files.find(relativePath);
            return it == files.end() ? nullptr : &it->second;
        }

        bool SameContent(const FileState &a, const FileState &b) {
            return a.HasMetadata && b.HasMetadata && a.SizeBytes == b.SizeBytes && a.Hash == b.Hash;
        }

        bool ChangedFromBase(const FileState *current, const FileState *base) {
            if (base == nullptr) {
                return current != nullptr;
            }
            if (current == nullptr) {
                return true;
            }
            if (!base->HasMetadata) {
                return false;
            }
            return !SameContent(*current, *base);
        }

        bool CopyDeviceChangeToHost(
            const std::filesystem::path &stagingRoot,
            const std::filesystem::path &hostRoot,
            const std::string &relativePath,
            std::string *error
        ) {
            return CopySharedFolderFile(stagingRoot, hostRoot, relativePath, error);
        }

        bool ReconcileLegacyPath(
            const std::filesystem::path &stagingRoot,
            const std::filesystem::path &hostRoot,
            const std::string &relativePath,
            const FileState *host,
            const FileState *device,
            std::string *error
        ) {
            if (host != nullptr && device == nullptr) {
                return RemoveSharedFolderFile(hostRoot, relativePath, error);
            }
            if (device != nullptr && host == nullptr) {
                return CopyDeviceChangeToHost(stagingRoot, hostRoot, relativePath, error);
            }
            if (host == nullptr || device == nullptr || SameContent(*host, *device)) {
                return true;
            }
            if (device->ModifiedTick > host->ModifiedTick) {
                return CopyDeviceChangeToHost(stagingRoot, hostRoot, relativePath, error);
            }
            return true;
        }

        bool SaveSharedFolderSnapshot(
            const std::string &snapshotPath,
            const std::string &rootPath,
            std::string *error
        ) {
            FileStateMap files;
            if (!CollectRelativeFileStates(rootPath, &files, error)) {
                return false;
            }

            const std::filesystem::path path(snapshotPath);
            std::error_code ec;
            std::filesystem::create_directories(path.parent_path(), ec);
            if (ec) {
                if (error != nullptr) {
                    *error = ec.message();
                }
                return false;
            }

            const std::filesystem::path tempPath = path.string() + ".tmp";
            std::ofstream file(tempPath);
            if (!file.is_open()) {
                if (error != nullptr) {
                    *error = "Could not write shared folder sync state.";
                }
                return false;
            }
            for (const auto &[relativePath, state]: files) {
                file << "v2\t"
                     << state.SizeBytes << '\t'
                     << state.ModifiedTick << '\t'
                     << state.Hash << '\t'
                     << relativePath << '\n';
            }
            file.close();
            if (!file.good()) {
                if (error != nullptr) {
                    *error = "Could not write shared folder sync state.";
                }
                return false;
            }

            std::filesystem::remove(path, ec);
            ec.clear();
            std::filesystem::rename(tempPath, path, ec);
            if (ec) {
                if (error != nullptr) {
                    *error = ec.message();
                }
                return false;
            }
            return true;
        }

        bool DeleteSharedFolderDevicePaths(
            const SdkInfo &sdk,
            const std::string &serial,
            const std::vector<std::string> &relativePaths,
            const std::function<bool()> &shouldCancel,
            std::string *output
        ) {
            const std::string deviceRoot = GetSharedFolderDevicePath();
            for (const auto &relativePath: relativePaths) {
                if (!IsSafeSnapshotRelativePath(relativePath)) {
                    continue;
                }

                auto removed = DeleteDevicePath(
                    sdk,
                    serial,
                    JoinDevicePath(deviceRoot, relativePath),
                    shouldCancel
                );
                if (output != nullptr && !removed.Output.empty()) {
                    *output += removed.Output;
                }
                if (!removed.Success) {
                    if (output != nullptr && output->empty()) {
                        *output = "Could not delete shared folder file on device.";
                    }
                    return false;
                }
            }
            return true;
        }

        void AppendConflictSummary(std::string *output, const SharedFolderReconcileChanges &changes) {
            if (output == nullptr || changes.ConflictPaths.empty()) {
                return;
            }

            if (!output->empty() && output->back() != '\n') {
                output->push_back('\n');
            }
            *output += "Shared folder conflicts were saved as:\n";
            for (const auto &path: changes.ConflictPaths) {
                *output += path;
                output->push_back('\n');
            }
        }

        SharedFolderSyncResult PushSharedFolderToDevice(
            const SdkInfo &sdk,
            const std::string &serial,
            const std::string &avdName,
            const std::function<bool()> &shouldCancel
        ) {
            std::string error;
            if (!EnsureSharedFolderHostPath(avdName, &error)) {
                return Failure(error.empty() ? "Could not prepare shared folder." : error);
            }

            const std::string hostPath = GetSharedFolderHostPath(avdName);
            const std::string devicePath = GetSharedFolderDevicePath();
            const auto created = CreateDeviceDirectory(sdk, serial, devicePath, shouldCancel);
            if (!created.Success) {
                return Failure(created.Output.empty() ? "Could not create shared folder on device." : created.Output);
            }

            std::error_code ec;
            if (std::filesystem::is_empty(hostPath, ec) && !ec) {
                return {.Success = true, .Output = created.Output};
            }

            auto pushed = RunSharedFolderAdbCommand(
                sdk,
                BuildPushSharedFolderToDeviceArgs(serial, hostPath, devicePath),
                shouldCancel
            );
            if (!created.Output.empty()) {
                pushed.Output = created.Output + pushed.Output;
            }
            return pushed;
        }

        PullSharedFolderResult PullSharedFolderFromDevice(
            const SdkInfo &sdk,
            const std::string &serial,
            const std::string &avdName,
            const std::function<bool()> &shouldCancel
        ) {
            std::string error;
            if (!EnsureSharedFolderHostPath(avdName, &error)) {
                return PullFailure(error.empty() ? "Could not prepare shared folder." : error);
            }

            const std::string hostPath = GetSharedFolderHostPath(avdName);
            const std::string devicePath = GetSharedFolderDevicePath();
            const auto created = CreateDeviceDirectory(sdk, serial, devicePath, shouldCancel);
            if (!created.Success) {
                return PullFailure(created.Output.empty() ? "Could not create shared folder on device." : created.Output);
            }

            const std::filesystem::path stagingPath = BuildStagingPath(avdName);
            std::error_code ec;
            std::filesystem::remove_all(stagingPath, ec);
            ec.clear();
            std::filesystem::create_directories(stagingPath, ec);
            if (ec) {
                return PullFailure(ec.message());
            }

            auto pulled = RunSharedFolderAdbCommand(
                sdk,
                BuildPullSharedFolderFromDeviceArgs(serial, devicePath, stagingPath.string()),
                shouldCancel
            );
            PullSharedFolderResult result;
            result.Success = pulled.Success;
            result.Output = std::move(pulled.Output);
            result.StagingPath = stagingPath;
            result.HostPath = hostPath;
            if (!pulled.Success) {
                std::filesystem::remove_all(stagingPath, ec);
                return result;
            }

            if (!created.Output.empty()) {
                result.Output = created.Output + result.Output;
            }
            return result;
        }
    }

    std::string GetSharedFolderHostPath(const std::string &avdName) {
        const std::string rootPath = Paths::GetAppConfigPath("shared");
        const std::string folderName = SanitizeFolderName(avdName);
        if (rootPath.empty() || folderName.empty()) {
            return rootPath;
        }
        return Paths::JoinPaths({rootPath, folderName});
    }

    std::string GetSharedFolderDevicePath() {
        return "/sdcard/CoreDeckShared";
    }

    const char *GetOpenSharedFolderHostLabel() {
#if defined(_WIN32)
        return "Open Shared Folder in File Explorer";
#elif defined(__APPLE__)
        return "Open Shared Folder in Finder";
#else
        return "Open Shared Folder in File Manager";
#endif
    }

    OpenFolderCommand BuildOpenFolderCommand(const std::string &folderPath) {
#if defined(_WIN32)
        return {.Executable = "explorer.exe", .Args = {folderPath}};
#elif defined(__APPLE__)
        return {.Executable = "/usr/bin/open", .Args = {folderPath}};
#else
        return {.Executable = "xdg-open", .Args = {folderPath}};
#endif
    }

    std::string BuildSharedFolderContentPath(const std::string &folderPath) {
        return (std::filesystem::path(folderPath) / ".").string();
    }

    std::string BuildSharedFolderDeviceContentPath(const std::string &devicePath) {
        const std::string normalized = NormalizeDevicePath(devicePath);
        return normalized == "/" ? "/." : normalized + "/.";
    }

    std::vector<std::string> BuildPushSharedFolderToDeviceArgs(
        const std::string &serial,
        const std::string &hostPath,
        const std::string &devicePath
    ) {
        return {
            "-s",
            serial,
            "push",
            BuildSharedFolderContentPath(hostPath),
            NormalizeDevicePath(devicePath),
        };
    }

    std::vector<std::string> BuildPullSharedFolderFromDeviceArgs(
        const std::string &serial,
        const std::string &devicePath,
        const std::string &hostPath
    ) {
        return {
            "-s",
            serial,
            "pull",
            "-a",
            BuildSharedFolderDeviceContentPath(devicePath),
            hostPath,
        };
    }

        bool MergePulledSharedFolder(
            const std::string &stagingPath,
            const std::string &hostPath,
            std::string *error
        ) {
        if (error != nullptr) {
            error->clear();
        }

        const std::filesystem::path stagingRoot = stagingPath;
        const std::filesystem::path hostRoot = hostPath;

        std::error_code ec;
        if (!std::filesystem::is_directory(stagingRoot, ec) || ec) {
            if (error != nullptr) {
                *error = ec ? ec.message() : "Pulled shared folder staging path is not a directory.";
            }
            return false;
        }

        std::filesystem::create_directories(hostRoot, ec);
        if (ec) {
            if (error != nullptr) {
                *error = ec.message();
            }
            return false;
        }

        for (std::filesystem::recursive_directory_iterator it(
                 stagingRoot,
                 std::filesystem::directory_options::skip_permission_denied,
                 ec
             ),
             end;
             it != end;
             it.increment(ec)) {
            if (ec) {
                if (error != nullptr) {
                    *error = ec.message();
                }
                return false;
            }

            const std::filesystem::path source = it->path();
            std::filesystem::path relative = std::filesystem::relative(source, stagingRoot, ec);
            if (ec) {
                if (error != nullptr) {
                    *error = ec.message();
                }
                return false;
            }

            const std::filesystem::path target = hostRoot / relative;
            const auto status = it->symlink_status(ec);
            if (ec) {
                if (error != nullptr) {
                    *error = ec.message();
                }
                return false;
            }
            if (std::filesystem::is_symlink(status)) {
                if (error != nullptr) {
                    *error = UNSUPPORTED_SYMLINK_ERROR;
                }
                return false;
            }
            if (std::filesystem::is_directory(status)) {
                if (std::filesystem::exists(target, ec) && !ec && !std::filesystem::is_directory(target, ec)) {
                    it.disable_recursion_pending();
                    continue;
                }
                if (ec) {
                    if (error != nullptr) {
                        *error = ec.message();
                    }
                    return false;
                }
                std::filesystem::create_directories(target, ec);
                if (ec) {
                    if (error != nullptr) {
                        *error = ec.message();
                    }
                    return false;
                }
                continue;
            }

            if (!std::filesystem::is_regular_file(status)) {
                continue;
            }

            const std::filesystem::file_time_type sourceTime = std::filesystem::last_write_time(source, ec);
            if (ec) {
                if (error != nullptr) {
                    *error = ec.message();
                }
                return false;
            }

            bool shouldCopy = true;
            if (std::filesystem::exists(target, ec) && !ec) {
                if (!std::filesystem::is_regular_file(target, ec) || ec) {
                    if (ec) {
                        if (error != nullptr) {
                            *error = ec.message();
                        }
                        return false;
                    }
                    continue;
                }

                const std::filesystem::file_time_type targetTime = std::filesystem::last_write_time(target, ec);
                if (ec) {
                    if (error != nullptr) {
                        *error = ec.message();
                    }
                    return false;
                }
                shouldCopy = sourceTime > targetTime;
            } else if (ec) {
                if (error != nullptr) {
                    *error = ec.message();
                }
                return false;
            }

            if (!shouldCopy) {
                continue;
            }

            std::filesystem::create_directories(target.parent_path(), ec);
            if (ec) {
                if (error != nullptr) {
                    *error = ec.message();
                }
                return false;
            }
            std::filesystem::copy_file(source, target, std::filesystem::copy_options::overwrite_existing, ec);
            if (ec) {
                if (error != nullptr) {
                    *error = ec.message();
                }
                return false;
            }
            std::filesystem::last_write_time(target, sourceTime, ec);
            if (ec) {
                if (error != nullptr) {
                    *error = ec.message();
                }
                return false;
            }
        }

        return true;
    }

    bool ReconcilePulledSharedFolder(
        const std::string &stagingPath,
        const std::string &hostPath,
        const std::string &snapshotPath,
        std::string *error,
        SharedFolderReconcileChanges *changes
    ) {
        if (error != nullptr) {
            error->clear();
        }
        if (changes != nullptr) {
            changes->DeviceDeletedPaths.clear();
            changes->ConflictPaths.clear();
        }

        const std::filesystem::path stagingRoot = stagingPath;
        const std::filesystem::path hostRoot = hostPath;

        std::error_code ec;
        std::filesystem::create_directories(hostRoot, ec);
        if (ec) {
            if (error != nullptr) {
                *error = ec.message();
            }
            return false;
        }

        FileStateMap baseFiles;
        FileStateMap hostFiles;
        FileStateMap deviceFiles;
        if (!LoadSharedFolderSnapshot(snapshotPath, &baseFiles, error)) {
            return false;
        }
        if (!CollectRelativeFileStates(hostRoot, &hostFiles, error)) {
            return false;
        }
        if (!CollectRelativeFileStates(stagingRoot, &deviceFiles, error)) {
            return false;
        }

        std::set<std::string> allPaths;
        for (const auto &[path, _]: baseFiles) {
            allPaths.insert(path);
        }
        for (const auto &[path, _]: hostFiles) {
            allPaths.insert(path);
        }
        for (const auto &[path, _]: deviceFiles) {
            allPaths.insert(path);
        }

        for (const auto &relativePath: allPaths) {
            const FileState *base = FindFileState(baseFiles, relativePath);
            const FileState *host = FindFileState(hostFiles, relativePath);
            const FileState *device = FindFileState(deviceFiles, relativePath);

            if (base != nullptr && !base->HasMetadata) {
                if (!ReconcileLegacyPath(stagingRoot, hostRoot, relativePath, host, device, error)) {
                    return false;
                }
                continue;
            }

            const bool hostExists = host != nullptr;
            const bool deviceExists = device != nullptr;
            const bool hostChanged = ChangedFromBase(host, base);
            const bool deviceChanged = ChangedFromBase(device, base);

            if (hostExists && deviceExists) {
                if (!hostChanged && !deviceChanged) {
                    continue;
                }
                if (hostChanged && !deviceChanged) {
                    continue;
                }
                if (!hostChanged && deviceChanged) {
                    if (!CopyDeviceChangeToHost(stagingRoot, hostRoot, relativePath, error)) {
                        return false;
                    }
                    continue;
                }
                if (SameContent(*host, *device)) {
                    continue;
                }
                if (!CopyConflictFile(stagingRoot / std::filesystem::path(relativePath),
                                      hostRoot,
                                      relativePath,
                                      "device",
                                      changes,
                                      error)) {
                    return false;
                }
                continue;
            }

            if (hostExists && !deviceExists) {
                if (base == nullptr) {
                    continue;
                }
                if (!hostChanged) {
                    if (!RemoveSharedFolderFile(hostRoot, relativePath, error)) {
                        return false;
                    }
                    continue;
                }
                // Preserve the edited host copy under a conflict name and remove
                // the original path so the deleted side does not come back next sync.
                if (!CopyConflictFile(hostRoot / std::filesystem::path(relativePath),
                                      hostRoot,
                                      relativePath,
                                      "host",
                                      changes,
                                      error)) {
                    return false;
                }
                if (!RemoveSharedFolderFile(hostRoot, relativePath, error)) {
                    return false;
                }
                continue;
            }

            if (!hostExists && deviceExists) {
                if (base == nullptr) {
                    if (!CopyDeviceChangeToHost(stagingRoot, hostRoot, relativePath, error)) {
                        return false;
                    }
                    continue;
                }
                if (!deviceChanged) {
                    if (changes != nullptr) {
                        changes->DeviceDeletedPaths.push_back(relativePath);
                    }
                    continue;
                }
                // Preserve the edited device copy under a conflict name and delete
                // the original path so the resolved state stays stable.
                if (!CopyConflictFile(stagingRoot / std::filesystem::path(relativePath),
                                      hostRoot,
                                      relativePath,
                                      "device",
                                      changes,
                                      error)) {
                    return false;
                }
                if (changes != nullptr) {
                    changes->DeviceDeletedPaths.push_back(relativePath);
                }
                continue;
            }
        }

        return true;
    }

    bool EnsureSharedFolderHostPath(const std::string &avdName, std::string *error) {
        if (error != nullptr) {
            error->clear();
        }

        const std::string folderPath = GetSharedFolderHostPath(avdName);
        if (folderPath.empty()) {
            if (error != nullptr) {
                *error = "Could not determine the shared folder location.";
            }
            return false;
        }

        std::error_code ec;
        std::filesystem::create_directories(folderPath, ec);
        if (ec) {
            if (error != nullptr) {
                *error = ec.message();
            }
            return false;
        }

        return std::filesystem::is_directory(folderPath, ec) && !ec;
    }

    bool EnsureSharedFolderHostPath(std::string *error) {
        return EnsureSharedFolderHostPath("", error);
    }

    bool OpenSharedFolderHostPath(const std::string &avdName, std::string *error) {
        if (!EnsureSharedFolderHostPath(avdName, error)) {
            return false;
        }

        const OpenFolderCommand command = BuildOpenFolderCommand(GetSharedFolderHostPath(avdName));
        return StreamCommandArgsWithEnvCancelable(command.Executable, command.Args, "", {}, nullptr, {});
    }

    bool OpenSharedFolderHostPath(std::string *error) {
        return OpenSharedFolderHostPath("", error);
    }

    SharedFolderSyncResult SyncSharedFolder(
        const SdkInfo &sdk,
        const std::string &serial,
        const std::string &avdName,
        const SharedFolderSyncMode mode,
        const std::function<bool()> &shouldCancel
    ) {
        if (serial.empty()) {
            return Failure("No device selected.");
        }

        auto pulled = PullSharedFolderFromDevice(sdk, serial, avdName, shouldCancel);
        if (!pulled.Success) {
            return pulled;
        }

        const std::string snapshotPath = BuildSnapshotPath(avdName).string();
        std::string error;
        SharedFolderReconcileChanges changes;
        auto cleanupStaging = [&] {
            std::error_code ec;
            std::filesystem::remove_all(pulled.StagingPath, ec);
        };

        if (!ReconcilePulledSharedFolder(
                pulled.StagingPath.string(),
                pulled.HostPath,
                snapshotPath,
                &error,
                &changes
            )) {
            cleanupStaging();
            return Failure(error.empty() ? "Could not sync shared folder." : error);
        }

        if (mode == SharedFolderSyncMode::DeviceToHost) {
            if (!SaveSharedFolderSnapshot(snapshotPath, pulled.StagingPath.string(), &error)) {
                cleanupStaging();
                return Failure(error.empty() ? "Could not update shared folder sync state." : error);
            }
            cleanupStaging();
            AppendConflictSummary(&pulled.Output, changes);
            return pulled;
        }

        std::string deviceOutput;
        if (!DeleteSharedFolderDevicePaths(sdk, serial, changes.DeviceDeletedPaths, shouldCancel, &deviceOutput)) {
            cleanupStaging();
            return Failure(deviceOutput.empty() ? "Could not update shared folder on device." : deviceOutput);
        }

        auto pushed = PushSharedFolderToDevice(sdk, serial, avdName, shouldCancel);
        if (!pushed.Success) {
            cleanupStaging();
            if (!pulled.Output.empty()) {
                pushed.Output = pulled.Output + pushed.Output;
            }
            if (!deviceOutput.empty()) {
                pushed.Output = deviceOutput + pushed.Output;
            }
            return pushed;
        }
        if (!SaveSharedFolderSnapshot(snapshotPath, pulled.HostPath, &error)) {
            cleanupStaging();
            return Failure(error.empty() ? "Could not update shared folder sync state." : error);
        }
        cleanupStaging();
        AppendConflictSummary(&pushed.Output, changes);
        if (!deviceOutput.empty()) {
            pushed.Output = deviceOutput + pushed.Output;
        }
        if (!pulled.Output.empty()) {
            pushed.Output = pulled.Output + pushed.Output;
        }
        return pushed;
    }
}
