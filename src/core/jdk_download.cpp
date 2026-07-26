//
// Created by kangaroo. on 26/07/2026.
//

#include "jdk_download.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <optional>
#include <rfl.hpp>
#include <rfl/json.hpp>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#include <winhttp.h>
#else
#include <curl/curl.h>
#endif

#include "jdk.h"
#include "paths.h"
#include "process.h"
#include "utilities.h"

namespace CoreDeck {
    namespace {
        constexpr int MIN_DOWNLOADABLE_LTS = 17;

        struct AdoptiumAvailableReleases {
            std::vector<int> available_lts_releases; // NOLINT(readability-identifier-naming)
        };

        struct TemurinPackageFile {
            std::string link;
            std::string name;
            std::uintmax_t size = 0;
        };

        struct TemurinBinary {
            TemurinPackageFile package;
        };

        struct TemurinVersionData {
            int major = 0;
            std::string openjdk_version; // NOLINT(readability-identifier-naming)
        };

        struct TemurinRelease {
            std::vector<TemurinBinary> binaries;
            TemurinVersionData version_data; // NOLINT(readability-identifier-naming)
        };

        struct AzulPackage {
            std::string download_url; // NOLINT(readability-identifier-naming)
            std::vector<int> java_version; // NOLINT(readability-identifier-naming)
            std::string name;
            int openjdk_build_number = 0; // NOLINT(readability-identifier-naming)
        };

        void SetProgress(
            const std::shared_ptr<SdkOperationProgress> &progress,
            const float percent,
            const std::string &status,
            const std::string &detail = ""
        ) {
            if (!progress || progress->CancelRequested.load()) {
                return;
            }

            std::lock_guard lock(progress->Mutex);
            progress->Percent = percent;
            progress->StatusText = status;
            progress->DetailText = detail;
            progress->Finished = false;
            progress->Succeeded = false;
        }

        bool IsCancelRequested(const std::shared_ptr<SdkOperationProgress> &progress) {
            return progress && progress->CancelRequested.load();
        }

        JdkInstallResult FailInstall(
            const std::shared_ptr<SdkOperationProgress> &progress,
            const std::string &error
        ) {
            if (progress) {
                std::lock_guard lock(progress->Mutex);
                progress->Finished = true;
                progress->Succeeded = false;
                progress->StatusText = "JDK installation failed.";
                progress->DetailText = error;
            }
            return {.Succeeded = false, .Error = error};
        }

        JdkInstallResult CancelledInstall(const std::shared_ptr<SdkOperationProgress> &progress) {
            if (progress) {
                std::lock_guard lock(progress->Mutex);
                progress->Finished = true;
                progress->Succeeded = false;
                progress->StatusText = "Cancelled.";
                progress->DetailText.clear();
            }
            return {.Succeeded = false, .Cancelled = true, .Error = "Operation cancelled."};
        }

        JdkInstallResult SuccessInstall(
            const std::shared_ptr<SdkOperationProgress> &progress,
            const std::string &javaHomePath
        ) {
            if (progress) {
                std::lock_guard lock(progress->Mutex);
                progress->Finished = true;
                progress->Succeeded = true;
                progress->Percent = 1.0F;
                progress->StatusText = "JDK installed.";
                progress->DetailText = javaHomePath;
            }
            return {.Succeeded = true, .JavaHomePath = javaHomePath};
        }

        std::filesystem::path CreateTempDirectory() {
            const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
            std::filesystem::path dir = std::filesystem::temp_directory_path() / StrConcat("coredeck-jdk-", std::to_string(now));
            std::filesystem::create_directories(dir);
            return dir;
        }

        std::string UrlEncodeSpaces(std::string value) {
            std::string result;
            for (const char c: value) {
                if (c == ' ') {
                    result += "%20";
                } else {
                    result.push_back(c);
                }
            }
            return result;
        }

        std::string CurrentOsForAdoptium() {
#if defined(_WIN32)
            return "windows";
#elif defined(__APPLE__)
            return "mac";
#else
            return "linux";
#endif
        }

        std::string CurrentArchForAdoptium() {
#if defined(__aarch64__) || defined(_M_ARM64)
            return "aarch64";
#else
            return "x64";
#endif
        }

        std::string CurrentOsForAzul() {
#if defined(_WIN32)
            return "windows";
#elif defined(__APPLE__)
            return "macos";
#else
            return "linux";
#endif
        }

        std::string CurrentArchForAzul() {
#if defined(__aarch64__) || defined(_M_ARM64)
            return "arm";
#else
            return "x86";
#endif
        }

