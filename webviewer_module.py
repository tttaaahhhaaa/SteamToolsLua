"""
SteamToolsLua Webviewer + Torrent Engine Module
Provides in-app browser (pywebview) and aria2c torrent integration.
"""
import threading, subprocess, json, os, time, re, shutil, queue
import requests
from pathlib import Path

# ---- Torrent Engine (aria2c wrapper) ----
class TorrentEngine:
    """Manages aria2c for BitTorrent downloads."""
    def __init__(self, aria2c_path=None, download_dir=None, log_func=None):
        self.aria2c = aria2c_path or str(Path(__file__).resolve().parent / 'aria2c.exe')
        self.download_dir = Path(download_dir or os.environ['TEMP']) / 'SteamToolsLua_Torrents'
        self.download_dir.mkdir(parents=True, exist_ok=True)
        self.log = log_func or (lambda *a: None)
        self._proc = None
        self._rpc_port = 16801
        self._rpc_token = 'steamtools'
        self._rpc_secret = f'token:{self._rpc_token}'
        self._active = {}
    
    def start(self):
        """Start aria2c with JSON-RPC enabled."""
        if self._proc and self._proc.poll() is None:
            return True  # Already running
        cmd = [
            self.aria2c,
            '--enable-rpc', f'--rpc-listen-port={self._rpc_port}',
            f'--rpc-secret={self._rpc_token}',
            '--rpc-listen-all=false', '--rpc-allow-origin-all=true',
            '--continue=true', f'--dir={self.download_dir}',
            '--max-connection-per-server=4', '--split=4',
            '--bt-max-peers=50', '--seed-time=0',
            '--summary-interval=0', '--console-log-level=warn'
        ]
        try:
            self._proc = subprocess.Popen(
                cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                creationflags=0x08000000  # CREATE_NO_WINDOW
            )
            self.log('[Torrent] aria2c başlatıldı')
            return True
        except Exception as e:
            self.log(f'[Torrent] aria2c başlatılamadı: {e}')
            return False
    
    def stop(self):
        """Shutdown aria2c."""
        if self._proc and self._proc.poll() is None:
            self._proc.terminate()
            self._proc.wait(timeout=5)
            self.log('[Torrent] aria2c durduruldu')
    
    def add_torrent(self, torrent_path, select_files=None):
        """Add a .torrent file for download. Returns gid if successful."""
        if not Path(torrent_path).exists():
            self.log(f'[Torrent] Dosya bulunamadı: {torrent_path}')
            return None
        import requests as _req
        # Build payload
        params = {'uris': [str(Path(torrent_path).resolve())]}
        if select_files:
            # aria2c uses index-based file selection (0-indexed)
            params['select-file'] = ','.join(str(i) for i in select_files)
        payload = {
            'jsonrpc': '2.0', 'id': 'stluator',
            'method': 'aria2.addTorrent',
            'params': [self._rpc_secret]
        }
        # Add torrent file as URI
        payload['params'].append(str(Path(torrent_path).resolve()))
        try:
            r = _req.post(f'http://localhost:{self._rpc_port}/jsonrpc',
                          json=payload, timeout=5)
            result = r.json()
            if 'result' in result:
                gid = result['result']
                self._active[gid] = torrent_path
                self.log(f'[Torrent] Eklendi: {Path(torrent_path).name} [{gid}]')
                return gid
            else:
                self.log(f'[Torrent] RPC hatası: {result}')
        except Exception as e:
            self.log(f'[Torrent] Ekleme hatası: {e}')
        return None
    
    def add_uri(self, uri):
        """Add a magnet link or HTTP URL. Returns gid."""
        import requests as _req
        payload = {
            'jsonrpc': '2.0', 'id': 'stluator',
            'method': 'aria2.addUri',
            'params': [[self._rpc_secret], [uri]]
        }
        try:
            r = _req.post(f'http://localhost:{self._rpc_port}/jsonrpc',
                          json=payload, timeout=5)
            result = r.json()
            if 'result' in result:
                gid = result['result']
                self._active[gid] = uri
                self.log(f'[Torrent] URI eklendi: {uri[:50]}... [{gid}]')
                return gid
        except Exception as e:
            self.log(f'[Torrent] URI hatası: {e}')
        return None
    
    def get_status(self, gid=None):
        """Get download status. If gid is None, get all active."""
        import requests as _req
        if gid:
            payload = {
                'jsonrpc': '2.0', 'id': 'stluator',
                'method': 'aria2.tellStatus',
                'params': [self._rpc_secret, gid]
            }
        else:
            payload = {
                'jsonrpc': '2.0', 'id': 'stluator',
                'method': 'aria2.tellActive',
                'params': [self._rpc_secret]
            }
        try:
            r = _req.post(f'http://localhost:{self._rpc_port}/jsonrpc',
                          json=payload, timeout=5)
            result = r.json()
            return result.get('result', [])
        except:
            return []
    
    def get_files(self, gid):
        """Get file list for a download."""
        import requests as _req
        payload = {
            'jsonrpc': '2.0', 'id': 'stluator',
            'method': 'aria2.getFiles',
            'params': [self._rpc_secret, gid]
        }
        try:
            r = _req.post(f'http://localhost:{self._rpc_port}/jsonrpc',
                          json=payload, timeout=5)
            result = r.json()
            return result.get('result', [])
        except:
            return []
    
    def wait_for_complete(self, gid, check_interval=2, timeout=7200):
        """Wait for download to complete. Returns True if complete."""
        elapsed = 0
        while elapsed < timeout:
            status = self.get_status(gid)
            if isinstance(status, dict) and status.get('status') == 'complete':
                self.log(f'[Torrent] İndirme tamam: {gid}')
                return True
            time.sleep(check_interval)
            elapsed += check_interval
        self.log(f'[Torrent] Zaman aşımı: {gid}')
        return False


