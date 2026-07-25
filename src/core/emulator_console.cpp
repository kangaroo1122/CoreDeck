#include "emulator_console.h"

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <ranges>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
using CoreDeckSocket = SOCKET;
static constexpr CoreDeckSocket COREDECK_INVALID_SOCKET = INVALID_SOCKET;
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
using CoreDeckSocket = int;
static constexpr CoreDeckSocket COREDECK_INVALID_SOCKET = -1;
#endif

namespace CoreDeck::EmulatorConsole {
    namespace {
#if defined(_WIN32)
        struct WsaInit {
            WsaInit() {
                WSADATA d;
                WSAStartup(MAKEWORD(2, 2), &d);
            }

            ~WsaInit() {
                WSACleanup();
            }
        };

        void EnsureWsa() {
            static WsaInit init;
        }

        void CloseSock(const CoreDeckSocket s) {
            closesocket(s);
        }

        void SetNonBlocking(const CoreDeckSocket s) {
            u_long m = 1;
            ioctlsocket(s, FIONBIO, &m);
        }

        int LastErr() {
            return WSAGetLastError();
        }

        bool ErrIsWouldBlock(const int e) {
            return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS;
        }
#else
        void EnsureWsa() {
        }

        void CloseSock(const CoreDeckSocket s) {
            close(s);
        }

        void SetNonBlocking(const CoreDeckSocket s) {
            const int f = fcntl(s, F_GETFL, 0);
            fcntl(s, F_SETFL, f | O_NONBLOCK);
        }

        int LastErr() {
            return errno;
        }

        bool ErrIsWouldBlock(const int e) {
            return e == EWOULDBLOCK || e == EAGAIN || e == EINPROGRESS;
        }
#endif

        std::string ReadAuthToken() {
            const char *home =
#ifdef _WIN32
                std::getenv("USERPROFILE");
#else
                std::getenv("HOME"); // NOLINT(concurrency-mt-unsafe)
#endif
            if (!home) {
                return "";
            }
            const std::filesystem::path p = std::filesystem::path(home) / ".emulator_console_auth_token";
            std::ifstream f(p);
            if (!f) {
                return "";
            }
            std::stringstream ss;
            ss << f.rdbuf();
            std::string token = ss.str();
            while (!token.empty() && (token.back() == '\n' || token.back() == '\r' || token.back() == ' ' || token.back() == '\t')) {
                token.pop_back();
            }
            return token;
        }

        bool WaitWritable(const CoreDeckSocket s, const int timeoutMs) {
            fd_set wf;
            FD_ZERO(&wf);
            FD_SET(s, &wf);
            timeval tv{.tv_sec = timeoutMs / 1000, .tv_usec = (timeoutMs % 1000) * 1000};
            return select(static_cast<int>(s) + 1, nullptr, &wf, nullptr, &tv) > 0;
        }

        bool WaitReadable(const CoreDeckSocket s, const int timeoutMs) {
            fd_set rf;
            FD_ZERO(&rf);
            FD_SET(s, &rf);
            timeval tv{.tv_sec = timeoutMs / 1000, .tv_usec = (timeoutMs % 1000) * 1000};
            return select(static_cast<int>(s) + 1, &rf, nullptr, nullptr, &tv) > 0;
        }

        bool ConnectLocalhost(const CoreDeckSocket s, const int port, const int timeoutMs) {
            SetNonBlocking(s);

            sockaddr_in addr = {};
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            addr.sin_port = htons(static_cast<u_short>(port));

            const int rc = connect(s, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
            if (rc != 0 && !ErrIsWouldBlock(LastErr())) {
                return false;
            }
            if (rc != 0 && !WaitWritable(s, timeoutMs)) {
                return false;
            }

            int error = 0;
#if defined(_WIN32)
            int len = sizeof(error);
#else
            socklen_t len = sizeof(error);
#endif
            if (getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>(&error), &len) != 0) {
                return false;
            }
            return error == 0;
        }

        bool SendAll(const CoreDeckSocket s, const std::string &payload, const int timeoutMs) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
            size_t sent = 0;
            while (sent < payload.size()) {
#if defined(_WIN32)
                const int n = send(s, payload.data() + sent, static_cast<int>(payload.size() - sent), 0);
#else
                const ssize_t n = send(s, payload.data() + sent, payload.size() - sent, 0);
#endif
                if (n > 0) {
                    sent += static_cast<size_t>(n);
                    continue;
                }
                if (n < 0 && !ErrIsWouldBlock(LastErr())) {
                    return false;
                }
                if (std::chrono::steady_clock::now() >= deadline) {
                    return false;
                }
                WaitWritable(s, 100);
            }
            return true;
        }

        std::string ReceiveResponse(const CoreDeckSocket s, const int timeoutMs) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
            std::string response;
            std::array<char, 512> buffer{};

