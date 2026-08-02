//
// Created by AbdulMuaz Aqeel on 19/04/2026.
//

#include "sdk_bootstrap.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#include <winhttp.h>
#else
#include <curl/curl.h>
#endif

#include "paths.h"
#include "archive.h"
#include "process.h"
#include "sha256.h"
#include "sdk_progress.h"
#include "system_image.h"
#include "utilities.h"

namespace CoreDeck {
    namespace {
        void SetProgress(
            const std::shared_ptr<SdkOperationProgress> &progress,
            const float percent,
            const std::string &status,
            const std::string &detail = ""
        ) {
            ReportSdkProgress(progress, percent, status, detail);
        }

        SdkBootstrapResult Fail(
            const std::shared_ptr<SdkOperationProgress> &progress,
            const std::string &error,
            const char *status = "Command-line tools installation failed."
        ) {
            if (progress) {
                std::lock_guard lock(progress->Mutex);
                progress->Finished = true;
                progress->Succeeded = false;
                progress->StatusText = status;
                progress->DetailText = error;
            }
            return {.Succeeded = false, .Error = error};
        }

        SdkBootstrapResult Success(
            const std::shared_ptr<SdkOperationProgress> &progress,
            const char *status = "Command-line tools installed."
        ) {
            ReportSdkProgress(progress, 1.0F, status);
            if (progress) {
                std::lock_guard lock(progress->Mutex);
                progress->Finished = true;
                progress->Succeeded = true;
            }
            return {.Succeeded = true};
        }

        bool IsCancelRequested(const std::shared_ptr<SdkOperationProgress> &progress) {
            return progress && progress->CancelRequested.load();
        }

        SdkBootstrapResult Cancelled(const std::shared_ptr<SdkOperationProgress> &progress) {
            if (progress) {
                std::lock_guard lock(progress->Mutex);
                progress->Finished = true;
                progress->Succeeded = false;
                progress->StatusText = "Cancelled.";
                progress->DetailText.clear();
            }
            return {.Succeeded = false, .Cancelled = true, .Error = "Operation cancelled."};
        }

        std::string FindCmdlineTool(const std::string &binDir, const std::string &name) {
#if defined(_WIN32)
            for (const auto *ext: {".bat", ".exe"}) {
                const std::string candidate = Paths::JoinPaths({binDir, name + ext});
                if (std::filesystem::exists(candidate)) {
                    return candidate;
                }
            }
            return "";
#else
            const std::string candidate = Paths::JoinPaths({binDir, name});
            return std::filesystem::exists(candidate) ? candidate : "";
#endif
        }

        std::filesystem::path CreateTempDirectory() {
            const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
            std::filesystem::path dir = std::filesystem::temp_directory_path() / StrConcat("coredeck-sdk-", std::to_string(now));
            std::filesystem::create_directories(dir);
            return dir;
        }

        bool CopyDirectoryContents(
            const std::filesystem::path &from,
            const std::filesystem::path &to,
            std::string &error,
            const std::shared_ptr<SdkOperationProgress> &progress = nullptr
        ) {
            std::error_code ec;
            std::filesystem::create_directories(to, ec);
            if (ec) {
                error = detail::FormatFilesystemError(
                    "Creating command-line tools destination directory",
                    {},
                    to,
                    ec
                );
                return false;
            }

            const bool sourceIsDirectory = std::filesystem::is_directory(from, ec);
            if (ec || !sourceIsDirectory) {
                if (!ec) {
                    ec = std::make_error_code(std::errc::not_a_directory);
                }
                error = detail::FormatFilesystemError(
                    "Opening extracted command-line tools directory",
                    from,
                    to,
                    ec
                );
                return false;
            }

            std::filesystem::directory_iterator entry(from, ec);
            const std::filesystem::directory_iterator end;
            if (ec) {
                error = detail::FormatFilesystemError(
                    "Enumerating extracted command-line tools directory",
                    from,
                    to,
                    ec
                );
                return false;
            }

            while (entry != end) {
                if (IsCancelRequested(progress)) {
                    error = "Operation cancelled.";
                    return false;
                }

                const std::filesystem::path source = entry->path();
                const std::filesystem::path target = to / source.filename();
                std::filesystem::copy(
                    source,
                    target,
                    std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing,
                    ec
                );
                if (ec) {
                    error = detail::FormatFilesystemError(
                        "Copying command-line tools entry",
                        source,
                        target,
                        ec
                    );
                    return false;
                }
                if (IsCancelRequested(progress)) {
                    error = "Operation cancelled.";
                    return false;
                }

                entry.increment(ec);
                if (ec) {
                    error = detail::FormatFilesystemError(
                        "Enumerating extracted command-line tools directory",
                        from,
                        to,
                        ec
                    );
                    return false;
                }
            }

            return true;
        }

