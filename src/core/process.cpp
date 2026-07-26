//
// Created by AbdulMuaz Aqeel on 02/04/2026.
//

#include "process.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <string>
#include <thread>

#if defined(_WIN32)
#include <windows.h>
#include <tlhelp32.h>
#include <io.h>
#include <fcntl.h>
#include <process.h>
#else
#include <csignal>
#include <unistd.h>
#include <sys/fcntl.h>
#include <sys/wait.h>
#endif

#if defined(_WIN32)
namespace CoreDeck {
    namespace {
        bool EnvKeyEquals(const std::string &a, const std::string &b) {
            return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](const char lhs, const char rhs) {
                       return std::toupper(static_cast<unsigned char>(lhs)) == std::toupper(static_cast<unsigned char>(rhs));
                   });
        }
    }

    static std::string QuoteArg(const std::string &arg) {
        if (!arg.empty() && arg.find_first_of(" \t\"") == std::string::npos) return arg;
        std::string out = "\"";
        for (size_t i = 0; i < arg.size(); ++i) {
            size_t backslashes = 0;
            while (i < arg.size() && arg[i] == '\\') {
                ++backslashes;
                ++i;
            }
            if (i == arg.size()) {
                out.append(backslashes * 2, '\\');
                break;
            }
            if (arg[i] == '"') {
                out.append(backslashes * 2 + 1, '\\');
                out.push_back('"');
            } else {
                out.append(backslashes, '\\');
                out.push_back(arg[i]);
            }
        }
        out.push_back('"');
        return out;
    }

    static bool IsBatchFile(const std::string &path) {
        if (path.size() < 4) return false;
        std::string ext = path.substr(path.size() - 4);
        std::ranges::transform(ext, ext.begin(), ::tolower);
        return ext == ".bat" || ext == ".cmd";
    }

    static std::string BuildCommandLine(const std::string &path, const std::vector<std::string> &args) {
        std::string cmd = QuoteArg(path);
        for (const auto &arg: args) {
            cmd.push_back(' ');
            cmd += QuoteArg(arg);
        }
        if (IsBatchFile(path)) return "cmd.exe /S /C \"" + cmd + "\"";
        return cmd;
    }

    static bool EnvironmentContainsKey(const ProcessEnvironment &environment, const std::string &key) {
        return std::ranges::any_of(environment, [&](const auto &entry) {
            return EnvKeyEquals(entry.first, key);
        });
    }

    static std::vector<char> BuildEnvironmentBlock(const ProcessEnvironment &environment) {
        std::vector<char> block;
        if (environment.empty()) {
            return block;
        }

        LPCH rawEnv = GetEnvironmentStringsA();
        if (rawEnv != nullptr) {
            for (LPCH current = rawEnv; *current != '\0'; current += std::strlen(current) + 1) {
                const std::string entry = current;
                if (!entry.empty() && entry[0] == '=') {
                    block.insert(block.end(), entry.begin(), entry.end());
                    block.push_back('\0');
                    continue;
                }

                const auto equals = entry.find('=');
                const std::string key = equals == std::string::npos ? entry : entry.substr(0, equals);
                if (!EnvironmentContainsKey(environment, key)) {
                    block.insert(block.end(), entry.begin(), entry.end());
                    block.push_back('\0');
                }
            }
            FreeEnvironmentStringsA(rawEnv);
        }

        for (const auto &[key, value]: environment) {
            if (key.empty()) {
                continue;
            }
            const std::string entry = key + "=" + value;
            block.insert(block.end(), entry.begin(), entry.end());
            block.push_back('\0');
        }
        block.push_back('\0');
        return block;
    }
}
#endif

namespace CoreDeck {
    namespace {
        void EmitCompleteLines(
            std::string &partial,
            const std::function<void(const std::string &)> &onLine
        ) {
            std::size_t pos = 0;
            while ((pos = partial.find_first_of("\n\r")) != std::string::npos) {
                if (auto line = partial.substr(0, pos); !line.empty() && onLine) {
                    onLine(line);
                }
                auto next = partial.find_first_not_of("\n\r", pos);
                partial = (next == std::string::npos) ? std::string() : partial.substr(next);
            }
        }

        bool ShouldCancel(const std::function<bool()> &shouldCancel) {
            return shouldCancel && shouldCancel();
        }
    }

