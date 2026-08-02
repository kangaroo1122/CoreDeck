## [v0.12.0-beta.2](https://github.com/kangaroo1122/CoreDeck/releases/tag/v0.12.0-beta.2) — 2026-08-02

- Keep miniz and tinyxml2 development headers and libraries out of macOS installation images

## [v0.12.0-beta.1](https://github.com/kangaroo1122/CoreDeck/releases/tag/v0.12.0-beta.1) — 2026-08-02

- Add guided Android SDK installation during onboarding, including command-line tools, licenses, platform tools, emulator, and a stable SDK platform
- Install fresh SDKs transactionally with rollback on failure or cancellation and continuous progress across every setup phase
- Parse Google's repository metadata with tinyxml2, verify downloads with official SHA-1 or fallback SHA-256 metadata, and extract archives in-process with miniz
- Harden archive path validation, emulator stop/relaunch lifecycle handling, Windows process cleanup, and asynchronous system image operations

## [v0.11.1](https://github.com/kangaroo1122/CoreDeck/releases/tag/v0.11.1) — 2026-08-01

- Keep Preferences open beneath update result dialogs started from its System Updates section
- Skip quit confirmation when no emulator sessions are running
- Polish Simplified Chinese translations across preferences, SDK/JDK setup, updates, and emulator options

## [v0.11.0](https://github.com/kangaroo1122/CoreDeck/releases/tag/v0.11.0) — 2026-08-01

- Add System Updates preferences with Stable monitoring enabled by default and an option to include Beta releases
- Select the newest compatible release and platform-specific package from GitHub Releases
- Download updates inside CoreDeck with progress, cancellation, SHA-256 verification, and same-file replacement
- Show the same update result dialogs from Preferences and the Help menu
- Open verified DMG installers on macOS, ask before starting Windows MSI installers, and open the downloaded package location on Linux
- Move System Updates to the end of the Preferences menu
- Skip quit confirmation during onboarding and keep docked panels visible behind the quit confirmation

## [v0.11.0-beta.5](https://github.com/kangaroo1122/CoreDeck/releases/tag/v0.11.0-beta.5) — 2026-08-01

- Keep docked panels visible and their adjusted proportions stable behind the quit confirmation

## [v0.11.0-beta.4](https://github.com/kangaroo1122/CoreDeck/releases/tag/v0.11.0-beta.4) — 2026-08-01

- Replace the previous checksum and installer in a shared update download directory

## [v0.11.0-beta.3](https://github.com/kangaroo1122/CoreDeck/releases/tag/v0.11.0-beta.3) — 2026-08-01

- Show the same up-to-date and update-available dialogs from Preferences and the Help menu

## [v0.11.0-beta.2](https://github.com/kangaroo1122/CoreDeck/releases/tag/v0.11.0-beta.2) — 2026-08-01

- Move System Updates to the end of the Preferences menu
- Show update-check progress and results inside Preferences while it is open
- Prevent update dialogs from hiding Preferences or blocking the quit confirmation

## [v0.11.0-beta.1](https://github.com/kangaroo1122/CoreDeck/releases/tag/v0.11.0-beta.1) — 2026-08-01

- Add a System Updates section to Preferences with Stable monitoring enabled by default
- Add an option to include Beta and other prerelease versions in update checks
- Select the newest compatible release and platform-specific package from GitHub Releases
- Download update packages inside CoreDeck with progress and cancellation support
- Verify downloaded packages against the release-provided SHA-256 checksum before opening them
- Open verified DMG installers on macOS and the downloaded archive location on Linux
- Ask for confirmation before starting the Windows MSI, with a clear notice that CoreDeck will exit
- Skip quit confirmation on the onboarding screen, where no emulator session is running yet

## [v0.10.0](https://github.com/kangaroo1122/CoreDeck/releases/tag/v0.10.0) — 2026-07-30

