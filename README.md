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