        std::filesystem::path UniqueSiblingPath(const std::filesystem::path &target, const std::string_view suffix) {
            std::filesystem::path parent = target.parent_path();
            if (parent.empty()) {
                parent = ".";
            }
            std::string name = target.filename().string();
            if (name.empty()) {
                name = "sdk";
            }
            const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
            return parent / StrConcat(".", name, "-", std::string(suffix), "-", std::to_string(now));
        }

        class BootstrapDirectoryCleanup {
        public:
            explicit BootstrapDirectoryCleanup(std::filesystem::path sdkRoot)
                : m_SdkRoot(std::move(sdkRoot)),
                  m_CmdlineToolsRoot(m_SdkRoot / "cmdline-tools") {
                std::error_code ec;
                m_SdkRootExisted = std::filesystem::exists(m_SdkRoot, ec);
                ec.clear();
                m_CmdlineToolsRootExisted = std::filesystem::exists(m_CmdlineToolsRoot, ec);
            }

            ~BootstrapDirectoryCleanup() {
                if (!m_Active) {
                    return;
                }

                std::error_code ec;
                if (!m_CmdlineToolsRootExisted) {
                    std::filesystem::remove_all(m_CmdlineToolsRoot, ec);
                }
                if (!m_SdkRootExisted) {
                    std::filesystem::remove_all(m_SdkRoot, ec);
                }
            }

            void Disarm() {
                m_Active = false;
            }

        private:
            std::filesystem::path m_SdkRoot;
            std::filesystem::path m_CmdlineToolsRoot;
            bool m_SdkRootExisted = false;
            bool m_CmdlineToolsRootExisted = false;
            bool m_Active = true;
        };

        bool ReplaceDirectoryWithStagedInstall(
            const std::filesystem::path &stagingDir,
            const std::filesystem::path &installDir,
            std::string &error
        ) {
            std::error_code ec;
            std::filesystem::path backupDir;

            const bool installDirExists = std::filesystem::exists(installDir, ec);
            if (ec) {
                error = detail::FormatFilesystemError(
                    "Inspecting existing command-line tools directory",
                    installDir,
                    {},
                    ec
                );
                return false;
            }
            if (installDirExists) {
                backupDir = UniqueSiblingPath(installDir, "previous");
                std::filesystem::rename(installDir, backupDir, ec);
                if (ec) {
                    error = detail::FormatFilesystemError(
                        "Moving existing command-line tools to backup",
                        installDir,
                        backupDir,
                        ec
                    );
                    return false;
                }
            }

            std::filesystem::rename(stagingDir, installDir, ec);
            if (ec) {
                error = detail::FormatFilesystemError(
                    "Moving staged command-line tools into place",
                    stagingDir,
                    installDir,
                    ec
                );
                if (!backupDir.empty()) {
                    std::error_code restoreEc;
                    if (!std::filesystem::exists(installDir, restoreEc)) {
                        std::filesystem::rename(backupDir, installDir, restoreEc);
                    }
                    if (restoreEc) {
                        error += "\n" + detail::FormatFilesystemError(
                            "Restoring previous command-line tools directory",
                            backupDir,
                            installDir,
                            restoreEc
                        );
                    }
                }
                return false;
            }

            if (!backupDir.empty()) {
                std::filesystem::remove_all(backupDir, ec);
            }
            return true;
        }

        bool IsInstalledPackage(const std::vector<SdkPackage> &packages, const std::string &path) {
            return std::ranges::any_of(packages, [&](const SdkPackage &package) {
                return package.Path == path && package.Installed;
            });
        }

        std::string JoinPackagePaths(const std::vector<std::string> &packages) {
            std::string result;
            for (std::size_t i = 0; i < packages.size(); i++) {
                if (i > 0) {
                    result += ", ";
                }
                result += packages[i];
            }
            return result;
        }

        constexpr const char *COMMAND_LINE_TOOLS_REPOSITORY_URL =
            "https://dl.google.com/android/repository/repository2-3.xml";

        std::string CurrentCommandLineToolsHostOs() {
#if defined(_WIN32)
            return "windows";
#elif defined(__APPLE__)
            return "macosx";
#else
            return "linux";
#endif
        }

        std::string CurrentCommandLineToolsHostArch() {
#if defined(__APPLE__) && (defined(__aarch64__) || defined(_M_ARM64))
            return "aarch64";
#elif defined(__APPLE__)
            return "x64";
#else
            return "";
#endif
        }

        class Sha1 {
        public:
            void Update(const std::uint8_t *data, const std::size_t size) {
                m_TotalBytes += size;
                std::size_t offset = 0;
                while (offset < size) {
                    const std::size_t copySize = std::min(size - offset, m_Buffer.size() - m_BufferSize);
                    std::copy_n(data + offset, copySize, m_Buffer.data() + m_BufferSize);
                    m_BufferSize += copySize;
                    offset += copySize;
                    if (m_BufferSize == m_Buffer.size()) {
                        ProcessBlock(m_Buffer.data());
                        m_BufferSize = 0;
                    }
                }
            }