- Add AVD snapshot management with snapshot size and modified-time details, refresh support, guarded deletion, and running-emulator protection
- Show each AVD's API level alongside its system image type and running status in the AVD list
- Harden snapshot deletion and AVD operation flows against unsafe paths, asynchronous failures, and stale UI-thread state
- Discover the latest Android SDK Command-line Tools from Google's official repository metadata with a fixed-version fallback and archive verification
- Keep snapshot dialogs centered while asynchronous results change their height
- Streamline font preferences by removing the separate font-file chooser and placing font, size, and reset controls on one row

## [v0.10.0-beta.2](https://github.com/kangaroo1122/CoreDeck/releases/tag/v0.10.0-beta.2) — 2026-07-29

- Keep the snapshot dialog centered while asynchronous results change its height
- Discover the latest Android SDK Command-line Tools from Google's official repository metadata with a fixed-version fallback and archive verification

## [v0.10.0-beta.1](https://github.com/kangaroo1122/CoreDeck/releases/tag/v0.10.0-beta.1) — 2026-07-29

- Add AVD snapshot management with snapshot size and modified-time details, refresh support, guarded deletion, and running-emulator protection
- Show each AVD's API level alongside its system image type and running status in the AVD list
- Harden snapshot deletion against unsafe names, symbolic links, path traversal, and filesystem enumeration failures
- Move AVD creation and system image preloading results back to the UI thread to avoid concurrent container access
- Preserve create, delete, and wipe dialogs when an operation fails, display actionable errors, and update disk usage caches safely on the UI thread

## [v0.9.2](https://github.com/kangaroo1122/CoreDeck/releases/tag/v0.9.2) — 2026-07-29

- Add macOS Intel x86-64 build and unsigned DMG release artifact alongside the Apple Silicon build

## [v0.9.1](https://github.com/kangaroo1122/CoreDeck/releases/tag/v0.9.1) — 2026-07-28

- Restore the main window size and maximized state after restarting CoreDeck

## [v0.9.0](https://github.com/kangaroo1122/CoreDeck/releases/tag/v0.9.0) — 2026-07-28

- Add Device Explorer as a bottom panel that follows the selected running AVD, waits for the emulator to become ready, and returns to `Select a running AVD first.` when no usable device is selected
- Support device file browsing, upload, download, folder upload, folder creation, and guarded deletion from a compact single-row action toolbar
- Add per-AVD shared folders with host/emulator open actions and metadata-first incremental sync that propagates one-sided changes and deletes
- Preserve conflicting shared-folder edits as conflict-named copies while keeping sync scoped to regular files inside the shared folder
- Rework the main dock into a fixed five-panel layout with independent View toggles, 2:1 default splits, single-panel fill behavior, and persisted user-adjusted split ratios across hide/show and app restarts
- Add horizontal scrolling for long Output Log lines while keeping log text selectable
- Add UI font size controls with reliable active font resizing, bundled PingFang SC Heavy as the default UI font, and clearer font selector behavior
- Split Android SDK and Java preferences into separate menu entries, hide transient shared-folder status from the AVD list, and ask for confirmation before quitting CoreDeck

## [v0.9.0-beta.9](https://github.com/kangaroo1122/CoreDeck/releases/tag/v0.9.0-beta.9) — 2026-07-28

- Rework the main dock into a fixed five-panel layout driven by current visibility so hidden panels no longer leave stale dock nodes behind
- Keep Device Explorer pinned to its bottom slot while updating its contents from the selected running AVD, matching the Output Log lifecycle
- Preserve user-adjusted dock split ratios when panels are hidden, shown again, or restored after restarting CoreDeck
- Preserve the intended layout rules: top and bottom groups default to 2:1 when both exist, bottom panels default to 2:1 when both are visible, single visible panels fill their group, and all panels may be hidden

## [v0.9.0-beta.8](https://github.com/kangaroo1122/CoreDeck/releases/tag/v0.9.0-beta.8) — 2026-07-28