            while (std::chrono::steady_clock::now() < deadline) {
                if (!WaitReadable(s, 50)) {
                    if (!response.empty()) {
                        break;
                    }
                    continue;
                }

#if defined(_WIN32)
                const int n = recv(s, buffer.data(), static_cast<int>(buffer.size()), 0);
#else
                const ssize_t n = recv(s, buffer.data(), buffer.size(), 0);
#endif
                if (n > 0) {
                    response.append(buffer.data(), static_cast<std::size_t>(n));
                    if (response.find("\nOK") != std::string::npos || response.find("\nKO") != std::string::npos) {
                        break;
                    }
                    continue;
                }
                if (n == 0) {
                    break;
                }
                if (!ErrIsWouldBlock(LastErr())) {
                    break;
                }
            }

            return response;
        }

        std::string Trim(std::string value) {
            while (!value.empty() && (value.back() == '\r' || value.back() == '\n' || value.back() == ' ' || value.back() == '\t')) {
                value.pop_back();
            }
            while (!value.empty() && (value.front() == '\r' || value.front() == '\n' || value.front() == ' ' || value.front() == '\t')) {
                value.erase(value.begin());
            }
            return value;
        }

        std::string FirstPayloadLine(const std::string &response) {
            std::stringstream stream(response);
            std::string line;
            while (std::getline(stream, line)) {
                line = Trim(line);
                if (line.empty() || line == "OK" || line.starts_with("KO")) {
                    continue;
                }
                if (line.starts_with("Android Console:")) {
                    continue;
                }
                return line;
            }
            return "";
        }

        bool IsPortBindable(const int port) {
            const CoreDeckSocket s = socket(AF_INET, SOCK_STREAM, 0);
            if (s == COREDECK_INVALID_SOCKET) {
                return false;
            }

            sockaddr_in addr = {};
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            addr.sin_port = htons(static_cast<u_short>(port));
            const int rc = bind(s, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
            CloseSock(s);
            return rc == 0;
        }

        bool IsPortPairBindable(const int consolePort) {
            return IsPortBindable(consolePort) && IsPortBindable(consolePort + 1);
        }
    }

    int FindFreePort(const int startPort, const int endPort, const std::vector<int> &reservedConsolePorts) {
        EnsureWsa();
        for (int port = startPort; port <= endPort; port += 2) {
            if (std::ranges::find(reservedConsolePorts, port) != reservedConsolePorts.end()) {
                continue;
            }
            if (IsPortPairBindable(port)) {
                return port;
            }
        }
        return -1;
    }

    bool IsAvailable(const int port, const int timeoutMs) {
        EnsureWsa();
        const CoreDeckSocket s = socket(AF_INET, SOCK_STREAM, 0);
        if (s == COREDECK_INVALID_SOCKET) {
            return false;
        }
        const bool connected = ConnectLocalhost(s, port, timeoutMs);
        CloseSock(s);
        return connected;
    }

    std::string QueryAvdName(const int port, const int timeoutMs) {
        EnsureWsa();
        const CoreDeckSocket s = socket(AF_INET, SOCK_STREAM, 0);
        if (s == COREDECK_INVALID_SOCKET) {
            return "";
        }

        if (!ConnectLocalhost(s, port, timeoutMs)) {
            CloseSock(s);
            return "";
        }

        [[maybe_unused]] const std::string greeting = ReceiveResponse(s, 100);
        const std::string token = ReadAuthToken();
        if (!token.empty()) {
            if (!SendAll(s, "auth " + token + "\r\n", timeoutMs)) {
                CloseSock(s);
                return "";
            }
            const std::string authResponse = ReceiveResponse(s, timeoutMs);
            if (authResponse.find("KO") != std::string::npos) {
                CloseSock(s);
                return "";
            }
        }

        if (!SendAll(s, "avd name\r\n", timeoutMs)) {
            CloseSock(s);
            return "";
        }

        const std::string response = ReceiveResponse(s, timeoutMs);
        CloseSock(s);
        return FirstPayloadLine(response);
    }

    int FindAvdConsolePort(const std::string &avdName, const int startPort, const int endPort) {
        if (avdName.empty()) {
            return -1;
        }

        for (int port = startPort; port <= endPort; port += 2) {
            if (QueryAvdName(port) == avdName) {
                return port;
            }
        }
        return -1;
    }

    bool SendKill(const int port, const int timeoutMs) {
        EnsureWsa();
        const CoreDeckSocket s = socket(AF_INET, SOCK_STREAM, 0);
        if (s == COREDECK_INVALID_SOCKET) {
            return false;
        }

        if (!ConnectLocalhost(s, port, timeoutMs)) {
            CloseSock(s);
            return false;
        }

        std::string payload;
        if (const std::string token = ReadAuthToken(); !token.empty()) {
            payload = "auth " + token + "\r\n";
        }
        payload += "kill\r\n";

        const bool sent = SendAll(s, payload, timeoutMs);
#if defined(_WIN32)
        shutdown(s, SD_SEND);
#else
        shutdown(s, SHUT_WR);
#endif
        CloseSock(s);
        return sent;
    }
}
