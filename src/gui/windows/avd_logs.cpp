//
// Created by AbdulMuaz Aqeel on 15/04/2026.
//

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "imgui.h"
#include "imgui_internal.h"

#include "avd_logs.h"
#include "../context.h"
#include "../localization.h"
#include "../theme.h"
#include "../widgets.h"
#include "../../core/file_dialog.h"
#include "../../core/log_filter.h"

namespace CoreDeck {
    namespace {
        constexpr auto LOGCAT_RETRY_DELAY = std::chrono::milliseconds(1500);

        struct PanelInputs {
            std::shared_ptr<LogBuffer> EmulatorLog;
            std::string AvdName;
            std::string DisplayName;
            std::string Serial;
            bool HasSelection = false;
            bool IsRunning = false;
        };

        struct EmulatorPanelView {
            LogFilterResult Filter;
            std::string Placeholder;
            bool HasContent = false;
            std::size_t LineCount = 0;
        };

        struct SyncSelection {
            bool Active = false;
            int Start = 0;
            int End = 0;
        };

        struct PriorityOption {
            LogcatPriority Value;
            const char *Label;
        };

        struct BufferOption {
            LogcatBuffer Value;
            const char *Label;
        };

        constexpr PriorityOption PRIORITY_OPTIONS[] = {
            {LogcatPriority::Verbose, "Verbose+"},
            {LogcatPriority::Debug, "Debug+"},
            {LogcatPriority::Info, "Info+"},
            {LogcatPriority::Warning, "Warning+"},
            {LogcatPriority::Error, "Error+"},
            {LogcatPriority::Fatal, "Fatal"},
        };

        constexpr BufferOption BUFFER_OPTIONS[] = {
            {LogcatBuffer::Main, "Main"},
            {LogcatBuffer::System, "System"},
            {LogcatBuffer::Crash, "Crash"},
            {LogcatBuffer::All, "All buffers"},
        };

        PanelInputs ResolveInputs(Context &context) {
            PanelInputs inputs;
            const int selected = context.Catalog.SelectedAvd;
            if (selected < 0 || selected >= static_cast<int>(context.Catalog.Avds.size())) {
                return inputs;
            }

            const AvdInfo &avd = context.Catalog.Avds[selected];
            inputs.HasSelection = true;
            inputs.AvdName = avd.Name;
            inputs.DisplayName = avd.DisplayName.empty() ? avd.Name : avd.DisplayName;
            inputs.IsRunning = context.Host.Manager.IsRunning(avd.Name);
            inputs.EmulatorLog = context.Host.Manager.GetLog(avd.Name);
            inputs.Serial = EmulatorSerialForConsolePort(context.Host.Manager.GetConsolePort(avd.Name));
            return inputs;
        }

        Context::LogViewState &ResolveEmulatorViewState(Context &context, const std::string &avdName) {
            return context.Logs.PerAvdView[avdName];
        }

        Context::LogcatViewState &ResolveLogcatViewState(Context &context, const std::string &avdName) {
            return context.Logs.PerAvdLogcatView[avdName];
        }

        EmulatorPanelView BuildEmulatorView(const PanelInputs &inputs, const Context::LogViewState &state) {
            EmulatorPanelView view;
            if (!inputs.HasSelection) {
                view.Placeholder = Tr("Select an AVD to view logs");
                return view;
            }
            if (!inputs.EmulatorLog) {
                char buffer[256];
                std::snprintf(buffer, sizeof(buffer), Tr("Run the \"%s\" AVD to view logs"), inputs.AvdName.c_str());
                view.Placeholder = buffer;
                return view;
            }

            const auto lines = inputs.EmulatorLog->GetLines();
            view.LineCount = lines.size();
            LogFilterOptions options;
            options.Query = state.Search;
            options.UseRegex = state.UseRegex;
            view.Filter = FilterLog(lines, options);
            view.HasContent = !view.Filter.Joined.empty();
            if (!view.HasContent) {
                view.Placeholder = lines.empty() ? Tr("No available logs to view") : Tr("No matching log entries found");
            }
            return view;
        }

        bool RenderEmulatorToolbarButtons(const PanelInputs &inputs, const bool hasContent) {
            const bool disabled = !inputs.EmulatorLog;
            if (disabled) {
                ImGui::BeginDisabled();
            }
            if (PrimaryButton(Icons::TRASH)) {
                inputs.EmulatorLog->Clear();
            }
            if (disabled) {
                ImGui::EndDisabled();
            }
            ImGui::SameLine();

            const bool canCopy = inputs.EmulatorLog && hasContent;
            if (!canCopy) {
                ImGui::BeginDisabled();
            }
            const bool copyClicked = PrimaryButton(Icons::COPY);
            if (!canCopy) {
                ImGui::EndDisabled();
            }
            ImGui::SameLine();
            return copyClicked;
        }

