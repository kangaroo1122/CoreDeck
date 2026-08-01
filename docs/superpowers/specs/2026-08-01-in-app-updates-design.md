# In-App Updates Design

**Date:** 2026-08-01

## Goal

Add a system update settings section with Stable/Beta update-channel control and an in-app download flow that verifies the selected GitHub release asset before opening the platform installer or archive.

## Scope

- Add a persisted `Include beta releases` preference, disabled by default.
- Keep update checks manual or automatic according to the existing application flow, but only start package downloads after the user presses `Download`.
- Query GitHub releases, select the newest eligible release for the configured channel, and select the current platform/architecture asset.
- Download the asset and its `.sha256` checksum with progress and cancellation.
- Verify the downloaded asset before exposing the install action.
- Windows: ask for confirmation before starting the MSI and explain that CoreDeck will exit.
- macOS: open the DMG after verification and keep CoreDeck running.
- Linux: open the downloaded archive location and keep CoreDeck running.

This feature does not silently replace the running application, restart it, implement rollback, or introduce a third-party updater framework.

## Architecture

`version_check` will own GitHub release metadata parsing, release-channel filtering, semantic-version ordering, platform/architecture asset selection, and checksum-file parsing. A separate update-download module will own streaming downloads, temporary/cache paths, SHA-256 verification, and cancellation. The GUI update state will expose check and download progress to the existing update modal and the new Preferences section.

The existing WinHTTP (Windows) and libcurl (macOS/Linux) transports will be reused. SHA-256 will be implemented behind a small tested interface because the current project has no cross-platform checksum abstraction. Platform opening will extend the existing utility behavior with a local-path launcher.

## Release selection

- Stable mode selects non-draft, non-prerelease releases from the GitHub releases list.
- Beta mode selects non-draft releases including prereleases.
- The highest complete semantic version wins; prerelease identifiers such as `beta.1` and `beta.2` are ordered numerically.
- Assets are selected for the running OS and architecture. Checksum assets are paired by the exact package filename plus `.sha256`.
- If no matching asset exists, the UI offers the GitHub Releases page instead of downloading an incompatible file.

## User flow

1. The existing update check reports the newest eligible release and release notes.
2. The user presses `Download` in the update modal.
3. A progress state shows the package name, byte progress, and cancellation action.
4. The package and checksum are verified. Failed or cancelled downloads are removed.
5. Windows presents an install confirmation stating that CoreDeck will exit; confirmation starts MSI and exits, while cancellation leaves the verified package available.
6. macOS opens the DMG; Linux opens the containing folder.

## Persistence

`IncludeBetaUpdates` is added to `AppSettings` with a false default and `DefaultIfMissing` compatibility for existing settings files. The Preferences sidebar gets a `System Updates` section containing the channel checkbox and a manual check action.

## Error handling

Network errors, HTTP errors, missing release assets, checksum parse failures, checksum mismatches, unsupported architectures, and inability to open the downloaded package are shown in the update UI. The existing GitHub Releases link remains available as a fallback.

## Testing

Unit tests cover settings defaults/migration, prerelease ordering, channel filtering, asset selection, checksum parsing, and checksum verification. Network and platform process launching remain thin integration boundaries; their failure states are tested through deterministic helpers rather than live GitHub requests.