            std::string Finalize() {
                const std::uint64_t bitLength = m_TotalBytes * 8;
                m_Buffer[m_BufferSize++] = 0x80;
                if (m_BufferSize > 56) {
                    std::fill(m_Buffer.begin() + static_cast<std::ptrdiff_t>(m_BufferSize), m_Buffer.end(), 0);
                    ProcessBlock(m_Buffer.data());
                    m_BufferSize = 0;
                }
                std::fill(m_Buffer.begin() + static_cast<std::ptrdiff_t>(m_BufferSize), m_Buffer.begin() + 56, 0);
                for (int i = 0; i < 8; i++) {
                    m_Buffer[56 + i] = static_cast<std::uint8_t>(bitLength >> (56 - (i * 8)));
                }
                ProcessBlock(m_Buffer.data());

                std::string result;
                result.reserve(40);
                constexpr char HEX[] = "0123456789abcdef";
                for (const std::uint32_t value: m_State) {
                    for (int shift = 28; shift >= 0; shift -= 4) {
                        result.push_back(HEX[(value >> shift) & 0x0F]);
                    }
                }
                return result;
            }

        private:
            static std::uint32_t RotateLeft(const std::uint32_t value, const int count) {
                return (value << count) | (value >> (32 - count));
            }

            void ProcessBlock(const std::uint8_t *block) {
                std::array<std::uint32_t, 80> words{};
                for (int i = 0; i < 16; i++) {
                    words[i] = (static_cast<std::uint32_t>(block[i * 4]) << 24) |
                               (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16) |
                               (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8) |
                               static_cast<std::uint32_t>(block[i * 4 + 3]);
                }
                for (int i = 16; i < 80; i++) {
                    words[i] = RotateLeft(words[i - 3] ^ words[i - 8] ^ words[i - 14] ^ words[i - 16], 1);
                }

                std::uint32_t a = m_State[0];
                std::uint32_t b = m_State[1];
                std::uint32_t c = m_State[2];
                std::uint32_t d = m_State[3];
                std::uint32_t e = m_State[4];
                for (int i = 0; i < 80; i++) {
                    std::uint32_t function = 0;
                    std::uint32_t constant = 0;
                    if (i < 20) {
                        function = (b & c) | ((~b) & d);
                        constant = 0x5A827999;
                    } else if (i < 40) {
                        function = b ^ c ^ d;
                        constant = 0x6ED9EBA1;
                    } else if (i < 60) {
                        function = (b & c) | (b & d) | (c & d);
                        constant = 0x8F1BBCDC;
                    } else {
                        function = b ^ c ^ d;
                        constant = 0xCA62C1D6;
                    }
                    const std::uint32_t next = RotateLeft(a, 5) + function + e + constant + words[i];
                    e = d;
                    d = c;
                    c = RotateLeft(b, 30);
                    b = a;
                    a = next;
                }
                m_State[0] += a;
                m_State[1] += b;
                m_State[2] += c;
                m_State[3] += d;
                m_State[4] += e;
            }

            std::array<std::uint32_t, 5> m_State = {
                0x67452301,
                0xEFCDAB89,
                0x98BADCFE,
                0x10325476,
                0xC3D2E1F0,
            };
            std::array<std::uint8_t, 64> m_Buffer{};
            std::size_t m_BufferSize = 0;
            std::uint64_t m_TotalBytes = 0;
        };

        std::string Sha1ForFile(const std::string &path) {
            std::ifstream input(path, std::ios::binary);
            if (!input.is_open()) {
                return "";
            }
            Sha1 sha1;
            std::array<char, 64 * 1024> buffer{};
            while (input) {
                input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
                const std::streamsize count = input.gcount();
                if (count > 0) {
                    sha1.Update(
                        reinterpret_cast<const std::uint8_t *>(buffer.data()),
                        static_cast<std::size_t>(count)
                    );
                }
            }
            return sha1.Finalize();
        }

#if defined(_WIN32)
        std::wstring Utf8ToWide(const std::string &value) {
            if (value.empty()) {
                return {};
            }

            const int size = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
            std::wstring result(static_cast<std::size_t>(size), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, result.data(), size);
            if (!result.empty() && result.back() == L'\0') {
                result.pop_back();
            }
            return result;
        }