        std::string CurrentHwBitnessForAzul() {
            return "64";
        }

        std::string CurrentOsForCorretto() {
#if defined(_WIN32)
            return "windows";
#elif defined(__APPLE__)
            return "macos";
#else
            return "linux";
#endif
        }

        std::string CurrentArchForCorretto() {
#if defined(__aarch64__) || defined(_M_ARM64)
            return "aarch64";
#else
            return "x64";
#endif
        }

        std::string ArchiveExtension() {
#if defined(_WIN32)
            return "zip";
#else
            return "tar.gz";
#endif
        }

        std::string TemurinPackageUrl(const int featureVersion) {
            return StrConcat(
                "https://api.adoptium.net/v3/assets/feature_releases/",
                std::to_string(featureVersion),
                "/ga?architecture=",
                CurrentArchForAdoptium(),
                "&heap_size=normal&image_type=jdk&jvm_impl=hotspot&os=",
                CurrentOsForAdoptium(),
                "&vendor=eclipse&page_size=1&sort_method=DEFAULT&sort_order=DESC"
            );
        }

        std::string CorrettoPackageUrl(const int featureVersion) {
            return StrConcat(
                "https://corretto.aws/downloads/latest/amazon-corretto-",
                std::to_string(featureVersion),
                "-",
                CurrentArchForCorretto(),
                "-",
                CurrentOsForCorretto(),
                "-jdk.",
                ArchiveExtension()
            );
        }

        std::string AzulPackageUrl(const int featureVersion) {
            return StrConcat(
                "https://api.azul.com/metadata/v1/zulu/packages/?java_version=",
                std::to_string(featureVersion),
                "&os=",
                CurrentOsForAzul(),
                "&arch=",
                CurrentArchForAzul(),
                "&hw_bitness=",
                CurrentHwBitnessForAzul(),
                "&archive_type=",
                UrlEncodeSpaces(ArchiveExtension()),
                "&java_package_type=jdk&javafx_bundled=false&release_status=ga",
                "&availability_types=CA&certifications=tck&latest=true&page=1&page_size=8"
            );
        }

        std::string UserAgent() {
            return StrConcat("CoreDeck/", COREDECK_VERSION);
        }

