# SteamToolsLua v1.0.1

Dark purple-themed desktop tool for Steam game injection management. Injects ZIP games, tracks titles via library, multilingual (TR/EN/ES/FR/DE/JP). Standalone EXE — no Python required.

## Features

- **SteamDB Browser** — Browse 25000+ Steam games with 12-card pages, 5000-game refresh rotation, cache-first startup
- **Unlock All** — Batch manifest download for all ZIPs in `used/` folder; extracts AppID from `.lua` filename; marks completed ZIPs as `(UNLOCKED)`
- **Online Fix** — Auto-opens correct game page on online-fix.me with smart word-matching
- **Inject All** — Select and inject multiple ZIPs at once
- **Library** — View injected games with date/name sorting
- **AI Providers** — Groq, OpenAI, Anthropic, Google, OpenRouter, DeepSeek, Ollama
- **Multilingual** — 6 languages with instant switching
- **Search History** — Last 50 queries with purple outline
- **Download Recall** — Saved game names from download button
- **Red Dot** — Visual indicator for already-injected games
- **Real Progress** — Determinate progress bar with streaming download for SteamDB fetch and manifest downloads

## Quick Start

Download `SteamToolsLua.exe` from [Releases](https://github.com/tttaaahhhaaa/SteamToolsLua/releases) and run it. No installation or Python needed.

## Changelog

### v1.0.1 — SteamDB Browser & Unlock All

#### New Features
- **SteamDB Browser**: Browse 25000+ Steam games directly in the app with 12 cards per page, 5000-game refresh rotation, cache-first startup, and real determinate progress bar
- **Unlock All (🔓)**: Batch manifest download — scans all ZIPs in `used/` folder, extracts AppID from each `.lua` filename, runs manifests.ps1 sequentially for each game, marks completed ZIPs as `(UNLOCKED)` (skipped on subsequent runs)
- **Online Fix**: Smart word-matching algorithm that scores links by visible text (×2) + URL slug (×1), threshold ≥3 — no more always-opening Forza

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
