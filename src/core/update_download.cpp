#include "update_download.h"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

#include "paths.h"
#include "sha256.h"

#if defined(_WIN32)
#include <windows.h>
#include <winhttp.h>
#else
#include <curl/curl.h>
#endif

namespace CoreDeck {
    namespace {
        void SetProgressStatus(const std::shared_ptr<UpdateDownloadProgress> &progress, const std::string &status) {
            if (!progress) return;
            std::lock_guard lock(progress->Mutex);
            progress->Status = status;
        }

#if defined(_WIN32)
        std::wstring ToWide(const std::string &value) {
            if (value.empty()) return {};
            const int size = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
            if (size <= 1) return {};
            std::wstring result(static_cast<std::size_t>(size), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, result.data(), size);
            result.resize(static_cast<std::size_t>(size - 1));
            return result;
        }

        bool DownloadFile(const std::string &url, const std::filesystem::path &path,
                          const std::shared_ptr<UpdateDownloadProgress> &progress, std::string &error) {
            const std::wstring wideUrl = ToWide(url);
            URL_COMPONENTS components{};
            components.dwStructSize = sizeof(components);
            components.dwHostNameLength = static_cast<DWORD>(-1);
            components.dwUrlPathLength = static_cast<DWORD>(-1);
            components.dwExtraInfoLength = static_cast<DWORD>(-1);
            if (!WinHttpCrackUrl(wideUrl.c_str(), 0, 0, &components)) {
                error = "Invalid update download URL.";
                return false;
            }
            const std::wstring host(components.lpszHostName, components.dwHostNameLength);
            std::wstring target(components.lpszUrlPath, components.dwUrlPathLength);
            if (components.dwExtraInfoLength > 0) target.append(components.lpszExtraInfo, components.dwExtraInfoLength);
            HINTERNET session = WinHttpOpen(L"CoreDeck Update", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
            HINTERNET connect = session ? WinHttpConnect(session, host.c_str(), components.nPort, 0) : nullptr;
            HINTERNET request = connect ? WinHttpOpenRequest(connect, L"GET", target.c_str(), nullptr,
                WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                components.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0) : nullptr;
            bool ok = request && WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                                    WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                      WinHttpReceiveResponse(request, nullptr);
            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            std::uint64_t downloaded = 0;
            while (ok && file) {
                if (progress && progress->CancelRequested.load()) { ok = false; error = "Cancelled"; break; }
                DWORD available = 0;
                if (!WinHttpQueryDataAvailable(request, &available)) { ok = false; break; }
                if (available == 0) break;
                std::vector<char> buffer(available);
                DWORD read = 0;
                if (!WinHttpReadData(request, buffer.data(), available, &read)) { ok = false; break; }
                file.write(buffer.data(), read);
                downloaded += read;
                if (progress) {
                    std::lock_guard lock(progress->Mutex);
                    progress->DownloadedBytes = downloaded;
                }
            }
            if (!ok && error.empty()) error = "Update download failed.";
            if (request) WinHttpCloseHandle(request);
            if (connect) WinHttpCloseHandle(connect);
            if (session) WinHttpCloseHandle(session);
            return ok && file.good();
        }
#else
        struct CurlDownloadContext {
            std::ofstream *File = nullptr;
            std::shared_ptr<UpdateDownloadProgress> Progress;
        };

        size_t CurlWriteFile(const char *data, const size_t size, const size_t count, void *userData) {
            auto *context = static_cast<CurlDownloadContext *>(userData);
            const size_t bytes = size * count;
            context->File->write(data, static_cast<std::streamsize>(bytes));
            return context->File->good() ? bytes : 0;
        }

        int CurlProgress(void *userData, const curl_off_t total, const curl_off_t now, curl_off_t, curl_off_t) {
            auto *context = static_cast<CurlDownloadContext *>(userData);
            if (context->Progress) {
                std::lock_guard lock(context->Progress->Mutex);
                context->Progress->TotalBytes = total > 0 ? static_cast<std::uint64_t>(total) : 0;
                context->Progress->DownloadedBytes = now > 0 ? static_cast<std::uint64_t>(now) : 0;
            }
            return context->Progress && context->Progress->CancelRequested.load() ? 1 : 0;
        }

        bool DownloadFile(const std::string &url, const std::filesystem::path &path,
                          const std::shared_ptr<UpdateDownloadProgress> &progress, std::string &error) {
            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            CURL *curl = file ? curl_easy_init() : nullptr;
            if (!curl) { error = "Could not start update download."; return false; }
            CurlDownloadContext context{&file, progress};
            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_USERAGENT, "CoreDeck Update");
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteFile);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &context);
            curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
            curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, CurlProgress);
            curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &context);
            const CURLcode result = curl_easy_perform(curl);
            curl_easy_cleanup(curl);
            if (result != CURLE_OK) {
                error = result == CURLE_ABORTED_BY_CALLBACK ? "Cancelled" : curl_easy_strerror(result);
                return false;
            }
            return file.good();
        }
