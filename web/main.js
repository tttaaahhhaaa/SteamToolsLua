/* ── WebView2 IPC Bridge (replaces eel) ──────────────────────── */
let _msgId = 0;
const _callbacks = {};
const _isWebView2 = typeof window.chrome !== 'undefined' && window.chrome.webview;

if (_isWebView2) {
  window.chrome.webview.addEventListener('message', e => {
    const msg = JSON.parse(e.data);
    if (msg.id && _callbacks[msg.id]) {
      _callbacks[msg.id](msg.result);
      delete _callbacks[msg.id];
    }
  });
}

function _callEel(cmd, args) {
  return new Promise((resolve) => {
    if (_isWebView2) {
      const id = ++_msgId;
      _callbacks[id] = resolve;
      window.chrome.webview.postMessage(JSON.stringify({ id, cmd, args }));
    } else {
      fetch('/api/' + cmd, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(args)
      }).then(r => r.json()).then(resolve).catch(() => resolve(null));
    }
  });
}

const eel = new Proxy({}, {
  get(target, name) {
    if (name === 'expose') return () => {};
    return (...args) => {
      const a = args.length <= 1 ? (args[0] || null) : args;
      return () => _callEel(name, a);
    };
  }
});

/* ── SteamToolsLua v2.0.0 Frontend ──────────────────────────────────── */
let allGames = [], filteredGames = [], selectedGame = null;
const PAGE_SIZE = 25;
let currentPage = 1, currentTab = 'all', searchQuery = '';
const visitedPages = new Set();
window._cache = {};

document.addEventListener('DOMContentLoaded', async () => {
  document.getElementById('loaderText').textContent = 'Initializing...';
  initNavigation();
  initRightPanel();
  document.getElementById('loaderText').textContent = 'Scanning Steam...';
  await checkSteam();
  document.getElementById('loaderText').textContent = 'Loading settings...';
  await loadSettings();
  document.getElementById('loaderText').textContent = 'Loading AI providers...';
  await loadAIProviders();
  document.getElementById('loader').style.display = 'none';
  // background scan
  scanGames();
});

/* ── Navigation ──────────────────────────────────────────────────────────── */
function initNavigation() {
  document.querySelectorAll('.nav-btn').forEach(btn => {
    btn.addEventListener('click', () => {
      document.querySelectorAll('.nav-btn').forEach(b => b.classList.remove('active'));
      btn.classList.add('active');
      document.querySelectorAll('.page').forEach(p => p.classList.remove('active'));
      const page = document.getElementById('page-' + btn.dataset.page);
      if (page) page.classList.add('active');
      if (btn.dataset.page === 'license') checkLicense();
    });
  });
}

function switchPage(name) {
  document.querySelectorAll('.nav-btn').forEach(b => b.classList.toggle('active', b.dataset.page === name));
  document.querySelectorAll('.page').forEach(p => p.classList.remove('active'));
  const p = document.getElementById('page-' + name);
  if (p) p.classList.add('active');
}

/* ── Toast ──────────────────────────────────────────────────────────────── */
function toast(msg, type = 'info') {
  const t = document.getElementById('toast');
  const el = document.createElement('div');
  el.className = 'toast ' + type;
  el.textContent = msg;
  t.appendChild(el);
  setTimeout(() => { el.style.opacity = '0'; setTimeout(() => el.remove(), 300); }, 3000);
}

function showLoader(show) {
  document.getElementById('loader').style.display = show ? 'flex' : 'none';
}

/* ── Steam ──────────────────────────────────────────────────────────────── */
async function checkSteam() {
  const data = await eel.scan_games()();
  const dot = document.getElementById('steamDot');
  const lbl = document.getElementById('steamLabel');
  if (data && data.steam_path) {
    dot.className = 'status-dot online';
    lbl.textContent = 'Steam: ' + data.steam_path;
  } else {
    dot.className = 'status-dot offline';
    lbl.textContent = 'Steam not found';
    toast('Steam not found!', 'error');
  }
}