        bool DownloadFile(
            const std::string &url,
            const std::filesystem::path &destination,
            const std::shared_ptr<SdkOperationProgress> &progress,
            const SdkProgressRange &progressRange,
            std::string &error
        ) {
            if (IsCancelRequested(progress)) {
                error = "Operation cancelled.";
                return false;
            }

            constexpr const wchar_t *HOST = L"dl.google.com";
            const auto pathStart = url.find("/android/");
            if (pathStart == std::string::npos) {
                error = "Unsupported download URL.";
                return false;
            }

            HINTERNET session = WinHttpOpen(
                L"CoreDeck Android SDK Bootstrap",
                WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                WINHTTP_NO_PROXY_NAME,
                WINHTTP_NO_PROXY_BYPASS,
                0
            );
            if (!session) {
                error = "Could not open WinHTTP session.";
                return false;
            }
            WinHttpSetTimeouts(session, 15000, 15000, 15000, 15000);

            HINTERNET connect = WinHttpConnect(session, HOST, INTERNET_DEFAULT_HTTPS_PORT, 0);
            if (!connect) {
                WinHttpCloseHandle(session);
                error = "Could not connect to dl.google.com.";
                return false;
            }

            const std::string path = url.substr(pathStart);
            const std::wstring widePath = Utf8ToWide(path);
            HINTERNET request = WinHttpOpenRequest(
                connect,
                L"GET",
                widePath.c_str(),
                nullptr,
                WINHTTP_NO_REFERER,
                WINHTTP_DEFAULT_ACCEPT_TYPES,
                WINHTTP_FLAG_SECURE
            );
            if (!request) {
                WinHttpCloseHandle(connect);
                WinHttpCloseHandle(session);
                error = "Could not create download request.";
                return false;
            }

            const BOOL sent = WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                              WinHttpReceiveResponse(request, nullptr);
            if (!sent) {
                WinHttpCloseHandle(request);
                WinHttpCloseHandle(connect);
                WinHttpCloseHandle(session);
                error = "Download request failed.";
                return false;
            }

            DWORD status = 0;
            DWORD statusSize = sizeof(status);
            WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX);
            if (status < 200 || status >= 300) {
                WinHttpCloseHandle(request);
                WinHttpCloseHandle(connect);
                WinHttpCloseHandle(session);
                error = StrConcat("Download failed with HTTP ", std::to_string(status), ".");
                return false;
            }

            std::ofstream out(destination, std::ios::binary);
            if (!out.is_open()) {
                WinHttpCloseHandle(request);
                WinHttpCloseHandle(connect);
                WinHttpCloseHandle(session);
                error = "Could not write downloaded file.";
                return false;
            }

            std::uint64_t total = 0;
            DWORD total32 = 0;
            DWORD totalSize = sizeof(total32);
            if (WinHttpQueryHeaders(
                    request,
                    WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                    WINHTTP_HEADER_NAME_BY_INDEX,
                    &total32,
                    &totalSize,
                    WINHTTP_NO_HEADER_INDEX
                )) {
                total = total32;
            }

            std::uint64_t downloaded = 0;
            DWORD available = 0;
            bool transferOk = true;
            while (true) {
                if (!WinHttpQueryDataAvailable(request, &available)) {
                    error = "Command-line tools download failed while reading the response.";
                    transferOk = false;
                    break;
                }
                if (available == 0) {
                    break;
                }
                if (IsCancelRequested(progress)) {
                    WinHttpCloseHandle(request);
                    WinHttpCloseHandle(connect);
                    WinHttpCloseHandle(session);
                    error = "Operation cancelled.";
                    return false;
                }

                std::string chunk(available, '\0');
                DWORD read = 0;
                if (!WinHttpReadData(request, chunk.data(), available, &read) || read == 0) {
                    error = "Command-line tools download failed while reading the response.";
                    transferOk = false;
                    break;
                }
                chunk.resize(read);
                out.write(chunk.data(), static_cast<std::streamsize>(chunk.size()));
                downloaded += read;

                if (total > 0) {
                    const float fraction = static_cast<float>(downloaded) / static_cast<float>(total);
                    ReportSdkProgressInSubrange(
                        progress,
                        progressRange,
                        fraction,
                        "Downloading command-line tools..."
                    );
                }
            }

            WinHttpCloseHandle(request);
            WinHttpCloseHandle(connect);
            WinHttpCloseHandle(session);
            std::error_code fileEc;
            return transferOk &&
                   std::filesystem::exists(destination, fileEc) &&
                   std::filesystem::file_size(destination, fileEc) > 0 &&
                   !fileEc;
        }

#else
        size_t WriteFileCallback(const char *ptr, const size_t size, const size_t nmemb, void *userdata) {
            auto *out = static_cast<std::ofstream *>(userdata);
            out->write(ptr, static_cast<std::streamsize>(size * nmemb));
            return size * nmemb;
        }

        struct DownloadProgressContext {
            std::shared_ptr<SdkOperationProgress> Progress;
            SdkProgressRange Range;
        };