#endif
    }

    namespace detail {
        std::optional<std::string> ParseSha256Checksum(const std::string &text, const std::string &expectedFilename) {
            std::istringstream stream(text);
            std::string hash;
            std::string filename;
            stream >> hash >> filename;
            if (!filename.empty() && filename.front() == '*') filename.erase(filename.begin());
            const bool validHash = hash.size() == 64 && std::all_of(hash.begin(), hash.end(), [](const char c) {
                return std::isxdigit(static_cast<unsigned char>(c)) != 0;
            });
            if (!validHash || std::filesystem::path(filename).filename() != expectedFilename) return std::nullopt;
            for (char &c: hash) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return hash;
        }

        std::string Sha256File(const std::string &path) {
            return CoreDeck::Sha256File(path);
        }
    }

    UpdateDownloadResult DownloadAndVerifyUpdate(
        const ReleaseAsset &package,
        const ReleaseAsset &checksum,
        const std::shared_ptr<UpdateDownloadProgress> &progress
    ) {
        UpdateDownloadResult result;
        const std::string packageName = std::filesystem::path(package.Name).filename().string();
        if (packageName.empty() || packageName != package.Name || checksum.DownloadUrl.empty()) {
            result.Error = "The release does not contain a valid update package and checksum.";
            return result;
        }

        std::error_code ec;
        const std::filesystem::path directory = Paths::GetAppConfigPath("updates");
        std::filesystem::create_directories(directory, ec);
        if (ec) { result.Error = "Could not create the update download directory."; return result; }
        const std::filesystem::path checksumPath = directory / (packageName + ".sha256");
        const std::filesystem::path checksumPartialPath = checksumPath.string() + ".part";
        const std::filesystem::path partialPath = directory / (packageName + ".part");
        const std::filesystem::path packagePath = directory / packageName;

        SetProgressStatus(progress, "Downloading checksum...");
        std::string error;
        if (!DownloadFile(checksum.DownloadUrl, checksumPartialPath, nullptr, error)) {
            result.Error = error;
            std::filesystem::remove(checksumPartialPath, ec);
            return result;
        }
        std::ifstream checksumFile(checksumPartialPath);
        const std::string checksumText((std::istreambuf_iterator<char>(checksumFile)), std::istreambuf_iterator<char>());
        checksumFile.close();
        const auto expectedHash = detail::ParseSha256Checksum(checksumText, packageName);
        if (!expectedHash) {
            std::filesystem::remove(checksumPartialPath, ec);
            result.Error = "The update checksum file is invalid.";
            return result;
        }
        std::filesystem::remove(checksumPath, ec);
        ec.clear();
        std::filesystem::rename(checksumPartialPath, checksumPath, ec);
        if (ec) {
            result.Error = "Could not replace the update checksum file.";
            return result;
        }

        if (std::filesystem::exists(packagePath) && detail::Sha256File(packagePath.string()) == *expectedHash) {
            result.Succeeded = true;
            result.PackagePath = packagePath.string();
            SetProgressStatus(progress, "Update ready to install.");
            return result;
        }

        if (progress) {
            std::lock_guard lock(progress->Mutex);
            progress->DownloadedBytes = 0;
            progress->TotalBytes = package.Size;
        }
        SetProgressStatus(progress, "Downloading update...");
        if (!DownloadFile(package.DownloadUrl, partialPath, progress, error)) {
            result.Cancelled = progress && progress->CancelRequested.load();
            result.Error = result.Cancelled ? "" : error;
            std::filesystem::remove(partialPath, ec);
            return result;
        }

        SetProgressStatus(progress, "Verifying update...");
        if (detail::Sha256File(partialPath.string()) != *expectedHash) {
            std::filesystem::remove(partialPath, ec);
            result.Error = "The downloaded update failed SHA-256 verification.";
            return result;
        }
        std::filesystem::remove(packagePath, ec);
        std::filesystem::rename(partialPath, packagePath, ec);
        if (ec) { result.Error = "Could not finalize the downloaded update."; return result; }
        result.Succeeded = true;
        result.PackagePath = packagePath.string();
        SetProgressStatus(progress, "Update ready to install.");
        return result;
    }
}
