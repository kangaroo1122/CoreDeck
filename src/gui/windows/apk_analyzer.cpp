#include "apk_analyzer.h"

#include <chrono>
#include "imgui.h"
#include "../context.h"
#include "../async_work.h"
#include "../widgets.h"
#include "../localization.h"
#include "../../core/file_dialog.h"

namespace CoreDeck {
    namespace {
        void Poll(Context::ApkAnalyzerState &work) {
            if (work.Analysis.valid() && work.Analysis.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                try {
                    work.Report = work.Analysis.get();
                    work.Status = work.Report.Cancelled ? "Cancelled." : work.Report.Error;
                    work.Details = work.Report.Error.empty() ? "" : work.Report.Details;
                } catch (const std::exception &e) {
                    work.Status = "Could not analyze APK.";
                    work.Details = e.what();
                }
            }
            if (work.Installation.valid() && work.Installation.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                try {
                    const auto result = work.Installation.get();
                    work.Status = result.Cancelled ? "Installation cancelled; check the device for its final state." :
                        result.Succeeded ? "APK installed." : result.Error;
                    work.Details = result.Succeeded ? "" : result.Details;
                } catch (const std::exception &e) {
                    work.Status = "Could not install APK.";
                    work.Details = e.what();
                }
            }
        }

        void Field(const char *label, const std::string &value) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", Tr(label));
            ImGui::TableNextColumn();
            ImGui::TextWrapped("%s", value.empty() ? Tr("Not specified") : value.c_str());
        }
    }

    void ShutdownApkAnalyzer(Context &context) {
        if (context.ApkAnalyzer.Cancel) context.ApkAnalyzer.Cancel->store(true);
        ConsumeFuture(context.ApkAnalyzer.Analysis);
        ConsumeFuture(context.ApkAnalyzer.Installation);
    }

    void BuildApkAnalyzerWindow(Context &context) {
        auto &work = context.ApkAnalyzer;
        Poll(work);
        if (!work.Open) return;
        ImGui::SetNextWindowSize(EmV(48, 38), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSizeConstraints(EmV(34, 26), ImVec2(FLT_MAX, FLT_MAX));
        const std::string title = std::string(Tr("APK Analyzer")) + "###ApkAnalyzer";
        if (!ImGui::Begin(title.c_str(), &work.Open, ImGuiWindowFlags_NoDocking)) {
            ImGui::End();
            return;
        }
        bool busy = work.Analysis.valid() || work.Installation.valid();
        if (PrimaryButton("Choose APK...", !busy)) {
            const char *filters[] = {"*.apk"};
            if (const auto path = FileDialog::PickFile(Tr("Choose APK..."), filters, 1, "APK")) {
                work.Path = *path;
                work.Report = {};
                work.Status.clear();
                work.Details.clear();
                work.Cancel = std::make_shared<std::atomic<bool>>(false);
                work.Analysis = std::async(std::launch::async,
                    [sdk = context.Host.Sdk, path = *path, cancel = work.Cancel] {
                        return AnalyzeApk(sdk, path, [cancel] { return cancel->load(); });
                    });
                busy = true;
            }
        }
        if (busy) {
            ImGui::SameLine();
            if (NegativeButton("Cancel", !work.Cancel->load())) work.Cancel->store(true);
        }
        ImGui::TextWrapped("%s", work.Path.empty() ? Tr("Select an APK to inspect before installation.") : work.Path.c_str());
        ImGui::Separator();

        ImGui::BeginChild("ApkReport", ImVec2(0, -Eh(7)), ImGuiChildFlags_None);
        if (busy) ImGui::TextDisabled("%s", Tr(work.Analysis.valid() ? "Analyzing APK..." : "Installing APK..."));
        if (!work.Status.empty()) ImGui::TextWrapped("%s", Tr(work.Status.c_str()));
        if (!work.Details.empty() && ImGui::CollapsingHeader(Tr("Details"))) {
            ImGui::TextWrapped("%s", work.Details.c_str());
        }
        const bool ready = !work.Report.PackageName.empty() && work.Report.Error.empty() && !work.Report.Cancelled;
        if (ready) {
            const auto &report = work.Report;
            if (ImGui::BeginTable("ApkMetadata", 2, ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("Key", 0, 1);
                ImGui::TableSetupColumn("Value", 0, 2);
                Field("Application", report.Label);
                Field("Package name", report.PackageName);
                Field("Version name", report.VersionName);
                Field("Version code", report.VersionCode);
                Field("Minimum Android API", report.MinSdk);
                Field("Target Android API", report.TargetSdk);
                std::string abis;
                for (const auto &abi: report.Abis) { if (!abis.empty()) abis += ", "; abis += abi; }
                Field("Native architectures", abis.empty() ? Tr("No native ABI declared") : abis);
                ImGui::EndTable();
            }
            ImGui::Spacing();
            ImGui::TextDisabled("%s (%zu)", Tr("Permissions"), report.Permissions.size());
            for (const auto &permission: report.Permissions) ImGui::TextWrapped("%s", permission.c_str());
        }
        ImGui::EndChild();
        ImGui::Separator();

        std::string name;
        const int selected = context.Catalog.SelectedAvd;
        if (selected >= 0 && selected < static_cast<int>(context.Catalog.Avds.size())) name = context.Catalog.Avds[selected].Name;
        const auto serial = name.empty() ? "" : EmulatorSerialForConsolePort(context.Host.Manager.GetConsolePort(name));
        const bool online = !name.empty() && context.Host.Manager.IsRunning(name) && !serial.empty() && HasAdb(context.Host.Sdk);
        ImGui::TextWrapped("%s: %s", Tr("Target emulator"), name.empty() ? Tr("None") : name.c_str());
        const bool enabled = ready && online && !busy;
        if (!online) ImGui::TextDisabled("%s", Tr("Select a running emulator with ADB available."));
        if (PrimaryButton("Install APK", enabled)) {
            work.Status.clear();
            work.Details.clear();
            work.Cancel = std::make_shared<std::atomic<bool>>(false);
            work.Installation = std::async(std::launch::async,
                [sdk = context.Host.Sdk, serial, path = work.Report.Path, cancel = work.Cancel] {
                    return InstallApk(sdk, serial, path, [cancel] { return cancel->load(); });
                });
        }
        ImGui::SameLine();
        if (PrimaryButton("View app Logcat", enabled)) {
            auto &logs = context.Logs.PerAvdLogcatView[name];
            logs.PackageFilter = work.Report.PackageName;
            logs.SelectedPid = 0;
            logs.Search.clear();
            logs.UseRegex = false;
            logs.Paused = false;
            context.Logs.ActiveSource = LogSource::Logcat;
            context.UI.ShowLogPanel = true;
            work.Open = false;
        }
        ImGui::End();
    }
}