        int DownloadProgressCallback(void *clientp, const curl_off_t dltotal, const curl_off_t dlnow, curl_off_t, curl_off_t) {
            auto *context = static_cast<DownloadProgressContext *>(clientp);
            if (context != nullptr && IsCancelRequested(context->Progress)) {
                return 1;
            }
            if (context == nullptr || !context->Progress || dltotal <= 0) {
                return 0;
            }

            const float fraction = static_cast<float>(dlnow) / static_cast<float>(dltotal);
            ReportSdkProgressInSubrange(
                context->Progress,
                context->Range,
                fraction,
                "Downloading command-line tools..."
            );
            return 0;
        }

        bool DownloadFile(
            const std::string &url,
            const std::filesystem::path &destination,
            const std::shared_ptr<SdkOperationProgress> &progress,
            const SdkProgressRange &progressRange,
            std::string &error
        ) {
            if (IsCancelRequested(progress)) {
                error = "Operation cancelled.";
                return false;
            }

            CURL *curl = curl_easy_init();
            if (!curl) {
                error = "Could not initialize curl.";
                return false;
            }

            std::ofstream out(destination, std::ios::binary);
            if (!out.is_open()) {
                curl_easy_cleanup(curl);
                error = "Could not write downloaded file.";
                return false;
            }

            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_USERAGENT, "CoreDeck Android SDK Bootstrap");
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 20L);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 600L);
            curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteFileCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out);
            curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
            curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, DownloadProgressCallback);
            DownloadProgressContext progressContext{progress, progressRange};
            curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &progressContext);

            const CURLcode rc = curl_easy_perform(curl);
            curl_easy_cleanup(curl);
            out.close();

            if (rc != CURLE_OK) {
                error = IsCancelRequested(progress) ? "Operation cancelled." : "Command-line tools download failed.";
                return false;
            }
            std::error_code fileEc;
            return std::filesystem::exists(destination, fileEc) &&
                   std::filesystem::file_size(destination, fileEc) > 0 &&
                   !fileEc;
        }