- Avoid a crash when reopening Output Log after Device Explorer is the only visible bottom panel

## [v0.9.0-beta.7](https://github.com/kangaroo1122/CoreDeck/releases/tag/v0.9.0-beta.7) — 2026-07-28

- Remove the per-row Device Explorer shortcut from the AVD list and keep Device Explorer controlled by the View state
- Keep Device Explorer in a waiting state while a selected emulator is still booting, then refresh files after the device becomes ready
- Hide transient shared-folder open status above the AVD list to prevent list layout jitter
- Fix bottom panel docking so Output Log and Device Explorer fill the bottom area correctly when either panel is hidden, and restore the 2:1 split when both are visible

## [v0.9.0-beta.6](https://github.com/kangaroo1122/CoreDeck/releases/tag/v0.9.0-beta.6) — 2026-07-28

- Make Device Explorer follow the selected running AVD automatically and return to `Select a running AVD first.` when the selected AVD is stopped or unavailable
- Remove the redundant Device Explorer entry from the Tools menu while keeping Shared Folder actions there
- Ask for confirmation before quitting CoreDeck from the window close button or Quit menu

## [v0.9.0-beta.5](https://github.com/kangaroo1122/CoreDeck/releases/tag/v0.9.0-beta.5) — 2026-07-28

- Fix UI font size changes by updating ImGui's active base font size after startup, theme resets, and font rebuilds
- Keep Device Explorer idle until a running AVD is explicitly opened from its file-manager action
- Hide transient shared-folder adb not-found errors while an emulator is still starting
- Upgrade shared folder sync to metadata-first incremental change detection with snapshot-based three-way reconcile, propagating one-sided changes and deletes
- Preserve conflicting shared-folder edits as conflict-named copies while keeping sync scoped to regular files inside the share
- Remove the extra macOS View menu full screen item

## [v0.9.0-beta.4](https://github.com/kangaroo1122/CoreDeck/releases/tag/v0.9.0-beta.4) — 2026-07-27

- Fix UI font size changes by updating ImGui's active base font size after startup, theme resets, and font rebuilds
- Upgrade shared folder sync to a snapshot-based three-way reconcile that propagates one-sided changes and deletes
- Preserve conflicting shared-folder edits as conflict-named copies while keeping sync scoped to regular files inside the share

## [v0.9.0-beta.3](https://github.com/kangaroo1122/CoreDeck/releases/tag/v0.9.0-beta.3) — 2026-07-27

- Switch UI font size selection from a slider to a dropdown
- Ensure UI font size changes rebuild the active font cleanly
- Show Output Log and Device Explorer as peer bottom windows, with independent View menu toggles
- Use a 2:1 default split when both bottom windows are visible, while allowing the divider to be dragged
- Restore a visible bottom panel when saved ImGui layout state has no bottom dock id
- Add horizontal scrolling for long Output Log lines while keeping log text selectable
- Move Device Explorer file actions into a top icon toolbar

## [v0.9.0-beta.2](https://github.com/kangaroo1122/CoreDeck/releases/tag/v0.9.0-beta.2) — 2026-07-27

- Add UI font size control in Appearance preferences
- Split Android SDK and Java (JDK) preferences into separate menu entries
- Propagate device-side shared file deletions back to the host shared folder
- Add per-row Device Explorer actions for running AVDs while keeping the Tools menu entry
- Hide routine shared-folder sync status from the AVD list to avoid layout jitter

## [v0.9.0-beta.1](https://github.com/kangaroo1122/CoreDeck/releases/tag/v0.9.0-beta.1) — 2026-07-27

- Add a Tools menu for the selected running AVD, with Device Explorer and Shared Folder actions
- Show Device Explorer as a bottom-panel tab that follows the selected running AVD
- Support device file browsing, upload, download, folder upload, folder creation, and guarded deletion
- Add per-AVD shared folders under CoreDeck's config directory with adb-backed bidirectional sync
- Keep the newest same-name shared files by modified time and leave host shared folders in place after shutdown
- Open the shared folder on the host or inside Android from the Tools menu
- Use bundled PingFang SC Heavy as the default UI font
- Apply font selector choices to the primary UI font and clarify candidate ordering