        std::vector<int> FallbackLtsReleases() {
            return {17, 21, 25};
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

        bool ParseHttpsUrl(const std::string &url, std::wstring &host, std::wstring &path) {
            constexpr std::string_view prefix = "https://";
            if (!url.starts_with(prefix)) {
                return false;
            }
            const std::size_t pathStart = url.find('/', prefix.size());
            if (pathStart == std::string::npos) {
                return false;
            }
            host = Utf8ToWide(url.substr(prefix.size(), pathStart - prefix.size()));
            path = Utf8ToWide(url.substr(pathStart));
            return !host.empty() && !path.empty();
        }

        std::optional<std::string> HttpGet(const std::string &url) {
            std::wstring host;
            std::wstring path;
            if (!ParseHttpsUrl(url, host, path)) {
                return std::nullopt;
            }

            const std::string ua = UserAgent();
            const std::wstring wideUa(ua.begin(), ua.end());
            HINTERNET session = WinHttpOpen(
                wideUa.c_str(),
                WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                WINHTTP_NO_PROXY_NAME,
                WINHTTP_NO_PROXY_BYPASS,
                0
            );
            if (!session) {
                return std::nullopt;
            }
            WinHttpSetTimeouts(session, 15000, 15000, 15000, 15000);

            HINTERNET connect = WinHttpConnect(session, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
            if (!connect) {
                WinHttpCloseHandle(session);
                return std::nullopt;
            }

            HINTERNET request = WinHttpOpenRequest(
                connect,
                L"GET",
                path.c_str(),
                nullptr,
                WINHTTP_NO_REFERER,
                WINHTTP_DEFAULT_ACCEPT_TYPES,
                WINHTTP_FLAG_SECURE
            );
            if (!request) {
                WinHttpCloseHandle(connect);
                WinHttpCloseHandle(session);
                return std::nullopt;
            }

            const wchar_t *headers = L"Accept: application/json\r\n";
            const BOOL sent = WinHttpSendRequest(request, headers, static_cast<DWORD>(-1L), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                              WinHttpReceiveResponse(request, nullptr);
            std::optional<std::string> result;
            if (sent) {
                DWORD status = 0;
                DWORD statusSize = sizeof(status);
                WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX);
                if (status >= 200 && status < 300) {
                    std::string body;
                    DWORD available = 0;
                    while (WinHttpQueryDataAvailable(request, &available) && available > 0) {
                        std::string chunk(available, '\0');
                        DWORD read = 0;
                        if (!WinHttpReadData(request, chunk.data(), available, &read)) {
                            break;
                        }
                        chunk.resize(read);
                        body.append(chunk);
                    }
                    result = std::move(body);
                }
            }

            WinHttpCloseHandle(request);
            WinHttpCloseHandle(connect);
            WinHttpCloseHandle(session);
            return result;
        }

        bool DownloadFile(
            const std::string &url,
            const std::filesystem::path &destination,
            const std::shared_ptr<SdkOperationProgress> &progress,
            std::string &error
        ) {
            if (IsCancelRequested(progress)) {
                error = "Operation cancelled.";
                return false;
            }

            std::wstring host;
            std::wstring path;
            if (!ParseHttpsUrl(url, host, path)) {
                error = "Unsupported download URL.";
                return false;
            }

            const std::string ua = UserAgent();
            const std::wstring wideUa(ua.begin(), ua.end());
            HINTERNET session = WinHttpOpen(
                wideUa.c_str(),
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

            HINTERNET connect = WinHttpConnect(session, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
            if (!connect) {
                WinHttpCloseHandle(session);
                error = "Could not connect to JDK download host.";
                return false;
            }

            HINTERNET request = WinHttpOpenRequest(
                connect,
                L"GET",
                path.c_str(),
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
                error = "JDK download request failed.";
                return false;
            }

            DWORD status = 0;
            DWORD statusSize = sizeof(status);
            WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX);
            if (status < 200 || status >= 300) {
                WinHttpCloseHandle(request);
                WinHttpCloseHandle(connect);
                WinHttpCloseHandle(session);
                error = StrConcat("JDK download failed with HTTP ", std::to_string(status), ".");
                return false;
            }

            std::ofstream out(destination, std::ios::binary);
            if (!out.is_open()) {
                WinHttpCloseHandle(request);
                WinHttpCloseHandle(connect);
                WinHttpCloseHandle(session);
                error = "Could not write downloaded JDK file.";
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
                    error = "JDK download failed while reading the response.";
                    transferOk = false;
                    break;
                }
                if (available == 0) {
                    break;
                }
                if (IsCancelRequested(progress)) {
                    error = "Operation cancelled.";
                    WinHttpCloseHandle(request);
                    WinHttpCloseHandle(connect);
                    WinHttpCloseHandle(session);
                    return false;
                }

                std::string chunk(available, '\0');
                DWORD read = 0;
                if (!WinHttpReadData(request, chunk.data(), available, &read) || read == 0) {
                    error = "JDK download failed while reading the response.";
                    transferOk = false;
                    break;
                }
                chunk.resize(read);
                out.write(chunk.data(), static_cast<std::streamsize>(chunk.size()));
                downloaded += read;

                if (total > 0) {
                    const float fraction = static_cast<float>(downloaded) / static_cast<float>(total);
                    SetProgress(progress, 0.08F + (fraction * 0.62F), "Downloading JDK...");
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

        std::string PowerShellQuote(const std::string &value) {
            std::string escaped = "'";
            for (const char c: value) {
                if (c == '\'') {
                    escaped += "''";
                } else {
                    escaped.push_back(c);
                }
            }
            escaped.push_back('\'');
            return escaped;
        }

        bool ExtractArchive(
            const std::filesystem::path &archivePath,
            const std::filesystem::path &destination,
            const std::shared_ptr<SdkOperationProgress> &progress
        ) {
            const std::string command = StrConcat(
                "Expand-Archive -LiteralPath ",
                PowerShellQuote(archivePath.string()),
                " -DestinationPath ",
                PowerShellQuote(destination.string()),
                " -Force"
            );
            return StreamCommandArgsWithEnvCancelable(
                "powershell.exe",
                {"-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", command},
                "",
                {},
                {},
                [&progress] {
                    return IsCancelRequested(progress);
                }
            );
        }
#else
        size_t WriteStringCallback(const char *ptr, const size_t size, const size_t nmemb, void *userdata) {
            auto *buf = static_cast<std::string *>(userdata);
            buf->append(ptr, size * nmemb);
            return size * nmemb;
        }

        size_t WriteFileCallback(const char *ptr, const size_t size, const size_t nmemb, void *userdata) {
            auto *out = static_cast<std::ofstream *>(userdata);
            out->write(ptr, static_cast<std::streamsize>(size * nmemb));
            return size * nmemb;
        }

        std::optional<std::string> HttpGet(const std::string &url) {
            CURL *curl = curl_easy_init();
            if (!curl) {
                return std::nullopt;
            }

            std::string body;
            curl_slist *headers = curl_slist_append(nullptr, "Accept: application/json");
            const std::string userAgent = UserAgent();
            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
            curl_easy_setopt(curl, CURLOPT_USERAGENT, userAgent.c_str());
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
            curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteStringCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);

            const CURLcode rc = curl_easy_perform(curl);
            curl_slist_free_all(headers);
            curl_easy_cleanup(curl);
            if (rc != CURLE_OK) {
                return std::nullopt;
            }
            return body;
        }

        int DownloadProgressCallback(void *clientp, const curl_off_t dltotal, const curl_off_t dlnow, curl_off_t, curl_off_t) {
            auto *progress = static_cast<std::shared_ptr<SdkOperationProgress> *>(clientp);
            if (progress != nullptr && IsCancelRequested(*progress)) {
                return 1;
            }
            if (progress == nullptr || !*progress || dltotal <= 0) {
                return 0;
            }

            const float fraction = static_cast<float>(dlnow) / static_cast<float>(dltotal);
            SetProgress(*progress, 0.08F + (fraction * 0.62F), "Downloading JDK...");
            return 0;
        }

        bool DownloadFile(
            const std::string &url,
            const std::filesystem::path &destination,
            const std::shared_ptr<SdkOperationProgress> &progress,
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
                error = "Could not write downloaded JDK file.";
                return false;
            }

            const std::string userAgent = UserAgent();
            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_USERAGENT, userAgent.c_str());
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 20L);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 900L);
            curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteFileCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out);
            curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
            curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, DownloadProgressCallback);
            curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &progress);

            const CURLcode rc = curl_easy_perform(curl);
            curl_easy_cleanup(curl);
            out.close();

            if (rc != CURLE_OK) {
                error = IsCancelRequested(progress) ? "Operation cancelled." : "JDK download failed.";
                return false;
            }
            std::error_code fileEc;
            return std::filesystem::exists(destination, fileEc) &&
                   std::filesystem::file_size(destination, fileEc) > 0 &&
                   !fileEc;
        }

        bool ExtractArchive(
            const std::filesystem::path &archivePath,
            const std::filesystem::path &destination,
            const std::shared_ptr<SdkOperationProgress> &progress
        ) {
            if (archivePath.string().ends_with(".zip")) {
                const std::string unzipPath = std::filesystem::exists("/usr/bin/unzip") ? "/usr/bin/unzip" : "unzip";
                return StreamCommandArgsWithEnvCancelable(
                    unzipPath,
                    {"-q", "-o", archivePath.string(), "-d", destination.string()},
                    "",
                    {},
                    {},
                    [&progress] {
                        return IsCancelRequested(progress);
                    }
                );
            }

            return StreamCommandArgsWithEnvCancelable(
                "tar",
                {"-xzf", archivePath.string(), "-C", destination.string()},
                "",
                {},
                {},
                [&progress] {
                    return IsCancelRequested(progress);
                }
            );
        }
