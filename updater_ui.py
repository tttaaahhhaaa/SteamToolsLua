import tkinter as tk
from tkinter import ttk, messagebox
import requests, json, os, sys, subprocess, time, re
from pathlib import Path
import ctypes

ctypes.windll.shcore.SetProcessDpiAwareness(2)

REPO = "tttaaahhhaaa/SteamToolsLua"
TOKEN_ENV = "STEAMTOOLS_PAT"

def parse_version(v):
    v = v.lstrip('vV')
    parts = v.split('.')
    try:
        return tuple(int(p) for p in parts[:3])
    except:
        return (0, 0, 0)

def get_current_version():
    for i, a in enumerate(sys.argv):
        if a == '--current' and i + 1 < len(sys.argv):
            return sys.argv[i + 1]
    try:
        p = Path(os.environ.get('APPDATA', '')) / 'SteamToolsLua' / 'version.txt'
        if p.exists(): return p.read_text(encoding='utf-8').strip()
    except: pass
    return '0.0.0'

def get_current_exe():
    if getattr(sys, 'frozen', False):
        return Path(sys.argv[0]).resolve()
    return Path(sys.argv[0]).resolve()

def get_token():
    t = os.environ.get(TOKEN_ENV, '')
    if t: return t
    try:
        p = Path(os.environ.get('APPDATA', '')) / 'SteamToolsLua' / '.pat'
        if p.exists(): return p.read_text(encoding='utf-8').strip()
    except: pass
    return ''

def fetch_releases():
    token = get_token()
    headers = {'User-Agent': 'SteamToolsLua-Updater'}
    if token: headers['Authorization'] = f'token {token}'
    r = requests.get(f'https://api.github.com/repos/{REPO}/releases?per_page=20', headers=headers, timeout=15)
    r.raise_for_status()
    return r.json()

def download_asset(url, dest, token):
    headers = {'User-Agent': 'SteamToolsLua-Updater'}
    if token: headers['Authorization'] = f'token {token}'
    r = requests.get(url, headers=headers, timeout=300, stream=True)
    r.raise_for_status()
    with open(str(dest), 'wb') as f:
        for chunk in r.iter_content(8192):
            if chunk: f.write(chunk)