        bool RenderEmulatorSearchBar(
            Context::LogViewState &state,
            const EmulatorPanelView &view,
            const int matchCount,
            bool &queryChanged
        ) {
            queryChanged = false;
            bool navChanged = false;

            const float squareButtonSize = ImGui::GetFrameHeight();
            const float searchWidth = Em(29.0F);
            const bool hasQuery = !state.Search.empty();
            const bool regexInvalid = state.UseRegex && hasQuery && !view.Filter.RegexValid;
            const int displayedIndex = matchCount > 0 ? state.ActiveMatchIndex + 1 : 0;
            const std::string counter = !hasQuery ? "0 / 0" : (regexInvalid ? "—" : std::to_string(displayedIndex) + " / " + std::to_string(matchCount));

            const ImGuiStyle &style = ImGui::GetStyle();
            const float counterWidth = ImGui::CalcTextSize(counter.c_str()).x;
            const float fixedWidth = (squareButtonSize * 3.0F) + counterWidth + (style.ItemSpacing.x * 4.0F);
            const float contentMaxX = ImGui::GetWindowContentRegionMax().x;
            float startX = ImGui::GetCursorPosX();
            if ((contentMaxX - startX) < fixedWidth + Em(10.0F)) {
                ImGui::NewLine();
                startX = ImGui::GetWindowContentRegionMin().x;
            }
            const float availableSearchWidth = std::max(Em(8.0F), contentMaxX - startX - fixedWidth);
            const float resolvedSearchWidth = std::min(searchWidth, availableSearchWidth);
            ImGui::SetCursorPosX(std::max(startX, contentMaxX - fixedWidth - resolvedSearchWidth));

            if (ToggleButton(".*##EmulatorRegexToggle", state.UseRegex, ImVec2(squareButtonSize, squareButtonSize))) {
                queryChanged = true;
            }
            ImGui::SameLine();

            char searchBuffer[256];
            std::strncpy(searchBuffer, state.Search.c_str(), sizeof(searchBuffer) - 1);
            searchBuffer[sizeof(searchBuffer) - 1] = '\0';
            const std::string hint = IconWithLabel(Icons::SEARCH, state.UseRegex ? "Regex" : "Search logs...");
            ImGui::SetNextItemWidth(resolvedSearchWidth);
            if (regexInvalid) {
                ImGui::PushStyleColor(ImGuiCol_Border, HexColor(Colors::NEGATIVE));
                ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0F);
            }
            const bool edited = ImGui::InputTextWithHint("##EmulatorLogSearch", hint.c_str(), searchBuffer, sizeof(searchBuffer));
            const bool enterPressed = ImGui::IsItemDeactivatedAfterEdit() && ImGui::IsKeyPressed(ImGuiKey_Enter, false);
            if (regexInvalid) {
                ImGui::PopStyleVar();
                ImGui::PopStyleColor();
            }
            if (regexInvalid && ImGui::IsItemHovered()) {
                ImGui::SetTooltip(Tr("Invalid regex: %s"), view.Filter.RegexError.c_str());
            }
            if (edited) {
                state.Search = searchBuffer;
                queryChanged = true;
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%s", counter.c_str());
            ImGui::SameLine();

            const bool canNavigate = matchCount > 0;
            if (!canNavigate) {
                ImGui::BeginDisabled();
            }
            if (ImGui::Button((std::string{Icons::CHEVRON_LEFT} + "##EmulatorLogPrev").c_str(), ImVec2(squareButtonSize, squareButtonSize))) {
                state.ActiveMatchIndex = (state.ActiveMatchIndex - 1 + matchCount) % matchCount;
                navChanged = true;
            }
            ImGui::SameLine();
            if (ImGui::Button((std::string{Icons::CHEVRON_RIGHT} + "##EmulatorLogNext").c_str(), ImVec2(squareButtonSize, squareButtonSize))) {
                state.ActiveMatchIndex = (state.ActiveMatchIndex + 1) % matchCount;
                navChanged = true;
            }
            if (!canNavigate) {
                ImGui::EndDisabled();
            }
            if (canNavigate && enterPressed) {
                state.ActiveMatchIndex = (state.ActiveMatchIndex + 1) % matchCount;
                navChanged = true;
            }
            return navChanged;
        }

        std::size_t LineIndexFor(const std::string &joined, const std::size_t offset) {
            std::size_t line = 0;
            const std::size_t end = std::min(offset, joined.size());
            for (std::size_t i = 0; i < end; ++i) {
                if (joined[i] == '\n') {
                    ++line;
                }
            }
            return line;
        }

