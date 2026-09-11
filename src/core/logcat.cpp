#include "logcat.h"

#include <algorithm>
#include <charconv>
#include <regex>
#include <sstream>
#include <unordered_set>

#include "process.h"
#include "utilities.h"

namespace CoreDeck {
    namespace {
        std::string TrimCopy(const std::string &value) {
            const auto start = value.find_first_not_of(" \t\r\n");
            if (start == std::string::npos) {
                return "";
            }
            const auto end = value.find_last_not_of(" \t\r\n");
            return value.substr(start, end - start + 1);
        }

        bool ParseInteger(const std::string &value, int &result) {
            if (value.empty()) {
                return false;
            }
            const char *begin = value.data();
            const char *end = begin + value.size();
            const auto parsed = std::from_chars(begin, end, result);
            return parsed.ec == std::errc{} && parsed.ptr == end;
        }

        LogcatPriority ParsePriority(const std::string &value) {
            if (value.size() != 1) {
                return LogcatPriority::Unknown;
            }
            switch (value[0]) {
                case 'V': return LogcatPriority::Verbose;
                case 'D': return LogcatPriority::Debug;
                case 'I': return LogcatPriority::Info;
                case 'W': return LogcatPriority::Warning;
                case 'E': return LogcatPriority::Error;
                case 'F': return LogcatPriority::Fatal;
                default: return LogcatPriority::Unknown;
            }
        }

        int PriorityRank(const LogcatPriority priority) {
            switch (priority) {
                case LogcatPriority::Verbose: return 0;
                case LogcatPriority::Debug: return 1;
                case LogcatPriority::Info: return 2;
                case LogcatPriority::Warning: return 3;
                case LogcatPriority::Error: return 4;
                case LogcatPriority::Fatal: return 5;
                case LogcatPriority::Unknown: return -1;
            }
            return -1;
        }

        std::vector<LogcatProcess> QueryProcesses(
            const SdkInfo &sdk,
            const std::string &serial,
            const std::shared_ptr<std::atomic<bool>> &cancelRequested
        ) {
            std::string output;
            const bool ok = StreamCommandArgsWithEnvCancelable(
                sdk.AdbPath,
                {"-s", serial, "shell", "ps", "-A", "-o", "PID,NAME"},
                "",
                BuildAndroidToolEnvironment(sdk),
                [&output](const std::string &line) {
                    output += line;
                    output.push_back('\n');
                },
                [cancelRequested] {
                    return cancelRequested->load();
                }
            );
            return ok ? ParseAdbProcessList(output) : std::vector<LogcatProcess>{};
        }
    }

    LogcatEntry ParseLogcatThreadtimeLine(const std::string &line) {
        LogcatEntry entry;
        entry.Raw = line;

        std::istringstream stream(line);
        std::string date;
        std::string time;
        std::string pid;
        std::string tid;
        std::string priority;
        if (!(stream >> date >> time >> pid >> tid >> priority) ||
            date.size() != 5 || date[2] != '-' || time.size() < 8) {
            entry.Message = line;
            return entry;
        }

        int parsedPid = -1;
        int parsedTid = -1;
        const LogcatPriority parsedPriority = ParsePriority(priority);
        if (!ParseInteger(pid, parsedPid) || !ParseInteger(tid, parsedTid) || parsedPriority == LogcatPriority::Unknown) {
            entry.Message = line;
            return entry;
        }

        std::string remainder;
        std::getline(stream, remainder);
        remainder = TrimCopy(remainder);
        const auto separator = remainder.find(':');

        entry.Timestamp = date + " " + time;
        entry.Pid = parsedPid;
        entry.Tid = parsedTid;
        entry.Priority = parsedPriority;
        if (separator == std::string::npos) {
            entry.Message = remainder;
        } else {
            entry.Tag = TrimCopy(remainder.substr(0, separator));
            entry.Message = TrimCopy(remainder.substr(separator + 1));
        }
        return entry;
    }

    std::vector<LogcatProcess> ParseAdbProcessList(const std::string &output) {
        std::vector<LogcatProcess> result;
        std::unordered_set<int> seen;
        std::istringstream lines(output);
        std::string line;
        while (std::getline(lines, line)) {
            std::istringstream parts(TrimCopy(line));
            std::string pidText;
            std::string name;
            parts >> pidText >> name;
            int pid = 0;
            if (!ParseInteger(pidText, pid) || pid <= 0 || name.empty() || !seen.insert(pid).second) {
                continue;
            }
            result.push_back({.Pid = pid, .Name = name});
        }
        std::ranges::sort(result, [](const LogcatProcess &left, const LogcatProcess &right) {
            if (left.Name == right.Name) {
                return left.Pid < right.Pid;
            }
            return left.Name < right.Name;
        });
        return result;
    }