    bool StreamCommandArgsWithEnvCancelable(
        const std::string &path,
        const std::vector<std::string> &args,
        const std::string &stdinData,
        const ProcessEnvironment &environment,
        const std::function<void(const std::string &)> &onLine,
        const std::function<bool()> &shouldCancel
    ) {
#if defined(_WIN32)
        SECURITY_ATTRIBUTES sa = {};
        sa.nLength = sizeof(sa);
        sa.bInheritHandle = TRUE;

        HANDLE hOutR = nullptr, hOutW = nullptr;
        HANDLE hInR = nullptr, hInW = nullptr;
        if (!CreatePipe(&hOutR, &hOutW, &sa, 0)) return false;
        SetHandleInformation(hOutR, HANDLE_FLAG_INHERIT, 0);
        if (!CreatePipe(&hInR, &hInW, &sa, 0)) {
            CloseHandle(hOutR);
            CloseHandle(hOutW);
            return false;
        }
        SetHandleInformation(hInW, HANDLE_FLAG_INHERIT, 0);

        std::string cmdLine = BuildCommandLine(path, args);
        std::vector<char> environmentBlock = BuildEnvironmentBlock(environment);

        STARTUPINFOA si = {};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = hInR;
        si.hStdOutput = hOutW;
        si.hStdError = hOutW;

        PROCESS_INFORMATION pi = {};
        if (!CreateProcessA(
                nullptr,
                const_cast<char *>(cmdLine.c_str()),
                nullptr,
                nullptr,
                TRUE,
                CREATE_NO_WINDOW,
                environmentBlock.empty() ? nullptr : environmentBlock.data(),
                nullptr,
                &si,
                &pi
            )) {
            CloseHandle(hOutR);
            CloseHandle(hOutW);
            CloseHandle(hInR);
            CloseHandle(hInW);
            return false;
        }

        CloseHandle(hOutW);
        CloseHandle(hInR);

        if (!stdinData.empty()) {
            DWORD written = 0;
            WriteFile(hInW, stdinData.data(), static_cast<DWORD>(stdinData.size()), &written, nullptr);
        }
        CloseHandle(hInW);

        std::string partial;
        std::array<char, 512> buf{};
        bool cancelled = false;

        auto drainOutput = [&] {
            DWORD available = 0;
            while (PeekNamedPipe(hOutR, nullptr, 0, nullptr, &available, nullptr) && available > 0) {
                DWORD read = 0;
                const DWORD toRead = std::min<DWORD>(static_cast<DWORD>(buf.size()), available);
                if (!ReadFile(hOutR, buf.data(), toRead, &read, nullptr) || read == 0) {
                    break;
                }
                partial.append(buf.data(), read);
                EmitCompleteLines(partial, onLine);
            }
        };

        while (true) {
            drainOutput();
            if (ShouldCancel(shouldCancel)) {
                cancelled = true;
                TerminateProcessTree(pi.dwProcessId);
                break;
            }
            if (WaitForSingleObject(pi.hProcess, 50) == WAIT_OBJECT_0) {
                drainOutput();
                break;
            }
        }

        if (!partial.empty() && onLine) onLine(partial);

        if (cancelled) {
            WaitForSingleObject(pi.hProcess, 2000);
        }
        DWORD exitCode = 1;
        if (!cancelled && GetExitCodeProcess(pi.hProcess, &exitCode) == 0) {
            exitCode = 1;
        }
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        CloseHandle(hOutR);
        return !cancelled && exitCode == 0;
#else
        int outPipe[2];
        int inPipe[2];
        if (pipe(outPipe) == -1) {
            return false;
        }
        if (pipe(inPipe) == -1) {
            close(outPipe[0]);
            close(outPipe[1]);
            return false;
        }

        const pid_t pid = fork();
        if (pid < 0) {
            close(outPipe[0]);
            close(outPipe[1]);
            close(inPipe[0]);
            close(inPipe[1]);
            return false;
        }

        if (pid == 0) {
            setpgid(0, 0);
            close(outPipe[0]);
            close(inPipe[1]);
            dup2(outPipe[1], STDOUT_FILENO);
            dup2(outPipe[1], STDERR_FILENO);
            dup2(inPipe[0], STDIN_FILENO);
            close(outPipe[1]);
            close(inPipe[0]);

            for (const auto &[key, value]: environment) {
                if (!key.empty()) {
                    setenv(key.c_str(), value.c_str(), 1);
                }
            }

            std::vector<const char *> argv;
            argv.push_back(path.c_str());
            for (const auto &a: args) {
                argv.push_back(a.c_str());
            }
            argv.push_back(nullptr);
            execvp(path.c_str(), const_cast<char *const *>(argv.data()));
            _exit(127);
        }

        close(outPipe[1]);
        close(inPipe[0]);
        const int flags = fcntl(outPipe[0], F_GETFL, 0);
        if (flags != -1) {
            fcntl(outPipe[0], F_SETFL, flags | O_NONBLOCK);
        }

        if (!stdinData.empty()) {
            [[maybe_unused]] ssize_t w = write(inPipe[1], stdinData.data(), stdinData.size());
        }
        close(inPipe[1]);

        std::string partial;
        std::array<char, 512> buf{};
        bool cancelled = false;
        bool exited = false;
        bool waitOk = false;
        int exitStatus = 0;

        auto drainOutput = [&] {
            ssize_t r = 0;
            while ((r = read(outPipe[0], buf.data(), buf.size())) > 0) {
                partial.append(buf.data(), r);
                EmitCompleteLines(partial, onLine);
            }
        };

        while (!exited) {
            drainOutput();
            if (ShouldCancel(shouldCancel)) {
                cancelled = true;
                TerminateProcessTree(pid);
                break;
            }

            int status = 0;
            const pid_t waitResult = waitpid(pid, &status, WNOHANG);
            if (waitResult == pid) {
                exitStatus = status;
                waitOk = true;
                exited = true;
            } else if (waitResult == -1) {
                exited = true;
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        }

        drainOutput();
        if (!partial.empty() && onLine) {
            onLine(partial);
        }

        close(outPipe[0]);
        if (!cancelled && !waitOk) {
            waitOk = waitpid(pid, &exitStatus, 0) == pid;
        }
        return !cancelled && waitOk && WIFEXITED(exitStatus) && WEXITSTATUS(exitStatus) == 0;
#endif
    }

    void StreamCommandArgsWithEnv(
        const std::string &path,
        const std::vector<std::string> &args,
        const std::string &stdinData,
        const ProcessEnvironment &environment,
        const std::function<void(const std::string &)> &onLine
    ) {
        (void) StreamCommandArgsWithEnvCancelable(
            path,
            args,
            stdinData,
            environment,
            onLine,
            {}
        );
    }

    void StreamCommandArgs(
        const std::string &path,
        const std::vector<std::string> &args,
        const std::string &stdinData,
        const std::function<void(const std::string &)> &onLine
    ) {
        StreamCommandArgsWithEnv(path, args, stdinData, {}, onLine);
    }

    std::string RunCommandArgsWithEnv(
        const std::string &path,
        const std::vector<std::string> &args,
        const std::string &stdinData,
        const ProcessEnvironment &environment
    ) {
        std::string out;
        StreamCommandArgsWithEnv(path, args, stdinData, environment, [&out](const std::string &line) {
            out += line;
            out.push_back('\n');
        });
        return out;
    }

    std::string RunCommandArgs(const std::string &path, const std::vector<std::string> &args, const std::string &stdinData) {
        return RunCommandArgsWithEnv(path, args, stdinData, {});
    }

    ProcessId SpawnProcessWithPipe(const std::string &path, const std::vector<std::string> &args, int &outputFd) {
#if defined(_WIN32)
        HANDLE hReadPipe, hWritePipe;
        SECURITY_ATTRIBUTES sa = {};
        sa.nLength = sizeof(sa);
        sa.bInheritHandle = TRUE;

        if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) {
            return 0;
        }

        if (!SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0)) {
            CloseHandle(hReadPipe);
            CloseHandle(hWritePipe);
            return 0;
        }

