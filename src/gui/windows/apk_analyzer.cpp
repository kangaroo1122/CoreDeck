// APK file tree adapted from devmuaz/CoreDeck (56e7fa2), integrated with CoreDeck device tools.
#include "apk_analyzer.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <unordered_set>
#include <utility>
#include "imgui.h"
#include "../context.h"
#include "../application.h"
#include "../async_work.h"
#include "../widgets.h"
#include "../theme.h"
#include "../localization.h"
#include "../../core/file_dialog.h"
#include "../../core/utilities.h"

namespace CoreDeck {
    namespace {
        struct FileNode {
            std::string Name;
            std::string FullPath;
            bool Directory = false;
            bool Visible = true;
            std::int64_t RawSize = 0;
            std::int64_t DownloadSize = 0;
            std::uint32_t Alignment = 0;
            bool Stored = false;
            std::vector<FileNode> Children;
        };


        struct FlatFile {
            FileNode *Node = nullptr;
            int Depth = 0;
        };

        struct FileCache {
            std::string Key;
            FileNode Root;
            std::unordered_set<std::string> Open;
            int FileCount = 0;
        };



        FileCache &FilesCache() { static FileCache cache; return cache; }
        std::string FileNameOf(const std::string &path) { return std::filesystem::path(path).filename().string(); }
        bool NameLess(const std::string &a, const std::string &b) { return a < b; }
        std::string FormatBytes(std::int64_t size) {
            return size < 0 ? "-" + FormatFileSize(static_cast<std::uintmax_t>(-size)) : FormatFileSize(size);
        }
        FileNode *FindOrAddChild(FileNode &parent, const std::string &name, const std::string &fullPath, const bool directory) {
            for (auto &child: parent.Children) {
                if (child.Name == name) {
                    return &child;
                }
            }
            FileNode created;
            created.Name = name;
            created.FullPath = fullPath;
            created.Directory = directory;
            parent.Children.push_back(std::move(created));
            return &parent.Children.back();
        }

        void AddFileEntry(FileNode &root, const ApkFileEntry &entry) {
            if (entry.Path.empty() || entry.Path == "/") {
                root.FullPath = "/";
                root.Directory = true;
                root.RawSize = entry.RawSize;
                root.DownloadSize = entry.DownloadSize;
                return;
            }

            const bool directory = entry.Path.ends_with('/');
            std::string relative = entry.Path;
            if (relative.starts_with('/')) {
                relative.erase(relative.begin());
            }
            if (relative.ends_with('/')) {
                relative.pop_back();
            }

            FileNode *node = &root;
            std::string walked = "/";
            std::size_t start = 0;
            while (start <= relative.size()) {
                const auto slash = relative.find('/', start);
                const bool last = slash == std::string::npos;
                const std::string name = relative.substr(start, last ? std::string::npos : slash - start);
                if (!name.empty()) {
                    const bool childDirectory = !last || directory;
                    const std::string childPath = childDirectory ? walked + name + "/" : walked + name;
                    FileNode *child = FindOrAddChild(*node, name, last ? entry.Path : childPath, childDirectory);
                    if (last) {
                        child->Directory = directory;
                        child->FullPath = entry.Path;
                        child->RawSize = entry.RawSize;
                        child->DownloadSize = entry.DownloadSize;
                        child->Alignment = entry.Alignment;
                        child->Stored = entry.Stored;
                    } else if (!child->Directory) {
                        child->Directory = true;
                        child->FullPath = childPath;
                    }
                    node = child;
                    if (child->Directory && !child->FullPath.empty()) {
                        walked = child->FullPath;
                        if (!walked.ends_with('/')) {
                            walked.push_back('/');
                        }
                    } else {
                        walked = childPath;
                    }
                }
                if (last) {
                    break;
                }
                start = slash + 1;
            }
        }

        void SortFileNode(FileNode &node) {
            std::ranges::sort(node.Children, [](const FileNode &left, const FileNode &right) {
                if (left.DownloadSize != right.DownloadSize) {
                    return left.DownloadSize > right.DownloadSize;
                }
                return NameLess(left.Name, right.Name);
            });
            for (auto &child: node.Children) {
                SortFileNode(child);
            }
        }

        int CountFiles(const FileNode &node) {
            int count = node.Directory ? 0 : 1;
            for (const auto &child: node.Children) {
                count += CountFiles(child);
            }
            return count;
        }