class UpdaterUI:
    def __init__(self):
        self.root = tk.Tk()
        self.root.title("SteamToolsLua Updater")
        self.root.geometry("680x520")
        self.root.configure(bg='#0d1724')
        self.root.resizable(True, True)
        self.root.minsize(500, 350)
        self.current_exe = get_current_exe()
        self.current_dir = self.current_exe.parent
        self.current_ver = get_current_version()

        self._setup_ui()
        self._load_releases()

    def _setup_ui(self):
        header = tk.Label(self.root, text="SteamToolsLua Updater",
                          fg='#8fd3ff', bg='#0d1724', font=('Segoe UI', 16, 'bold'))
        header.pack(pady=(14, 2))

        info = tk.Label(self.root, text=f"Current: v{self.current_ver}  |  {self.current_exe.name}",
                        fg='#686880', bg='#0d1724', font=('Segoe UI', 10))
        info.pack(pady=(0, 8))

        frame = tk.Frame(self.root, bg='#0d1724')
        frame.pack(fill=tk.BOTH, expand=True, padx=16, pady=4)

        cols = ('version', 'name', 'status')
        self.tree = ttk.Treeview(frame, columns=cols, show='headings', height=12)
        self.tree.heading('version', text='Version')
        self.tree.heading('name', text='Name')
        self.tree.heading('status', text='Status')
        self.tree.column('version', width=100, anchor='center')
        self.tree.column('name', width=380, anchor='w')
        self.tree.column('status', width=120, anchor='center')

        scroll = ttk.Scrollbar(frame, orient=tk.VERTICAL, command=self.tree.yview)
        self.tree.configure(yscrollcommand=scroll.set)
        self.tree.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        scroll.pack(side=tk.RIGHT, fill=tk.Y)

        style = ttk.Style()
        style.theme_use('alt')
        style.configure('Treeview', background='#0a0a16', foreground='#e0e0f0',
                        fieldbackground='#0a0a16', rowheight=28, font=('Segoe UI', 10))
        style.configure('Treeview.Heading', background='#1a1a38', foreground='#b098ff',
                        font=('Segoe UI', 10, 'bold'))

        btn_frame = tk.Frame(self.root, bg='#0d1724')
        btn_frame.pack(fill=tk.X, padx=16, pady=10)

        self.status_lbl = tk.Label(btn_frame, text='', fg='#f6ad55', bg='#0d1724', font=('Segoe UI', 10))
        self.status_lbl.pack(side=tk.LEFT)

        self.update_btn = tk.Button(btn_frame, text="Update", font=('Segoe UI', 12, 'bold'),
                                     bg='#2d6a4f', fg='#ffffff', relief=tk.FLAT, padx=24, pady=6,
                                     activebackground='#3d8a6f', cursor='hand2',
                                     command=self._do_update)
        self.update_btn.pack(side=tk.RIGHT)
        self.update_btn.config(state=tk.DISABLED)

        close_btn = tk.Button(btn_frame, text="Close", font=('Segoe UI', 11),
                               bg='#333355', fg='#e0e0f0', relief=tk.FLAT, padx=16, pady=6,
                               activebackground='#444477', cursor='hand2',
                               command=self.root.destroy)
        close_btn.pack(side=tk.RIGHT, padx=(0, 8))

    def _load_releases(self):
        self.releases = []
        try:
            data = fetch_releases()
            latest_tag = data[0]['tag_name'] if data else ''
            for rel in data:
                tag = rel.get('tag_name', '?')
                name = rel.get('name', '') or tag
                is_current = (tag == f'v{self.current_ver}' or tag == self.current_ver)
                status = '✓ Current' if is_current else ''
                self.releases.append({'tag': tag, 'name': name, 'is_current': is_current,
                                       'assets': rel.get('assets', []), 'prerelease': rel.get('prerelease', False)})
                tags = f'  {tag}'
                self.tree.insert('', tk.END, values=(tags, name, status), tags=('current' if is_current else 'normal'))
            self.tree.tag_configure('current', foreground='#48bb78')
            # Find latest non-prerelease newer than current
            self.update_target = None
            _cv = parse_version(self.current_ver)
            for rel in data:
                if not rel.get('prerelease', False) and rel.get('assets'):
                    tag = rel.get('tag_name', '')
                    if parse_version(tag) > _cv:
                        self.update_target = rel
                        break
            if self.update_target:
                self.update_btn.config(state=tk.NORMAL)
                self.status_lbl.config(text=f"Ready to update to {self.update_target['tag_name']}", fg='#48bb78')
            else:
                self.status_lbl.config(text="No newer release found", fg='#f6ad55')
        except Exception as ex:
            self.status_lbl.config(text=f"Error: {ex}", fg='#f56565')
            self.tree.insert('', tk.END, values=('Error', str(ex), ''), tags=('normal',))

    def _do_update(self):
        if not self.update_target: return
        tag = self.update_target['tag_name']
        assets = self.update_target['assets']
        exe_asset = None
        for a in assets:
            if a['name'].lower().endswith('.exe'):
                exe_asset = a
                break
        if not exe_asset:
            messagebox.showerror("Error", "No .exe asset found in release.")
            return

        dest = self.current_dir / f"SteamToolsLua_{tag}.exe"
        token = get_token()
        try:
            self.status_lbl.config(text=f"Downloading {tag}...", fg='#f6ad55')
            self.update_btn.config(state=tk.DISABLED)
            self.root.update()

            dl_url = exe_asset.get('browser_download_url', '') or exe_asset['url']
            if 'api.github.com' in dl_url:
                token = get_token()
            download_asset(dl_url, dest, token)
            self.status_lbl.config(text=f"Downloaded {tag}. Installing...", fg='#f6ad55')
            self.root.update()

            # Kill old process
            old_name = self.current_exe.name
            subprocess.run(['taskkill', '/f', '/im', old_name], capture_output=True, timeout=10)
            time.sleep(1)

            # Replace old with new: try to delete old, retry 3 times
            for attempt in range(3):
                try:
                    if self.current_exe.exists():
                        self.current_exe.unlink()
                    break
                except:
                    if attempt < 2:
                        time.sleep(2)
                    else:
                        messagebox.showwarning("Warning",
                            f"Could not delete {old_name}.\n"
                            f"Please close it manually.\n\n"
                            f"New version saved as:\n{dest.name}")
                        subprocess.Popen([str(dest)])
                        self.root.destroy()
                        return

            dest.rename(self.current_exe)
            subprocess.Popen([str(self.current_exe)])
            self.root.destroy()
        except Exception as ex:
            self.status_lbl.config(text=f"Update failed: {ex}", fg='#f56565')
            self.update_btn.config(state=tk.NORMAL)

if __name__ == '__main__':
    app = UpdaterUI()
    app.root.mainloop()