        std::string cmdLine = BuildCommandLine(path, args);

        STARTUPINFOA si = {};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdOutput = hWritePipe;
        si.hStdError = hWritePipe;

        PROCESS_INFORMATION pi = {};

        if (!CreateProcessA(nullptr, const_cast<char *>(cmdLine.c_str()), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            CloseHandle(hReadPipe);
            CloseHandle(hWritePipe);
            return 0;
        }

        CloseHandle(pi.hThread);
        CloseHandle(hWritePipe);

        DWORD pipeMode = PIPE_NOWAIT;
        SetNamedPipeHandleState(hReadPipe, &pipeMode, nullptr, nullptr);

        outputFd = _open_osfhandle(reinterpret_cast<intptr_t>(hReadPipe), _O_RDONLY);
        if (outputFd == -1) {
            CloseHandle(hReadPipe);
            return 0;
        }

        return pi.dwProcessId;
#else
        int pipeFd[2];
        if (pipe(pipeFd) == -1) {
            return -1;
        }

        const pid_t pid = fork();

        if (pid < 0) {
            close(pipeFd[0]);
            close(pipeFd[1]);
            return -1;
        }

        if (pid == 0) {
            setpgid(0, 0);
            close(pipeFd[0]);

            dup2(pipeFd[1], STDOUT_FILENO);
            dup2(pipeFd[1], STDERR_FILENO);
            close(pipeFd[1]);

            std::vector<const char *> argv;
            argv.push_back(path.c_str());
            for (const auto &arg: args) {
                argv.push_back(arg.c_str());
            }
            argv.push_back(nullptr);

            execvp(path.c_str(), const_cast<char *const *>(argv.data()));
            _exit(1);
        }

        close(pipeFd[1]);
        outputFd = pipeFd[0];

        const int flags = fcntl(outputFd, F_GETFL, 0);
        fcntl(outputFd, F_SETFL, flags | O_NONBLOCK);

        return pid;
#endif
    }

