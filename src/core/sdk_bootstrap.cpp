//
// Created by AbdulMuaz Aqeel on 19/04/2026.
//

#include "sdk_bootstrap.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#include <winhttp.h>
#else
#include <curl/curl.h>
#endif

#include "paths.h"
#include "process.h"
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
            if (!progress) {
                return;
            }

            std::lock_guard lock(progress->Mutex);
            progress->Percent = percent;
            progress->StatusText = status;
            progress->Finished = false;
            progress->Succeeded = false;
            progress->DetailText = detail;
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
            if (progress) {
                std::lock_guard lock(progress->Mutex);
                progress->Finished = true;
                progress->Succeeded = true;
                progress->Percent = 1.0F;
                progress->StatusText = status;
            }
            return {.Succeeded = true};
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

        bool CopyDirectoryContents(const std::filesystem::path &from, const std::filesystem::path &to, std::string &error) {
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
            std::string &error
        ) {
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
            while (WinHttpQueryDataAvailable(request, &available) && available > 0) {
                std::string chunk(available, '\0');
                DWORD read = 0;
                if (!WinHttpReadData(request, chunk.data(), available, &read)) {
                    break;
                }
                chunk.resize(read);
                out.write(chunk.data(), static_cast<std::streamsize>(chunk.size()));
                downloaded += read;

                if (total > 0) {
                    const float fraction = static_cast<float>(downloaded) / static_cast<float>(total);
                    SetProgress(progress, 0.05F + (fraction * 0.65F), "Downloading command-line tools...");
                }
            }

            WinHttpCloseHandle(request);
            WinHttpCloseHandle(connect);
            WinHttpCloseHandle(session);
            return std::filesystem::exists(destination) && std::filesystem::file_size(destination) > 0;
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

        bool ExtractZip(const std::filesystem::path &zipPath, const std::filesystem::path &destination) {
            const std::string command = StrConcat(
                "Expand-Archive -LiteralPath ",
                PowerShellQuote(zipPath.string()),
                " -DestinationPath ",
                PowerShellQuote(destination.string()),
                " -Force"
            );
            RunCommandArgs("powershell.exe", {"-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", command});
            return std::filesystem::exists(destination / "cmdline-tools");
        }
#else
        size_t WriteFileCallback(const char *ptr, const size_t size, const size_t nmemb, void *userdata) {
            auto *out = static_cast<std::ofstream *>(userdata);
            out->write(ptr, static_cast<std::streamsize>(size * nmemb));
            return size * nmemb;
        }

        int DownloadProgressCallback(void *clientp, const curl_off_t dltotal, const curl_off_t dlnow, curl_off_t, curl_off_t) {
            auto *progress = static_cast<std::shared_ptr<SdkOperationProgress> *>(clientp);
            if (progress == nullptr || !*progress || dltotal <= 0) {
                return 0;
            }

            const float fraction = static_cast<float>(dlnow) / static_cast<float>(dltotal);
            SetProgress(*progress, 0.05F + (fraction * 0.65F), "Downloading command-line tools...");
            return 0;
        }

        bool DownloadFile(
            const std::string &url,
            const std::filesystem::path &destination,
            const std::shared_ptr<SdkOperationProgress> &progress,
            std::string &error
        ) {
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
            curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &progress);

            const CURLcode rc = curl_easy_perform(curl);
            curl_easy_cleanup(curl);
            out.close();

            if (rc != CURLE_OK) {
                error = "Command-line tools download failed.";
                return false;
            }
            return std::filesystem::exists(destination) && std::filesystem::file_size(destination) > 0;
        }

        bool ExtractZip(const std::filesystem::path &zipPath, const std::filesystem::path &destination) {
            const std::string unzipPath = std::filesystem::exists("/usr/bin/unzip") ? "/usr/bin/unzip" : "unzip";
            RunCommandArgs(unzipPath, {"-q", "-o", zipPath.string(), "-d", destination.string()});
            return std::filesystem::exists(destination / "cmdline-tools");
        }
#endif
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

    SdkInfo BuildSdkInfoFromSdkRoot(const std::string &sdkRoot, const std::string &javaHomePath) {
        SdkInfo sdk;
        sdk.SdkPath = sdkRoot;
        sdk.JavaHomePath = javaHomePath;
        sdk.EmulatorPath = Paths::JoinPaths({sdkRoot, "emulator", "emulator" + Paths::GetExecutableExtension()});

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
        if (sdkRoot.empty()) {
            return Fail(progress, "Choose an Android SDK directory first.");
        }

        std::error_code ec;
        SetProgress(progress, 0.02F, "Preparing SDK directory...");
        std::filesystem::create_directories(sdkRoot, ec);
        if (ec) {
            return Fail(progress, StrConcat("Could not create SDK directory: ", ec.message()));
        }

        if (HasSdkManager(sdkRoot)) {
            return Success(progress);
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
        const std::string url = CommandLineToolsDownloadUrl();
        SetProgress(progress, 0.05F, "Downloading command-line tools...", url);
        if (!DownloadFile(url, zipPath, progress, error)) {
            std::filesystem::remove_all(tempDir, ec);
            return Fail(progress, error.empty() ? "Command-line tools download failed." : error);
        }

        SetProgress(progress, 0.74F, "Extracting command-line tools...");
        if (!ExtractZip(zipPath, extractDir)) {
            std::filesystem::remove_all(tempDir, ec);
            return Fail(progress, "Could not extract command-line tools.");
        }

        SetProgress(progress, 0.88F, "Installing command-line tools...");
        const std::filesystem::path extractedTools = extractDir / "cmdline-tools";
        const std::filesystem::path latestTools = std::filesystem::path(sdkRoot) / "cmdline-tools" / "latest";
        std::filesystem::create_directories(latestTools, ec);
        if (ec) {
            std::filesystem::remove_all(tempDir, ec);
            return Fail(progress, StrConcat("Could not create cmdline-tools/latest: ", ec.message()));
        }

        if (!CopyDirectoryContents(extractedTools, latestTools, error)) {
            std::filesystem::remove_all(tempDir, ec);
            return Fail(progress, StrConcat("Could not install command-line tools: ", error));
        }

        std::filesystem::remove_all(tempDir, ec);
        if (!HasSdkManager(sdkRoot)) {
            return Fail(progress, "Command-line tools were installed, but sdkmanager was not found.");
        }

        return Success(progress);
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

        SetProgress(progress, 0.02F, "Fetching SDK packages from official sources...");
        const SdkPackageListResult list = ListSdkPackages(sdk, false);
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

        SetProgress(
            progress,
            0.05F,
            "Installing base Android SDK packages...",
            JoinPackagePaths(packagesToInstall)
        );
        if (!InstallSdkPackages(sdk, packagesToInstall, progress)) {
            return Fail(progress, "Base Android SDK package installation failed.", "Android SDK setup failed.");
        }

        return Success(progress, "Android SDK setup completed.");
    }
}
