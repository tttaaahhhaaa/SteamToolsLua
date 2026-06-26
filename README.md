# SteamToolsLua v1.9.0

Native C + WebView2 desktop tool for Steam game management, Online Fix downloads, AI translation, and CloudRedirect. Standalone 193 KB EXE — no Python, no Node.js, no runtime dependencies.

## Features

- **Online Fix Search** — Search online-fix.me directly, browse results, get download links
- **Steam Library Scanner** — Scan steamapps/common, depotcache, used/ folder for installed games
- **AI Translation** — Translate game names/descriptions via Google Translate or configured AI provider
- **CloudRedirect** — Launch CloudRedirectTR to manage Steam Cloud save redirection
- **Steam API** — Look up app details, multiplayer info, prices from Steam Store
- **Registry Settings** — Save paths and preferences in Windows Registry
- **Auto Update** — Check for updates from GitHub releases
- **Cyberpunk Theme** — Dark purple UI with virtual scroll, pagination, filter tabs
- **Dual-Mode** — WebView2 primary, HTTP server fallback for browsers without WebView2

## Quick Start

Download `SteamToolsLua_v1.9.0.exe` from [Releases](https://github.com/tttaaahhhaaa/SteamToolsLua/releases). Place it anywhere alongside `WebView2Loader.dll` and the `web/` folder, then run. Requires WebView2 Runtime (included in Windows 11, or download from Microsoft).

## Build

```sh
gcc -std=c99 -Os main.c utils.c handlers.c -lwinhttp -lws2_32 -lole32 -loleaut32 -luuid -lshell32 -lcomctl32 -mwindows -o SteamToolsLua_v1.9.0.exe
```

#### UI Improvements
- Non-modal progress windows (no freeze on focus loss / minimize)
- All tkinter UI updates use `after()` for thread safety
- Dark-themed `TProgressbar` styling matching the UI theme
- Unlock All window uses themed scrollbar (`ttk.Scrollbar`)
- `_check_online_status` disabled (was causing freeze on card creation)
- `update_idletasks()` during card render loop to keep UI responsive
- Removed CPU priority setting (`psutil`)
- Fixed `limit_var` trace reference (page size no longer configurable)

#### Technical Changes
- `_STEAMDB_LIMIT`: 5000 games loaded per cache slice
- `_STEAMDB_PAGE_SIZE`: fixed at 12 (non-configurable)
- Cache-only startup: loads from disk, no API call until Refresh
- Refresh rotates offset through cache (`offset += 5000`), only fetches API if cache missing
- Manifest download runs synchronously in unlock thread for real progress tracking
- `injected_games.ini` replaced with ZIP-direct scanning (AppID from `.lua` filename)

## For Developers

### Requirements

- Python 3.14+
- PyInstaller

### Build

```batch
pip install -r requirements.txt
build.bat
```

## Links

- YouTube: [@oWL-Nexial](https://www.youtube.com/@oWL-Nexial)
