//
// Created by AbdulMuaz Aqeel on 18/04/2026.
//

#include <algorithm>
#include <cctype>
#include <rfl/json.hpp>

#include "version_check.h"
#include "utilities.h"

#if defined(_WIN32)
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#else
#include <curl/curl.h>
#endif

namespace CoreDeck {
    namespace {
        struct GitHubReleaseAsset {
            std::string name; // NOLINT(readability-identifier-naming)
            std::string browser_download_url; // NOLINT(readability-identifier-naming)
            std::uint64_t size = 0;
        };

        struct GitHubLatestRelease {
            std::string tag_name; // NOLINT(readability-identifier-naming)
            std::optional<std::string> body; // NOLINT(readability-identifier-naming)
            std::optional<bool> prerelease; // NOLINT(readability-identifier-naming)
            std::optional<bool> draft; // NOLINT(readability-identifier-naming)
            std::vector<GitHubReleaseAsset> assets; // NOLINT(readability-identifier-naming)
        };

        void TrimInPlace(std::string &s) {
            while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
                s.erase(s.begin());
            }
            while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
                s.pop_back();
            }
        }

        std::string ExtractWhatsNewSection(const std::string &body) {
            size_t lastSeparatorStart = std::string::npos;
            size_t scan = 0;
            while (scan < body.size()) {
                const size_t lineStart = scan;
                const size_t newline = body.find('\n', scan);
                const size_t lineEnd = newline == std::string::npos ? body.size() : newline;
                std::string line = body.substr(lineStart, lineEnd - lineStart);
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                if (line == "---") {
                    lastSeparatorStart = lineStart;
                }
                if (newline == std::string::npos) {
                    break;
                }
                scan = newline + 1;
            }

            if (lastSeparatorStart == std::string::npos) {
                return body;
            }
            return body.substr(0, lastSeparatorStart);
        }

        RemoteRelease BuildRemoteRelease(const GitHubLatestRelease &value) {
            RemoteRelease release;
            release.Version = value.tag_name;
            release.Notes = ExtractWhatsNewSection(value.body.value_or(""));
            release.IsPrerelease = value.prerelease.value_or(false);
            for (const auto &asset: value.assets) {
                release.Assets.push_back({asset.name, asset.browser_download_url, asset.size});
            }
            TrimInPlace(release.Notes);
            return release;
        }
    }


    namespace detail {
        std::optional<std::string> ParseLatestReleaseTag(const std::string &body) {
            try {
                const auto parsed = rfl::json::read<GitHubLatestRelease, rfl::DefaultIfMissing>(body);
                if (!parsed) {
                    return std::nullopt;
                }
                const std::string &tag = parsed.value().tag_name;
                if (tag.empty()) {
                    return std::nullopt;
                }
                return tag;
            } catch (...) {
                return std::nullopt;
            }
        }


        std::optional<RemoteRelease> ParseLatestRelease(const std::string &body) {
            try {
                const auto parsed = rfl::json::read<GitHubLatestRelease, rfl::DefaultIfMissing>(body);
                if (!parsed) {
                    return std::nullopt;
                }
                const auto &value = parsed.value();
                if (value.tag_name.empty()) {
                    return std::nullopt;
                }
                return BuildRemoteRelease(value);
            } catch (...) {
                return std::nullopt;
            }
        }

        int CompareSemanticVersion(const std::string &newVersion, const std::string &currentVersion) {
            struct ParsedVersion {
                std::vector<int> Core;
                std::vector<std::string> PreRelease;
            };

            auto parse = [](const std::string &raw) -> ParsedVersion {
                std::string s = raw;
                if (!s.empty() && (s[0] == 'v' || s[0] == 'V')) {
                    s.erase(s.begin());
                }
                const size_t dash = s.find('-');
                const std::string core = dash == std::string::npos ? s : s.substr(0, dash);
                ParsedVersion result;
                size_t pos = 0;
                while (pos < core.size()) {
                    const size_t dot = core.find('.', pos);
                    const std::string seg = dot == std::string::npos ? core.substr(pos) : core.substr(pos, dot - pos);
                    int n = 0;
                    for (const char c: seg) {
                        if (c < '0' || c > '9') {
                            break;
                        }
                        n = (n * 10) + (c - '0');
                    }
                    result.Core.push_back(n);
                    if (dot == std::string::npos) {
                        break;
                    }
                    pos = dot + 1;
                }
                while (result.Core.size() < 3) {
                    result.Core.push_back(0);
                }
                if (dash != std::string::npos) {
                    const std::string pre = s.substr(dash + 1);
                    pos = 0;
                    while (pos <= pre.size()) {
                        const size_t dot = pre.find('.', pos);
                        result.PreRelease.push_back(dot == std::string::npos ? pre.substr(pos) : pre.substr(pos, dot - pos));
                        if (dot == std::string::npos) {
                            break;
                        }
                        pos = dot + 1;
                    }
                }
                return result;
            };

            const ParsedVersion va = parse(newVersion);
            const ParsedVersion vb = parse(currentVersion);
            const size_t n = std::max(va.Core.size(), vb.Core.size());
            for (size_t i = 0; i < n; ++i) {
                const int a = i < va.Core.size() ? va.Core[i] : 0;
                const int b = i < vb.Core.size() ? vb.Core[i] : 0;
                if (a != b) {
                    return a < b ? -1 : 1;
                }
            }
            if (va.PreRelease.empty() && !vb.PreRelease.empty()) {
                return 1;
            }
            if (!va.PreRelease.empty() && vb.PreRelease.empty()) {
                return -1;
            }
            for (size_t i = 0; i < std::max(va.PreRelease.size(), vb.PreRelease.size()); ++i) {
                if (i >= va.PreRelease.size()) return -1;
                if (i >= vb.PreRelease.size()) return 1;
                const std::string &a = va.PreRelease[i];
                const std::string &b = vb.PreRelease[i];
                const bool aNumeric = !a.empty() && std::all_of(a.begin(), a.end(), [](const char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; });
                const bool bNumeric = !b.empty() && std::all_of(b.begin(), b.end(), [](const char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; });
                if (aNumeric && bNumeric) {
                    const long long an = std::stoll(a);
                    const long long bn = std::stoll(b);
                    if (an != bn) return an < bn ? -1 : 1;
                } else if (aNumeric != bNumeric) {
                    return aNumeric ? -1 : 1;
                } else if (a != b) {
                    return a < b ? -1 : 1;
                }
            }
            return 0;
        }

        std::optional<ReleaseAsset> SelectReleaseAsset(
            const RemoteRelease &release,
            const std::string &platform,
            const std::string &architecture
        ) {
            std::string expected;
            if (platform == "windows") expected = "coredeck-windows-" + architecture + ".msi";
            else if (platform == "macos") expected = "coredeck-darwin-" + architecture + "-unsigned.dmg";
            else if (platform == "linux") expected = "coredeck-linux-" + architecture + ".tar.gz";
            if (expected.empty()) return std::nullopt;
            for (const auto &asset: release.Assets) {
                if (asset.Name == expected) return asset;
            }
            return std::nullopt;
        }

        std::string CurrentPlatform() {
#if defined(_WIN32)
            return "windows";
#elif defined(__APPLE__)
            return "macos";
#elif defined(__linux__)
            return "linux";
#else
            return "unknown";
#endif
        }

        std::string CurrentArchitecture() {
#if defined(_M_ARM64) || defined(__aarch64__)
            return "arm64";
#else
            return "x86-64";
#endif
        }

        std::optional<RemoteRelease> SelectNewestRelease(
            const std::string &body,
            const bool includeBetaUpdates,
            const std::string &currentVersion
        ) {
            try {
                const auto parsed = rfl::json::read<std::vector<GitHubLatestRelease>, rfl::DefaultIfMissing>(body);
                if (!parsed) return std::nullopt;
                std::optional<RemoteRelease> selected;
                for (const auto &value: parsed.value()) {
                    if (value.tag_name.empty() || value.draft.value_or(false)) continue;
                    if (!includeBetaUpdates && value.prerelease.value_or(false)) continue;
                    RemoteRelease release = BuildRemoteRelease(value);
                    if (CompareSemanticVersion(release.Version, currentVersion) <= 0) continue;
                    if (!selected || CompareSemanticVersion(release.Version, selected->Version) > 0) {
                        selected = std::move(release);
                    }
                }
                return selected;
            } catch (...) {
                return std::nullopt;
            }
        }
    }

    namespace {
#if defined(_WIN32)
        std::optional<std::string> HttpGet(const wchar_t *host, const wchar_t *path, const std::wstring &userAgent) {
            HINTERNET session = WinHttpOpen(
                userAgent.c_str(),
                WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                WINHTTP_NO_PROXY_NAME,
                WINHTTP_NO_PROXY_BYPASS,
                0
            );
            if (!session) return std::nullopt;

            WinHttpSetTimeouts(session, 15000, 15000, 15000, 15000);

            HINTERNET connect = WinHttpConnect(session, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
            if (!connect) {
                WinHttpCloseHandle(session);
                return std::nullopt;
            }

            HINTERNET request = WinHttpOpenRequest(
                connect,
                L"GET",
                path,
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

            const wchar_t *headers = L"Accept: application/vnd.github+json\r\n";
            BOOL sent = WinHttpSendRequest(request, headers, static_cast<DWORD>(-1L), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(request, nullptr);

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
                        if (!WinHttpReadData(request, chunk.data(), available, &read)) break;
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
#else
        size_t CurlWriteCallback(const char *ptr, const size_t size, size_t nmemb, void *userdata) {
            auto *buf = static_cast<std::string *>(userdata);
            buf->append(ptr, size * nmemb);
            return size * nmemb;
        }

        std::optional<std::string> HttpGet(const std::string &url, const std::string &userAgent) {
            CURL *curl = curl_easy_init();
            if (!curl) {
                return std::nullopt;
            }

            std::string body;
            curl_slist *headers = curl_slist_append(nullptr, "Accept: application/vnd.github+json");

            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
            curl_easy_setopt(curl, CURLOPT_USERAGENT, userAgent.c_str());
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
            curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
            curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);

            const CURLcode rc = curl_easy_perform(curl);
            curl_slist_free_all(headers);
            curl_easy_cleanup(curl);

            if (rc != CURLE_OK) {
                return std::nullopt;
            }
            return body;
        }
#endif
    }

    std::optional<RemoteRelease> QueryRemoteNewerVersion(const bool includeBetaUpdates) {
#if defined(_WIN32)
        const std::string ua = StrConcat("CoreDeck/", COREDECK_VERSION);
        std::wstring userAgent(ua.begin(), ua.end());
        auto fetched = HttpGet(L"api.github.com", L"/repos/kangaroo1122/CoreDeck/releases?per_page=30", userAgent);
#else
        const std::string userAgent = StrConcat("CoreDeck/", COREDECK_VERSION);
        auto fetched = HttpGet("https://api.github.com/repos/kangaroo1122/CoreDeck/releases?per_page=30", userAgent);
#endif
        if (!fetched) {
            return std::nullopt;
        }
        std::string body = std::move(fetched.value());
        TrimInPlace(body);
        if (body.empty()) {
            return std::nullopt;
        }

        auto remote = detail::SelectNewestRelease(body, includeBetaUpdates, COREDECK_VERSION);
        if (!remote) {
            return std::nullopt;
        }
        remote->Package = detail::SelectReleaseAsset(*remote, detail::CurrentPlatform(), detail::CurrentArchitecture());
        if (remote->Package) {
            const std::string checksumName = remote->Package->Name + ".sha256";
            for (const auto &asset: remote->Assets) {
                if (asset.Name == checksumName) {
                    remote->Checksum = asset;
                    break;
                }
            }
        }
        return remote;
    }
}