/* ── Games Scanning ─────────────────────────────────────────────────────── */
async function scanGames() {
  showLoader(true);
  try {
    const data = await eel.scan_games()();
    if (data.error) { toast(data.error, 'error'); showLoader(false); return; }
    const merged = [], seen = new Set();
    for (const g of (data.installed || [])) {
      if (!seen.has(g.appid)) { seen.add(g.appid); merged.push({ ...g, type: 'installed' }); }
    }
    for (const g of (data.depotcache || [])) {
      if (!seen.has(g.appid)) { seen.add(g.appid); merged.push({ ...g, type: 'depot' }); }
    }
    for (const g of (data.library || [])) {
      if (!seen.has(g.appid)) { seen.add(g.appid); merged.push({ ...g, type: 'library' }); }
    }
    allGames = merged;
    document.getElementById('gameCount').textContent = allGames.length;
    document.getElementById('versionBadge').textContent = '2.0.0';
    applyFilter();
    toast(`Found ${allGames.length} games`, 'success');
  } catch (e) { toast('Scan failed: ' + e, 'error'); }
  showLoader(false);
}

/* ── Filter / Virtual Scroll ────────────────────────────────────────────── */
function switchTab(tab) {
  currentTab = tab;
  currentPage = 1;
  document.querySelectorAll('.tab-btn').forEach(b => b.classList.toggle('active', b.dataset.tab === tab));
  applyFilter();
}

function filterGames() {
  searchQuery = document.getElementById('gameSearch').value.toLowerCase();
  currentPage = 1;
  applyFilter();
}

function applyFilter() {
  filteredGames = allGames.filter(g => {
    if (currentTab === 'installed' && g.type !== 'installed') return false;
    if (currentTab === 'depot' && g.type !== 'depot') return false;
    if (currentTab === 'library' && g.type !== 'library') return false;
    if (currentTab === 'verified' && !window._cache?.[g.name]) return false;
    if (searchQuery) {
      return String(g.appid).includes(searchQuery) || g.name.toLowerCase().includes(searchQuery);
    }
    return true;
  });
  renderVirtual();
}

function renderVirtual() {
  const content = document.getElementById('vsContent');
  const pagination = document.getElementById('pagination');
  const totalPages = Math.ceil(filteredGames.length / PAGE_SIZE) || 1;
  const start = (currentPage - 1) * PAGE_SIZE;
  const end = Math.min(start + PAGE_SIZE, filteredGames.length);
  const pageGames = filteredGames.slice(start, end);

  content.innerHTML = pageGames.map(g => {
    const isSelected = selectedGame && selectedGame.appid === g.appid;
    let badges = '';
    if (g.type === 'installed') badges += '<span class="badge badge-installed"><i class="fas fa-check"></i> Inst.</span>';
    if (g.type === 'library') badges += '<span class="badge badge-installed"><i class="fas fa-file-archive"></i> Lib</span>';
    if (window._cache?.[g.name]) badges += '<span class="badge badge-verified"><i class="fas fa-robot"></i> Verified</span>';
    const safeName = String(g.name).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;');
    return `<div class="game-row ${isSelected ? 'selected' : ''}" data-appid="${g.appid}" data-name="${safeName}" data-type="${g.type}" data-folder="${(g.folder||'').replace(/"/g,'&quot;')}">
      <span class="appid">${g.appid}</span>
      <span class="name">${safeName} ${badges}</span>
    </div>`;
  }).join('');

  content.querySelectorAll('.game-row').forEach(row => {
    row.addEventListener('click', () => {
      const appid = parseInt(row.dataset.appid);
      const name = row.dataset.name;
      const type = row.dataset.type;
      const folder = row.dataset.folder;
      selectGame({ appid, name, type, folder });
    });
  });

  visitedPages.add(currentPage);
  let pagHtml = '';
  const maxVisible = 7;
  let ps = Math.max(1, currentPage - Math.floor(maxVisible / 2));
  let pe = Math.min(totalPages, ps + maxVisible - 1);
  if (pe - ps < maxVisible - 1) ps = Math.max(1, pe - maxVisible + 1);
  if (currentPage > 1) pagHtml += `<button class="page-btn" onclick="goPage(${currentPage - 1})"><i class="fas fa-chevron-left"></i></button>`;
  for (let i = ps; i <= pe; i++) {
    pagHtml += `<button class="page-btn ${i === currentPage ? 'active' : ''} ${visitedPages.has(i) ? 'visited' : ''}" onclick="goPage(${i})">${i}</button>`;
  }
  if (currentPage < totalPages) pagHtml += `<button class="page-btn" onclick="goPage(${currentPage + 1})"><i class="fas fa-chevron-right"></i></button>`;
  pagination.innerHTML = pagHtml;
}

function goPage(n) {
  currentPage = n;
  visitedPages.add(n);
  renderVirtual();
}