        bool MarkFile(FileNode &node, const char *filter, const bool ancestorMatched) {
            const bool filtering = filter != nullptr && filter[0] != '\0';
            const bool self = !filtering || ContainsIgnoreCase(node.Name, filter) || ContainsIgnoreCase(node.FullPath, filter);
            bool descendant = false;
            for (auto &child: node.Children) {
                if (MarkFile(child, filter, ancestorMatched || self)) {
                    descendant = true;
                }
            }
            node.Visible = !filtering || ancestorMatched || self || descendant;
            return self || descendant;
        }

        void FlattenFiles(
            FileNode &node,
            const int depth,
            const std::unordered_set<std::string> &open,
            const bool forceOpen,
            std::vector<FlatFile> &rows
        ) {
            if (!node.Visible) {
                return;
            }
            rows.push_back({.Node = &node, .Depth = depth});
            if (!forceOpen && !open.contains(node.FullPath)) {
                return;
            }
            for (auto &child: node.Children) {
                FlattenFiles(child, depth + 1, open, forceOpen, rows);
            }
        }

        FileCache &CachedFiles(const ApkReport &report, const std::uint32_t epoch) {
            auto &cache = FilesCache();
            const std::string key = StrConcat(std::to_string(epoch), ":", report.Path, ":", std::to_string(report.Files.Value.size()));
            if (cache.Key == key) {
                return cache;
            }

            FileNode root;
            root.Name = FileNameOf(report.Path);
            if (root.Name.empty()) {
                root.Name = Tr("APK");
            }
            root.FullPath = "/";
            root.Directory = true;
            for (const auto &entry: report.Files.Value) {
                AddFileEntry(root, entry);
            }
            if (root.RawSize == 0 && report.FileSize.Ok) {
                root.RawSize = report.FileSize.Value;
            }
            if (root.DownloadSize == 0 && report.DownloadSize.Ok) {
                root.DownloadSize = report.DownloadSize.Value;
            }
            SortFileNode(root);

            cache = {};
            cache.Key = key;
            cache.Root = std::move(root);
            cache.FileCount = CountFiles(cache.Root);
            cache.Open.insert("/");
            return cache;
        }



        void Poll(Context::ApkAnalyzerState &work) {
            if (work.Analysis.valid() && work.Analysis.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                try {
                    work.Report = work.Analysis.get();
                    ++work.ContentEpoch;
                    work.Status = work.Report.Cancelled ? "Cancelled." : work.Report.Error;
                    work.Details = work.Report.Error.empty() ? "" : work.Report.Details;
                } catch (const std::exception &e) {
                    work.Status = "Could not analyze APK.";
                    work.Details = e.what();
                } catch (...) {
                    work.Status = "Could not analyze APK.";
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
                } catch (...) {
                    work.Status = "Could not install APK.";
                }
            }
        }


        bool Busy(const Context::ApkAnalyzerState &work) {
            return work.Analysis.valid() || work.Installation.valid() || work.Comparison.valid();
        }

        void StartAnalysis(Context &context, const std::string &path) {
            auto &work = context.ApkAnalyzer;
            if (Busy(work)) return;
            work.Path = path;
            work.Report = {};
            work.Compare = {};
            work.ComparePath.clear();
            work.Filter[0] = '\0';
            work.Status.clear();
            work.Details.clear();
            work.Cancel = std::make_shared<std::atomic<bool>>(false);
            work.Progress = std::make_shared<std::atomic<float>>(0.0F);
            work.Analysis = std::async(std::launch::async, [sdk = context.Host.Sdk, path, cancel = work.Cancel, progress = work.Progress] {
                return AnalyzeApk(sdk, path, [cancel] { return cancel->load(); }, [progress](float value) { progress->store(value); });
            });
            auto &recent = work.RecentApks;
            std::erase(recent, path);
            recent.insert(recent.begin(), path);
            if (recent.size() > 8) recent.resize(8);
            PersistAppSettings(context);
        }

        std::optional<std::string> ChooseApk() {
            const char *filters[] = {"*.apk", "*.APK"};
            return FileDialog::PickFile(Tr("Choose APK..."), filters, 2, "APK");
        }

        void InfoChip(const std::string &text) {
            if (ImGui::GetCursorPosX() > ImGui::GetStyle().WindowPadding.x &&
                ImGui::CalcTextSize(text.c_str()).x + ImGui::GetStyle().FramePadding.x * 2 > ImGui::GetContentRegionAvail().x)
                ImGui::NewLine();
            CategoryChip(text.c_str(), false);
            ImGui::SameLine();
        }