# ---- Webviewer (pywebview) ----
class Webviewer:
    """In-app browser using pywebview for Online Fix interaction."""
    
    def __init__(self, log_func=None):
        self.log = log_func or (lambda *a: None)
        self._window = None
        self._started = threading.Event()
    
    def open_page_blocking(self, url, title='Online-Fix.me', width=1200, height=800):
        """Open a game page in a pywebview window (blocking)."""
        import webview
        self._started.set()
        self._window = webview.create_window(
            title, url=url,
            width=width, height=height,
            resizable=True, min_size=(800, 600)
        )
        webview.start(private_mode=False)
    
    def open_page_async(self, url, title='Online-Fix.me', on_close=None):
        """Open page in a separate thread. Returns immediately."""
        def _run():
            try:
                self.open_page_blocking(url, title)
            except Exception as e:
                self.log(f'[Webviewer] Hata: {e}')
            finally:
                if on_close: on_close()
        t = threading.Thread(target=_run, daemon=True)
        t.start()
        self._started.wait(timeout=5)
        return t
    
    def inject_js(self, js_code):
        """Execute JS in the webviewer page."""
        if self._window:
            try:
                self._window.evaluate_js(js_code)
            except:
                pass
    
    def extract_links_from_page(self, html):
        """Extract download links from game page HTML."""
        links = {'structured': [], 'ext': [], 'torrent': [], 'mega': [], 'other': []}
        for m in re.finditer(r'(https?://(?:uploads|hosters|drive|torrents)\.online-fix\.me:\d+/[^"\'<>]+)', html):
            links['structured'].append(m.group(1).rstrip('/'))
        for m in re.finditer(r'(https?://uploads\.online-fix\.me:\d+/(?:uploads|torrents)/[^"\'<>]+)', html):
            links['structured'].append(m.group(1).rstrip('/'))
        for m in re.finditer(r'href="(https?://online-fix\.me/ext/[^"]+)"', html):
            links['ext'].append(m.group(1))
        for m in re.finditer(r'href="(https?://[^"]+\.torrent)"', html):
            links['torrent'].append(m.group(1))
        for m in re.finditer(r'(https?://mega\.nz/[^"\'<>]+)', html):
            links['mega'].append(m.group(1))
        return links
    
    def __del__(self):
        if self._window:
            try: self._window.destroy()
            except: pass