/* ── Right Panel ─────────────────────────────────────────────────────────── */
function initRightPanel() {
  document.getElementById('rpContent').innerHTML = '<div class="rp-placeholder"><i class="fas fa-cube"></i><p>Select a game to see details</p></div>';
}

function selectGame(game) {
  selectedGame = game;
  renderVirtual();
  const rp = document.getElementById('rpContent');
  rp.innerHTML = `<div class="detail-header"><h2>${game.name}</h2><span class="appid-badge">${game.appid}</span></div>
    <div style="margin-bottom:12px"><span class="badge badge-installed">${game.type}</span></div>
    <div class="detail-actions">
      <button class="btn btn-primary btn-lg" onclick="launchGame()" style="flex:1"><i class="fas fa-play"></i> Launch</button>
      <button class="btn" onclick="searchOF()" style="flex:1"><i class="fas fa-download"></i> Online Fix</button>
    </div>
    <div class="detail-actions">
      <button class="btn" onclick="verifySingle()"><i class="fas fa-robot"></i> Verify AI</button>
      <button class="btn" onclick="aiTranslate()"><i class="fas fa-language"></i> Translate</button>
      ${game.folder ? `<button class="btn" onclick="openFolder()"><i class="fas fa-folder-open"></i> Folder</button>` : ''}
    </div>
    <div class="detail-info">
      ${game.folder ? `<p><strong>Path:</strong> <span style="font-size:11px">${game.folder}</span></p>` : ''}
      <p style="margin-top:8px;font-size:12px;color:var(--text-muted)">
        <i class="fas fa-info-circle"></i> Click Launch to start via steam://rungameid/
      </p>
    </div>`;
}

async function launchGame() {
  if (!selectedGame) return;
  await eel.launch_game(selectedGame.appid)();
  toast('Launching ' + selectedGame.name + ' via Steam...', 'success');
}

async function openFolder() {
  if (!selectedGame || !selectedGame.folder) return;
  const ok = await eel.open_folder(selectedGame.folder)();
  if (!ok) toast('Folder not found', 'error');
}

async function searchOF() {
  if (!selectedGame) return;
  switchPage('onlinefix');
  document.getElementById('ofSearch').value = selectedGame.name;
  await searchOnlineFix();
}

async function verifySingle() {
  if (!selectedGame) return;
  showLoader(true);
  const result = await eel.verify_games_batch([selectedGame.name])();
  showLoader(false);
  if (result && result[selectedGame.name] !== undefined) {
    const ok = result[selectedGame.name];
    toast(`${selectedGame.name}: ${ok ? 'Verified ✓' : 'Not Available'}`, ok ? 'success' : 'warning');
    if (ok && !window._cache) window._cache = {};
    window._cache[selectedGame.name] = ok;
    applyFilter();
  } else {
    toast('Verification failed — check AI provider key', 'error');
  }
}

async function aiTranslate() {
  if (!selectedGame) return;
  const text = prompt('Enter text to translate to Turkish:', selectedGame.name);
  if (!text) return;
  showLoader(true);
  const result = await eel.ai_translate(text)();
  showLoader(false);
  if (result.translated) {
    toast('Translation: ' + result.translated, 'info');
  } else {
    toast('Translation failed: ' + (result.error || 'unknown'), 'error');
  }
}

/* ── Online Fix ─────────────────────────────────────────────────────────── */
async function searchOnlineFix() {
  const query = document.getElementById('ofSearch').value;
  if (!query) { toast('Enter a search term', 'warning'); return; }
  showLoader(true);
  const results = await eel.search_online_fix(query)();
  showLoader(false);
  const list = document.getElementById('ofList');
  document.getElementById('ofDetail').style.display = 'none';
  document.getElementById('ofResults').style.display = 'block';
  if (results.error) { toast(results.error, 'error'); return; }
  if (Array.isArray(results) && results.length) {
    list.innerHTML = results.map(r =>
      `<div class="of-result">
        <span class="of-name">${r.name}</span>
        <button class="btn btn-sm" onclick="getOFDownloadLinks('${r.url.replace(/'/g,"\\'")}','${r.name.replace(/'/g,"\\'")}')"><i class="fas fa-link"></i> Links</button>
      </div>`).join('');
  } else {
    list.innerHTML = '<p style="color:var(--text-muted)">No results found</p>';
    toast('No results', 'warning');
  }
}