## [v0.8.0](https://github.com/kangaroo1122/CoreDeck/releases/tag/v0.8.0) — 2026-07-26

- Add Android SDK Platforms and SDK Tools management, including first-run installation for command-line tools, platform-tools, emulator, and the latest stable SDK Platform
- Add managed JDK setup with separate onboarding and preferences, JDK 17+ guidance, official LTS downloads, configurable Java home, and safer replacement/cleanup behavior
- Add Appearance preferences for Light/Dark theme switching and Simplified Chinese language support with CJK font fallback selection
- Improve AVD creation and management with installed/all system image filtering, host-supported ABI filtering, current-image highlighting, display-name renaming, emulator port allocation fixes, and reliable cold boots for affected Quick Boot snapshots
- Improve SDK, system image, preferences, and onboarding layouts with resizable dialogs, adaptive package tables, hover tooltips for truncated names, and better spacing on shorter windows
- Use the native macOS menu bar for app, view, and help actions while keeping the in-window menu on Windows and Linux; keep menu titles in sync after language changes
- Update About and release links, add manual release workflow support, package unsigned macOS DMGs, validate Windows ARM64 builds, fix WiX MSI packaging, and correct Windows live CPU usage readings

## [v0.8.0-beta.7](https://github.com/kangaroo1122/CoreDeck/releases/tag/v0.8.0-beta.7) — 2026-07-26

- Use the native macOS menu bar for app, view, and help actions
- Keep Windows and Linux on the in-window ImGui menu bar
- Improve first-run JDK/SDK setup spacing when download controls are visible

## [v0.8.0-beta.6](https://github.com/kangaroo1122/CoreDeck/releases/tag/v0.8.0-beta.6) — 2026-07-26

- Improve Android JDK/SDK package table height and resizing behavior
- Add hover tooltips for truncated SDK package and system image table cells
- Split first-run setup into separate JDK and SDK steps with a JDK 17+ recommendation
- Add managed JDK downloads from official LTS package sources during onboarding and JDK setup
- Avoid replacing an existing managed JDK until the new download has been verified
- Ask users to pick an empty folder before downloading JDK or SDK files into a non-SDK directory
- Clean up cancelled SDK setup downloads and show setup failures without crashing the settings window

## [v0.8.0-beta.5](https://github.com/kangaroo1122/CoreDeck/releases/tag/v0.8.0-beta.5) — 2026-07-26

- Show full SDK Manager system image names and filter the AVD image picker to the host-supported ABI
- Keep the Android JDK/SDK onboarding step clear of the top edge on shorter windows
- Make Preferences resizable and let SDK package lists adapt to the available window height

## [v0.8.0-beta.4](https://github.com/kangaroo1122/CoreDeck/releases/tag/v0.8.0-beta.4) — 2026-07-25

- Add Android SDK Platforms and SDK Tools management, with onboarding install for cmdline-tools, platform-tools, emulator, and the latest stable SDK Platform
- Match SDK Platforms and SDK Tools package lists to Android SDK Updater-style summary/details views
- Allow cancelling Android SDK tool downloads and package install operations
- Show only SDK Platform packages in the SDK Platforms tab, with Name, API Level, Revision, and Status columns
- Add JDK selection to onboarding and harden SDK package operations against path changes and command failures
- Prefer stable SDK tool releases over matching preview or RC builds when picking the latest version
- Fix SDK onboarding layout overflow and duplicate Browse button IDs

## [v0.8.0-beta.3](https://github.com/kangaroo1122/CoreDeck/releases/tag/v0.8.0-beta.3) — 2026-07-25

