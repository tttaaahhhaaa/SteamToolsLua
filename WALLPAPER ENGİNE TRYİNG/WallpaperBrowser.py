import os, re, json, base64, subprocess, threading, webbrowser, winreg, sys, time
from io import BytesIO
from tkinter import messagebox
import customtkinter as ctk
from PIL import Image, ImageSequence
import requests

WORKSHOP_SSR_URL = "https://steamcommunity.com/workshop/browse/_ssr/"
APP_ID = "431960"
THUMB_SIZE = (220, 130)
COLUMNS = 3
CACHE_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "cache")
PAGE_CACHE_DIR = os.path.join(CACHE_DIR, "pages")
THUMB_CACHE_DIR = os.path.join(CACHE_DIR, "thumbs")

ACCOUNTS = {
    'ruiiixx':     base64.b64decode('UzY3R0JUQjgzRDNZ').decode(),
    'premexilmenledgconis': base64.b64decode('M3BYYkhaSmxEYg==').decode(),
    'vAbuDy':      base64.b64decode('Qm9vbHE4dmlw').decode(),
    'adgjl1182':   base64.b64decode('UUVUVU85OTk5OQ==').decode(),
    'gobjj16182':  base64.b64decode('enVvYmlhbzgyMjI=').decode(),
    '787109690':   base64.b64decode('SHVjVXhZTVFpZzE1').decode(),
}

os.makedirs(CACHE_DIR, exist_ok=True)
ctk.set_appearance_mode("Dark")
ctk.set_default_color_theme("blue")

# MAIN_PROJECT-style dark purple palette overrides
BG = "#08080e"
FG = "#e0e0f0"
FRAME_BG = "#0a0a16"
ACCENT = "#7c6fff"
ACCENT_HOVER = "#6a5ee0"
SECONDARY = "#686880"
BORDER = "#1a1a38"
CARD_BG = "#0c0c18"
GREEN = "#50c878"
RED = "#e06060"
GOLD = "#e8a050"

# ---------------------------------------------------------------------------
#  Auto-detect helpers  (mirrors SteamToolsLua's _sam_scan_games approach)
# ---------------------------------------------------------------------------

def _read_reg_str(hive, key, name):
    try:
        k = winreg.OpenKey(hive, key)
        v = winreg.QueryValueEx(k, name)[0]
        winreg.CloseKey(k)
        return v
    except Exception:
        return None

def _get_steam_path():
    p = _read_reg_str(winreg.HKEY_LOCAL_MACHINE, r"Software\Valve\Steam", "InstallPath")
    if p and os.path.isdir(p):
        return p
    p = _read_reg_str(winreg.HKEY_CURRENT_USER, r"Software\Valve\Steam", "SteamPath")
    if p and os.path.isdir(p):
        return p
    for d in [r"C:\Program Files (x86)\Steam", r"C:\Program Files\Steam"]:
        if os.path.isdir(d):
            return d
    return ""

def _get_library_paths():
    """Returns all Steam library folders (mirrors _sam_scan_games pattern)."""
    steam = _get_steam_path()
    if not steam:
        return []
    libs = [steam]
    vdf = os.path.join(steam, "steamapps", "libraryfolders.vdf")
    if os.path.isfile(vdf):
        try:
            with open(vdf, "r", encoding="utf-8", errors="replace") as f:
                txt = f.read()
            for m in re.finditer(r'"path"\s*"([^"]+)"', txt):
                p = m.group(1).replace("\\\\", "\\")
                if p not in libs and os.path.isdir(p):
                    libs.append(p)
        except Exception:
            pass
    return libs

def find_wallpaper_engine_path():
    # 1 — Registry uninstall key
    for hive in (winreg.HKEY_LOCAL_MACHINE, winreg.HKEY_CURRENT_USER):
        p = _read_reg_str(hive, r"SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Steam App 431960", "InstallLocation")
        if p and os.path.isdir(p):
            return p
    # 2 — Scan every library + appmanifest for installdir field
    for lib in _get_library_paths():
        we = os.path.join(lib, "steamapps", "common", "wallpaper_engine")
        if os.path.isdir(we):
            return we
        # check appmanifest for exact installdir
        acf = os.path.join(lib, "steamapps", "appmanifest_431960.acf")
        if os.path.isfile(acf):
            try:
                with open(acf, "r", encoding="utf-8", errors="replace") as f:
                    txt = f.read()
                m = re.search(r'"installdir"\s*"([^"]+)"', txt)
                if m:
                    installdir = m.group(1)
                    candidate = os.path.join(lib, "steamapps", "common", installdir)
                    if os.path.isdir(candidate):
                        return candidate
            except Exception:
                pass
    # 3 — Fallback: old-style VDF parsing (numeric key)
    steam = _get_steam_path()
    if steam:
        vdf = os.path.join(steam, "steamapps", "libraryfolders.vdf")
        if os.path.isfile(vdf):
            try:
                with open(vdf, "r", encoding="utf-8", errors="replace") as f:
                    for line in f:
                        m = re.search(r'"(\d+)"\s+"(.+)"', line)
                        if m:
                            lib = m.group(2).replace("\\\\", "\\")
                            we = os.path.join(lib, "steamapps", "common", "wallpaper_engine")
                            if os.path.isdir(we):
                                return we
            except Exception:
                pass
    return ""