        int CallbackSetSelection(ImGuiInputTextCallbackData *data) {
            const auto *selection = static_cast<SyncSelection *>(data->UserData);
            if (selection && selection->Active) {
                data->CursorPos = selection->End;
                data->SelectionStart = selection->Start;
                data->SelectionEnd = selection->End;
            }
            return 0;
        }

        bool ApplyScrollToLine(const int lineIndex, ImGuiWindow *window) {
            if (lineIndex < 0 || !window) {
                return false;
            }
            const float lineHeight = ImGui::GetTextLineHeight();
            const float regionHeight = window->InnerRect.GetHeight();
            window->Scroll.y = std::max(0.0F, (static_cast<float>(lineIndex) * lineHeight) - (regionHeight * 0.3F));
            return true;
        }

        ImVec2 ResolveEmulatorLogContentSize(const std::string &display) {
            const ImGuiStyle &style = ImGui::GetStyle();
            const ImVec2 available = ImGui::GetContentRegionAvail();
            const char *textStart = display.data();
            const char *textEnd = textStart + display.size();
            const char *lineStart = textStart;
            float maxLineWidth = 0.0F;
            int lineCount = 1;
            for (const char *cursor = textStart; cursor < textEnd; ++cursor) {
                if (*cursor != '\n') {
                    continue;
                }
                maxLineWidth = std::max(maxLineWidth, ImGui::CalcTextSize(lineStart, cursor, false).x);
                lineStart = cursor + 1;
                ++lineCount;
            }
            maxLineWidth = std::max(maxLineWidth, ImGui::CalcTextSize(lineStart, textEnd, false).x);
            return {
                std::max(available.x, maxLineWidth + (style.FramePadding.x * 2.0F) + style.ScrollbarSize),
                std::max(available.y, (static_cast<float>(lineCount) * ImGui::GetTextLineHeight()) + (style.FramePadding.y * 2.0F) + style.ScrollbarSize)
            };
        }