- Fix emulator port allocation when launching multiple or externally started AVDs
- Cold boot AVDs whose Quick Boot snapshot was saved from Android Studio's hidden-window mode
- Update About author attribution and website link

## [v0.8.0-beta.2](https://github.com/kangaroo1122/CoreDeck/releases/tag/v0.8.0-beta.2) — 2026-07-25

- Add manual Light/Dark theme switching in Appearance preferences
- Add Simplified Chinese language switching with CJK font fallback selection
- Add configurable JDK home for Android SDK command-line tools
- Add Installed/All filtering and current-image highlighting to the system image picker
- Add AVD display-name renaming from the AVD list row
- Point update checks and release downloads to the forked GitHub releases page
- Add a default-on preference to confirm before using Wipe & Run from the AVD list
- Add Windows ARM64 build validation and release artifacts
- Fix WiX setup for Windows MSI packaging
- Add manual release workflow runs and package macOS release artifacts as unsigned DMGs

## [v0.8.0-beta.1](https://github.com/devmuaz/CoreDeck/releases/tag/v0.8.0-beta.1) — 2026-06-02

- Fix live resource usage CPU always showing 0% on Windows by aggregating stats across the emulator process tree (thanks [@sjoerdev](https://github.com/sjoerdev), [#27](https://github.com/devmuaz/CoreDeck/issues/27))
- Fix live resource usage CPU reading above 100% by normalizing against logical core count so the Details panel shows 0–100% of total system capacity

## [v0.7.0](https://github.com/devmuaz/CoreDeck/releases/tag/v0.7.0) — 2026-05-21

- Add 30+ new emulator option flags and merge saved configs with defaults so new options surface without losing user state
- Make popup windows movable
- Make Sentry crash reporting opt-in via a new preference
- Add a custom `SubtitledCheckbox` widget to replace ImGui's default checkboxes

## [v0.7.0-beta.6](https://github.com/devmuaz/CoreDeck/releases/tag/v0.7.0-beta.6) — 2026-05-19

- Replace the View menu checkboxes with Show/Hide text toggles for the AVD List, Options, Details, and Output Log panels
- Replace the modal close button with a rounded red-tinted custom render
- Add `MenuStyle`/`ComboStyle` RAII helpers plus `RoundedMenuItem`, `RoundedBeginMenu`, and `RoundedSelectable` widgets, and map `-camera-back`/`-front` values to human-readable labels
- Fix VS Build Tools detection in VSCode build tasks ([#26](https://github.com/devmuaz/CoreDeck/issues/26))
- Fix HiDPI borders, table overflow, toolbar button shapes, and Windows UTF-8 glyphs, and polish the storage breakdown bar ([#23](https://github.com/devmuaz/CoreDeck/issues/23))
- Make editor configs cross-platform on Ninja and document the dev setup in the README

## [v0.7.0-beta.5](https://github.com/devmuaz/CoreDeck/releases/tag/v0.7.0-beta.5) — 2026-05-14

- Fix HiDPI text overflowing buttons and sub-windows by removing duplicate font scaling (thanks [@sjoerdev](https://github.com/sjoerdev), [#23](https://github.com/devmuaz/CoreDeck/issues/23))
- Switch hardcoded pixel widths and heights throughout the UI to text-derived units so dialogs, sub-panels, buttons, and table columns now scale with the font on high-DPI displays

## [v0.7.0-beta.3](https://github.com/devmuaz/CoreDeck/releases/tag/v0.7.0-beta.3) — 2026-05-14

- Add HiDPI monitor support so the UI scales correctly on high-DPI Windows displays at 100%, 125%, 150%, and 200% (thanks [@sjoerdev](https://github.com/sjoerdev), [#23](https://github.com/devmuaz/CoreDeck/issues/23))
- Add live resource usage section in the AVD Details window for running emulators, including CPU, memory, and disk I/O history
- Show full release notes inside the in-app update modal
- Fix emulator processes not being killed cleanly on app exit
- Fix Windows build failure caused by `psapi.h` being included without `windows.h` and an undefined `socket_t` alias in the emulator console
- Add project-wide `.clang-tidy` configuration and conform existing code to its rules
- Add initial VSCode and CMake integration configuration files

## [v0.6.0](https://github.com/devmuaz/CoreDeck/releases/tag/v0.6.0) — 2026-05-07

- Add skin management with a dedicated picker window for browsing, previewing, and assigning device skins from the SDK or custom paths
- Add log filtering to the AVD logs window with case-sensitive and regex search modes plus result navigation
- Expand preferences with theme controls and broader app settings
- Add unit tests covering the log filter and skin modules
- Add `.clang-tidy` with project-wide naming conventions and a curated check set
- Other minor bug fixes and improvements

## [v0.5.0](https://github.com/devmuaz/CoreDeck/releases/tag/v0.5.0) — 2026-05-04

- Ship macOS as a notarized `.dmg` with a drag-to-Applications window layout and a Retina-ready background image
- Add SHA256 checksums alongside every release artifact for download verification
- Sign nested Mach-O components (including `crashpad_handler`) explicitly during macOS packaging, replacing the deprecated `--deep` flag
- Surface Apple's full notarization log in CI when notarization fails
- Cache compilation across releases via `ccache` on Linux and macOS to shorten release times
- Wire release tags through to the embedded version string via a new `COREDECK_VERSION_OVERRIDE` CMake option

## [v0.4.0](https://github.com/devmuaz/CoreDeck/releases/tag/v0.4.0) — 2026-05-03

- Redesign AVD creation around dedicated device profile and system image picker dialogs with search, category filters, and table-based selection
- Redesign storage overview into a statistics-focused view with summary cards, a visual usage breakdown, and async size calculation
- Show more readable AVD and emulator details, including friendly GPU, screen, network, acceleration, and SELinux option labels
- Parse and display richer system image metadata for AVDs, including Google APIs, Google Play, 16 KB page size support, variants, and tags
- Improve system image management by moving image removal into the image picker and refreshing image state after install or uninstall
- Improve Windows emulator stop detection by tracking the emulator console port instead of relying only on the launcher process

## [v0.3.0](https://github.com/devmuaz/CoreDeck/releases/tag/v0.3.0) — 2026-04-29

- Add opt-in Sentry crash reporting via `sentry-native` with a `CrashReporter` facade so the SDK is fully isolated from the rest of the codebase
- Refactor `main.cpp` to delegate the full GLFW/ImGui lifecycle to `Application`, with RAII teardown and platform error handling
- Fix Windows title bar icon, Task Manager description, and em dash rendering in version info and the About dialog (thanks [@maramadany](https://github.com/maramadany))
- Fix Windows `.bat` handling, console flashes, and SDK license acceptance (thanks [@maramadany](https://github.com/maramadany))
- Fix emulator stop hang and orphaned `qemu` processes on shutdown (thanks [@maramadany](https://github.com/maramadany))
- Add `tools/audit_pch.sh` script to audit `pch.h` against actual header usage across the codebase

## [v0.2.0](https://github.com/devmuaz/CoreDeck/releases/tag/v0.2.0) — 2026-04-25

- Add device-type icons to the AVD list
- Auto-fill Name and Display Name in the Create AVD dialog from the selected device profile and system image
- Replace the runtime `curl` binary dependency with libcurl on macOS/Linux and WinHTTP on Windows (thanks [@maramadany](https://github.com/maramadany))
- Reduce idle CPU usage by replacing `glfwPollEvents` with focus-aware `glfwWaitEventsTimeout` (thanks [@maramadany](https://github.com/maramadany))
- Fix Create AVD button when no AVDs exist (thanks [@maramadany](https://github.com/maramadany))
- Fix blank AVD names by falling back to the internal AVD name when `avd.ini.displayname` is missing (thanks [@maramadany](https://github.com/maramadany))
- Fix Create AVD dialog layout and disabled-button states
- Fix font path resolution to be relative to the executable rather than the working directory (thanks [@maramadany](https://github.com/maramadany))
- Update README

## [v0.1.0](https://github.com/devmuaz/CoreDeck/releases/tag/v0.1.0) — 2026-04-22

- Fix crash when launching an AVD after a previous run of the same AVD

## [v0.1.0-beta.1](https://github.com/devmuaz/CoreDeck/releases/tag/v0.1.0-beta.1) — 2026-04-21

- Add Catch2 unit test suite with 35 tests covering utilities, paths, log buffer, and version check
- Add CI test step that runs the suite on Windows, macOS, Linux x86-64, and Linux ARM64 on every PR
- Fix log buffer FIFO eviction that was dropping newest entries instead of oldest
- Fix update check to correctly notify pre-release users when the matching stable release ships
- Auto-detect pre-release tags in the release workflow so betas are not marked as the latest stable

## [v0.0.8](https://github.com/devmuaz/CoreDeck/releases/tag/v0.0.8) — 2026-04-19

- Add system image install and uninstall via sdkmanager
- Add storage overview with per-AVD disk usage and async wipe user data
- Add auto and manual update check via GitHub API
- Add app settings persistence and preferences UI
- Add CONTRIBUTING.md with branching model and PR guidelines
- Refactor system image handling into its own core module
- Update README with features, preview screenshots, build dependencies, and contributing section

## [v0.0.7](https://github.com/devmuaz/CoreDeck/releases/tag/v0.0.7) — 2026-04-16

- Add Linux ARM64 build support
- Update artifact naming to platform-arch convention (`darwin-arm64`, `linux-x86-64`, `windows-x86-64`)

## [v0.0.6](https://github.com/devmuaz/CoreDeck/releases/tag/v0.0.6) — 2026-04-16

- Add Create AVD feature with system image, device profile, RAM, and GPU mode selection
- Add onboarding wizard with SDK setup, file picker, and validation
- Add menu bar, About dialog, and AVD delete with confirmation
- Refactor source into core/gui layers with Context-based window architecture
- Refactor create, delete, and about windows to modal window type with input validation
- Compile tinyfiledialogs as a separate static library to fix Windows build
- Fix GCC 13 build errors on Linux (missing `<algorithm>`, `constexpr std::string`)
- Enable cross-platform CI for build and release workflows
- Fix Windows title bar icon and install platform-specific icons per OS

## [v0.0.5](https://github.com/devmuaz/CoreDeck/releases/tag/v0.0.5) — 2026-04-12

- Fix Windows app icon in resources
- Fix Windows app details metadata

## [v0.0.4](https://github.com/devmuaz/CoreDeck/releases/tag/v0.0.4) — 2026-04-11

- Add error message box when GLFW or ImGui fail to initialize on Windows

## [v0.0.3](https://github.com/devmuaz/CoreDeck/releases/tag/v0.0.3) — 2026-04-11

- Fix Windows icon display
- Add installer shortcuts
- Improve CI workflows

## [v0.0.2](https://github.com/devmuaz/CoreDeck/releases/tag/v0.0.2) — 2026-04-11

- Add MIT license
- Fix MSI install directory and empty package issue

## [v0.0.1](https://github.com/devmuaz/CoreDeck/releases/tag/v0.0.1) — 2026-04-11

- Initial release
- AVD listing, launching, stopping, and wipe & run
- Per-AVD emulator options (GPU, RAM, CPU, boot, audio, network, camera)
- Live log viewer with search and auto-scroll
- Emulator manager with threading and proper cleanup
- Platform support for Windows, macOS (Apple Silicon), and Linux
- macOS app bundle with code signing and notarization
- Windows MSI installer and ZIP portable packaging
- GitHub Actions CI/CD for build and release
