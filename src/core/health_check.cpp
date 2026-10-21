#include "health_check.h"

#include <chrono>
#include <filesystem>
#include <regex>
#include <sstream>

#include "jdk.h"
#include "system_image.h"

namespace CoreDeck {
    std::vector<HealthCheckResult> RunEnvironmentHealthCheck(
        const SdkInfo &sdk, const std::shared_ptr<std::atomic<bool>> &cancel
    ) {
        std::vector<HealthCheckResult> results;
        if (cancel->load()) return results;
        const auto environment = BuildAndroidToolEnvironment(sdk);
        const auto exists = [](const std::string &path) {
            std::error_code error;
            return !path.empty() && std::filesystem::is_regular_file(path, error);
        };
        const auto run = [&](const std::string &path, const std::vector<std::string> &args,
                             std::string &output, const std::string &input = "") {
            if (path.empty() || cancel->load()) return false;
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
            return StreamCommandArgsWithEnvCancelable(path, args, input, environment,
                [&](const std::string &line) {
                    if (output.size() < 65536) output += line + "\n";
                }, [&] { return cancel->load() || std::chrono::steady_clock::now() >= deadline; });
        };
        const auto add = [&](const char *label, HealthStatus status, const std::string &detail, HealthFix fix) {
            results.push_back({label, status, detail, status == HealthStatus::Passed ? HealthFix::None : fix});
        };
        std::error_code error;
        const bool rootOk = !sdk.SdkPath.empty() && std::filesystem::is_directory(sdk.SdkPath, error);
        add("SDK directory", rootOk ? HealthStatus::Passed : HealthStatus::Failed,
            rootOk ? sdk.SdkPath : "Select an existing Android SDK directory.", HealthFix::Sdk);

        std::string output;
        const std::string java = sdk.JavaHomePath.empty() ? "java" : JavaExecutablePath(sdk.JavaHomePath);
        const bool javaRan = run(java, {"-version"}, output);
        int major = 0;
        std::istringstream lines(output);
        std::string line;
        while (std::getline(lines, line)) {
            if (line.starts_with("openjdk version") || line.starts_with("java version")) {
                major = JavaMajorVersionFromText(line);
                break;
            }
        }
        const bool javaOk = javaRan && major >= 17;
        add("Java runtime", javaOk ? HealthStatus::Passed : HealthStatus::Failed,
            javaOk ? line : "Java 17 or newer must be runnable. Check the configured JDK.", HealthFix::Jdk);
        if (cancel->load()) return results;

        const auto binary = [&](const char *label, const std::string &path,
                                const char *argument, const char *marker) {
            std::string text;
            const bool ok = exists(path) && run(path, {argument}, text) && text.find(marker) != std::string::npos;
            add(label, ok ? HealthStatus::Passed : HealthStatus::Failed,
                ok ? path : "Tool missing, failed to run, or timed out. Check SDK configuration.", HealthFix::Sdk);
        };
        binary("Android Debug Bridge", sdk.AdbPath, "version", "Android Debug Bridge version");
        if (cancel->load()) return results;
        binary("Android Emulator", sdk.EmulatorPath, "-version", "Android emulator version");
        if (cancel->load()) return results;

        output.clear();
        bool toolsOk = false;
        if (rootOk && javaOk && exists(sdk.SdkManagerPath) && exists(sdk.AvdManagerPath) &&
            run(sdk.SdkManagerPath, {"--sdk_root=" + sdk.SdkPath, "--version"}, output)) {
            // Ignore warning lines rather than mistaking numbers in diagnostics for a version.
            std::istringstream versions(output);
            const std::regex versionPattern(R"(^\s*[0-9]+(\.[0-9]+)+(\s+\(Android CLI\))?\s*$)");
            while (std::getline(versions, line)) {
                if (std::regex_match(line, versionPattern)) toolsOk = true;
            }
        }
        if (toolsOk) {
            std::string help;
            toolsOk = run(sdk.AvdManagerPath, {"--help"}, help) &&
                help.find("avdmanager") != std::string::npos;
        }
        add("SDK command-line tools", !javaOk ? HealthStatus::Skipped :
            (toolsOk ? HealthStatus::Passed : HealthStatus::Failed),
            !javaOk ? "Configure Java before checking SDK tools." :
            (toolsOk ? sdk.SdkManagerPath : "Tool missing, failed to run, or timed out. Check SDK configuration."),
            !javaOk ? HealthFix::Jdk : HealthFix::Sdk);
        if (cancel->load()) return results;

        output.clear();
        if (toolsOk && run(sdk.SdkManagerPath, {"--sdk_root=" + sdk.SdkPath, "--licenses"}, output, "N\n")) {
            const auto status = InterpretSdkLicenseOutput(output);
            add("SDK licenses", status == LicenseStatus::AllAccepted ? HealthStatus::Passed : HealthStatus::Warning,
                status == LicenseStatus::AllAccepted ? "SDK licenses are ready." :
                (status == LicenseStatus::SomeUnaccepted ? "Accept licenses when installing a package in Android SDK settings." :
                 "Unable to determine license status. Try checking again."), HealthFix::Sdk);
        } else if (toolsOk) {
            add("SDK licenses", HealthStatus::Warning,
                "License check failed or timed out. Try checking again.", HealthFix::Sdk);
        } else {
            add("SDK licenses", HealthStatus::Skipped, "SDK tools must work before licenses can be checked.", HealthFix::Sdk);
        }
        if (cancel->load()) return results;

        if (rootOk) {
            try {
                const auto images = ListSystemImages(sdk);
                add("System images", images.empty() ? HealthStatus::Warning : HealthStatus::Passed,
                    images.empty() ? "No compatible system image found. Install one from Create AVD." :
                    "Compatible system images are available.", HealthFix::Images);
            } catch (const std::filesystem::filesystem_error &) {
                add("System images", HealthStatus::Warning, "Unable to read system images. Check SDK permissions.", HealthFix::Sdk);
            }
        } else {
            add("System images", HealthStatus::Skipped, "Select an existing Android SDK directory.", HealthFix::Sdk);
        }
        error.clear();
        const auto space = rootOk ? std::filesystem::space(sdk.SdkPath, error) : std::filesystem::space_info{};
        if (!rootOk || error) {
            add("SDK disk space", HealthStatus::Skipped, "Unable to read free space for the SDK directory.", HealthFix::None);
        } else {
            constexpr uintmax_t recommended = 10ULL * 1024 * 1024 * 1024;
            add("SDK disk space", space.available >= recommended ? HealthStatus::Passed : HealthStatus::Warning,
                space.available >= recommended ? "At least 10 GiB is available on the SDK volume." :
                "Less than 10 GiB is available on the SDK volume. Consider freeing space before installing images.", HealthFix::None);
        }
        return results;
    }
}