class OnlineFixFlow:
    """Orchestrates the complete Online Fix download flow."""
    
    def __init__(self, game_name, appid, save_path, log_func=None, indicator_func=None, find_steam_func=None):
        self.game_name = game_name
        self.appid = appid
        self.save_path = Path(save_path) if save_path else None
        self.log = log_func or (lambda *a: None)
        self._set_indicator = indicator_func or (lambda *a: None)
        self._find_func = find_steam_func
        self._out_dir = None
        self._webviewer = None
        self._torrent = None
    
    def run(self):
        """Execute the full Online Fix flow."""
        self._set_indicator('Online-Fix aranıyor...', 'working')
        sess = requests.Session()
        sess.headers.update({'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36'})
        
        # 1) Find game page URL
        game_url = self._find_game_page(sess)
        if not game_url:
            self._set_indicator('Online-Fix: bulunamadı', 'offline')
            return
        
        self.log(f'[OnlineFix] Sayfa: {game_url}')
        
        # 2) Fetch game page
        r = sess.get(game_url, timeout=15)
        phpsessid = sess.cookies.get('PHPSESSID', '')
        html = r.text
        
        # 3) Extract all download links
        links = Webviewer().extract_links_from_page(html)
        
        # 4) Setup output directory
        _safe_name = re.sub(r'[\\/:*?"<>|]', '_', self.game_name.strip())[:80]
        _of_root = self.save_path / 'Online Fixes' if self.save_path else Path(os.environ['TEMP']) / 'SteamToolsLua' / 'OnlineFix'
        _of_root.mkdir(parents=True, exist_ok=True)
        self._out_dir = _of_root / _safe_name
        self._out_dir.mkdir(parents=True, exist_ok=True)
        
        # 5) Open webviewer for user interaction
        self._webviewer = Webviewer(log_func=self.log)
        self._webviewer.open_page_async(game_url, title=f'Online-Fix: {self.game_name}')
        
        # 6) Try structured download first (fastest)
        if links['structured']:
            self._download_structured(sess, links['structured'], phpsessid)
            return
        
        # 7) Try torrent
        if links['torrent']:
            self._download_torrent(links['torrent'])
            return
        
        # 8) ext/Mega links - handle in webviewer
        if links['ext'] or links['mega']:
            self._handle_custom_links(links, sess, phpsessid)
            return
        
        self.log('[OnlineFix] Hiçbir indirme linki bulunamadı.')
        self._set_indicator('Online-Fix: link yok', 'offline')
    
    def _find_game_page(self, sess):
        """Search online-fix.me for the game page URL."""
        import urllib.parse
        game_url = None
        # Try by appid first
        if self.appid:
            r = sess.get(f'https://online-fix.me/index.php?do=search&subaction=search&story={self.appid}', timeout=15)
            for m in re.finditer(r'href="(https://online-fix\.me/games/[^"]+\.html)"', r.text):
                candidate = m.group(1).split('#')[0]
                if candidate:
                    game_url = candidate
                    break
        # Try by name
        if not game_url:
            q = urllib.parse.quote(self.game_name[:80].strip())
            r = sess.get(f'https://online-fix.me/index.php?do=search&subaction=search&story={q}', timeout=15)
            seen = set()
            candidates = []
            for m in re.finditer(r'href="(https://online-fix\.me/games/[^"]+\.html)"', r.text):
                u = m.group(1).split('#')[0]
                if u not in seen:
                    seen.add(u)
                    candidates.append(u)
            search_words = set(re.findall(r'[a-z0-9]+', self.game_name.lower()))
            best_score = 0
            for c in candidates:
                slug = c.split('/')[-1].replace('.html', '').replace('-po-seti', '').replace('-', ' ').lower()
                words = set(re.findall(r'[a-z0-9]+', slug))
                score = len(search_words & words)
                if score > best_score:
                    best_score = score
                    game_url = c
        return game_url
    
    def _download_structured(self, sess, dl_urls, phpsessid):
        """Download from structured upload URLs (current logic)."""
        _of_headers = {'User-Agent': 'Mozilla/5.0', 'Referer': 'https://online-fix.me/'}
        _of_cookies = {'PHPSESSID': phpsessid}
        
        # Try Fix Repair
        upload_base = None
        for u in dl_urls:
            if '/uploads/' in u:
                upload_base = u
                break
        if upload_base:
            repair_url = upload_base + '/Fix%20Repair/'
            self.log(f'[OnlineFix] Fix Repair aranıyor: {repair_url}')
            hr = sess.get(repair_url, timeout=15, headers=_of_headers, cookies=_of_cookies)
            if hr.status_code == 200 and 'Generic.rar' in hr.text:
                for m2 in re.finditer(r'<a href="([^"]+Generic[^"]*\.rar)"', hr.text, re.IGNORECASE):
                    self._download_and_extract(sess, m2.group(1), repair_url, _of_headers, _of_cookies)
                    return
        
        # Try torrent
        tor_base = None
        for u in dl_urls:
            if '/torrents/' in u:
                tor_base = u
                break
        if tor_base:
            self.log(f'[OnlineFix] Torrent aranıyor: {tor_base}')
            ht = sess.get(tor_base, timeout=15, headers=_of_headers, cookies=_of_cookies)
            if ht.status_code == 200:
                for m2 in re.finditer(r'<a href="([^"]+\.torrent)"', ht.text):
                    tor_path = m2.group(1)
                    tor_url = tor_path if tor_path.startswith('http') else tor_base.rstrip('/') + '/' + tor_path
                    self.log(f'[OnlineFix] Torrent indiriliyor: {tor_path}')
                    self._set_indicator('Online-Fix torrent...', 'working')
                    rt = sess.get(tor_url, timeout=120, headers=_of_headers, cookies=_of_cookies)
                    if rt.status_code != 200: break
                    _tor_path = self._out_dir / tor_path.split('/')[-1]
                    with open(str(_tor_path), 'wb') as f: f.write(rt.content)
                    self.log(f'[OnlineFix] Torrent kaydedildi: {_tor_path}')
                    # Start torrent via aria2c
                    self._start_torrent_download(_tor_path)
                    return
    
    def _download_and_extract(self, sess, rar_path, base_url, headers, cookies):
        """Download a .rar file and extract it."""
        rar_url = rar_path if rar_path.startswith('http') else base_url.rstrip('/') + '/' + rar_path
        rar_name = rar_path.split('/')[-1] if '/' in rar_path else rar_path
        _dl_path = self._out_dir / rar_name
        self.log(f'[OnlineFix] İndiriliyor: {rar_name}')
        self._set_indicator('Online-Fix indiriliyor...', 'working')
        rd = sess.get(rar_url, stream=True, timeout=120, headers=headers, cookies=cookies)
        if rd.status_code != 200:
            self.log(f'[OnlineFix] HTTP {rd.status_code}')
            self._set_indicator('Online-Fix: HTTP hatası', 'offline')
            return
        total = int(rd.headers.get('Content-Length', 0))
        downloaded = 0
        t0 = time.time()
        with open(str(_dl_path), 'wb') as f:
            for chunk in rd.iter_content(chunk_size=65536):
                if chunk:
                    f.write(chunk)
                    downloaded += len(chunk)
                    if total:
                        pct = int(downloaded * 100 / total)
                        elapsed = time.time() - t0
                        speed = downloaded / elapsed / 1024 if elapsed > 0 else 0
                        self._set_indicator(f'OnlineFix: %{pct} ({speed:.0f} KB/s)', 'working')
        self.log(f'[OnlineFix] İndirme tamam: {_dl_path}')
        self._extract_and_inject(_dl_path)
    
    def _extract_and_inject(self, rar_path):
        """Extract rar with 7z, then inject into Steam if found."""
        _extract_dir = self._out_dir / rar_path.stem
        _extract_ok = False
        for _pw in ('', 'knkm', 'online-fix.me'):
            try:
                _cmd = ['7z', 'x', str(rar_path), f'-o{str(_extract_dir)}', '-y']
                if _pw: _cmd.append(f'-p{_pw}')
                _result = subprocess.run(_cmd, capture_output=True, text=True, timeout=60, creationflags=0x08000000)
                if _result.returncode == 0:
                    _extract_ok = True
                    break
            except: pass
        if _extract_ok:
            self.log(f'[OnlineFix] Çıkartıldı: {_extract_dir}')
            try: rar_path.unlink(); self.log('[OnlineFix] .rar silindi')
            except: pass
            # Try to inject via global find_steam_game_path
            try:
                _find_func = getattr(self, '_find_func', None)
                if _find_func is None:
                    # Try to get from global
                    import __main__
                    _st_app = getattr(__main__, 'SteamApp', None)
                    if _st_app and hasattr(_st_app, '_find_steam_game_path'):
                        _find_func = _st_app._find_steam_game_path
                if _find_func:
                    _steam_path = _find_func(self.appid)
                    if _steam_path:
                        self.log(f'[OnlineFix] Steam oyunu bulundu: {_steam_path}')
                        for _src in _extract_dir.rglob('*'):
                            if _src.is_file():
                                try: _rel = str(_src.relative_to(_extract_dir))
                                except: _rel = _src.name
                                _dst = _steam_path / _rel
                                _dst.parent.mkdir(parents=True, exist_ok=True)
                                shutil.copy2(str(_src), str(_dst))
                        self._set_indicator('OnlineFix oyuna eklendi', 'online')
                        os.startfile(str(_steam_path))
                        return
            except: pass
            self._set_indicator('OnlineFix hazır', 'online')
            os.startfile(str(_extract_dir))
        else:
            self.log('[OnlineFix] 7z çıkartılamadı')
            os.startfile(str(self._out_dir))
    
    def _download_torrent(self, torrent_urls):
        """Download using torrent via aria2c."""
        import urllib.request
        _tor_path = self._out_dir / 'game.torrent'
        # Download the .torrent file
        try:
            urllib.request.urlretrieve(torrent_urls[0], str(_tor_path))
            self.log(f'[OnlineFix] Torrent dosyası indirildi: {_tor_path}')
        except:
            self.log('[OnlineFix] Torrent dosyası indirilemedi')
            return
        self._start_torrent_download(_tor_path)
    
    def _start_torrent_download(self, torrent_path):
        """Start torrent download via aria2c."""
        self._set_indicator('Torrent başlatılıyor...', 'working')
        tor = get_torrent_engine()
        gid = tor.add_torrent(str(torrent_path))
        if gid:
            self._set_indicator('Torrent indiriliyor...', 'working')
            # Monitor progress
            def _monitor():
                while True:
                    status = tor.get_status(gid)
                    if isinstance(status, dict):
                        s = status.get('status', '')
                        if s == 'complete':
                            self.log('[Torrent] İndirme tamam!')
                            # Find downloaded files
                            files = tor.get_files(gid)
                            for f in files:
                                fpath = Path(f.get('path', ''))
                                if fpath.exists() and fpath.suffix.lower() in ('.rar', '.zip', '.7z'):
                                    self._extract_and_inject(fpath)
                                    return
                                elif fpath.exists():
                                    # Copy to out_dir
                                    try:
                                        shutil.copy2(str(fpath), str(self._out_dir / fpath.name))
                                    except: pass
                            self._set_indicator('Torrent tamam', 'online')
                            os.startfile(str(self._out_dir))
                            return
                        elif s == 'error':
                            self.log(f'[Torrent] Hata: {status.get("errorMessage", "")}')
                            self._set_indicator('Torrent hatası', 'offline')
                            return
                        elif 'downloadSpeed' in status:
                            speed = int(status['downloadSpeed']) // 1024
                            completed = int(status.get('completedLength', 0))
                            total_len = int(status.get('totalLength', 0))
                            if total_len > 0:
                                pct = int(completed * 100 / total_len)
                                self._set_indicator(f'Torrent: %{pct} ({speed} KB/s)', 'working')
                    time.sleep(2)
            threading.Thread(target=_monitor, daemon=True).start()
        else:
            self._set_indicator('Torrent başlatılamadı', 'offline')
            # Fallback: open .torrent file
            os.startfile(str(torrent_path))
    
    def _handle_custom_links(self, links, sess, phpsessid):
        """Handle ext/Mega links via webviewer interaction."""
        _pwd = ''
        self._set_indicator('Özel linkler işleniyor...', 'working')
        self.log(f'[OnlineFix] {len(links["ext"]) + len(links["mega"])} custom link')
        # Open ext links in webviewer (they're already shown in the webviewer)
        # Just log what was found
        if links['ext']:
            self.log(f'[OnlineFix] ext linkler: {links["ext"][:3]}')
        if links['mega']:
            self.log(f'[OnlineFix] Mega linkler: {links["mega"][:3]}')


# ---- Global instances ----
_torrent_engine = None
_webviewer_instance = None

def get_torrent_engine():
    global _torrent_engine
    if _torrent_engine is None:
        _torrent_engine = TorrentEngine()
        _torrent_engine.start()
    return _torrent_engine

def get_webviewer():
    global _webviewer_instance
    if _webviewer_instance is None:
        _webviewer_instance = Webviewer()
    return _webviewer_instance
