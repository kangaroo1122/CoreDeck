//
// Created by CoreDeck contributors on 02/08/2026.
//

#include "archive.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>

#include <miniz.h>

#if !defined(_WIN32)
#include <sys/stat.h>
#endif

namespace CoreDeck {
    namespace {
        std::string NormalizeEntry(std::string value) {
            for (char &character: value) {
                if (character == '\\') {
                    character = '/';
                }
            }
            while (value.starts_with("./")) {
                value.erase(0, 2);
            }
            return value;
        }

        bool IsSafeEntry(const std::string &name) {
            const std::string normalized = NormalizeEntry(name);
            if (normalized.empty() || normalized.front() == '/') {
                return false;
            }
            if (normalized.size() >= 2 && normalized[1] == ':') {
                return false;
            }

            std::size_t start = 0;
            while (start <= normalized.size()) {
                const std::size_t slash = normalized.find('/', start);
                const std::string component = slash == std::string::npos
                                                  ? normalized.substr(start)
                                                  : normalized.substr(start, slash - start);
                if (component == "..") {
                    return false;
                }
                if (slash == std::string::npos) {
                    break;
                }
                start = slash + 1;
            }
            return true;
        }

        std::string StripTopLevel(std::string value) {
            value = NormalizeEntry(std::move(value));
            const std::size_t slash = value.find('/');
            return slash == std::string::npos ? std::string() : value.substr(slash + 1);
        }

        bool IsWithinRoot(
            const std::filesystem::path &root,
            const std::filesystem::path &target
        ) {
            const auto [rootPosition, targetPosition] = std::ranges::mismatch(root, target);
            (void) targetPosition;
            return rootPosition == root.end();
        }

        std::optional<std::filesystem::path> ResolveSafeTarget(
            const std::filesystem::path &root,
            const std::string &relative
        ) {
            if (!IsSafeEntry(relative)) {
                return std::nullopt;
            }

            const std::filesystem::path relativePath(relative);
            if (relativePath.is_absolute() || relativePath.has_root_name() || relativePath.has_root_directory()) {
                return std::nullopt;
            }

            std::error_code ec;
            const std::filesystem::path target = std::filesystem::weakly_canonical(root / relativePath, ec);
            if (ec || !IsWithinRoot(root, target)) {
                return std::nullopt;
            }
            return target;
        }

        void MakeExecutable(const std::filesystem::path &path) {
#if defined(_WIN32)
            (void) path;
#else
            struct stat info = {};
            if (stat(path.c_str(), &info) == 0) {
                (void) chmod(path.c_str(), info.st_mode | S_IXUSR | S_IXGRP | S_IXOTH);
            }
#endif
        }

        bool ShouldBeExecutable(const std::string &relative, const mz_uint32 externalAttributes) {
            if ((externalAttributes >> 16U) & 0111U) {
                return true;
            }
            return relative.starts_with("bin/") || relative.find("/bin/") != std::string::npos;
        }

        struct WriteState {
            std::ofstream Output;
            bool Failed = false;
        };

        size_t WriteCallback(
            void *opaque,
            const mz_uint64 /*offset*/,
            const void *buffer,
            const size_t size
        ) {
            auto *state = static_cast<WriteState *>(opaque);
            if (state->Failed) {
                return 0;
            }
            state->Output.write(static_cast<const char *>(buffer), static_cast<std::streamsize>(size));
            if (!state->Output) {
                state->Failed = true;
                return 0;
            }
            return size;
        }
    }

    bool ExtractZip(
        const std::string &zipPath,
        const std::string &destDir,
        const ExtractOptions &options,
        const ExtractProgressFn &onProgress,
        std::string &error
    ) {
        mz_zip_archive archive = {};
        if (!mz_zip_reader_init_file(&archive, zipPath.c_str(), 0)) {
            error = "Could not open ZIP archive.";
            return false;
        }

        std::error_code ec;
        std::filesystem::create_directories(destDir, ec);
        if (ec) {
            mz_zip_reader_end(&archive);
            error = "Could not create extraction directory.";
            return false;
        }
        const std::filesystem::path extractionRoot = std::filesystem::canonical(destDir, ec);
        if (ec) {
            mz_zip_reader_end(&archive);
            error = "Could not resolve extraction directory.";
            return false;
        }

        const mz_uint entryCount = mz_zip_reader_get_num_files(&archive);
        bool ok = true;
        for (mz_uint index = 0; index < entryCount && ok; ++index) {
            mz_zip_archive_file_stat stat = {};
            if (!mz_zip_reader_file_stat(&archive, index, &stat)) {
                error = "Corrupt ZIP archive entry.";
                ok = false;
                break;
            }

            const std::string rawName = stat.m_filename;
            if (!IsSafeEntry(rawName)) {
                error = "Refusing to extract unsafe ZIP entry: " + rawName;
                ok = false;
                break;
            }

            std::string relative = NormalizeEntry(rawName);
            if (options.StripTopLevelDir) {
                relative = StripTopLevel(relative);
            }
            if (relative.empty()) {
                continue;
            }

            const auto target = ResolveSafeTarget(extractionRoot, relative);
            if (!target.has_value()) {
                error = "Refusing to extract unsafe ZIP entry: " + rawName;
                ok = false;
                break;
            }
            if (mz_zip_reader_is_file_a_directory(&archive, index)) {
                std::filesystem::create_directories(*target, ec);
                if (ec) {
                    error = "Could not create archive directory.";
                    ok = false;
                    break;
                }
            } else {
                std::filesystem::create_directories(target->parent_path(), ec);
                if (ec) {
                    error = "Could not create archive parent directory.";
                    ok = false;
                    break;
                }

                WriteState state;
                state.Output.open(*target, std::ios::binary | std::ios::trunc);
                if (!state.Output ||
                    !mz_zip_reader_extract_to_callback(&archive, index, WriteCallback, &state, 0) ||
                    state.Failed) {
                    error = "Could not extract archive entry: " + relative;
                    ok = false;
                    break;
                }
                state.Output.close();
                if (ShouldBeExecutable(relative, stat.m_external_attr)) {
                    MakeExecutable(*target);
                }
            }

            if (onProgress && !onProgress(entryCount == 0 ? 1.0F : static_cast<float>(index + 1) / static_cast<float>(entryCount))) {
                error = "Extraction cancelled.";
                ok = false;
            }
        }

        mz_zip_reader_end(&archive);
        return ok;
    }
}
