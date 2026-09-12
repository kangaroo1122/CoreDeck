#ifndef COREDECK_LOGCAT_H
#define COREDECK_LOGCAT_H

#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "sdk.h"

namespace CoreDeck {
    enum class LogcatBuffer : uint8_t {
        Main,
        System,
        Crash,
        All,
    };

    enum class LogcatPriority : uint8_t {
        Verbose,
        Debug,
        Info,
        Warning,
        Error,
        Fatal,
        Unknown,
    };

    struct LogcatEntry {
        std::string Raw;
        std::string Timestamp;
        int Pid = -1;
        int Tid = -1;
        LogcatPriority Priority = LogcatPriority::Unknown;
        std::string Tag;
        std::string Message;
    };

    struct LogcatProcess {
        int Pid = 0;
        std::string Name;
    };

    struct LogcatFilterOptions {
        LogcatPriority MinimumPriority = LogcatPriority::Debug;
        int Pid = 0;
        std::string Query;
        bool UseRegex = false;
        bool CaseSensitive = false;
    };

    struct LogcatFilterResult {
        std::vector<std::size_t> Indices;
        bool RegexValid = true;
        std::string RegexError;
    };

    struct LogcatStreamStatus {
        std::string AvdName;
        std::string Serial;
        LogcatBuffer Buffer = LogcatBuffer::Main;
        bool Connecting = false;
        bool Running = false;
        std::string Error;
    };

    LogcatEntry ParseLogcatThreadtimeLine(const std::string &line);

    std::vector<LogcatProcess> ParseAdbProcessList(const std::string &output);

    LogcatFilterResult FilterLogcatEntries(
        const std::vector<LogcatEntry> &entries,
        const LogcatFilterOptions &options
    );

    const char *LogcatBufferArgument(LogcatBuffer buffer);

    const char *LogcatPriorityLabel(LogcatPriority priority);

    class LogcatStream {
    public:
        LogcatStream() = default;
        LogcatStream(const LogcatStream &) = delete;
        LogcatStream(LogcatStream &&) = delete;
        LogcatStream &operator=(const LogcatStream &) = delete;
        LogcatStream &operator=(LogcatStream &&) = delete;
        ~LogcatStream();

        void Start(
            const SdkInfo &sdk,
            const std::string &avdName,
            const std::string &serial,
            LogcatBuffer buffer
        );

        void Stop();

        void Clear();

        [[nodiscard]] bool Matches(
            const std::string &adbPath,
            const std::string &avdName,
            const std::string &serial,
            LogcatBuffer buffer
        ) const;

        [[nodiscard]] LogcatStreamStatus Status() const;

        [[nodiscard]] std::vector<LogcatEntry> Entries() const;

        [[nodiscard]] std::vector<LogcatProcess> Processes() const;

        [[nodiscard]] std::uint64_t Revision() const;

    private:
        static constexpr std::size_t MAX_ENTRIES = 10000;

        mutable std::mutex m_Mutex;
        std::deque<LogcatEntry> m_Entries;
        std::vector<LogcatProcess> m_Processes;
        LogcatStreamStatus m_Status;
        std::string m_AdbPath;
        std::thread m_Thread;
        std::thread m_ProcessThread;
        std::shared_ptr<std::atomic<bool>> m_CancelRequested;
        std::atomic<std::uint64_t> m_Revision{0};
    };
}

#endif // COREDECK_LOGCAT_H