        void DrawEmptyLogBody(const char *id, const std::string &message, const bool isError = false) {
            const float footerHeight = ImGui::GetFrameHeightWithSpacing();
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
            if (ImGui::BeginChild(
                    id,
                    ImVec2(0, -footerHeight),
                    ImGuiChildFlags_Borders,
                    ImGuiWindowFlags_NoScrollbar
                )) {
                const ImVec2 available = ImGui::GetContentRegionAvail();
                const ImVec2 textSize = ImGui::CalcTextSize(message.c_str());
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0F, (available.x - textSize.x) * 0.5F));
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + std::max(0.0F, (available.y - textSize.y) * 0.45F));
                if (isError) {
                    ImGui::TextColored(HexColor(Colors::NEGATIVE), "%s", message.c_str());
                } else {
                    ImGui::TextDisabled("%s", message.c_str());
                }
            }
            ImGui::EndChild();
            ImGui::PopStyleColor();
        }

        bool RenderEmulatorLogBody(
            const PanelInputs &inputs,
            const Context::LogViewState &state,
            const EmulatorPanelView &view,
            const SyncSelection &sync,
            const bool focusLog,
            const int scrollLine,
            const bool hasQuery
        ) {
            if (!view.HasContent) {
                DrawEmptyLogBody("##EmulatorLogEmpty", view.Placeholder);
                return true;
            }

            const std::string &display = view.Filter.Joined;
            std::vector<char> buffer(display.begin(), display.end());
            buffer.push_back('\0');

            ImGuiInputTextFlags flags = ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_NoUndoRedo;
            if (sync.Active) {
                flags |= ImGuiInputTextFlags_CallbackAlways;
            }

            const ImVec2 contentSize = ResolveEmulatorLogContentSize(display);
            ImGui::SetNextWindowContentSize(contentSize);
            ImGui::BeginChild(
                "##EmulatorLogText",
                ImVec2(0, -ImGui::GetFrameHeightWithSpacing()),
                ImGuiChildFlags_None,
                ImGuiWindowFlags_HorizontalScrollbar
            );
            ImGuiWindow *logWindow = ImGui::GetCurrentWindow();
            bool scrollApplied = true;
            if (scrollLine >= 0) {
                scrollApplied = ApplyScrollToLine(scrollLine, logWindow);
            }

            ImGui::PushStyleColor(ImGuiCol_TextSelectedBg, HexColor(Colors::ACCENT_INFO, 0.55F));
            if (focusLog) {
                ImGui::SetKeyboardFocusHere();
            }
            ImGui::InputTextMultiline(
                "##EmulatorLogTextInput",
                buffer.data(),
                buffer.size(),
                contentSize,
                flags,
                sync.Active ? CallbackSetSelection : nullptr,
                sync.Active ? const_cast<SyncSelection *>(&sync) : nullptr // NOLINT(cppcoreguidelines-pro-type-const-cast)
            );
            ImGui::PopStyleColor();

            if (scrollLine < 0 && inputs.EmulatorLog && state.AutoScroll && !hasQuery && view.HasContent && inputs.EmulatorLog->HasNewContent()) {
                logWindow->Scroll.y = logWindow->ScrollMax.y;
                inputs.EmulatorLog->ResetNewContentFlag();
            }
            ImGui::EndChild();
            return scrollApplied;
        }

        void DrawAutoScrollFooter(const std::string &status, bool &autoScroll, const char *id) {
            ImGui::TextDisabled("%s", status.c_str());
            const std::string visibleLabel = Tr("Auto-scroll");
            const std::string checkboxLabel = visibleLabel + "###" + id;
            const float checkboxWidth = ImGui::CalcTextSize(visibleLabel.c_str()).x + ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x;
            ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - checkboxWidth));
            ImGui::Checkbox(checkboxLabel.c_str(), &autoScroll);
        }

        void DrawEmulatorLogPanel(Context &context, const PanelInputs &inputs) {
            Context::LogViewState scratch{};
            Context::LogViewState &state = inputs.HasSelection ? ResolveEmulatorViewState(context, inputs.AvdName) : scratch;
            EmulatorPanelView view = BuildEmulatorView(inputs, state);
            int matchCount = static_cast<int>(view.Filter.Matches.size());
            state.ActiveMatchIndex = std::clamp(state.ActiveMatchIndex, 0, std::max(0, matchCount - 1));

            const bool copyClicked = RenderEmulatorToolbarButtons(inputs, view.HasContent);
            bool queryChanged = false;
            const bool navChanged = RenderEmulatorSearchBar(state, view, matchCount, queryChanged);
            if (queryChanged) {
                state.ActiveMatchIndex = 0;
                view = BuildEmulatorView(inputs, state);
                matchCount = static_cast<int>(view.Filter.Matches.size());
                context.Logs.PendingScroll = matchCount > 0;
                context.Logs.PendingSyncFrames = matchCount > 0 ? 2 : 0;
            }
            if (navChanged) {
                context.Logs.PendingScroll = true;
                context.Logs.PendingFocus = true;
                context.Logs.PendingSyncFrames = 2;
            }
            if (copyClicked && view.HasContent) {
                ImGui::SetClipboardText(view.Filter.Joined.c_str());
            }

            SyncSelection sync;
            int scrollLine = -1;
            const bool hasActiveMatch = !view.Filter.Matches.empty() && state.ActiveMatchIndex < static_cast<int>(view.Filter.Matches.size());
            if (hasActiveMatch && context.Logs.PendingSyncFrames > 0) {
                const auto &[startOffset, endOffset] = view.Filter.Matches[state.ActiveMatchIndex];
                sync = {.Active = true, .Start = static_cast<int>(startOffset), .End = static_cast<int>(endOffset)};
            }
            if (hasActiveMatch && context.Logs.PendingScroll) {
                scrollLine = static_cast<int>(LineIndexFor(view.Filter.Joined, view.Filter.Matches[state.ActiveMatchIndex].StartOffset));
            }

            const bool scrollApplied = RenderEmulatorLogBody(
                inputs,
                state,
                view,
                sync,
                context.Logs.PendingFocus && hasActiveMatch,
                scrollLine,
                !state.Search.empty()
            );
            DrawAutoScrollFooter(
                std::to_string(view.LineCount) + " " + Tr("lines") + " · " + Tr("Emulator"),
                state.AutoScroll,
                "EmulatorAutoScroll"
            );
            if (scrollApplied) {
                context.Logs.PendingScroll = false;
            }
            context.Logs.PendingFocus = false;
            if (context.Logs.PendingSyncFrames > 0) {
                --context.Logs.PendingSyncFrames;
            }
        }

        const char *PriorityOptionLabel(const LogcatPriority priority) {
            for (const auto &option: PRIORITY_OPTIONS) {
                if (option.Value == priority) {
                    return option.Label;
                }
            }
            return "Debug+";
        }

        const char *BufferOptionLabel(const LogcatBuffer buffer) {
            for (const auto &option: BUFFER_OPTIONS) {
                if (option.Value == buffer) {
                    return option.Label;
                }
            }
            return "Main";
        }

        ImVec4 PriorityColor(const LogcatPriority priority) {
            switch (priority) {
                case LogcatPriority::Info: return HexColor(Colors::POSITIVE);
                case LogcatPriority::Warning: return HexColor(Colors::WARNING_STRONG);
                case LogcatPriority::Error:
                case LogcatPriority::Fatal: return HexColor(Colors::NEGATIVE);
                case LogcatPriority::Verbose: return HexColor(Colors::TEXT_SUBTLE);
                case LogcatPriority::Debug:
                case LogcatPriority::Unknown: return ImGui::GetStyleColorVec4(ImGuiCol_Text);
            }
            return ImGui::GetStyleColorVec4(ImGuiCol_Text);
        }

        std::string ProcessLabel(const int pid, const std::vector<LogcatProcess> &processes) {
            if (pid <= 0) {
                return Tr("All processes");
            }
            const auto it = std::ranges::find_if(processes, [pid](const LogcatProcess &process) {
                return process.Pid == pid;
            });
            if (it == processes.end()) {
                return "PID " + std::to_string(pid);
            }
            if (it->Name.empty()) {
                return "PID " + std::to_string(pid);
            }
            return it->Name + " · " + std::to_string(it->Pid);
        }

        std::vector<LogcatProcess> BuildVisibleProcessList(
            const std::vector<LogcatProcess> &knownProcesses,
            const std::vector<LogcatEntry> &entries
        ) {
            std::unordered_map<int, std::string> namesByPid;
            namesByPid.reserve(knownProcesses.size());
            for (const auto &process: knownProcesses) {
                namesByPid[process.Pid] = process.Name;
            }

            std::vector<LogcatProcess> result;
            std::unordered_set<int> includedPids;
            for (const auto &entry: entries) {
                if (entry.Pid <= 0 || !includedPids.insert(entry.Pid).second) {
                    continue;
                }
                result.push_back({.Pid = entry.Pid, .Name = namesByPid[entry.Pid]});
            }
            std::ranges::sort(result, [](const LogcatProcess &left, const LogcatProcess &right) {
                if (left.Name.empty() != right.Name.empty()) {
                    return !left.Name.empty();
                }
                if (left.Name == right.Name) {
                    return left.Pid < right.Pid;
                }
                return left.Name < right.Name;
            });
            return result;
        }

        void StopLogcatForInvalidTarget(Context &context, const PanelInputs &inputs) {
            const LogcatStreamStatus status = context.Host.Logcat.Status();
            if ((!status.Running && !status.Connecting) || status.AvdName.empty()) {
                return;
            }
            if (!inputs.HasSelection || !inputs.IsRunning || inputs.Serial.empty() || status.AvdName != inputs.AvdName) {
                context.Host.Logcat.Stop();
            }
        }

        void EnsureLogcatStream(Context &context, const PanelInputs &inputs, Context::LogcatViewState &state) {
            if (!inputs.HasSelection || !inputs.IsRunning || inputs.Serial.empty() || context.Host.Sdk.AdbPath.empty()) {
                return;
            }

            const bool matches = context.Host.Logcat.Matches(
                context.Host.Sdk.AdbPath,
                inputs.AvdName,
                inputs.Serial,
                state.Buffer
            );
            const LogcatStreamStatus status = context.Host.Logcat.Status();
            if (matches && (status.Running || status.Connecting)) {
                return;
            }

            const auto now = std::chrono::steady_clock::now();
            if (matches && now - state.LastStartAttempt < LOGCAT_RETRY_DELAY) {
                return;
            }

            state.LastStartAttempt = now;
            if (!matches) {
                state.CachedEntries.clear();
                state.CachedRevision = std::numeric_limits<std::uint64_t>::max();
                state.SelectedPid = 0;
            }
            context.Host.Logcat.Start(context.Host.Sdk, inputs.AvdName, inputs.Serial, state.Buffer);
        }

        bool RefreshLogcatCache(Context &context, Context::LogcatViewState &state) {
            if (state.Paused) {
                return false;
            }
            const std::uint64_t revision = context.Host.Logcat.Revision();
            if (revision == state.CachedRevision) {
                return false;
            }
            state.CachedEntries = context.Host.Logcat.Entries();
            state.CachedRevision = revision;
            return true;
        }

        const LogcatFilterResult &ResolveLogcatFilter(Context::LogcatViewState &state) {
            const bool cacheMatches =
                state.FilteredRevision == state.CachedRevision &&
                state.FilteredMinimumPriority == state.MinimumPriority &&
                state.FilteredPid == state.SelectedPid &&
                state.FilteredSearch == state.Search &&
                state.FilteredUseRegex == state.UseRegex;
            if (cacheMatches) {
                return state.CachedFilter;
            }

            LogcatFilterOptions options;
            options.MinimumPriority = state.MinimumPriority;
            options.Pid = state.SelectedPid;
            options.Query = state.Search;
            options.UseRegex = state.UseRegex;
            state.CachedFilter = FilterLogcatEntries(state.CachedEntries, options);
            state.FilteredRevision = state.CachedRevision;
            state.FilteredMinimumPriority = state.MinimumPriority;
            state.FilteredPid = state.SelectedPid;
            state.FilteredSearch = state.Search;
            state.FilteredUseRegex = state.UseRegex;
            return state.CachedFilter;
        }

        void DrawPriorityCombo(Context::LogcatViewState &state) {
            ImGui::SetNextItemWidth(Em(10.0F));
            ComboStyle style;
            if (ImGui::BeginCombo("##LogcatPriority", Tr(PriorityOptionLabel(state.MinimumPriority)))) {
                for (const auto &option: PRIORITY_OPTIONS) {
                    const bool selected = option.Value == state.MinimumPriority;
                    if (RoundedSelectable(Tr(option.Label), selected)) {
                        state.MinimumPriority = option.Value;
                    }
                    if (selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", Tr("Minimum log level"));
            }
        }

        void DrawProcessCombo(Context::LogcatViewState &state, const std::vector<LogcatProcess> &processes) {
            const std::string preview = ProcessLabel(state.SelectedPid, processes);
            ImGui::SetNextItemWidth(Em(19.0F));
            ImGui::SetNextWindowSizeConstraints(ImVec2(Em(18.0F), 0), ImVec2(Em(36.0F), Eh(20.0F)));
            ComboStyle style;
            if (ImGui::BeginCombo("##LogcatProcess", preview.c_str())) {
                if (RoundedSelectable(Tr("All processes"), state.SelectedPid == 0)) {
                    state.SelectedPid = 0;
                }
                for (const auto &process: processes) {
                    const std::string label = process.Name + " · " + std::to_string(process.Pid);
                    if (RoundedSelectable(label.c_str(), state.SelectedPid == process.Pid)) {
                        state.SelectedPid = process.Pid;
                    }
                }
                ImGui::EndCombo();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", Tr("Application or process"));
            }
        }

        bool DrawBufferCombo(Context::LogcatViewState &state) {
            bool changed = false;
            ImGui::SetNextItemWidth(Em(11.0F));
            ComboStyle style;
            if (ImGui::BeginCombo("##LogcatBuffer", Tr(BufferOptionLabel(state.Buffer)))) {
                for (const auto &option: BUFFER_OPTIONS) {
                    const bool selected = option.Value == state.Buffer;
                    if (RoundedSelectable(Tr(option.Label), selected)) {
                        state.Buffer = option.Value;
                        changed = true;
                    }
                    if (selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", Tr("Log buffer"));
            }
            return changed;
        }

        void ExportLogcat(
            Context::LogcatViewState &state,
            const PanelInputs &inputs,
            const LogcatFilterResult &filtered
        ) {
            static const char *filters[] = {"*.txt", "*.log"};
            const std::string defaultName = inputs.AvdName + "-logcat.txt";
            const auto selectedPath = FileDialog::SaveFile(Tr("Export Logcat"), filters, 2, Tr("Log files"), defaultName);
            if (!selectedPath) {
                return;
            }

            std::ofstream output(*selectedPath, std::ios::binary | std::ios::trunc);
            if (!output) {
                state.ExportStatus = Tr("Could not export Logcat.");
                state.ExportSucceeded = false;
                return;
            }
            for (const std::size_t index: filtered.Indices) {
                output << state.CachedEntries[index].Raw << '\n';
            }
            state.ExportSucceeded = output.good();
            state.ExportStatus = Tr(state.ExportSucceeded ? "Logcat exported." : "Could not export Logcat.");
        }

        LogcatFilterResult DrawLogcatToolbar(
            Context &context,
            const PanelInputs &inputs,
            Context::LogcatViewState &state,
            const std::vector<LogcatProcess> &processes,
            const bool enabled
        ) {
            if (!enabled) {
                ImGui::BeginDisabled();
            }
            DrawPriorityCombo(state);
            ImGui::SameLine();
            DrawProcessCombo(state, processes);
            ImGui::SameLine();
            if (DrawBufferCombo(state)) {
                state.LastStartAttempt = {};
                state.CachedEntries.clear();
                state.CachedRevision = std::numeric_limits<std::uint64_t>::max();
                state.SelectedPid = 0;
                state.Paused = false;
                context.Host.Logcat.Start(context.Host.Sdk, inputs.AvdName, inputs.Serial, state.Buffer);
            }

            ImGui::Spacing();

            const float squareButtonSize = ImGui::GetFrameHeight();
            ToggleButton(".*##LogcatRegexToggle", state.UseRegex, ImVec2(squareButtonSize, squareButtonSize));
            ImGui::SameLine();

            char searchBuffer[256];
            std::strncpy(searchBuffer, state.Search.c_str(), sizeof(searchBuffer) - 1);
            searchBuffer[sizeof(searchBuffer) - 1] = '\0';
            ImGui::SetNextItemWidth(std::max(Em(12.0F), ImGui::GetContentRegionAvail().x - Em(26.0F)));
            ImGui::InputTextWithHint("##LogcatSearch", Tr("Filter tag or message..."), searchBuffer, sizeof(searchBuffer));
            state.Search = searchBuffer;

            LogcatFilterResult filtered = ResolveLogcatFilter(state);
            if (!filtered.RegexValid && ImGui::IsItemHovered()) {
                ImGui::SetTooltip(Tr("Invalid regex: %s"), filtered.RegexError.c_str());
            }

            ImGui::SameLine();
            const std::string pauseLabel = state.Paused ? Tr("Resume") : Tr("Pause");
            if (PrimaryButton(pauseLabel.c_str())) {
                state.Paused = !state.Paused;
                if (!state.Paused) {
                    state.CachedRevision = std::numeric_limits<std::uint64_t>::max();
                }
            }
            ImGui::SameLine();
            if (PrimaryButton(IconWithLabel(Icons::TRASH, "Clear").c_str())) {
                context.Host.Logcat.Clear();
                state.CachedEntries.clear();
                state.CachedRevision = context.Host.Logcat.Revision();
                filtered.Indices.clear();
                state.ExportStatus.clear();
            }
            ImGui::SameLine();
            const bool canExport = filtered.RegexValid && !filtered.Indices.empty();
            if (!canExport) {
                ImGui::BeginDisabled();
            }
            if (PrimaryButton(IconWithLabel(Icons::DOWNLOAD, "Export").c_str())) {
                ExportLogcat(state, inputs, filtered);
            }
            if (!canExport) {
                ImGui::EndDisabled();
            }
            if (!enabled) {
                ImGui::EndDisabled();
            }
            return filtered;
        }

        void DrawLogcatEntryRow(const LogcatEntry &entry) {
            ImGui::TableNextRow();
            if (entry.Priority == LogcatPriority::Unknown) {
                ImGui::TableSetColumnIndex(4);
                ImGui::TextDisabled("%s", entry.Raw.c_str());
                return;
            }

            ImGui::TableSetColumnIndex(0);
            const std::string timestamp = entry.Timestamp.size() > 6 ? entry.Timestamp.substr(6) : entry.Timestamp;
            ImGui::TextDisabled("%s", timestamp.c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::TextColored(PriorityColor(entry.Priority), "%s", LogcatPriorityLabel(entry.Priority));
            ImGui::TableSetColumnIndex(2);
            ImGui::TextDisabled("%d", entry.Pid);
            ImGui::TableSetColumnIndex(3);
            ImGui::TextDisabled("%s", entry.Tag.c_str());
            ImGui::TableSetColumnIndex(4);
            ImGui::TextUnformatted(entry.Message.c_str());
        }

        void DrawLogcatBody(
            Context::LogcatViewState &state,
            const LogcatFilterResult &filtered,
            const bool receivedNewEntries
        ) {
            if (!filtered.RegexValid) {
                ImGui::TextColored(HexColor(Colors::NEGATIVE), Tr("Invalid regex: %s"), filtered.RegexError.c_str());
            }

            const ImGuiTableFlags flags =
                ImGuiTableFlags_RowBg |
                ImGuiTableFlags_Resizable |
                ImGuiTableFlags_ScrollX |
                ImGuiTableFlags_ScrollY |
                ImGuiTableFlags_SizingFixedFit;
            const float footerHeight = ImGui::GetFrameHeightWithSpacing();
            if (ImGui::BeginTable("##LogcatEntries", 5, flags, ImVec2(0, -footerHeight))) {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn(Tr("Time"), ImGuiTableColumnFlags_WidthFixed, Em(10.5F));
                ImGui::TableSetupColumn(Tr("Level"), ImGuiTableColumnFlags_WidthFixed, Em(2.5F));
                ImGui::TableSetupColumn("PID", ImGuiTableColumnFlags_WidthFixed, Em(5.0F));
                ImGui::TableSetupColumn("Tag", ImGuiTableColumnFlags_WidthFixed, Em(15.0F));
                ImGui::TableSetupColumn(Tr("Message"), ImGuiTableColumnFlags_WidthStretch, Em(30.0F));
                ImGui::TableHeadersRow();

                ImGuiListClipper clipper;
                clipper.Begin(static_cast<int>(filtered.Indices.size()));
                while (clipper.Step()) {
                    for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                        DrawLogcatEntryRow(state.CachedEntries[filtered.Indices[row]]);
                    }
                }
                if (receivedNewEntries && state.AutoScroll && !state.Paused && state.Search.empty()) {
                    ImGui::SetScrollY(ImGui::GetScrollMaxY());
                }
                ImGui::EndTable();
            }
        }

        void DrawLogcatFooter(Context::LogcatViewState &state, const LogcatFilterResult &filtered) {
            const std::string status =
                std::to_string(filtered.Indices.size()) + " " + Tr("lines") +
                " · threadtime · " + Tr(BufferOptionLabel(state.Buffer)) +
                (state.Paused ? Tr(" · Paused") : "");
            DrawAutoScrollFooter(status, state.AutoScroll, "LogcatAutoScroll");
        }

        void DrawLogcatPanel(Context &context, const PanelInputs &inputs) {
            Context::LogcatViewState scratch{};
            Context::LogcatViewState &state = inputs.HasSelection ? ResolveLogcatViewState(context, inputs.AvdName) : scratch;
            const bool targetReady =
                inputs.HasSelection &&
                inputs.IsRunning &&
                !inputs.Serial.empty() &&
                !context.Host.Sdk.AdbPath.empty();

            if (targetReady) {
                EnsureLogcatStream(context, inputs, state);
            }
            const bool receivedNewEntries = targetReady && RefreshLogcatCache(context, state);
            const LogcatStreamStatus status = context.Host.Logcat.Status();
            const std::vector<LogcatProcess> processes = BuildVisibleProcessList(
                context.Host.Logcat.Processes(),
                state.CachedEntries
            );

            const LogcatFilterResult filtered = DrawLogcatToolbar(context, inputs, state, processes, targetReady);
            if (!state.ExportStatus.empty()) {
                ImGui::TextColored(
                    HexColor(state.ExportSucceeded ? Colors::POSITIVE : Colors::NEGATIVE),
                    "%s",
                    state.ExportStatus.c_str()
                );
            }

            std::string placeholder;
            bool placeholderIsError = false;
            if (!inputs.HasSelection) {
                placeholder = Tr("Select an AVD to view logs");
            } else if (!inputs.IsRunning || inputs.Serial.empty()) {
                char buffer[256];
                std::snprintf(buffer, sizeof(buffer), Tr("Run the \"%s\" AVD to view Logcat"), inputs.AvdName.c_str());
                placeholder = buffer;
            } else if (context.Host.Sdk.AdbPath.empty()) {
                placeholder = Tr("ADB is not available.");
                placeholderIsError = true;
            } else if (state.CachedEntries.empty() && status.Connecting) {
                placeholder = Tr("Waiting for Logcat...");
            } else if (state.CachedEntries.empty() && !status.Error.empty()) {
                placeholder = Tr(status.Error.c_str());
                placeholderIsError = true;
            } else if (state.CachedEntries.empty()) {
                placeholder = Tr("No Logcat entries received yet.");
            }

            if (placeholder.empty()) {
                DrawLogcatBody(state, filtered, receivedNewEntries);
            } else {
                DrawEmptyLogBody("##LogcatEmpty", placeholder, placeholderIsError);
            }
            DrawLogcatFooter(state, filtered);
        }

        void DrawSelectedAvdHeader(const PanelInputs &inputs) {
            if (!inputs.HasSelection) {
                return;
            }
            ImGui::TextUnformatted(inputs.DisplayName.c_str());
            if (inputs.DisplayName != inputs.AvdName) {
                ImGui::SameLine();
                ImGui::TextDisabled("(%s)", inputs.AvdName.c_str());
            }
            ImGui::Separator();
        }
    }

    void BuildAvdLogsWindow(Context &context) {
        if (!context.UI.ShowLogPanel) {
            const LogcatStreamStatus status = context.Host.Logcat.Status();
            if (status.Running || status.Connecting) {
                context.Host.Logcat.Stop();
            }
            return;
        }

        const std::string title = TrLabel("Logs###Output Log");
        const ImGuiID dockId = context.UI.OutputLogDockId != 0 ? context.UI.OutputLogDockId : context.UI.BottomDockId;
        if (dockId != 0) {
            ImGui::SetNextWindowDockID(dockId, ImGuiCond_Always);
        }
        ImGui::Begin(title.c_str());
        if (ImGui::GetWindowDockID() != 0) {
            context.UI.OutputLogDockId = ImGui::GetWindowDockID();
            if (context.UI.BottomDockId == 0) {
                context.UI.BottomDockId = context.UI.OutputLogDockId;
            }
        }

        const PanelInputs inputs = ResolveInputs(context);
        StopLogcatForInvalidTarget(context, inputs);
        DrawSelectedAvdHeader(inputs);

        if (ImGui::BeginTabBar("##LogSources")) {
            if (ImGui::BeginTabItem(Tr("Emulator"))) {
                context.Logs.ActiveSource = LogSource::Emulator;
                DrawEmulatorLogPanel(context, inputs);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Logcat")) {
                context.Logs.ActiveSource = LogSource::Logcat;
                DrawLogcatPanel(context, inputs);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        ImGui::End();
    }
}