#endif

        bool IsBetterAzulPackageName(const std::string &name) {
            const std::string lowered = LowerCopy(name);
            return lowered.find("musl") == std::string::npos &&
                   lowered.find("crac") == std::string::npos &&
                   lowered.find("-jre") == std::string::npos &&
                   lowered.find("jdk") != std::string::npos;
        }

        std::string VersionFromParts(const std::vector<int> &parts, const int build, const int featureVersion) {
            if (parts.empty()) {
                return StrConcat(std::to_string(featureVersion), " LTS");
            }

            std::string version;
            for (std::size_t i = 0; i < parts.size(); i++) {
                if (i > 0) {
                    version.push_back('.');
                }
                version += std::to_string(parts[i]);
            }
            if (build > 0) {
                version += StrConcat("+", std::to_string(build));
            }
            if (featureVersion >= MIN_DOWNLOADABLE_LTS) {
                version += "-LTS";
            }
            return version;
        }

        std::string VendorInstallDirName(const JdkPackage &package) {
            std::string vendor = JdkVendorId(package.Vendor);
            std::string version = package.JavaVersion.empty() ? std::to_string(package.FeatureVersion) : package.JavaVersion;
            for (char &c: version) {
                if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|' || c == '+') {
                    c = '-';
                }
            }
            return StrConcat(vendor, "-", version);
        }

        std::filesystem::path UniqueSiblingPath(const std::filesystem::path &target, const std::string_view suffix) {
            std::filesystem::path parent = target.parent_path();
            if (parent.empty()) {
                parent = ".";
            }
            std::string name = target.filename().string();
            if (name.empty()) {
                name = "jdk";
            }
            const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
            return parent / StrConcat(".", name, "-", std::string(suffix), "-", std::to_string(now));
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
                error = ec.message();
                return false;
            }

            for (const auto &entry: std::filesystem::directory_iterator(from, ec)) {
                if (ec) {
                    error = ec.message();
                    return false;
                }
                if (IsCancelRequested(progress)) {
                    error = "Operation cancelled.";
                    return false;
                }
                const std::filesystem::path target = to / entry.path().filename();
                std::filesystem::copy(
                    entry.path(),
                    target,
                    std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing,
                    ec
                );
                if (ec) {
                    error = ec.message();
                    return false;
                }
                if (IsCancelRequested(progress)) {
                    error = "Operation cancelled.";
                    return false;
                }
            }

            return true;
        }

        bool ReplaceDirectoryWithStagedInstall(
            const std::filesystem::path &stagingDir,
            const std::filesystem::path &installDir,
            std::string &error
        ) {
            std::error_code ec;
            std::filesystem::path backupDir;

            if (std::filesystem::exists(installDir, ec)) {
                backupDir = UniqueSiblingPath(installDir, "previous");
                std::filesystem::rename(installDir, backupDir, ec);
                if (ec) {
                    error = StrConcat("Could not prepare existing JDK directory for replacement: ", ec.message());
                    return false;
                }
            }

            std::filesystem::rename(stagingDir, installDir, ec);
            if (ec) {
                error = StrConcat("Could not move JDK into place: ", ec.message());
                if (!backupDir.empty()) {
                    std::error_code restoreEc;
                    if (!std::filesystem::exists(installDir, restoreEc)) {
                        std::filesystem::rename(backupDir, installDir, restoreEc);
                    }
                    if (restoreEc) {
                        error += StrConcat(" The previous JDK could not be restored: ", restoreEc.message());
                    }
                }
                return false;
            }

            if (!backupDir.empty()) {
                std::filesystem::remove_all(backupDir, ec);
            }
            return true;
        }

        std::vector<JdkPackage> BuildCorrettoPackages(const std::vector<int> &releases) {
            std::vector<JdkPackage> packages;
            for (const int featureVersion: releases) {
                JdkPackage package;
                package.Vendor = JdkVendor::AmazonCorretto;
                package.FeatureVersion = featureVersion;
                package.JavaVersion = StrConcat(std::to_string(featureVersion), " LTS latest");
                package.ArchiveName = StrConcat(
                    "amazon-corretto-",
                    std::to_string(featureVersion),
                    "-",
                    CurrentArchForCorretto(),
                    "-",
                    CurrentOsForCorretto(),
                    "-jdk.",
                    ArchiveExtension()
                );
                package.DownloadUrl = CorrettoPackageUrl(featureVersion);
                packages.push_back(std::move(package));
            }
            return packages;
        }
    }

    const char *JdkVendorDisplayName(const JdkVendor vendor) {
        switch (vendor) {
            case JdkVendor::EclipseTemurin:
                return "Eclipse Temurin";
            case JdkVendor::AzulZulu:
                return "Azul Zulu";
            case JdkVendor::AmazonCorretto:
                return "Amazon Corretto";
        }
        return "Eclipse Temurin";
    }

    const char *JdkVendorId(const JdkVendor vendor) {
        switch (vendor) {
            case JdkVendor::EclipseTemurin:
                return "temurin";
            case JdkVendor::AzulZulu:
                return "zulu";
            case JdkVendor::AmazonCorretto:
                return "corretto";
        }
        return "temurin";
    }

    std::string DefaultJdkInstallRoot() {
        const std::string home = Paths::GetHomeDirectory();
        if (home.empty()) {
            return "";
        }

#if defined(_WIN32)
        if (const char *localAppData = std::getenv("LOCALAPPDATA")) { // NOLINT(concurrency-mt-unsafe)
            return Paths::JoinPaths({std::string(localAppData), "CoreDeck", "jdks"});
        }
        return Paths::JoinPaths({home, "AppData", "Local", "CoreDeck", "jdks"});
#elif defined(__APPLE__)
        return Paths::JoinPaths({home, "Library", "Application Support", "CoreDeck", "jdks"});
#else
        if (const char *xdgDataHome = std::getenv("XDG_DATA_HOME")) { // NOLINT(concurrency-mt-unsafe)
            return Paths::JoinPaths({std::string(xdgDataHome), "coredeck", "jdks"});
        }
        return Paths::JoinPaths({home, ".local", "share", "coredeck", "jdks"});
#endif
    }

    bool CanInstallJdkIntoRequestedDirectory(const std::string &requestedJavaHomePath) {
        return detail::CanInstallJdkIntoRequestedDirectory(requestedJavaHomePath);
    }

    JdkPackageListResult ListLatestLtsJdkPackages(const JdkVendor vendor) {
        if (vendor == JdkVendor::AmazonCorretto) {
            return {.Packages = BuildCorrettoPackages(FallbackLtsReleases())};
        }

        auto releasesBody = HttpGet("https://api.adoptium.net/v3/info/available_releases");
        std::vector<int> releases =
            releasesBody && !releasesBody->empty()
                ? detail::ParseJdkLtsReleases(*releasesBody)
                : FallbackLtsReleases();
        if (releases.empty()) {
            releases = FallbackLtsReleases();
        }

        JdkPackageListResult result;
        for (const int featureVersion: releases) {
            const std::string packageUrl =
                vendor == JdkVendor::EclipseTemurin
                    ? TemurinPackageUrl(featureVersion)
                    : AzulPackageUrl(featureVersion);
            auto body = HttpGet(packageUrl);
            if (!body || body->empty()) {
                continue;
            }

            std::optional<JdkPackage> package =
                vendor == JdkVendor::EclipseTemurin
                    ? detail::ParseTemurinJdkPackage(*body, featureVersion)
                    : detail::ParseAzulJdkPackage(*body, featureVersion);
            if (package.has_value()) {
                result.Packages.push_back(std::move(*package));
            }
        }

        if (result.Packages.empty()) {
            result.Error = "No JDK packages are available for this system.";
        }
        return result;
    }

    JdkInstallResult InstallJdkPackage(
        const JdkPackage &package,
        const std::shared_ptr<SdkOperationProgress> &progress,
        const std::string &requestedJavaHomePath
    ) {
        try {
            if (package.DownloadUrl.empty()) {
                return FailInstall(progress, "No JDK download URL is available.");
            }
            const std::string installDirString = detail::ResolveJdkInstallDirectory(package, requestedJavaHomePath);
            if (installDirString.empty()) {
                return FailInstall(progress, "Could not determine a JDK install directory.");
            }
            if (IsCancelRequested(progress)) {
                return CancelledInstall(progress);
            }

            const bool useRequestedPath =
                !Paths::NormalizePath(requestedJavaHomePath).empty() &&
                !LooksLikeJavaHome(requestedJavaHomePath);
            const std::filesystem::path installDir = installDirString;
            std::error_code ec;
            SetProgress(progress, 0.02F, "Preparing JDK directory...");
            const bool installDirExists = std::filesystem::exists(installDir, ec);
            if (ec) {
                return FailInstall(progress, StrConcat("Could not access the selected JDK path: ", ec.message()));
            }
            if (installDirExists && !std::filesystem::is_directory(installDir, ec)) {
                if (ec) {
                    return FailInstall(progress, StrConcat("Could not access the selected JDK path: ", ec.message()));
                }
                return FailInstall(progress, "The selected JDK path is not a directory.");
            }
            if (useRequestedPath && !detail::CanInstallJdkIntoRequestedDirectory(installDir.string())) {
                return FailInstall(progress, "Choose an empty folder for the JDK download.");
            }

            const std::filesystem::path installParent = installDir.parent_path();
            if (!installParent.empty()) {
                std::filesystem::create_directories(installParent, ec);
                if (ec) {
                    return FailInstall(progress, StrConcat("Could not create JDK parent directory: ", ec.message()));
                }
            }

            const std::filesystem::path tempDir = CreateTempDirectory();
            const std::filesystem::path archivePath = tempDir / (package.ArchiveName.empty() ? StrConcat("jdk.", ArchiveExtension()) : package.ArchiveName);
            const std::filesystem::path extractDir = tempDir / "extract";
            std::filesystem::create_directories(extractDir, ec);
            if (ec) {
                std::filesystem::remove_all(tempDir, ec);
                return FailInstall(progress, StrConcat("Could not create temporary directory: ", ec.message()));
            }

            const std::filesystem::path stagingDir = UniqueSiblingPath(installDir, "installing");
            std::filesystem::remove_all(stagingDir, ec);
            if (ec) {
                std::filesystem::remove_all(tempDir, ec);
                return FailInstall(progress, StrConcat("Could not prepare JDK staging directory: ", ec.message()));
            }
            std::filesystem::create_directories(stagingDir, ec);
            if (ec) {
                std::filesystem::remove_all(tempDir, ec);
                return FailInstall(progress, StrConcat("Could not create JDK staging directory: ", ec.message()));
            }

            std::string error;
            SetProgress(progress, 0.08F, "Downloading JDK...", package.DownloadUrl);
            if (!DownloadFile(package.DownloadUrl, archivePath, progress, error)) {
                std::filesystem::remove_all(tempDir, ec);
                std::filesystem::remove_all(stagingDir, ec);
                if (IsCancelRequested(progress)) {
                    return CancelledInstall(progress);
                }
                return FailInstall(progress, error.empty() ? "JDK download failed." : error);
            }
            if (IsCancelRequested(progress)) {
                std::filesystem::remove_all(tempDir, ec);
                std::filesystem::remove_all(stagingDir, ec);
                return CancelledInstall(progress);
            }

            SetProgress(progress, 0.74F, "Extracting JDK...");
            if (!ExtractArchive(archivePath, extractDir, progress)) {
                std::filesystem::remove_all(tempDir, ec);
                std::filesystem::remove_all(stagingDir, ec);
                if (IsCancelRequested(progress)) {
                    return CancelledInstall(progress);
                }
                return FailInstall(progress, "Could not extract JDK archive.");
            }
            if (IsCancelRequested(progress)) {
                std::filesystem::remove_all(tempDir, ec);
                std::filesystem::remove_all(stagingDir, ec);
                return CancelledInstall(progress);
            }

            const std::string extractedJavaHome = detail::FindJavaHomeInDirectory(extractDir.string());
            if (extractedJavaHome.empty()) {
                std::filesystem::remove_all(tempDir, ec);
                std::filesystem::remove_all(stagingDir, ec);
                return FailInstall(progress, "The downloaded archive did not contain a JDK home.");
            }

            SetProgress(progress, 0.88F, "Installing JDK...");
            if (!CopyDirectoryContents(extractedJavaHome, stagingDir, error, progress)) {
                std::filesystem::remove_all(tempDir, ec);
                std::filesystem::remove_all(stagingDir, ec);
                if (IsCancelRequested(progress)) {
                    return CancelledInstall(progress);
                }
                return FailInstall(progress, StrConcat("Could not install JDK: ", error));
            }
            if (IsCancelRequested(progress)) {
                std::filesystem::remove_all(tempDir, ec);
                std::filesystem::remove_all(stagingDir, ec);
                return CancelledInstall(progress);
            }

            if (!LooksLikeJavaHome(stagingDir.string())) {
                std::filesystem::remove_all(tempDir, ec);
                std::filesystem::remove_all(stagingDir, ec);
                return FailInstall(progress, "JDK was installed, but java was not found under bin.");
            }
            if (!ReplaceDirectoryWithStagedInstall(stagingDir, installDir, error)) {
                std::filesystem::remove_all(tempDir, ec);
                std::filesystem::remove_all(stagingDir, ec);
                return FailInstall(progress, error.empty() ? "Could not move JDK into place." : error);
            }

            std::filesystem::remove_all(tempDir, ec);
            const std::string javaHomePath = installDir.string();
            if (!LooksLikeJavaHome(javaHomePath)) {
                return FailInstall(progress, "JDK was installed, but java was not found under bin.");
            }
            return SuccessInstall(progress, javaHomePath);
        } catch (const std::exception &ex) {
            return FailInstall(progress, StrConcat("JDK installation failed: ", ex.what()));
        } catch (...) {
            return FailInstall(progress, "JDK installation failed unexpectedly.");
        }
    }

    namespace detail {
        std::string ResolveJdkInstallDirectory(const JdkPackage &package, const std::string &requestedJavaHomePath) {
            const std::string requested = Paths::NormalizePath(requestedJavaHomePath);
            if (!requested.empty() && !LooksLikeJavaHome(requested)) {
                return requested;
            }

            const std::string installRoot = DefaultJdkInstallRoot();
            if (installRoot.empty()) {
                return "";
            }
            return (std::filesystem::path(installRoot) / VendorInstallDirName(package)).string();
        }

        bool CanInstallJdkIntoRequestedDirectory(const std::string &requestedJavaHomePath) {
            const std::string requested = Paths::NormalizePath(requestedJavaHomePath);
            if (requested.empty() || LooksLikeJavaHome(requested)) {
                return false;
            }

            std::error_code ec;
            const std::filesystem::path path(requested);
            if (!std::filesystem::exists(path, ec)) {
                if (ec) {
                    return false;
                }
                return true;
            }
            if (!std::filesystem::is_directory(path, ec)) {
                if (ec) {
                    return false;
                }
                return false;
            }
            return std::filesystem::is_empty(path, ec) && !ec;
        }

        std::vector<int> ParseJdkLtsReleases(const std::string &body) {
            try {
                const auto parsed = rfl::json::read<AdoptiumAvailableReleases>(body);
                if (!parsed) {
                    return {};
                }
                std::vector<int> releases;
                for (const int release: parsed.value().available_lts_releases) {
                    if (release >= MIN_DOWNLOADABLE_LTS) {
                        releases.push_back(release);
                    }
                }
                std::ranges::sort(releases);
                const auto unique = std::ranges::unique(releases);
                releases.erase(unique.begin(), unique.end());
                return releases;
            } catch (...) {
                return {};
            }
        }

        std::optional<JdkPackage> ParseTemurinJdkPackage(const std::string &body, const int featureVersion) {
            try {
                const auto parsed = rfl::json::read<std::vector<TemurinRelease>, rfl::DefaultIfMissing>(body);
                if (!parsed || parsed.value().empty()) {
                    return std::nullopt;
                }

                for (const TemurinRelease &release: parsed.value()) {
                    if (release.binaries.empty()) {
                        continue;
                    }
                    const TemurinPackageFile &file = release.binaries.front().package;
                    if (file.link.empty()) {
                        continue;
                    }

                    JdkPackage package;
                    package.Vendor = JdkVendor::EclipseTemurin;
                    package.FeatureVersion = featureVersion;
                    package.JavaVersion = release.version_data.openjdk_version.empty()
                                              ? StrConcat(std::to_string(featureVersion), " LTS")
                                              : release.version_data.openjdk_version;
                    package.ArchiveName = file.name;
                    package.DownloadUrl = file.link;
                    package.SizeBytes = file.size;
                    return package;
                }
            } catch (...) {
                return std::nullopt;
            }
            return std::nullopt;
        }

        std::optional<JdkPackage> ParseAzulJdkPackage(const std::string &body, const int featureVersion) {
            try {
                const auto parsed = rfl::json::read<std::vector<AzulPackage>, rfl::DefaultIfMissing>(body);
                if (!parsed || parsed.value().empty()) {
                    return std::nullopt;
                }

                const AzulPackage *fallback = nullptr;
                for (const AzulPackage &candidate: parsed.value()) {
                    if (candidate.download_url.empty()) {
                        continue;
                    }
                    if (fallback == nullptr) {
                        fallback = &candidate;
                    }
                    if (IsBetterAzulPackageName(candidate.name)) {
                        fallback = &candidate;
                        break;
                    }
                }
                if (fallback == nullptr) {
                    return std::nullopt;
                }

                JdkPackage package;
                package.Vendor = JdkVendor::AzulZulu;
                package.FeatureVersion = featureVersion;
                package.JavaVersion = VersionFromParts(fallback->java_version, fallback->openjdk_build_number, featureVersion);
                package.ArchiveName = fallback->name;
                package.DownloadUrl = fallback->download_url;
                return package;
            } catch (...) {
                return std::nullopt;
            }
        }

        std::string FindJavaHomeInDirectory(const std::string &directory) {
            std::error_code ec;
            if (directory.empty() || !std::filesystem::exists(directory, ec)) {
                return "";
            }

            const std::filesystem::path root(directory);
            if (LooksLikeJavaHome(root.string())) {
                return NormalizeJavaHomePath(root.string());
            }

            std::vector<std::filesystem::path> stack{root};
            while (!stack.empty()) {
                const std::filesystem::path current = stack.back();
                stack.pop_back();

                if (LooksLikeJavaHome(current.string())) {
                    return NormalizeJavaHomePath(current.string());
                }

                for (const auto &entry: std::filesystem::directory_iterator(current, ec)) {
                    if (ec) {
                        break;
                    }
                    std::error_code entryEc;
                    if (!entry.is_directory(entryEc)) {
                        continue;
                    }
                    stack.push_back(entry.path());
                }
            }
            return "";
        }
    }
}