def find_depot_exe():
    if getattr(sys, 'frozen', False):
        base = sys._MEIPASS
        candidates = [os.path.join(base, "DepotDownloaderMod", "DepotDownloaderMod.exe")]
    else:
        base = os.path.dirname(os.path.abspath(__file__))
        candidates = [
            os.path.join(base, "DepotDownloaderMod", "DepotDownloaderMod.exe"),
            os.path.join(base, "WallpaperEngineWorkshopDownloader-1.0.4", "Release", "DepotDownloaderMod", "DepotDownloaderMod.exe"),
            r"C:\Users\Taha\Desktop\SteamToolsLua_Repo\WALLPAPER ENGİNE TRYİNG\WallpaperEngineWorkshopDownloader-1.0.4\Release\DepotDownloaderMod\DepotDownloaderMod.exe",
        ]
    for p in candidates:
        if p and os.path.isfile(p):
            return p
    return ""

# ---------------------------------------------------------------------------
#  Scraper  (supports artist search: simply pass artist name in searchtext)
# ---------------------------------------------------------------------------

class WorkshopScraper:
    _session = None

    @classmethod
    def _get_session(cls):
        if cls._session is None:
            cls._session = requests.Session()
            cls._session.headers.update({'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36'})
        return cls._session

    @classmethod
    def fetch_page(cls, query, page):
        sess = cls._get_session()
        params = {
            'appid': APP_ID,
            'searchtext': query,
            'browsesort': 'popular',
            'section': 'readytouseitems',
            'actualsort': 'trend',
            'p': str(page),
        }
        r = sess.get(WORKSHOP_SSR_URL, params=params, timeout=25)
        r.raise_for_status()
        html = r.text

        file_ids = re.findall(r'sharedfiles/filedetails/\?id=(\d+)', html)
        titles = re.findall(r'class="workshopItemTitle[^"]*"[^>]*>([^<]+)<', html)
        previews = re.findall(r'class="workshopItemPreviewImage[^"]*"[^>]*src="([^"]+)"', html)

        seen = set()
        items = []
        used_ids = 0
        for fid in file_ids:
            if fid not in seen:
                seen.add(fid)
                if used_ids < len(titles) and used_ids < len(previews):
                    items.append({
                        'file_id': fid,
                        'title': titles[used_ids],
                        'preview_url': previews[used_ids],
                    })
                used_ids += 1
        return items

# ---------------------------------------------------------------------------
#  Main App  (everything hardcoded + auto-detected, no settings UI)
# ---------------------------------------------------------------------------