async function getOFDownloadLinks(url, name) {
  showLoader(true);
  const links = await eel.get_download_links(url)();
  showLoader(false);
  const detail = document.getElementById('ofDetail');
  document.getElementById('ofDetailName').textContent = 'Links: ' + name;
  document.getElementById('ofLinks').innerHTML = (Array.isArray(links) && links.length)
    ? links.map(l => `<a class="of-link" href="${l}" target="_blank"><i class="fas fa-external-link-alt"></i> ${l}</a>`).join('')
    : '<p style="color:var(--text-muted)">No download links found. Try opening the page in browser.</p>';
  detail.style.display = 'block';
}

/* ── CloudRedirect ──────────────────────────────────────────────────────── */
async function launchCloudRedirect() {
  const ok = await eel.launch_cloud_redirect()();
  if (ok) toast('CloudRedirect launched to TEMP', 'success');
  else toast('CloudRedirect.exe not bundled', 'error');
}

/* ── Restart Steam ──────────────────────────────────────────────────────── */
async function restartSteam() {
  const r = await eel.restart_steam()();
  toast(r.ok ? 'Steam stopping... will restart in 3s' : 'Error: ' + (r.error || 'unknown'), r.ok ? 'success' : 'error');
}

/* ── Update ──────────────────────────────────────────────────────────────── */
async function checkUpdate() {
  const r = await eel.check_update()();
  const banner = document.getElementById('updateBanner');
  if (r.available) {
    document.getElementById('updateText').textContent = `Update v${r.latest} available! (current: v${r.current})`;
    document.getElementById('updateLink').href = r.url;
    banner.style.display = 'block';
    toast('Update available: v' + r.latest, 'success');
  } else if (r.error) {
    toast('Update check failed: ' + r.error, 'error');
  } else {
    toast('You are up to date (v' + r.current + ')', 'info');
    banner.style.display = 'none';
  }
}

/* ── Settings ────────────────────────────────────────────────────────────── */
async function loadSettings() {
  const s = await eel.get_settings()();
  if (s.setups_path) document.getElementById('setupsPath').value = s.setups_path;
  if (s.onlinefix_path) document.getElementById('onlinefixPath').value = s.onlinefix_path;
  if (s.gamefiles_path) document.getElementById('gamefilesPath').value = s.gamefiles_path;
}

async function saveSettings() {
  await eel.save_settings({
    setups_path: document.getElementById('setupsPath').value,
    onlinefix_path: document.getElementById('onlinefixPath').value,
    gamefiles_path: document.getElementById('gamefilesPath').value
  })();
  toast('Settings saved', 'success');
}

async function browseFolder(id) {
  const path = await eel.browse_folder()();
  if (path) document.getElementById(id).value = path;
}

async function loadAIProviders() {
  const providers = await eel.get_ai_providers()();
  const container = document.getElementById('aiProviders');
  const select = document.getElementById('preferredProvider');
  container.innerHTML = '';
  select.innerHTML = '';
  for (const p of providers) {
    const div = document.createElement('div');
    div.className = 'ai-provider';
    div.innerHTML = `<label>${p.label}:</label><input type="password" id="key_${p.id}" placeholder="Enter API key..." onchange="saveAIKey('${p.id}', this.value)">`;
    container.appendChild(div);
    const opt = document.createElement('option');
    opt.value = p.id; opt.textContent = p.label;
    select.appendChild(opt);
  }
  const pref = await eel.get_preferred_provider()();
  select.value = pref;
  for (const p of providers) {
    const key = await eel.get_ai_key(p.id)();
    const inp = document.getElementById('key_' + p.id);
    if (key && inp) inp.value = key;
  }
}

async function saveAIKey(providerId, key) {
  await eel.save_ai_key(providerId, key)();
  toast(providerId + ' key saved', 'success');
}

async function setPreferredProvider() {
  const val = document.getElementById('preferredProvider').value;
  await eel.set_preferred_provider(val)();
  toast('Preferred provider updated', 'success');
}

/* ── License removed ─────────────────────────────────────────────────────── */

/* ── Expose globals for inline handlers ──────────────────────────────────── */
Object.assign(window, {
  scanGames, launchCloudRedirect, restartSteam, checkUpdate,
  searchOnlineFix, launchGame, activateLicense, saveSettings,
  browseFolder, saveAIKey, setPreferredProvider,
  filterGames, switchTab, goPage, getOFDownloadLinks,
  openFolder, verifySingle, aiTranslate
});
