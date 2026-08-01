#include "update_download.h"

#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>

#include "paths.h"

#if defined(_WIN32)
#include <windows.h>
#include <winhttp.h>
#else
#include <curl/curl.h>
#endif

namespace CoreDeck {
    namespace {
        constexpr std::array<std::uint32_t, 64> SHA256_K = {
            0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
            0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
            0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
            0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
            0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
            0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
            0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
            0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
        };

        std::uint32_t RotateRight(const std::uint32_t value, const unsigned count) {
            return (value >> count) | (value << (32U - count));
        }

        class Sha256 {
        public:
            void Update(const std::uint8_t *data, const std::size_t size) {
                for (std::size_t i = 0; i < size; ++i) {
                    m_Buffer[m_BufferSize++] = data[i];
                    if (m_BufferSize == m_Buffer.size()) {
                        Transform(m_Buffer.data());
                        m_BitLength += 512;
                        m_BufferSize = 0;
                    }
                }
            }

            std::array<std::uint8_t, 32> Final() {
                const std::uint64_t totalBits = m_BitLength + static_cast<std::uint64_t>(m_BufferSize) * 8U;
                m_Buffer[m_BufferSize++] = 0x80U;
                if (m_BufferSize > 56) {
                    while (m_BufferSize < 64) m_Buffer[m_BufferSize++] = 0;
                    Transform(m_Buffer.data());
                    m_BufferSize = 0;
                }
                while (m_BufferSize < 56) m_Buffer[m_BufferSize++] = 0;
                for (int i = 7; i >= 0; --i) {
                    m_Buffer[m_BufferSize++] = static_cast<std::uint8_t>((totalBits >> (i * 8)) & 0xffU);
                }
                Transform(m_Buffer.data());

                std::array<std::uint8_t, 32> digest{};
                for (std::size_t i = 0; i < m_State.size(); ++i) {
                    digest[i * 4] = static_cast<std::uint8_t>((m_State[i] >> 24) & 0xffU);
                    digest[i * 4 + 1] = static_cast<std::uint8_t>((m_State[i] >> 16) & 0xffU);
                    digest[i * 4 + 2] = static_cast<std::uint8_t>((m_State[i] >> 8) & 0xffU);
                    digest[i * 4 + 3] = static_cast<std::uint8_t>(m_State[i] & 0xffU);
                }
                return digest;
            }

        private:
            void Transform(const std::uint8_t *block) {
                std::array<std::uint32_t, 64> words{};
                for (std::size_t i = 0; i < 16; ++i) {
                    words[i] = (static_cast<std::uint32_t>(block[i * 4]) << 24) |
                               (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16) |
                               (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8) |
                               static_cast<std::uint32_t>(block[i * 4 + 3]);
                }
                for (std::size_t i = 16; i < words.size(); ++i) {
                    const std::uint32_t s0 = RotateRight(words[i - 15], 7) ^ RotateRight(words[i - 15], 18) ^ (words[i - 15] >> 3);
                    const std::uint32_t s1 = RotateRight(words[i - 2], 17) ^ RotateRight(words[i - 2], 19) ^ (words[i - 2] >> 10);
                    words[i] = words[i - 16] + s0 + words[i - 7] + s1;
                }

                auto [a, b, c, d, e, f, g, h] = m_State;
                for (std::size_t i = 0; i < words.size(); ++i) {
                    const std::uint32_t s1 = RotateRight(e, 6) ^ RotateRight(e, 11) ^ RotateRight(e, 25);
                    const std::uint32_t choice = (e & f) ^ (~e & g);
                    const std::uint32_t temp1 = h + s1 + choice + SHA256_K[i] + words[i];
                    const std::uint32_t s0 = RotateRight(a, 2) ^ RotateRight(a, 13) ^ RotateRight(a, 22);
                    const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
                    const std::uint32_t temp2 = s0 + majority;
                    h = g; g = f; f = e; e = d + temp1;
                    d = c; c = b; b = a; a = temp1 + temp2;
                }
                m_State[0] += a; m_State[1] += b; m_State[2] += c; m_State[3] += d;
                m_State[4] += e; m_State[5] += f; m_State[6] += g; m_State[7] += h;
            }

            std::array<std::uint32_t, 8> m_State = {
                0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
                0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U,
            };
            std::array<std::uint8_t, 64> m_Buffer{};
            std::size_t m_BufferSize = 0;
            std::uint64_t m_BitLength = 0;
        };

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
            std::ifstream file(path, std::ios::binary);
            if (!file) return {};
            Sha256 sha;
            std::array<std::uint8_t, 64 * 1024> buffer{};
            while (file) {
                file.read(reinterpret_cast<char *>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
                const auto count = file.gcount();
                if (count > 0) sha.Update(buffer.data(), static_cast<std::size_t>(count));
            }
            const auto digest = sha.Final();
            std::ostringstream result;
            result << std::hex << std::setfill('0');
            for (const std::uint8_t byte: digest) result << std::setw(2) << static_cast<unsigned>(byte);
            return result.str();
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