class WallpaperBrowser(ctk.CTk):
    def __init__(self):
        super().__init__()
        self.title("Wallpaper Engine Workshop Browser")
        self.geometry("1100x800")
        self.minsize(900, 600)

        self.we_path = find_wallpaper_engine_path()
        self.depot_exe = find_depot_exe()

        self.current_page = 1
        self.current_items = []
        self.page_cache = {}
        self.image_cache = {}
        self.anim_state = {}
        self.search_text = ""
        self._busy = False
        self._has_next = True

        self.setup_ui()
        self.show_paths_in_status()
        self.load_page(1)

    # ------------------------------------------------------------------
    #  UI
    # ------------------------------------------------------------------
    def setup_ui(self):
        self.configure(fg_color=BG)

        self.top_frame = ctk.CTkFrame(self, height=50, corner_radius=10, fg_color=FRAME_BG)
        self.top_frame.pack(fill="x", padx=15, pady=(15, 3))
        ctk.CTkLabel(self.top_frame, text="Wallpaper Engine Workshop",
                     font=ctk.CTkFont(size=16, weight="bold"), text_color=FG
                     ).pack(side="left", padx=(15, 10))

        self.sframe = ctk.CTkFrame(self, height=44, corner_radius=10, fg_color=FRAME_BG)
        self.sframe.pack(fill="x", padx=15, pady=3)
        self.search_entry = ctk.CTkEntry(self.sframe, placeholder_text="Search by title or artist name...",
                                          height=32, fg_color=CARD_BG, border_color=BORDER, text_color=FG)
        self.search_entry.pack(side="left", fill="x", expand=True, padx=(10, 6), pady=6)
        self.search_entry.bind("<Return>", lambda e: self.on_search())
        self.search_btn = ctk.CTkButton(self.sframe, text="Search", width=90, height=32,
                                         fg_color=ACCENT, hover_color=ACCENT_HOVER, text_color=FG, command=self.on_search)
        self.search_btn.pack(side="left", padx=(0, 10), pady=6)
        self.loading_lbl = ctk.CTkLabel(self.sframe, text="", text_color=SECONDARY)
        self.loading_lbl.pack(side="left", padx=4)

        self.grid_frame = ctk.CTkScrollableFrame(self, corner_radius=10, fg_color=FRAME_BG)
        self.grid_frame.pack(fill="both", expand=True, padx=15, pady=3)
        self.grid_frame.grid_columnconfigure(0, weight=1)
        self.grid_frame.grid_columnconfigure(1, weight=1)
        self.grid_frame.grid_columnconfigure(2, weight=1)

        self.placeholder = ctk.CTkLabel(self.grid_frame, text="Loading wallpapers...",
                                         font=ctk.CTkFont(size=18), text_color=SECONDARY)
        self.placeholder.grid(row=0, column=1, pady=200)

        self.nav_frame = ctk.CTkFrame(self, height=48, corner_radius=10, fg_color=FRAME_BG)
        self.nav_frame.pack(fill="x", padx=15, pady=2)
        self.prev_btn = ctk.CTkButton(self.nav_frame, text="\u25c0  Prev", width=90, height=30,
                                       fg_color="#1a1a38", hover_color="#2a2a5a", text_color=FG,
                                       command=self.prev_page, state="disabled")
        self.prev_btn.pack(side="left", padx=(12, 4), pady=8)
        self.page_lbl = ctk.CTkLabel(self.nav_frame, text="", font=ctk.CTkFont(size=12, weight="bold"),
                                      text_color=FG, width=80)
        self.page_lbl.pack(side="left", padx=4, pady=8)
        self.next_btn = ctk.CTkButton(self.nav_frame, text="Next  \u25b6", width=90, height=30,
                                       fg_color="#1a1a38", hover_color="#2a2a5a", text_color=FG,
                                       command=self.next_page, state="disabled")
        self.next_btn.pack(side="left", padx=4, pady=8)
        self.count_lbl = ctk.CTkLabel(self.nav_frame, text="", font=ctk.CTkFont(size=11), text_color=SECONDARY)
        self.count_lbl.pack(side="left", padx=12, pady=8)

        self.status_frame = ctk.CTkFrame(self, height=36, corner_radius=8, fg_color=FRAME_BG)
        self.status_frame.pack(fill="x", padx=15, pady=(2, 10))
        self.status = ctk.CTkLabel(self.status_frame, text="", font=ctk.CTkFont(size=11), text_color=SECONDARY)
        self.status.pack(side="left", padx=12, pady=4)

    def show_paths_in_status(self):
        parts = []
        if self.we_path:
            parts.append(f"WE: {os.path.basename(self.we_path)}")
        else:
            parts.append("WE: not found")
        if self.depot_exe:
            parts.append(f"DD: {os.path.basename(os.path.dirname(self.depot_exe))}")
        else:
            parts.append("DD: not found")
        self.status.configure(text="  |  ".join(parts))

    def update_status(self, text, color="gray"):
        self.status.configure(text=text, text_color=color)

    def set_loading(self, on):
        self._busy = on
        self.loading_lbl.configure(text="Loading..." if on else "")
        self.search_btn.configure(state="disabled" if on else "normal")
        self.update()

    # ------------------------------------------------------------------
    #  Search / Navigation
    # ------------------------------------------------------------------
    def on_search(self):
        if self._busy:
            return
        self.set_loading(True)
        self.current_page = 1
        self.page_cache.clear()
        self.image_cache.clear()
        self.anim_state.clear()
        self._has_next = True
        self.search_text = self.search_entry.get().strip()
        threading.Thread(target=self.fetch_worker, args=(self.search_text, 1), daemon=True).start()

    def prev_page(self):
        if self._busy or self.current_page <= 1:
            return
        p = self.current_page - 1
        if p in self.page_cache:
            self.current_page = p
            self.display_results(self.page_cache[p], p)
        else:
            self.set_loading(True)
            self.current_page = p
            threading.Thread(target=self.fetch_worker, args=(self.search_text, p), daemon=True).start()

    def next_page(self):
        if self._busy or not self._has_next:
            return
        p = self.current_page + 1
        if p in self.page_cache:
            self.current_page = p
            self.display_results(self.page_cache[p], p)
        else:
            self.set_loading(True)
            self.current_page = p
            threading.Thread(target=self.fetch_worker, args=(self.search_text, p), daemon=True).start()

    def load_page(self, page):
        self.set_loading(True)
        threading.Thread(target=self.fetch_worker, args=(self.search_text, page), daemon=True).start()

    # ------------------------------------------------------------------
    #  Fetch worker
    # ------------------------------------------------------------------
    def fetch_worker(self, query, page):
        try:
            items = WorkshopScraper.fetch_page(query, page)
            self.after(0, self.display_results, items, page)
        except Exception as e:
            self.after(0, self.handle_error, str(e))

    def handle_error(self, msg):
        self.set_loading(False)
        self.update_status(f"Error: {msg}", "#FF5252")

    # ------------------------------------------------------------------
    #  Display
    # ------------------------------------------------------------------
    def display_results(self, items, page):
        self.set_loading(False)

        for w in self.grid_frame.winfo_children():
            w.destroy()
        try:
            self.placeholder.grid_forget()
        except Exception:
            pass

        self.current_items = items
        self.page_cache[page] = items

        if not items:
            self._has_next = False
            ctk.CTkLabel(self.grid_frame, text="No more wallpapers.",
                         font=ctk.CTkFont(size=16), text_color=SECONDARY).grid(row=0, column=1, pady=200)
            self.update_status("No more wallpapers")
            self.update_nav()
            return

        self._has_next = len(items) >= 1

        row, col = 0, 0
        for item in items:
            card = self.build_card(item)
            card.grid(row=row, column=col, padx=8, pady=8, sticky="nsew")
            col += 1
            if col >= COLUMNS:
                col = 0
                row += 1

        self.update_nav()
        self.update_status(f"Page {page} \u2014 {len(items)} wallpapers")

    def build_card(self, item):
        card = ctk.CTkFrame(self.grid_frame, corner_radius=8, border_width=1, border_color=BORDER, fg_color=CARD_BG)

        img_frame = ctk.CTkLabel(card, text="", width=THUMB_SIZE[0], height=THUMB_SIZE[1], fg_color="#12122a")
        img_frame.pack(padx=8, pady=(8, 4))
        img_frame.bind("<Button-1>", lambda e, fid=item['file_id']: webbrowser.open(
            f"https://steamcommunity.com/sharedfiles/filedetails/?id={fid}"))

        title = item['title'] if len(item['title']) <= 48 else item['title'][:45] + "..."
        ctk.CTkLabel(card, text=title, font=ctk.CTkFont(size=12, weight="bold"), text_color=FG
                     ).pack(padx=8, pady=(0, 2), anchor="w")
        ctk.CTkLabel(card, text=f"ID: {item['file_id']}", font=ctk.CTkFont(size=10), text_color=SECONDARY
                     ).pack(padx=8, pady=(0, 4), anchor="w")

        bf = ctk.CTkFrame(card, fg_color="transparent")
        bf.pack(fill="x", padx=8, pady=(0, 8))
        ctk.CTkButton(bf, text="Download", height=28, fg_color=ACCENT, hover_color=ACCENT_HOVER, text_color=FG,
                       command=lambda fid=item['file_id'], t=item['title']: self.download(fid, t)
                       ).pack(side="left", fill="x", expand=True, padx=(0, 3))
        ctk.CTkButton(bf, text="Open", height=28, fg_color="#1a1a38", hover_color="#2a2a5a", text_color=FG,
                       command=lambda fid=item['file_id']: webbrowser.open(
                           f"https://steamcommunity.com/sharedfiles/filedetails/?id={fid}")
                       ).pack(side="left", fill="x", expand=True, padx=(3, 0))

        if item.get('preview_url'):
            threading.Thread(target=self.load_thumb, args=(item['preview_url'], img_frame, item['file_id']),
                             daemon=True).start()
        return card

    # ------------------------------------------------------------------
    #  Thumbnail loading with GIF animation support
    # ------------------------------------------------------------------
    def load_thumb(self, url, label, file_id):
        try:
            cache_key = f"static_{url}"
            if cache_key in self.image_cache:
                if label.winfo_exists():
                    self.after(0, lambda: label.configure(image=self.image_cache[cache_key], text=""))
                return

            r = requests.get(url, timeout=10, headers={'User-Agent': 'Mozilla/5.0'})
            pil_img = Image.open(BytesIO(r.content))

            n_frames = getattr(pil_img, 'n_frames', 1)
            if n_frames > 1:
                frames_pil = []
                for frame in ImageSequence.Iterator(pil_img):
                    thumb = frame.copy()
                    thumb.thumbnail(THUMB_SIZE, Image.LANCZOS)
                    frames_pil.append(thumb)
                frames_ctk = [ctk.CTkImage(light_image=f, dark_image=f, size=f.size) for f in frames_pil]
                self.anim_state[file_id] = {'frames': frames_ctk, 'idx': 0, 'label': label}
                if label.winfo_exists():
                    self.after(0, lambda fid=file_id: self.tick_anim(fid))
            else:
                pil_img.thumbnail(THUMB_SIZE, Image.LANCZOS)
                img = ctk.CTkImage(light_image=pil_img, dark_image=pil_img, size=pil_img.size)
                self.image_cache[cache_key] = img
                if label.winfo_exists():
                    self.after(0, lambda: label.configure(image=img, text=""))
        except Exception:
            if label.winfo_exists():
                self.after(0, lambda: label.configure(text="No Preview"))

    def tick_anim(self, file_id):
        state = self.anim_state.get(file_id)
        if not state:
            return
        label = state['label']
        if not label.winfo_exists():
            self.anim_state.pop(file_id, None)
            return
        frames = state['frames']
        idx = state['idx']
        try:
            label.configure(image=frames[idx], text="")
        except Exception:
            pass
        state['idx'] = (idx + 1) % len(frames)
        self.after(150, self.tick_anim, file_id)

    # ------------------------------------------------------------------
    #  Navigation helpers
    # ------------------------------------------------------------------
    def update_nav(self):
        self.page_lbl.configure(text=f"Page {self.current_page}")
        self.prev_btn.configure(state="normal" if self.current_page > 1 else "disabled")
        self.next_btn.configure(state="normal" if self._has_next else "disabled")
        self.count_lbl.configure(text=f"{len(self.current_items)} items")

    # ------------------------------------------------------------------
    #  Download
    # ------------------------------------------------------------------
    def download(self, file_id, title):
        if not self.we_path or not os.path.isdir(self.we_path):
            self.update_status("Wallpaper Engine not found!", "#FF5252")
            return
        if not self.depot_exe or not os.path.isfile(self.depot_exe):
            self.update_status("DepotDownloader not found!", "#FF5252")
            return

        self.update_status(f"Downloading {title}...", "#FFD700")
        threading.Thread(target=self.dl_worker, args=(file_id, title), daemon=True).start()

    def dl_worker(self, file_id, title):
        try:
            target_dir = os.path.join(self.we_path, "projects", "myprojects", file_id)
            os.makedirs(target_dir, exist_ok=True)

            username = 'adgjl1182'
            password = ACCOUNTS[username]

            cmd = [
                self.depot_exe,
                "-app", APP_ID,
                "-pubfile", file_id,
                "-verify-all",
                "-username", username,
                "-password", password,
                "-dir", target_dir,
            ]

            proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                    text=True, creationflags=subprocess.CREATE_NO_WINDOW)
            for line in proc.stdout:
                if line.strip():
                    print(f"[DD] {line.strip()}")
            proc.wait()

            if proc.returncode == 0:
                self.after(0, lambda: self.update_status(f"Download complete: {title}", "#00E676"))
                self.after(0, lambda: messagebox.showinfo("Done", f"Downloaded to:\n{target_dir}"))
            else:
                self.after(0, lambda: self.update_status(f"Download failed (code {proc.returncode})", "#FF5252"))
        except Exception as e:
            self.after(0, lambda: self.update_status(f"Error: {e}", "#FF5252"))

    # ------------------------------------------------------------------
    #  Cleanup
    # ------------------------------------------------------------------
    def on_closing(self):
        try:
            subprocess.Popen("taskkill /f /im DepotDownloaderMod.exe 2>nul",
                             creationflags=subprocess.CREATE_NO_WINDOW, shell=True)
        except Exception:
            pass
        self.destroy()


if __name__ == "__main__":
    app = WallpaperBrowser()
    app.protocol("WM_DELETE_WINDOW", app.on_closing)
    app.mainloop()