        void DrawFileTree(Context &context, FileCache &cache) {
            auto &work = context.ApkAnalyzer;
            const bool filtering = work.Filter[0] != '\0';
            MarkFile(cache.Root, work.Filter, false);

            std::vector<FlatFile> rows;
            rows.reserve(static_cast<std::size_t>(std::max(cache.FileCount, 16)));
            FlattenFiles(cache.Root, 0, cache.Open, filtering, rows);

            const std::int64_t downloadTotal = cache.Root.DownloadSize;
            PickerTableStyle tableStyle;
            if (ImGui::BeginTable("##ApkFileTree", 6, PICKER_TABLE_FLAGS | ImGuiTableFlags_ScrollX | ImGuiTableFlags_Resizable, ImVec2(-1.0F, -1.0F))) {
                ImGui::TableSetupScrollFreeze(0, 1);
                const std::string fileColumn = StrConcat(" ", Tr("File"));
                ImGui::TableSetupColumn(fileColumn.c_str(), ImGuiTableColumnFlags_WidthStretch, 3.2F);
                ImGui::TableSetupColumn(Tr("Raw Size"), ImGuiTableColumnFlags_WidthFixed, Em(11.0F));
                ImGui::TableSetupColumn(Tr(work.Report.Files.FromZip ? "ZIP compressed size" : "Download Size"), ImGuiTableColumnFlags_WidthFixed, Em(13.0F));
                ImGui::TableSetupColumn(Tr(work.Report.Files.FromZip ? "ZIP size share" : "% of Download Size"), ImGuiTableColumnFlags_WidthFixed, Em(16.0F));
                ImGui::TableSetupColumn(Tr("Compressed"), ImGuiTableColumnFlags_WidthFixed, Em(11.0F));
                ImGui::TableSetupColumn(Tr("Alignment"), ImGuiTableColumnFlags_WidthFixed, Em(10.0F));
                ImGui::TableHeadersRow();

                if (rows.empty()) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextDisabled("%s", Tr("No files match."));
                } else {
                    ImGuiListClipper clipper;
                    clipper.Begin(static_cast<int>(rows.size()));
                    while (clipper.Step()) {
                        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                            const FlatFile &file = rows.at(static_cast<std::size_t>(row));
                            FileNode *node = file.Node;
                            const int depth = file.Depth;
                            const bool hasChildren = !node->Children.empty();
                            const bool isOpen = filtering || cache.Open.contains(node->FullPath);

                            ImGui::TableNextRow();
                            ImGui::TableNextColumn();
                            ImGui::Indent(ImGui::GetTextLineHeight() * depth);
                            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth;
                            if (!hasChildren) flags |= ImGuiTreeNodeFlags_Leaf;
                            ImGui::SetNextItemOpen(isOpen);
                            const bool opened = ImGui::TreeNodeEx(node->FullPath.c_str(), flags, "%s", node->Name.c_str());
                            HoverTooltip(node->FullPath);
                            if (hasChildren && opened != isOpen && !filtering) {
                                if (opened) cache.Open.insert(node->FullPath);
                                else cache.Open.erase(node->FullPath);
                            }
                            ImGui::Unindent(ImGui::GetTextLineHeight() * depth);
                            ImGui::TableNextColumn();
                            ImGui::TextUnformatted(FormatBytes(node->RawSize).c_str());
                            ImGui::TableNextColumn();
                            ImGui::TextUnformatted(FormatBytes(node->DownloadSize).c_str());
                            ImGui::TableNextColumn();
                            const float fraction = downloadTotal > 0 ? static_cast<float>(node->DownloadSize) / downloadTotal : 0;
                            ImGui::ProgressBar(std::clamp(fraction, 0.0F, 1.0F), ImVec2(-1, ImGui::GetTextLineHeight()));
                            ImGui::TableNextColumn();
                            const char *compressed = Tr("Yes");
                            if (node->Directory) {
                                compressed = Tr("N/A");
                            } else if (node->Stored) {
                                compressed = Tr("No");
                            }
                            ImGui::TextUnformatted(compressed);
                            ImGui::TableNextColumn();
                            ImGui::TextUnformatted(node->Directory || !node->Stored ? Tr("N/A") :
                                node->Alignment >= 16384 ? "16 KB" : node->Alignment >= 4096 ? "4 KB" : Tr("Unaligned"));
                        }
                    }
                }
                ImGui::EndTable();
            }
        }


        void DrawComparison(Context::ApkAnalyzerState &work) {
            if (work.Comparison.valid()) {
                ImGui::TextDisabled("%s", Tr("Comparing APKs..."));
                return;
            }
            if (work.ComparePath.empty()) {
                ImGui::TextWrapped("%s", Tr("Choose a baseline APK to compare with the current APK."));
                return;
            }
            ImGui::TextWrapped("%s: %s", Tr("Baseline APK"), work.ComparePath.c_str());
            if (!work.Compare.Ok) {
                ImGui::TextWrapped("%s", Tr("Could not compare APKs."));
                return;
            }
            if (work.Compare.Value.empty()) {
                ImGui::TextDisabled("%s", Tr("No size differences found."));
                return;
            }
            if (ImGui::BeginTable("ApkComparison", 4, PICKER_TABLE_FLAGS, ImVec2(-1, -1))) {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn(Tr("File"), ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn(Tr("Baseline size"), ImGuiTableColumnFlags_WidthFixed, Em(10));
                ImGui::TableSetupColumn(Tr("Current size"), ImGuiTableColumnFlags_WidthFixed, Em(10));
                ImGui::TableSetupColumn(Tr("Difference"), ImGuiTableColumnFlags_WidthFixed, Em(10));
                ImGui::TableHeadersRow();
                for (const auto &entry : work.Compare.Value) {
                    if (!ContainsIgnoreCase(entry.Path, work.Filter)) continue;
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(entry.Path.c_str());
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(FormatBytes(entry.OldSize).c_str());
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(FormatBytes(entry.NewSize).c_str());
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(FormatBytes(entry.Difference).c_str());
                }
                ImGui::EndTable();
            }
        }

        float ButtonWidth(const char *label) {
            return ImGui::CalcTextSize(Tr(label)).x + ImGui::GetStyle().FramePadding.x * 2;
        }

        void ContinueRow(float nextWidth) {
            const float right = ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x;
            const float availableRight = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
            if (right + nextWidth <= availableRight) ImGui::SameLine();
        }

        void DrawEntry(Context &context) {
            auto &work = context.ApkAnalyzer;
            const bool busy = Busy(work);
            const ImVec2 available = ImGui::GetContentRegionAvail();
            const float width = std::max(1.0F, std::min(Em(38), available.x));
            const float padding = ImGui::GetStyle().WindowPadding.x;
            const float innerWidth = std::max(1.0F, width - padding * 2);
            const char *heading = Tr(busy ? "Analyzing APK..." : "Drop an APK here");
            const char *hint = Tr("Inspect package contents, sizes, and permissions.");
            const float headingHeight = ImGui::CalcTextSize(heading, nullptr, false, innerWidth).y;
            const float hintHeight = ImGui::CalcTextSize(hint, nullptr, false, innerWidth).y;
            const float cardHeight = Eh(5) + headingHeight + hintHeight + ImGui::GetFrameHeight();
            const float recentHeight = busy ? 0.0F : Eh(2.5F) +
                ImGui::GetTextLineHeightWithSpacing() * std::max(1, std::min(4, static_cast<int>(work.RecentApks.size())));
            const float height = cardHeight + recentHeight;
            const ImVec2 origin = ImGui::GetCursorPos();
            ImGui::SetCursorPos(ImVec2(origin.x + std::max(0.0F, (available.x - width) * 0.5F),
                origin.y + std::max(0.0F, (available.y - height) * 0.5F)));
            ImGui::BeginChild("ApkEntry", ImVec2(width, std::min(height, available.y)));

            ImGui::PushStyleColor(ImGuiCol_ChildBg, HexColor(Colors::SURFACE1));
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, ImGui::GetStyle().FrameRounding);
            ImGui::BeginChild("ApkDropZone", ImVec2(0, cardHeight), ImGuiChildFlags_Borders,
                              ImGuiWindowFlags_NoScrollbar);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + Eh(0.6F));
            auto centeredText = [](const char *text, bool muted) {
                const float width = ImGui::GetContentRegionAvail().x;
                const float textWidth = ImGui::CalcTextSize(text).x;
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0F, (width - textWidth) * 0.5F));
                ImGui::PushTextWrapPos(ImGui::GetWindowContentRegionMax().x);
                if (muted) ImGui::TextDisabled("%s", text);
                else ImGui::TextUnformatted(text);
                ImGui::PopTextWrapPos();
            };
            centeredText(Icons::UPLOAD, true);
            ImGui::Spacing();
            centeredText(heading, false);
            centeredText(hint, true);
            ImGui::Spacing();
            if (busy) {
                if (work.Progress) ImGui::ProgressBar(work.Progress->load(), ImVec2(-1, 0));
                const float buttonWidth = ButtonWidth("Cancel");
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0F, (ImGui::GetContentRegionAvail().x - buttonWidth) * 0.5F));
                if (NegativeButton("Cancel", work.Cancel && !work.Cancel->load())) work.Cancel->store(true);
            } else {
                const float buttonWidth = std::min(innerWidth, std::max(Em(12), ButtonWidth("Choose APK...")));
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0F, (ImGui::GetContentRegionAvail().x - buttonWidth) * 0.5F));
                if (PositiveButton("Choose APK...", true, ImVec2(buttonWidth, 0))) {
                    if (auto path = ChooseApk()) StartAnalysis(context, *path);
                }
            }
            ImGui::EndChild();
            ImGui::PopStyleVar();
            ImGui::PopStyleColor();

            if (!busy) {
                ImGui::Spacing();
                ImGui::TextDisabled("%s", Tr("Recent APKs"));
                ImGui::BeginChild("RecentApkList", ImVec2(0, 0));
                if (work.RecentApks.empty()) ImGui::TextDisabled("%s", Tr("No recent APKs"));
                std::string picked, removed;
                for (const auto &path: work.RecentApks) {
                    ImGui::PushID(path.c_str());
                    if (ImGui::Selectable(FileNameOf(path).c_str())) picked = path;
                    HoverTooltip(path);
                    if (ImGui::BeginPopupContextItem("RecentApkMenu")) {
                        if (ImGui::MenuItem(Tr("Remove from recent"))) removed = path;
                        ImGui::EndPopup();
                    }
                    ImGui::PopID();
                }
                if (!removed.empty()) { std::erase(work.RecentApks, removed); PersistAppSettings(context); }
                if (!picked.empty()) StartAnalysis(context, picked);
                ImGui::EndChild();
            }
            ImGui::EndChild();
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
        ConsumeFuture(context.ApkAnalyzer.Comparison);
    }

    void BuildApkAnalyzerWindow(Context &context) {
        auto &work = context.ApkAnalyzer;
        Poll(work);
        if (work.Comparison.valid() && work.Comparison.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            try {
                work.Compare = work.Comparison.get();
                work.Status = work.Cancel->load() ? "Cancelled." : work.Compare.Ok ? "" : "Could not compare APKs.";
                work.Details = work.Cancel->load() || work.Compare.Ok ? "" : work.Compare.Output;
            } catch (const std::exception &e) {
                work.Status = work.Cancel->load() ? "Cancelled." : "Could not compare APKs.";
                work.Details = work.Cancel->load() ? "" : e.what();
            } catch (...) {
                work.Status = "Could not compare APKs.";
            }
        }
        if (!work.Open) return;
        if (!work.PendingDrop.empty() && !Busy(work)) {
            auto path = std::exchange(work.PendingDrop, {});
            StartAnalysis(context, path);
        }

        const auto *viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(Em(1), Eh(0.8F)));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
        ImGui::Begin("###ApkAnalyzer", nullptr, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar);
        ImGui::PopStyleVar(2);

        bool busy = Busy(work);
        if (PrimaryButton("Back", true)) {
            work.Open = false;
            work.PendingDrop.clear();
            if (work.Cancel) work.Cancel->store(true);
        }
        ImGui::SameLine(); ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(Tr("APK Analyzer"));
        if ((work.Report.Files.Ok || work.Report.Summary.Ok) && !work.Report.Cancelled) {
            ContinueRow(ButtonWidth("Choose APK..."));
            if (PrimaryButton("Choose APK...", !busy)) {
                if (auto path = ChooseApk()) StartAnalysis(context, *path);
            }
            ContinueRow(Em(15));
            ImGui::BeginDisabled(Busy(work));
            ImGui::SetNextItemWidth(Em(15));
            if (ImGui::BeginCombo("##RecentApks", Tr("Recent APKs"))) {
                std::string picked;
                for (const auto &path: work.RecentApks) {
                    ImGui::PushID(path.c_str());
                    if (ImGui::Selectable(FileNameOf(path).c_str())) picked = path;
                    HoverTooltip(path);
                    ImGui::PopID();
                }
                if (!picked.empty()) StartAnalysis(context, picked);
                ImGui::EndCombo();
            }
            ImGui::EndDisabled();
            busy = Busy(work);
            ContinueRow(ButtonWidth("Compare APK..."));
            const bool ready = (work.Report.Files.Ok || work.Report.Summary.Ok) && !work.Report.Cancelled;
            if (PrimaryButton("Compare APK...", ready && !busy)) {
                if (auto path = ChooseApk()) {
                    work.ComparePath = *path;
                    work.SelectComparison = true;
                    work.Compare = {};
                    work.Status.clear();
                    work.Details.clear();
                    work.Cancel = std::make_shared<std::atomic<bool>>(false);
                    work.Comparison = std::async(std::launch::async,
                        [sdk = context.Host.Sdk, baseline = *path, current = work.Report.Path, cancel = work.Cancel] {
                            return LoadApkCompare(sdk, baseline, current, [cancel] { return cancel->load(); });
                        });
                }
            }
        }
        const bool ready = (work.Report.Files.Ok || work.Report.Summary.Ok) && !work.Report.Cancelled;
        busy = Busy(work);
        if (busy && ready) {
            ContinueRow(ButtonWidth("Cancel"));
            if (NegativeButton("Cancel", !work.Cancel->load())) work.Cancel->store(true);
        }
        ImGui::Separator();
        if (!work.Path.empty()) {
            ImGui::TextDisabled("%s", FileNameOf(work.Path).c_str());
            HoverTooltip(work.Path);
        }
        if (busy && ready) ImGui::TextDisabled("%s", Tr(work.Analysis.valid() ? "Analyzing APK..." :
                                               work.Comparison.valid() ? "Comparing APKs..." : "Installing APK..."));

        if (!work.Status.empty()) ImGui::TextWrapped("%s", Tr(work.Status.c_str()));
        if (!work.Details.empty() && ImGui::CollapsingHeader(Tr("Details"))) {
            ImGui::BeginChild("ApkErrors", ImVec2(0, Eh(5)), ImGuiChildFlags_Borders);
            ImGui::TextWrapped("%s", work.Details.c_str());
            ImGui::EndChild();
        }
        if (work.Report.ToolMissing) {
            if (PrimaryButton("Open SDK settings", true)) {
                context.UI.ShowPreferences = true;
                context.UI.OpenSdkPreferences = true;
            }
        }

        if (!ready) {
            DrawEntry(context);
            ImGui::End();
            return;
        }

        const auto &report = work.Report;
        if (ready) {
            InfoChip(std::string(Tr("Package name")) + ": " + report.PackageName);
            InfoChip(std::string(Tr("Version name")) + ": " + report.VersionName + " (" + report.VersionCode + ")");
            if (report.FileSize.Ok) InfoChip(std::string(Tr("APK size")) + ": " + FormatBytes(report.FileSize.Value));
            if (report.DownloadSize.Ok) InfoChip(std::string(Tr("Download size")) + ": " + FormatBytes(report.DownloadSize.Value));
            if (report.Debuggable.Ok) InfoChip(std::string(Tr("Debuggable")) + ": " + Tr(report.Debuggable.Value ? "Yes" : "No"));
            ImGui::NewLine();
            ImGui::SetNextItemWidth(std::min(Em(32), ImGui::GetContentRegionAvail().x));
            ImGui::InputTextWithHint("##ApkFilter", Tr("Filter files or permissions..."), work.Filter, sizeof(work.Filter));
            ContinueRow(Em(10));
            ImGui::TextDisabled("%s: %d", Tr("Files"), CachedFiles(report, work.ContentEpoch).FileCount);
        }

        const float footerWidth = ImGui::CalcTextSize(Tr("Target emulator")).x + Em(18) +
            ButtonWidth("Install APK") + ButtonWidth("View app Logcat") + ImGui::GetStyle().ItemSpacing.x * 3;
        const float footerHeight = Eh(footerWidth > ImGui::GetContentRegionAvail().x ? 7.0F : 4.0F);
        ImGui::BeginChild("ApkReport", ImVec2(0, -footerHeight), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
        if (ready && ImGui::BeginTabBar("ApkTabs")) {
            if (ImGui::BeginTabItem(Tr("Files"))) {
                if (report.Files.Ok) {
                    if (report.Files.FromZip) ImGui::TextWrapped("%s", Tr("Using ZIP file sizes; nested archives are shown as files."));
                    auto &cache = CachedFiles(report, work.ContentEpoch);
                    DrawFileTree(context, cache);
                } else {
                    ImGui::TextWrapped("%s", Tr("File listing failed. Other APK information remains available."));
                    ImGui::TextWrapped("%s", report.Files.Output.c_str());
                }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(Tr("Application"))) {
                if (ImGui::BeginTable("ApkMetadata", 2, ImGuiTableFlags_SizingStretchProp)) {
                    ImGui::TableSetupColumn("Key", 0, 1);
                    ImGui::TableSetupColumn("Value", 0, 3);
                    Field("Application", report.Label);
                    Field("Package name", report.PackageName);
                    Field("Version name", report.VersionName);
                    Field("Version code", report.VersionCode);
                    Field("Minimum Android API", report.MinSdk);
                    Field("Target Android API", report.TargetSdk);
                    std::string abis;
                    for (const auto &abi: report.Abis) { if (!abis.empty()) abis += ", "; abis += abi; }
                    Field("Native architectures", abis);
                    ImGui::EndTable();
                }
                ImGui::EndTabItem();
            }
            const std::string permissions = std::string(Tr("Permissions")) + " (" + std::to_string(report.Permissions.size()) + ")";
            if (ImGui::BeginTabItem(permissions.c_str())) {
                ImGui::BeginChild("Permissions");
                if (report.Permissions.empty()) ImGui::TextDisabled("%s", Tr("No permission metadata available."));
                for (const auto &permission: report.Permissions)
                    if (ContainsIgnoreCase(permission, work.Filter)) ImGui::TextUnformatted(permission.c_str());
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(Tr("Comparison"), nullptr, work.SelectComparison ? ImGuiTabItemFlags_SetSelected : 0)) {
                DrawComparison(work);
                ImGui::EndTabItem();
            }
            work.SelectComparison = false;
            ImGui::EndTabBar();
        }
        ImGui::EndChild();
        ImGui::Separator();

        std::string name;
        const int selected = context.Catalog.SelectedAvd;
        if (selected >= 0 && selected < static_cast<int>(context.Catalog.Avds.size())) name = context.Catalog.Avds[selected].Name;
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(Tr("Target emulator"));
        ContinueRow(Em(18));
        ImGui::SetNextItemWidth(std::min(Em(18), ImGui::GetContentRegionAvail().x));
        if (ImGui::BeginCombo("##TargetEmulator", name.empty() ? Tr("None") : name.c_str())) {
            for (int i = 0; i < static_cast<int>(context.Catalog.Avds.size()); ++i) {
                const auto &avd = context.Catalog.Avds[i];
                if (!context.Host.Manager.IsRunning(avd.Name)) continue;
                if (ImGui::Selectable(avd.Name.c_str(), selected == i)) {
                    context.Catalog.SelectedAvd = i;
                    name = avd.Name;
                }
            }
            ImGui::EndCombo();
        }
        const auto serial = name.empty() ? "" : EmulatorSerialForConsolePort(context.Host.Manager.GetConsolePort(name));
        const bool online = !name.empty() && context.Host.Manager.IsRunning(name) && !serial.empty() && HasAdb(context.Host.Sdk);
        const bool enabled = ready && !report.PackageName.empty() && online && !Busy(work);
        ContinueRow(ButtonWidth("Install APK"));
        if (PrimaryButton("Install APK", enabled)) {
            work.Status.clear();
            work.Details.clear();
            work.Cancel = std::make_shared<std::atomic<bool>>(false);
            work.Installation = std::async(std::launch::async,
                [sdk = context.Host.Sdk, serial, path = work.Report.Path, cancel = work.Cancel] {
                    return InstallApk(sdk, serial, path, [cancel] { return cancel->load(); });
                });
        }
        ContinueRow(ButtonWidth("View app Logcat"));
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

        if (!online) ImGui::TextWrapped("%s", Tr(!HasAdb(context.Host.Sdk) ? "ADB is unavailable. Check SDK settings." :
            name.empty() ? "Select a running emulator." : "The selected emulator is not running."));
        ImGui::End();
    }
}
