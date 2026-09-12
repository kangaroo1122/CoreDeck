#include <catch2/catch_test_macros.hpp>

#include "core/logcat.h"

using namespace CoreDeck;

namespace {
    std::vector<LogcatEntry> SampleEntries() {
        return {
            ParseLogcatThreadtimeLine("09-11 14:32:18.204  1842  1890 D CoreDeckDemo: Session restored"),
            ParseLogcatThreadtimeLine("09-11 14:32:19.011  1842  1890 W NetworkClient: Request retry scheduled"),
            ParseLogcatThreadtimeLine("09-11 14:32:20.093   623   701 E ActivityManager: Process crashed"),
        };
    }
}

TEST_CASE("threadtime Logcat parser extracts structured fields", "[logcat][parse]") {
    const auto entry = ParseLogcatThreadtimeLine(
        "09-11 14:32:18.204  1842  1890 I ActivityManager: Displayed com.example/.MainActivity"
    );

    CHECK(entry.Timestamp == "09-11 14:32:18.204");
    CHECK(entry.Pid == 1842);
    CHECK(entry.Tid == 1890);
    CHECK(entry.Priority == LogcatPriority::Info);
    CHECK(entry.Tag == "ActivityManager");
    CHECK(entry.Message == "Displayed com.example/.MainActivity");
}

TEST_CASE("unstructured Logcat lines remain visible", "[logcat][parse]") {
    const auto entry = ParseLogcatThreadtimeLine("--------- beginning of main");
    CHECK(entry.Priority == LogcatPriority::Unknown);
    CHECK(entry.Pid == -1);
    CHECK(entry.Message == "--------- beginning of main");
}

TEST_CASE("ADB process parser sorts names and removes duplicate PIDs", "[logcat][process]") {
    const auto processes = ParseAdbProcessList(
        "PID NAME\n"
        "1842 com.example.app\n"
        "623 system_server\n"
        "1842 com.example.app:worker\n"
    );

    REQUIRE(processes.size() == 2);
    CHECK(processes[0].Name == "com.example.app");
    CHECK(processes[0].Pid == 1842);
    CHECK(processes[1].Name == "system_server");
}

TEST_CASE("ADB process parser supports the full Android ps layout", "[logcat][process]") {
    const auto processes = ParseAdbProcessList(
        "USER      PID  PPID  VSZ      RSS   WCHAN            ADDR S NAME\n"
        "root        1     0  10946836 3916  0                   0 S init\n"
        "u0_a142  1842   623  2154324  98320 0                   0 S com.example.app\n"
    );

    REQUIRE(processes.size() == 2);
    CHECK(processes[0].Name == "com.example.app");
    CHECK(processes[0].Pid == 1842);
    CHECK(processes[1].Name == "init");
    CHECK(processes[1].Pid == 1);
}

TEST_CASE("Logcat filters combine priority PID and text", "[logcat][filter]") {
    const auto entries = SampleEntries();
    LogcatFilterOptions options;
    options.MinimumPriority = LogcatPriority::Warning;
    options.Pid = 1842;
    options.Query = "retry";

    const auto result = FilterLogcatEntries(entries, options);
    REQUIRE(result.RegexValid);
    REQUIRE(result.Indices.size() == 1);
    CHECK(result.Indices[0] == 1);
}

TEST_CASE("Logcat regex filter reports invalid expressions", "[logcat][filter]") {
    const auto entries = SampleEntries();
    LogcatFilterOptions options;
    options.Query = "(unclosed";
    options.UseRegex = true;

    const auto result = FilterLogcatEntries(entries, options);
    CHECK_FALSE(result.RegexValid);
    CHECK_FALSE(result.RegexError.empty());
    CHECK(result.Indices.empty());
}

TEST_CASE("Logcat buffer arguments match adb names", "[logcat][buffer]") {
    CHECK(std::string(LogcatBufferArgument(LogcatBuffer::Main)) == "main");
    CHECK(std::string(LogcatBufferArgument(LogcatBuffer::System)) == "system");
    CHECK(std::string(LogcatBufferArgument(LogcatBuffer::Crash)) == "crash");
    CHECK(std::string(LogcatBufferArgument(LogcatBuffer::All)) == "all");
}