    bool KillProcess(const ProcessId pid) {
#if defined(_WIN32)
        if (pid == 0) return false;

        HANDLE hProcess = OpenProcess(PROCESS_TERMINATE | PROCESS_QUERY_INFORMATION, FALSE, pid);
        if (hProcess == nullptr) return false;

        DWORD exitCode;
        if (!GetExitCodeProcess(hProcess, &exitCode)) {
            CloseHandle(hProcess);
            return false;
        }

        if (exitCode != STILL_ACTIVE) {
            CloseHandle(hProcess);
            return true;
        }

        const bool result = TerminateProcess(hProcess, 1);
        if (result) {
            WaitForSingleObject(hProcess, 500);
        }

        CloseHandle(hProcess);
        return result;
#else
        if (pid <= 0) {
            return false;
        }

        if (kill(pid, SIGTERM) == 0) {
            int status = 0;
            if (const pid_t result = waitpid(pid, &status, WNOHANG); result == 0) {
                usleep(500000);
                waitpid(pid, &status, WNOHANG);
            }
            return true;
        }
        return false;
#endif
    }

    bool WaitForProcessExit(const ProcessId pid, const int timeoutMs) {
#if defined(_WIN32)
        if (pid == 0) return false;
        HANDLE hProcess = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_INFORMATION, FALSE, pid);
        if (hProcess == nullptr) return true;
        const DWORD r = WaitForSingleObject(hProcess, timeoutMs < 0 ? INFINITE : static_cast<DWORD>(timeoutMs));
        CloseHandle(hProcess);
        return r == WAIT_OBJECT_0;
#else
        if (pid <= 0) {
            return false;
        }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (true) {
            int status = 0;
            const pid_t r = waitpid(pid, &status, WNOHANG);
            if (r == pid || r == -1) {
                return true;
            }
            if (std::chrono::steady_clock::now() >= deadline) {
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
#endif
    }

    bool TerminateProcessTree(const ProcessId pid) {
#if defined(_WIN32)
        if (pid == 0) return false;

        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap != INVALID_HANDLE_VALUE) {
            std::vector<DWORD> children;
            PROCESSENTRY32 pe = {};
            pe.dwSize = sizeof(pe);
            if (Process32First(snap, &pe)) {
                do {
                    if (pe.th32ParentProcessID == pid) children.push_back(pe.th32ProcessID);
                } while (Process32Next(snap, &pe));
            }
            CloseHandle(snap);
            for (const DWORD child: children) TerminateProcessTree(child);
        }

        HANDLE hProcess = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid);
        if (hProcess == nullptr) return true;
        const bool ok = TerminateProcess(hProcess, 1) != 0;
        if (ok) WaitForSingleObject(hProcess, 2000);
        CloseHandle(hProcess);
        return ok;
#else
        if (pid <= 0) {
            return false;
        }
        if (kill(-pid, SIGKILL) == 0) {
            WaitForProcessExit(pid, 2000);
            return true;
        }
        const bool ok = kill(pid, SIGKILL) == 0;
        if (ok) {
            WaitForProcessExit(pid, 2000);
        }
        return ok;
#endif
    }

    bool IsProcessRunning(const ProcessId pid) {
#if defined(_WIN32)
        if (pid == 0) return false;

        HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
        if (hProcess == nullptr) return false;

        DWORD exitCode;
        const bool success = GetExitCodeProcess(hProcess, &exitCode);
        CloseHandle(hProcess);

        return success && exitCode == STILL_ACTIVE;
#else
        if (pid <= 0) {
            return false;
        }

        int status = 0;
        if (const pid_t result = waitpid(pid, &status, WNOHANG); result == 0) {
            return true;
        }
        return false;
#endif
    }

    void CollectProcessTreePids(const ProcessId pid, std::vector<ProcessId> &out) {
        if (pid == 0) {
            return;
        }

#if defined(_WIN32)
        out.push_back(pid);

        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE) {
            return;
        }

        std::vector<DWORD> children;
        PROCESSENTRY32 pe = {};
        pe.dwSize = sizeof(pe);
        if (Process32First(snap, &pe)) {
            do {
                if (pe.th32ParentProcessID == pid) {
                    children.push_back(pe.th32ProcessID);
                }
            } while (Process32Next(snap, &pe));
        }
        CloseHandle(snap);

        for (const DWORD child: children) {
            CollectProcessTreePids(child, out);
        }
#else
        out.push_back(pid);
#endif
    }
}
