## Goal
- Steam game tools app: Online Fix downloader, SteamDB browser, auto-update, AI translation, embedded CloudRedirect, page nav.

## Changes in v1.7.2
- **Startup fix**: no more self-rename (was crashing). Only checks for `.update_info.txt`.
- **Update info file**: old exe writes `.update_info.txt` (old_name, old_path, updated_to) before spawning new exe.
- **Update completion dialog**: new exe shows "Delete old version?" on first launch after update. Yes → deletes old exe. Removes `.update_info.txt`.
- **Settings "Del Old" button**: scans for other `SteamToolsLua*.exe` files, asks confirmation, deletes them. Always available.
- **Viewed page persistence**: `app._sd_viewed` saved to `steamdb_viewed.json`, survives restart. Purple tracking works after app restart.
- **Page navigation**: numbered page buttons + ellipsis at bottom of SteamDB browser. Purple if viewed.

## Key Decisions
- `os._exit(0)` for update restart
- Download new exe to `SteamToolsLua_v{latest}.exe` next to current exe
- No `.old` files, no VBScript, no TerminateProcess
- No self-rename on startup (was causing crash)
- `.update_info.txt` is the handshake file between old and new exe
- Page viewed tracking persisted to disk

## Known Issues
- Need a working v1.6.0 exe with v1.6.0 bytecode to test full update flow
- Push to GitHub blocked on secrets; remove tokens before committing

## Critical Context
- `UPDATE_URL = "https://raw.githubusercontent.com/tttaaahhhaaa/SteamToolsLua/main/latest_version.txt"`
- `DOWNLOAD_BASE = "https://github.com/tttaaahhhaaa/SteamToolsLua/releases/download"`
- Repo branch: `master`
- Archive password: `knkm`
- PyInstaller `--add-data`: icon.ico, webviewer_module.py, aria2c.exe, CloudRedirect.exe, cloud_redirect.dll
