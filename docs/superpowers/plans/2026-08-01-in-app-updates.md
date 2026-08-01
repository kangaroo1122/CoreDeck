# In-App Updates Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add Stable/Beta update-channel settings and verified in-app release downloads with platform-specific install handoff.

**Architecture:** Extend release parsing to support GitHub release lists, prerelease filtering, semantic ordering, and platform asset selection. Add a focused downloader/checksum module using existing WinHTTP/libcurl transports, then connect its state to the update modal and Preferences. Keep installation platform-assisted rather than self-replacing.

**Tech Stack:** C++20, Dear ImGui, WinHTTP/libcurl, reflect-cpp JSON, Catch2, existing CoreDeck settings and process utilities.

---

### Task 1: Settings and release-selection tests

**Files:**
- Modify: `src/core/app_settings_types.h`
- Modify: `src/core/version_check.h`
- Modify: `tests/test_app_settings.cpp`
- Modify: `tests/test_version_check.cpp`

- [ ] Add failing tests for `IncludeBetaUpdates=false`, old JSON defaulting, prerelease ordering, channel filtering, and platform asset matching.
- [ ] Run the focused test binary and confirm the new tests fail for the missing fields/APIs.
- [ ] Add the settings field and release metadata types/helpers with minimal interfaces.
- [ ] Run focused tests and then the full test suite.

### Task 2: Release API and asset selection

**Files:**
- Modify: `src/core/version_check.cpp`
- Modify: `src/core/version_check.h`
- Modify: `src/gui/application.cpp`
- Modify: `src/gui/context.h`

- [ ] Replace latest-only metadata parsing with channel-aware release list parsing while retaining the existing notes behavior.
- [ ] Add complete prerelease comparison and current-platform/current-architecture asset selection.
- [ ] Add update state for selected asset, download status, progress, cancellation, and errors.
- [ ] Run version/settings tests before UI changes.

### Task 3: Download and SHA-256 verification

**Files:**
- Create: `src/core/update_download.h`
- Create: `src/core/update_download.cpp`
- Modify: `src/core/utilities.h`
- Modify: `src/core/utilities.cpp`
- Modify: `CMakeLists.txt`
- Create: `tests/test_update_download.cpp`

- [ ] Add failing tests for checksum parsing, checksum mismatch, and successful verification using deterministic local data.
- [ ] Implement streaming downloads through the existing platform HTTP transports, cache paths, cancellation, and SHA-256 verification.
- [ ] Add local-path opening for MSI/DMG/archive directory using existing platform process helpers.
- [ ] Run focused and full tests.

### Task 4: Preferences and update UI

**Files:**
- Modify: `src/gui/windows/preferences.cpp`
- Modify: `src/gui/windows/update.cpp`
- Modify: `src/gui/localization.cpp`
- Modify: `src/gui/application.cpp`

- [ ] Add the System Updates sidebar section with the beta checkbox and manual check action.
- [ ] Persist changes immediately and apply the channel to future checks.
- [ ] Add download progress, cancel, retry, verification result, and GitHub fallback states.
- [ ] On Windows show the explicit exit confirmation before starting MSI; on macOS/Linux open the verified artifact through the platform handler.
- [ ] Build and run tests; inspect the UI manually on the available host.

### Task 5: Documentation and verification

**Files:**
- Modify: `README.md`

- [ ] Document Stable default, optional Beta monitoring, and platform-specific installation behavior.
- [ ] Run formatting/build/test verification and inspect the final diff.
- [ ] Report the inability to create git commits if repository permissions remain restricted.
