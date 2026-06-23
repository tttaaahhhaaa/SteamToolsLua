## Goal
- Maintain Steam game tools app with Online Fix downloader, SteamDB browser, auto-update, AI translation, embedded CloudRedirect, and page navigation with viewed-page tracking.

## Constraints & Preferences
- All game pages opened in external browser (no in-app webviewer).
- Archive password is only `knkm`.
- Online Fix download → extract only; no injection into Steam folders.
- Torrent downloader removed entirely.
- Back/forward navigation buttons removed.
- Folder quick-access: Setups, Online Fixes, Game Files buttons in settings.
- All buttons need translations in 6 languages (tr/en/es/fr/de/ja).
- CloudRedirect bundled inside exe (not downloaded at runtime).
- UI Scale slider removed (was broken).
- Update: old exe downloads new exe alongside itself, spawns it, exits with `os._exit(0)`. New exe on first launch deletes old exe and renames itself to `SteamToolsLua.exe`.
- Page nav: number buttons at bottom, purple if 4+ game appids match previously viewed.

## Progress
### Done
- UI Scale slider removed.
- Update mechanism rewritten: download to `SteamToolsLua_v{latest}.exe` → spawn → `os._exit(0)`. No `.old`, no VBScript, no TerminateProcess.
- Minimize/restore bug fix: `root.bind('<Map>', ...)` for topmost toggle.
- Page navigation: numbered page buttons + ellipsis at bottom (`_rebuild_page_btns`). Purple for viewed pages.
- v1.7.1 uploaded to GitHub release & placed on desktop. v1.6.0 removed (bytecode version mismatch).
- Code pushed to GitHub (`master` branch).

### In Progress
- (none)

### Blocked
- (none)

## Key Decisions
- **`os._exit(0)` instead of `root.destroy()`** — terminates immediately regardless of background threads.
- **Download next to current exe** — simpler path, avoids cross-directory moves.
- **No `.old` file** — old exe deleted directly or renamed to `_todel.exe` then deleted.
- **Page nav tracking with appid set** — 4+ matching appids = same page content → purple.

## Next Steps
- None currently.

## Critical Context
- PHPSESSID from any game page → headers `Referer: https://online-fix.me/` + cookies `{"PHPSESSID": "<id>"}` for structured downloads.
- Structured URL regex: `https?://(?:uploads|hosters|drive|torrents)\.online-fix\.me:\d+/[^"']+`.
- 7z extraction password: only `knkm`.
- Auto-update: fetch version from raw git → download to `SteamToolsLua_v{latest}.exe` → `subprocess.Popen` → `os._exit(0)` after 500ms.
- UPDATE_URL = raw git `latest_version.txt`.
- DOWNLOAD_BASE = GitHub releases download.
- GitHub token: (redacted - stored in env var).
- Repo branch is `master` (not `main`).
- PyInstaller `--add-data`: `icon.ico`, `webviewer_module.py`, `aria2c.exe`, `CloudRedirect.exe`, `cloud_redirect.dll`.
- Page nav: `app._sd_viewed = {page_num: {appid_str, ...}}`. Up to 10 buttons shown at once with ellipsis.