#endif

        bool ExtractZip(
            const std::filesystem::path &zipPath,
            const std::filesystem::path &destination,
            const std::shared_ptr<SdkOperationProgress> &progress
        ) {
            std::string error;
            return CoreDeck::ExtractZip(
                zipPath.string(),
                destination.string(),
                ExtractOptions{},
                [&progress](const float fraction) {
                    ReportSdkProgressInSubrange(
                        progress,
                        {0.74F, 0.88F},
                        fraction,
                        "Extracting command-line tools..."
                    );
                    return !IsCancelRequested(progress);
                },
                error
            );
        }
    }

    std::string CommandLineToolsDownloadUrl() {
#if defined(_WIN32)
        return "https://dl.google.com/android/repository/commandlinetools-win-15859902_latest.zip";
#elif defined(__APPLE__) && defined(__aarch64__)
        return "https://dl.google.com/android/repository/commandlinetools-mac_arm64-15859902_latest.zip";
#elif defined(__APPLE__)
        return "https://dl.google.com/android/repository/commandlinetools-mac_x86_64-15859902_latest.zip";
#else
        return "https://dl.google.com/android/repository/commandlinetools-linux-15859902_latest.zip";
#endif
    }

    CommandLineToolsPackage BundledCommandLineToolsPackage() {
        CommandLineToolsPackage package;
        package.DownloadUrl = CommandLineToolsDownloadUrl();
#if defined(_WIN32)
        package.SizeBytes = 155655386ULL;
        package.Sha256 = "90ae805d20434428bffcb699c290860f19bb5f66a67e6b330067e3de801fb04a";
#elif defined(__APPLE__) && (defined(__aarch64__) || defined(__arm64__))
        package.SizeBytes = 156083281ULL;
        package.Sha256 = "835b62a26162b229b441d1f6d4680383815a270809eb33522c0d480fa5002c4e";
#elif defined(__APPLE__)
        package.SizeBytes = 156281494ULL;
        package.Sha256 = "c5a6378ab5cf7e0d5701921405115befff13e9ff7417fb588389338f8bd050f3";
#elif defined(__linux__)
        package.SizeBytes = 181833628ULL;
        package.Sha256 = "4e4c464f145a7512b57d088ac6c278c03c9eea610886b35a5e0804e74eedf583";
#else
        package.DownloadUrl.clear();
#endif
        return package;
    }

    namespace detail { // NOLINT(readability-identifier-naming)
        std::string FormatFilesystemError(
            const std::string &operation,
            const std::filesystem::path &source,
            const std::filesystem::path &destination,
            const std::error_code &error
        ) {
            std::string message = StrConcat(operation, " failed.");
            if (!source.empty()) {
                message += StrConcat("\nSource: ", source.string());
            }
            if (!destination.empty()) {
                message += StrConcat("\nDestination: ", destination.string());
            }
            message += StrConcat(
                "\nSystem error: ",
                error.message(),
                " (code ",
                std::to_string(error.value()),
                ", category ",
                error.category().name(),
                ")"
            );
            return message;
        }

        bool FileMatchesCommandLineToolsPackage(
            const std::string &path,
            const CommandLineToolsPackage &package
        ) {
            std::error_code ec;
            if (!std::filesystem::exists(path, ec) || ec) {
                return false;
            }
            if (package.SizeBytes > 0 && std::filesystem::file_size(path, ec) != package.SizeBytes) {
                return false;
            }
            if (ec) {
                return false;
            }
            if (!package.Sha1.empty() && Sha1ForFile(path) != LowerCopy(package.Sha1)) {
                return false;
            }
            return package.Sha256.empty() || EqualsIgnoreCaseHex(Sha256File(path), package.Sha256);
        }
    }

    bool CanInstallAndroidSdkIntoDirectory(const std::string &sdkRoot) {
        const std::string normalized = Paths::NormalizePath(sdkRoot);
        if (normalized.empty()) {
            return false;
        }

        try {
            std::error_code ec;
            const std::filesystem::path root(normalized);
            const bool exists = std::filesystem::exists(root, ec);
            if (ec) {
                return false;
            }
            if (!exists) {
                return true;
            }
            if (!std::filesystem::is_directory(root, ec) || ec) {
                return false;
            }
            if (std::filesystem::is_empty(root, ec)) {
                return !ec;
            }
            if (ec) {
                return false;
            }

            return HasSdkManager(normalized);
        } catch (...) {
            return false;
        }
    }

    SdkInfo BuildSdkInfoFromSdkRoot(const std::string &sdkRoot, const std::string &javaHomePath) {
        SdkInfo sdk;
        sdk.SdkPath = sdkRoot;
        sdk.JavaHomePath = javaHomePath;
        sdk.EmulatorPath = Paths::JoinPaths({sdkRoot, "emulator", "emulator" + Paths::GetExecutableExtension()});
        sdk.AdbPath = Paths::JoinPaths({sdkRoot, "platform-tools", "adb" + Paths::GetExecutableExtension()});

        auto loadFromBinDir = [&](const std::string &binDir) {
            if (sdk.AvdManagerPath.empty()) {
                sdk.AvdManagerPath = FindCmdlineTool(binDir, "avdmanager");
            }
            if (sdk.SdkManagerPath.empty()) {
                sdk.SdkManagerPath = FindCmdlineTool(binDir, "sdkmanager");
            }
        };

        loadFromBinDir(Paths::JoinPaths({sdkRoot, "cmdline-tools", "latest", "bin"}));
        const std::string cmdlineRoot = Paths::JoinPaths({sdkRoot, "cmdline-tools"});
        std::error_code ec;
        if ((sdk.AvdManagerPath.empty() || sdk.SdkManagerPath.empty()) &&
            std::filesystem::exists(cmdlineRoot, ec) &&
            std::filesystem::is_directory(cmdlineRoot, ec)) {
            for (const auto &entry: std::filesystem::directory_iterator(cmdlineRoot, ec)) {
                if (ec) {
                    break;
                }
                std::error_code entryEc;
                if (!entry.is_directory(entryEc)) {
                    continue;
                }
                loadFromBinDir(Paths::JoinPaths({entry.path().string(), "bin"}));
                if (!sdk.AvdManagerPath.empty() && !sdk.SdkManagerPath.empty()) {
                    break;
                }
            }
        }

        sdk.IsFound = std::filesystem::exists(sdk.EmulatorPath);
        return sdk;
    }

    SdkBootstrapResult BootstrapCommandLineTools(
        const std::string &sdkRoot,
        const std::shared_ptr<SdkOperationProgress> &progress
    ) {
        try {
            if (sdkRoot.empty()) {
                return Fail(progress, "Choose an Android SDK directory first.");
            }
            if (IsCancelRequested(progress)) {
                return Cancelled(progress);
            }
            if (!CanInstallAndroidSdkIntoDirectory(sdkRoot)) {
                return Fail(progress, "Choose an empty folder for the Android SDK download.");
            }
            BootstrapDirectoryCleanup cleanup(sdkRoot);

            std::error_code ec;
            SetProgress(progress, 0.02F, "Preparing SDK directory...");
            std::filesystem::create_directories(sdkRoot, ec);
            if (ec) {
                return Fail(progress, StrConcat("Could not create SDK directory: ", ec.message()));
            }

            if (HasSdkManager(sdkRoot)) {
                cleanup.Disarm();
                return Success(progress);
            }
            if (IsCancelRequested(progress)) {
                return Cancelled(progress);
            }

            const std::filesystem::path tempDir = CreateTempDirectory();
            const std::filesystem::path zipPath = tempDir / "commandlinetools.zip";
            const std::filesystem::path extractDir = tempDir / "extract";
            std::filesystem::create_directories(extractDir, ec);
            if (ec) {
                std::filesystem::remove_all(tempDir, ec);
                return Fail(progress, StrConcat("Could not create temporary directory: ", ec.message()));
            }

            std::string error;
            const CommandLineToolsPackage fallbackPackage = BundledCommandLineToolsPackage();
            if (fallbackPackage.DownloadUrl.empty() || fallbackPackage.SizeBytes == 0 || fallbackPackage.Sha256.empty()) {
                std::filesystem::remove_all(tempDir, ec);
                return Fail(progress, "Google does not publish command-line tools for this platform.");
            }

            std::string url = fallbackPackage.DownloadUrl;
            CommandLineToolsPackage expectedPackage = fallbackPackage;
            std::optional<CommandLineToolsPackage> metadataPackage;
            const std::filesystem::path metadataPath = tempDir / "repository.xml";
            if (DownloadFile(COMMAND_LINE_TOOLS_REPOSITORY_URL, metadataPath, progress, {0.02F, 0.05F}, error)) {
                std::ifstream metadata(metadataPath);
                std::string body(
                    (std::istreambuf_iterator<char>(metadata)),
                    std::istreambuf_iterator<char>()
                );
                metadataPackage = ParseCommandLineToolsRepository(
                    body,
                    CurrentCommandLineToolsHostOs(),
                    CurrentCommandLineToolsHostArch()
                );
                if (metadataPackage.has_value()) {
                    url = metadataPackage->DownloadUrl;
                    expectedPackage = *metadataPackage;
                }
            }
            error.clear();
            SetProgress(progress, 0.05F, "Downloading command-line tools...", url);
            if (!DownloadFile(url, zipPath, progress, {0.05F, 0.74F}, error)) {
                std::filesystem::remove_all(tempDir, ec);
                if (IsCancelRequested(progress)) {
                    return Cancelled(progress);
                }
                return Fail(progress, error.empty() ? "Command-line tools download failed." : error);
            }
            if (!detail::FileMatchesCommandLineToolsPackage(zipPath.string(), expectedPackage)) {
                std::filesystem::remove_all(tempDir, ec);
                return Fail(progress, "Downloaded command-line tools failed the official checksum or size check.");
            }
            if (IsCancelRequested(progress)) {
                std::filesystem::remove_all(tempDir, ec);
                return Cancelled(progress);
            }

            SetProgress(progress, 0.74F, "Extracting command-line tools...");
            if (!ExtractZip(zipPath, extractDir, progress)) {
                std::filesystem::remove_all(tempDir, ec);
                if (IsCancelRequested(progress)) {
                    return Cancelled(progress);
                }
                return Fail(progress, "Could not extract command-line tools.");
            }
            if (IsCancelRequested(progress)) {
                std::filesystem::remove_all(tempDir, ec);
                return Cancelled(progress);
            }

            SetProgress(progress, 0.88F, "Installing command-line tools...");
            const std::filesystem::path extractedTools = extractDir / "cmdline-tools";
            const std::filesystem::path cmdlineToolsRoot = std::filesystem::path(sdkRoot) / "cmdline-tools";
            const std::filesystem::path latestTools = cmdlineToolsRoot / "latest";
            std::filesystem::create_directories(cmdlineToolsRoot, ec);
            if (ec) {
                std::filesystem::remove_all(tempDir, ec);
                return Fail(progress, StrConcat("Could not create cmdline-tools directory: ", ec.message()));
            }
            const std::filesystem::path stagingTools = UniqueSiblingPath(latestTools, "installing");
            std::filesystem::remove_all(stagingTools, ec);
            if (ec) {
                std::filesystem::remove_all(tempDir, ec);
                return Fail(progress, StrConcat("Could not prepare cmdline-tools staging directory: ", ec.message()));
            }
            std::filesystem::create_directories(stagingTools, ec);
            if (ec) {
                std::filesystem::remove_all(tempDir, ec);
                return Fail(progress, StrConcat("Could not create cmdline-tools staging directory: ", ec.message()));
            }

            if (!CopyDirectoryContents(extractedTools, stagingTools, error, progress)) {
                std::filesystem::remove_all(tempDir, ec);
                std::filesystem::remove_all(stagingTools, ec);
                if (IsCancelRequested(progress)) {
                    return Cancelled(progress);
                }
                return Fail(progress, StrConcat("Could not install command-line tools: ", error));
            }
            if (IsCancelRequested(progress)) {
                std::filesystem::remove_all(tempDir, ec);
                std::filesystem::remove_all(stagingTools, ec);
                return Cancelled(progress);
            }
            if (!ReplaceDirectoryWithStagedInstall(stagingTools, latestTools, error)) {
                std::filesystem::remove_all(tempDir, ec);
                std::filesystem::remove_all(stagingTools, ec);
                return Fail(progress, error.empty() ? "Could not install command-line tools." : error);
            }

            std::filesystem::remove_all(tempDir, ec);
            if (!HasSdkManager(sdkRoot)) {
                return Fail(progress, "Command-line tools were installed, but sdkmanager was not found.");
            }

            cleanup.Disarm();
            return Success(progress);
        } catch (const std::exception &ex) {
            return Fail(progress, StrConcat("Command-line tools installation failed: ", ex.what()));
        } catch (...) {
            return Fail(progress, "Command-line tools installation failed unexpectedly.");
        }
    }

    SdkBootstrapResult BootstrapBaseAndroidSdk(
        const std::string &sdkRoot,
        const std::string &javaHomePath,
        const std::shared_ptr<SdkOperationProgress> &progress
    ) {
        const SdkBootstrapResult toolsResult = BootstrapCommandLineTools(sdkRoot, progress);
        if (!toolsResult.Succeeded) {
            return toolsResult;
        }

        const SdkBootstrapResult packagesResult = InstallBaseSdkPackages(
            BuildSdkInfoFromSdkRoot(sdkRoot, javaHomePath),
            progress
        );
        if (!packagesResult.Succeeded) {
            return packagesResult;
        }

        return Success(progress, "Android SDK setup completed.");
    }

    SdkBootstrapResult InstallBaseSdkPackages(
        const SdkInfo &sdk,
        const std::shared_ptr<SdkOperationProgress> &progress
    ) {
        if (sdk.SdkManagerPath.empty()) {
            return Fail(progress, "SDK Manager was not found.", "Android SDK setup failed.");
        }
        if (IsCancelRequested(progress)) {
            return Cancelled(progress);
        }

        const SdkProgressRange packageRange = GetSdkProgressRange(progress);
        constexpr float FetchPhaseEnd = 6.0F / 55.0F;
        SetSdkProgressSubrange(progress, packageRange, 0.0F, FetchPhaseEnd);
        SetProgress(progress, 0.0F, "Fetching SDK packages from official sources...");
        const SdkPackageListResult list = ListSdkPackages(sdk, false);
        if (IsCancelRequested(progress)) {
            return Cancelled(progress);
        }
        if (!list.Error.empty() || list.SdkManagerMissing) {
            return Fail(
                progress,
                list.Error.empty() ? "Could not fetch SDK package lists. Check your network connection." : list.Error,
                "Android SDK setup failed."
            );
        }

        std::vector<std::string> packagesToInstall;
        if (!IsInstalledPackage(list.Packages, "platform-tools")) {
            packagesToInstall.emplace_back("platform-tools");
        }
        if (!IsInstalledPackage(list.Packages, "emulator")) {
            packagesToInstall.emplace_back("emulator");
        }

        const bool hasPlatform = std::ranges::any_of(list.Packages, [](const SdkPackage &package) {
            return package.Installed && IsStableSdkPlatformPackage(package.Path);
        });
        if (!hasPlatform) {
            const std::string latestPlatform = SelectLatestSdkPlatformPackage(list.Packages);
            if (latestPlatform.empty()) {
                return Fail(progress, "Could not find an Android SDK Platform package.", "Android SDK setup failed.");
            }
            packagesToInstall.push_back(latestPlatform);
        }

        if (packagesToInstall.empty()) {
            return Success(progress, "Android SDK setup completed.");
        }

        const LicenseStatus licenseStatus = CheckSdkLicenses(sdk);
        if (IsCancelRequested(progress)) {
            return Cancelled(progress);
        }
        if (licenseStatus == LicenseStatus::SomeUnaccepted) {
            return Fail(
                progress,
                "Android SDK licenses must be accepted before installing packages.",
                "Android SDK setup failed."
            );
        }
        if (licenseStatus == LicenseStatus::CheckFailed) {
            return Fail(
                progress,
                "Could not query license state. Check that the SDK Manager is working.",
                "Android SDK setup failed."
            );
        }

        SetSdkProgressSubrange(progress, packageRange, FetchPhaseEnd, 1.0F);

        SetProgress(
            progress,
            0.0F,
            "Installing base Android SDK packages...",
            JoinPackagePaths(packagesToInstall)
        );
        if (!InstallSdkPackages(sdk, packagesToInstall, progress)) {
            if (IsCancelRequested(progress)) {
                return Cancelled(progress);
            }
            return Fail(progress, "Base Android SDK package installation failed.", "Android SDK setup failed.");
        }

        return Success(progress, "Android SDK setup completed.");
    }
}