    LogcatFilterResult FilterLogcatEntries(
        const std::vector<LogcatEntry> &entries,
        const LogcatFilterOptions &options
    ) {
        LogcatFilterResult result;
        std::regex compiled;
        if (!options.Query.empty() && options.UseRegex) {
            try {
                auto flags = std::regex::ECMAScript;
                if (!options.CaseSensitive) {
                    flags |= std::regex::icase;
                }
                compiled = std::regex(options.Query, flags);
            } catch (const std::regex_error &error) {
                result.RegexValid = false;
                result.RegexError = error.what();
                return result;
            }
        }

        const std::string substring = options.CaseSensitive ? options.Query : LowerCopy(options.Query);
        result.Indices.reserve(entries.size());
        for (std::size_t index = 0; index < entries.size(); ++index) {
            const LogcatEntry &entry = entries[index];
            if (entry.Priority != LogcatPriority::Unknown &&
                PriorityRank(entry.Priority) < PriorityRank(options.MinimumPriority)) {
                continue;
            }
            if (options.Pid > 0 && entry.Pid != options.Pid) {
                continue;
            }
            if (!options.Query.empty()) {
                if (options.UseRegex) {
                    if (!std::regex_search(entry.Raw, compiled)) {
                        continue;
                    }
                } else {
                    const std::string haystack = options.CaseSensitive ? entry.Raw : LowerCopy(entry.Raw);
                    if (haystack.find(substring) == std::string::npos) {
                        continue;
                    }
                }
            }
            result.Indices.push_back(index);
        }
        return result;
    }

    const char *LogcatBufferArgument(const LogcatBuffer buffer) {
        switch (buffer) {
            case LogcatBuffer::Main: return "main";
            case LogcatBuffer::System: return "system";
            case LogcatBuffer::Crash: return "crash";
            case LogcatBuffer::All: return "all";
        }
        return "main";
    }

    const char *LogcatPriorityLabel(const LogcatPriority priority) {
        switch (priority) {
            case LogcatPriority::Verbose: return "V";
            case LogcatPriority::Debug: return "D";
            case LogcatPriority::Info: return "I";
            case LogcatPriority::Warning: return "W";
            case LogcatPriority::Error: return "E";
            case LogcatPriority::Fatal: return "F";
            case LogcatPriority::Unknown: return "";
        }
        return "";
    }

    LogcatStream::~LogcatStream() {
        Stop();
    }

    void LogcatStream::Start(
        const SdkInfo &sdk,
        const std::string &avdName,
        const std::string &serial,
        const LogcatBuffer buffer
    ) {
        bool sameTarget = false;
        {
            std::lock_guard lock(m_Mutex);
            sameTarget = m_AdbPath == sdk.AdbPath &&
                         m_Status.AvdName == avdName &&
                         m_Status.Serial == serial &&
                         m_Status.Buffer == buffer;
        }
        Stop();

        auto cancelRequested = std::make_shared<std::atomic<bool>>(false);
        {
            std::lock_guard lock(m_Mutex);
            if (!sameTarget) {
                m_Entries.clear();
                m_Processes.clear();
                ++m_Revision;
            }
            m_AdbPath = sdk.AdbPath;
            m_Status = {
                .AvdName = avdName,
                .Serial = serial,
                .Buffer = buffer,
                .Connecting = true,
                .Running = false,
                .Error = {},
            };
            m_CancelRequested = cancelRequested;
        }

        m_Thread = std::thread([this, sdk, avdName, serial, buffer, cancelRequested] {
            const auto processes = QueryProcesses(sdk, serial, cancelRequested);
            if (cancelRequested->load()) {
                return;
            }
            {
                std::lock_guard lock(m_Mutex);
                m_Processes = processes;
                m_Status.Connecting = false;
                m_Status.Running = true;
            }

            const bool completed = StreamCommandArgsWithEnvCancelable(
                sdk.AdbPath,
                {"-s", serial, "logcat", "-v", "threadtime", "-b", LogcatBufferArgument(buffer)},
                "",
                BuildAndroidToolEnvironment(sdk),
                [this](const std::string &line) {
                    std::lock_guard lock(m_Mutex);
                    m_Entries.push_back(ParseLogcatThreadtimeLine(line));
                    if (m_Entries.size() > MAX_ENTRIES) {
                        m_Entries.pop_front();
                    }
                    ++m_Revision;
                },
                [cancelRequested] {
                    return cancelRequested->load();
                }
            );

            std::lock_guard lock(m_Mutex);
            m_Status.Connecting = false;
            m_Status.Running = false;
            if (!cancelRequested->load() && !completed) {
                m_Status.Error = "Logcat disconnected.";
            }
        });
    }

    void LogcatStream::Stop() {
        std::thread worker;
        {
            std::lock_guard lock(m_Mutex);
            if (m_CancelRequested) {
                m_CancelRequested->store(true);
            }
            if (m_Thread.joinable()) {
                worker = std::move(m_Thread);
            }
        }
        if (worker.joinable()) {
            worker.join();
        }
        std::lock_guard lock(m_Mutex);
        m_Status.Connecting = false;
        m_Status.Running = false;
        m_CancelRequested.reset();
    }

    void LogcatStream::Clear() {
        std::lock_guard lock(m_Mutex);
        m_Entries.clear();
        ++m_Revision;
    }

    bool LogcatStream::Matches(
        const std::string &adbPath,
        const std::string &avdName,
        const std::string &serial,
        const LogcatBuffer buffer
    ) const {
        std::lock_guard lock(m_Mutex);
        return m_AdbPath == adbPath &&
               m_Status.AvdName == avdName &&
               m_Status.Serial == serial &&
               m_Status.Buffer == buffer;
    }

    LogcatStreamStatus LogcatStream::Status() const {
        std::lock_guard lock(m_Mutex);
        return m_Status;
    }

    std::vector<LogcatEntry> LogcatStream::Entries() const {
        std::lock_guard lock(m_Mutex);
        return {m_Entries.begin(), m_Entries.end()};
    }

    std::vector<LogcatProcess> LogcatStream::Processes() const {
        std::lock_guard lock(m_Mutex);
        return m_Processes;
    }

    std::uint64_t LogcatStream::Revision() const {
        return m_Revision.load();
    }
}
