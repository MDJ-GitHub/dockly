import { fetchText, transcribe, navigate as navTo, imageUrl, wallpaperUrl, prefetchWallpapers } from './bridge.js';

const $ = (s) => document.querySelector(s);

/* ---------------- config ---------------- */
// Order here is the order in the dropdown. `suggest` is optional (engines without it show only your own history).
const ENGINES = {
  google:     { name: 'Google',     domain: 'google.com',     url: 'https://www.google.com/search?q=%s',   suggest: 'https://suggestqueries.google.com/complete/search?client=firefox&q=%s',
                images: 'https://www.google.com/search?tbm=isch&q=%s',          visual: 'https://lens.google.com/' },
  bing:       { name: 'Bing',       domain: 'bing.com',       url: 'https://www.bing.com/search?q=%s',     suggest: 'https://api.bing.com/osjson.aspx?query=%s',
                images: 'https://www.bing.com/images/search?q=%s',             visual: 'https://www.bing.com/visualsearch' },
  duckduckgo: { name: 'DuckDuckGo', domain: 'duckduckgo.com', url: 'https://duckduckgo.com/?q=%s',         suggest: 'https://duckduckgo.com/ac/?type=list&q=%s',
                images: 'https://duckduckgo.com/?q=%s&iax=images&ia=images',   visual: 'https://duckduckgo.com/?ia=images' },
  brave:      { name: 'Brave',      domain: 'brave.com',      url: 'https://search.brave.com/search?q=%s', suggest: 'https://search.brave.com/api/suggest?q=%s',
                images: 'https://search.brave.com/images?q=%s',                 visual: 'https://search.brave.com/images' },
  yahoo:      { name: 'Yahoo',      domain: 'yahoo.com',      url: 'https://search.yahoo.com/search?p=%s',
                images: 'https://images.search.yahoo.com/search/images?p=%s',   visual: 'https://images.search.yahoo.com/' },
  ecosia:     { name: 'Ecosia',     domain: 'ecosia.org',     url: 'https://www.ecosia.org/search?q=%s',   suggest: 'https://ac.ecosia.org/autocomplete?q=%s&type=list',
                images: 'https://www.ecosia.org/images?q=%s',                  visual: 'https://www.ecosia.org/images' },
  startpage:  { name: 'Startpage',  domain: 'startpage.com',  url: 'https://www.startpage.com/do/search?q=%s',
                images: 'https://www.startpage.com/do/search?cat=pics&q=%s',    visual: 'https://www.startpage.com/do/search?cat=pics' },
  qwant:      { name: 'Qwant',      domain: 'qwant.com',      url: 'https://www.qwant.com/?q=%s',
                images: 'https://www.qwant.com/?q=%s&t=images',                visual: 'https://www.qwant.com/?t=images' },
  yandex:     { name: 'Yandex',     domain: 'yandex.com',     url: 'https://yandex.com/search/?text=%s',   suggest: 'https://suggest.yandex.com/suggest-ff.cgi?part=%s',
                images: 'https://yandex.com/images/search?text=%s',             visual: 'https://yandex.com/images/' },
  youtube:    { name: 'YouTube',    domain: 'youtube.com',    url: 'https://www.youtube.com/results?search_query=%s', suggest: 'https://suggestqueries.google.com/complete/search?client=firefox&ds=yt&q=%s',
                images: 'https://www.google.com/search?tbm=isch&q=%s',          visual: 'https://lens.google.com/' },
  wikipedia:  { name: 'Wikipedia',  domain: 'wikipedia.org',  url: 'https://en.wikipedia.org/w/index.php?search=%s', suggest: 'https://en.wikipedia.org/w/api.php?action=opensearch&search=%s',
                images: 'https://commons.wikimedia.org/w/index.php?search=%s&ns6=1', visual: 'https://commons.wikimedia.org/' },
};
// AI assistants: no API, just open the site with the question.
// `copy: true` also puts the question on the clipboard, for sites that may ignore the ?q= parameter.
const AIS = {
  chatgpt:    { name: 'ChatGPT',    domain: 'chatgpt.com',           home: 'https://chatgpt.com/',             url: 'https://chatgpt.com/?q=%s' },
  gemini:     { name: 'Gemini',     domain: 'gemini.google.com',     home: 'https://gemini.google.com/app',    url: 'https://gemini.google.com/app?q=%s', copy: true },
  claude:     { name: 'Claude',     domain: 'claude.ai',             home: 'https://claude.ai/new',            url: 'https://claude.ai/new?q=%s' },
  copilot:    { name: 'Copilot',    domain: 'copilot.microsoft.com', home: 'https://copilot.microsoft.com/',   url: 'https://copilot.microsoft.com/?q=%s' },
  perplexity: { name: 'Perplexity', domain: 'perplexity.ai',         home: 'https://www.perplexity.ai/',       url: 'https://www.perplexity.ai/search?q=%s' },
  grok:       { name: 'Grok',       domain: 'grok.com',              home: 'https://grok.com/',                url: 'https://grok.com/?q=%s', copy: true },
  deepseek:   { name: 'DeepSeek',   domain: 'deepseek.com',          home: 'https://chat.deepseek.com/',       url: 'https://chat.deepseek.com/?q=%s', copy: true },
  mistral:    { name: 'Le Chat',    domain: 'mistral.ai',            home: 'https://chat.mistral.ai/chat',     url: 'https://chat.mistral.ai/chat?q=%s', copy: true },
  qwen:       { name: 'Qwen',       domain: 'qwen.ai',               home: 'https://chat.qwen.ai/',            url: 'https://chat.qwen.ai/?q=%s', copy: true },
  kimi:       { name: 'Kimi',       domain: 'kimi.com',              home: 'https://www.kimi.com/',            url: 'https://www.kimi.com/', copy: true },
  metaai:     { name: 'Meta AI',    domain: 'meta.ai',               home: 'https://www.meta.ai/',             url: 'https://www.meta.ai/', copy: true },
  you:        { name: 'You.com',    domain: 'you.com',               home: 'https://you.com/',                 url: 'https://you.com/search?q=%s&tbm=youchat' },
};
const BG_QUERY = {
  nature: 'nature landscape', games: 'video game', space: 'space galaxy',
  city: 'city night', abstract: 'abstract', anime: 'anime',
};
const DEFAULTS = {
  theme: 'system',
  bg: 'nature',
  engine: 'google',
  ai: 'chatgpt',
  rows: 3,
  newTab: false,
  sttUrl: '',
  findImages: true,
  bgMode: 'random',   // 'daily' = one picture per day, 'random' = random one of bgCount per day
  bgCount: 8,
  cardW: 0,          // px; 0 = automatic (depends on the screen width)
  scrollSecs: 5,     // auto-scroll interval in seconds; 0 = off
  intro: true,       // start animation
  glass: 0,          // opacity (percent) of the search bar and side cards; 0 = theme default
  country: '',
  gcal: '',
  feeds: [
    // general news
    'https://feeds.bbci.co.uk/news/rss.xml',
    'https://feeds.npr.org/1001/rss.xml',
    'https://www.aljazeera.com/xml/rss/all.xml',
    'https://www.theguardian.com/world/rss',
    // games
    'https://www.polygon.com/rss/index.xml',
    'https://www.rockpapershotgun.com/feed',
    'https://www.eurogamer.net/feed',
    'https://www.gamespot.com/feeds/mashup/',
    'https://www.pcgamer.com/rss/',
    'https://www.gamesradar.com/rss/',
    // consoles
    'https://www.nintendolife.com/feeds/latest',
    'https://www.pushsquare.com/feeds/latest',
    'https://www.purexbox.com/feeds/latest',
    // emulators
    'https://dolphin-emu.org/blog/feeds/',
    'https://www.libretro.com/index.php/feed/',
    'https://github.com/PCSX2/pcsx2/releases.atom',
    'https://github.com/RPCS3/rpcs3/releases.atom',
    // hardware and PCs
    'https://www.tomshardware.com/feeds/all',
    'https://www.techpowerup.com/rss/news',
    'https://videocardz.com/feed',
    'https://www.techspot.com/backend.xml',
    'https://wccftech.com/feed/',
    'https://www.windowscentral.com/rss',
    'https://www.phoronix.com/rss.php',
    'https://hackaday.com/blog/feed/',
    // tech
    'https://www.theverge.com/rss/index.xml',
    'https://feeds.arstechnica.com/arstechnica/index',
    'https://hnrss.org/frontpage',
  ],
};

/* ---------------- storage ---------------- */
function load(key, fallback) {
  try { const v = localStorage.getItem(key); return v ? JSON.parse(v) : fallback; } catch { return fallback; }
}
function save(key, value) {
  try { localStorage.setItem(key, JSON.stringify(value)); } catch { /* storage can be unavailable */ }
}
let settings = { ...DEFAULTS, ...load('newpage.settings', {}) };
// Until the user edits the list, always use the current defaults.
if (!settings.feedsCustom) settings.feeds = DEFAULTS.feeds;
const persist = () => save('newpage.settings', settings);
if (settings.intro) {
  document.documentElement.classList.add('intro');
  setTimeout(() => document.documentElement.classList.remove('intro'), 2000);
}
const navigate = (url) => navTo(url, !!settings.newTab); // honours "open results in a new tab"

function cacheGet(key, ttl) {
  const c = load(key, null);
  return c && Date.now() - c.t < ttl ? c.v : null;
}
const cacheSet = (key, v) => save(key, { t: Date.now(), v });

/* ---------------- toast ---------------- */
let toastTimer;
function toast(msg) {
  const el = $('#toast');
  el.textContent = msg;
  el.classList.add('show');
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => el.classList.remove('show'), 3500);
}

/* ---------------- theme ---------------- */
const applyTheme = () => { document.documentElement.dataset.theme = settings.theme; };

const applyGlass = () => {
  const g = Number(settings.glass);
  const root = document.documentElement.style;
  if (g > 0) root.setProperty('--glass-a', String(g / 100)); else root.removeProperty('--glass-a');
};

/* tab icon: an outlined rounded square with a plus (like the browser's add button), white on dark themes, black on light ones */
const lightScheme = matchMedia('(prefers-color-scheme: light)');
function applyTabIcon() {
  const color = lightScheme.matches ? '#000000' : '#ffffff';
  const svg = `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 32 32" fill="none" stroke="${color}" stroke-linecap="round"><rect x="2.5" y="2.5" width="27" height="27" rx="8" stroke-width="2.4"/><path d="M16 9v14M9 16h14" stroke-width="3"/></svg>`;
  const link = document.createElement('link');
  link.rel = 'icon';
  link.type = 'image/svg+xml';
  link.href = 'data:image/svg+xml,' + encodeURIComponent(svg);
  document.querySelectorAll('link[rel="icon"]').forEach((l) => l.remove());
  document.head.append(link);
}
lightScheme.addEventListener('change', applyTabIcon);

/* ---------------- background: one picture per day, or a random one of several per day ---------------- */
const seedFor = (str) => {
  let h = 2166136261;
  for (const c of str) h = Math.imul(h ^ c.charCodeAt(0), 16777619);
  return (h >>> 0).toString(36).padStart(6, '0').slice(0, 6);
};
const todayKey = () => { const d = new Date(); return dayKey(d.getFullYear(), d.getMonth(), d.getDate()); };

const bgPerDay = () => (settings.bgMode === 'daily' ? 1 : Math.min(20, Math.max(2, Math.round(Number(settings.bgCount)) || 8)));

async function fetchDailyWallpapers(day, prev) {
  const n = bgPerDay();
  // Skip yesterday's eight so the next day always looks different.
  const exclude = new Set(prev && prev.cat === settings.bg ? prev.list : []);
  let list = [];
  try {
    const cats = settings.bg === 'anime' ? '010' : '100';
    const url = 'https://wallhaven.cc/api/v1/search?q=' + encodeURIComponent(BG_QUERY[settings.bg] || 'nature') +
      `&categories=${cats}&purity=100&sorting=random&seed=${seedFor(day + settings.bg)}&atleast=1920x1080&ratios=16x9`;
    const json = JSON.parse(await fetchText(url));
    list = json.data.map((d) => d.path).filter((u) => !exclude.has(u)).slice(0, n);
  } catch { /* fall through to the fallback below */ }
  for (let i = list.length; i < n; i++) list.push(`https://picsum.photos/seed/${day}-${settings.bg}-${i}/1920/1080`);
  const w = { day, cat: settings.bg, n, list };
  save('newpage.wall', w);
  return w;
}

async function loadBackground() {
  if (settings.bg === 'plain') {               // just the page colour, no pictures
    const bg = $('#bg');
    bg.classList.remove('ready');
    bg.style.backgroundImage = '';
    prefetchWallpapers([]);                    // frees the stored pictures
    return;
  }
  const day = todayKey();
  let w = load('newpage.wall', null);
  if (!w || w.day !== day || w.cat !== settings.bg || w.n !== bgPerDay()) w = await fetchDailyWallpapers(day, w);

  // random pick on every open, never the same one twice in a row
  const last = load('newpage.wallLast', -1);
  let i;
  do { i = Math.floor(Math.random() * w.list.length); } while (i === last && w.list.length > 1);
  save('newpage.wallLast', i);

  const src = w.list[i];
  const fallback = `https://picsum.photos/seed/${day}-${settings.bg}-${i}/1920/1080`;
  const show = (u) => new Promise((resolve, reject) => {
    const img = new Image();
    img.onload = () => {
      const bg = $('#bg');
      bg.style.backgroundImage = `url("${u}")`;
      bg.classList.add('ready');
      resolve();
    };
    img.onerror = reject;
    img.src = u;
  });
  try { await show(await wallpaperUrl(src)); }
  catch { try { await show(await wallpaperUrl(fallback)); } catch { /* keep the plain background */ } }
  // the rest of today's pictures are stored in the background, so later tabs show instantly
  prefetchWallpapers(w.list);
}

/* ---------------- search + suggestions ---------------- */
const q = $('#q');
const suggBox = $('#sugg');
let suggSeq = 0, suggTimer, active = -1, lastRemote = [];

/* remembered searches */
const HIST_KEY = 'newpage.history';
const RESULT_SLOTS = 10;  // engine suggestions
const MATCH_SLOTS = 4;    // remembered searches pinned to the top while typing
const RECENT_SLOTS = 6;   // right column: latest searches
const MAX_HIST = 100;
let searches = load(HIST_KEY, []);

function remember(text) {
  const k = text.toLowerCase();
  const hit = searches.find((h) => h.q.toLowerCase() === k);
  if (hit) { hit.t = Date.now(); hit.n = (hit.n || 1) + 1; hit.q = text; }
  else searches.push({ q: text, t: Date.now(), n: 1 });
  searches.sort((a, b) => b.t - a.t);
  searches = searches.slice(0, MAX_HIST);
  save(HIST_KEY, searches);
}
function forget(text) {
  searches = searches.filter((h) => h.q !== text);
  save(HIST_KEY, searches);
}
// "mega man" -> "mega man zero 4": past searches that start with what you typed come first,
// then ones that merely contain it. Within each group: most-searched, then most recent.
function matches(typed) {
  const k = typed.toLowerCase();
  const rank = (a, b) => (b.n || 1) - (a.n || 1) || b.t - a.t;
  const starts = searches.filter((h) => h.q.toLowerCase().startsWith(k)).sort(rank);
  const has = searches.filter((h) => !h.q.toLowerCase().startsWith(k) && h.q.toLowerCase().includes(k)).sort(rank);
  return [...starts, ...has].slice(0, MATCH_SLOTS).map((h) => h.q);
}

/* small icons in front of the engine and AI selectors */
const iconUrl = (domain) => `https://icons.duckduckgo.com/ip3/${domain}.ico`;
function letterIcon(name) {
  const svg = `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16"><rect width="16" height="16" rx="4" fill="#6b7280"/><text x="8" y="12" font-size="10" text-anchor="middle" fill="#fff" font-family="sans-serif">${name[0]}</text></svg>`;
  return 'data:image/svg+xml,' + encodeURIComponent(svg);
}
function setIcon(img, item) {
  const fallback = () => { img.onerror = null; img.src = letterIcon(item.name); };
  img.onerror = fallback;
  imageUrl(iconUrl(item.domain)).then((u) => { img.src = u; }, fallback);
}

function makeDropdown(root, entries, current, onPick) {
  const ids = Object.keys(entries);
  let value = current, active = -1;

  const btn = document.createElement('button');
  btn.type = 'button';
  btn.className = 'dd-btn';
  btn.setAttribute('aria-haspopup', 'listbox');
  btn.setAttribute('aria-expanded', 'false');
  const ico = Object.assign(document.createElement('img'), { className: 'ico', alt: '' });
  const label = document.createElement('span');
  const chev = document.createElement('i');
  chev.className = 'chev';
  btn.append(ico, label, chev);

  const list = document.createElement('ul');
  list.className = 'dd-list';
  list.setAttribute('role', 'listbox');
  const items = ids.map((id) => {
    const li = document.createElement('li');
    li.setAttribute('role', 'option');
    const i = Object.assign(document.createElement('img'), { className: 'ico', alt: '' });
    setIcon(i, entries[id]);
    const t = document.createElement('span');
    t.textContent = entries[id].name;
    li.append(i, t);
    li.addEventListener('click', () => pick(id));
    return li;
  });
  list.append(...items);
  root.append(btn, list);

  const isOpen = () => list.classList.contains('open');
  const mark = () => items.forEach((li, i) => li.classList.toggle('active', i === active));
  function render() {
    setIcon(ico, entries[value]);
    label.textContent = entries[value].name;
    items.forEach((li, i) => li.setAttribute('aria-selected', String(ids[i] === value)));
  }
  function open() { list.classList.add('open'); btn.setAttribute('aria-expanded', 'true'); active = ids.indexOf(value); mark(); }
  function close() { list.classList.remove('open'); btn.setAttribute('aria-expanded', 'false'); }
  function pick(id) { value = id; render(); close(); onPick(id); btn.focus(); }

  btn.addEventListener('click', () => (isOpen() ? close() : open()));
  btn.addEventListener('keydown', (e) => {
    if (e.key === 'ArrowDown' || e.key === 'ArrowUp') {
      e.preventDefault();
      if (!isOpen()) { open(); return; }
      active = (active + (e.key === 'ArrowDown' ? 1 : -1) + ids.length) % ids.length;
      mark();
    } else if (e.key === 'Enter' && isOpen()) {
      e.preventDefault();
      pick(ids[active]);
    } else if (e.key === 'Escape') close();
  });
  document.addEventListener('click', (e) => { if (!root.contains(e.target)) close(); });
  render();
}

function fillEngines() {
  makeDropdown($('#engineDd'), ENGINES, settings.engine, (id) => {
    settings.engine = id;
    persist();
    if (q.value.trim()) suggest(q.value.trim());
  });
}

const looksLikeUrl = (t) => /^https?:\/\//i.test(t) || /^[^\s/]+\.[a-z]{2,}(\/\S*)?$/i.test(t);

function go(text) {
  const t = text.trim();
  if (!t) return;
  if (looksLikeUrl(t)) { navigate(/^https?:\/\//i.test(t) ? t : 'https://' + t); return; }
  remember(t);
  navigate(ENGINES[settings.engine].url.replace('%s', encodeURIComponent(t)));
}

function fillAis() {
  makeDropdown($('#aiDd'), AIS, settings.ai, (id) => { settings.ai = id; persist(); });
}

async function goAi(text) {
  const t = text.trim();
  const ai = AIS[settings.ai];
  if (!t) { navigate(ai.home); return; }
  if (ai.copy) { try { await navigator.clipboard.writeText(t); } catch { /* clipboard may be blocked */ } }
  navigate(ai.url.replace('%s', encodeURIComponent(t)));
}
$('#aiGo').addEventListener('click', () => goAi(q.value));

/* suggestion dropdown: suggestions on the left, latest searches on the right */
function hideSugg() { suggBox.classList.remove('open'); active = -1; }

function clockIcon() {
  const ns = 'http://www.w3.org/2000/svg';
  const svg = document.createElementNS(ns, 'svg');
  svg.setAttribute('viewBox', '0 0 24 24');
  const c = document.createElementNS(ns, 'circle');
  c.setAttribute('cx', '12'); c.setAttribute('cy', '12'); c.setAttribute('r', '8');
  const p = document.createElementNS(ns, 'path');
  p.setAttribute('d', 'M12 8v4l3 2');
  svg.append(c, p);
  return svg;
}

function makeItem(text, remembered) {
  const li = document.createElement('li');
  li.setAttribute('role', 'option');
  if (remembered) li.append(clockIcon());
  const span = document.createElement('span');
  span.textContent = text;
  li.append(span);
  if (remembered) {
    const x = document.createElement('button');
    x.type = 'button';
    x.textContent = '×';
    x.setAttribute('aria-label', 'Remove from recent searches');
    li.append(x);
  }
  li.addEventListener('mousedown', (e) => {
    e.preventDefault();
    if (e.target.closest('button')) { forget(text); refreshSugg(); } else go(text);
  });
  return li;
}

function column(title, items) {
  const sec = document.createElement('section');
  sec.className = 'col';
  const h = document.createElement('h4');
  h.textContent = title;
  const ul = document.createElement('ul');
  ul.setAttribute('role', 'listbox');
  ul.append(...items);
  sec.append(h, ul);
  return sec;
}

function showSugg(typed, remote) {
  suggBox.replaceChildren();
  active = -1;
  const mine = typed ? matches(typed) : [];
  const seen = new Set(mine.map((s) => s.toLowerCase()));
  const left = [
    ...mine.map((t) => makeItem(t, true)),
    ...remote.filter((s) => !seen.has(s.toLowerCase())).map((t) => makeItem(t, false)),
  ].slice(0, MATCH_SLOTS + RESULT_SLOTS);
  const recent = searches.slice(0, RECENT_SLOTS).map((h) => makeItem(h.q, true));
  const cols = [];
  if (left.length) cols.push(column('Suggestions', left));
  if (recent.length) cols.push(column('Recent searches', recent));
  if (!cols.length) { hideSugg(); return; }
  suggBox.classList.toggle('single', cols.length === 1);
  suggBox.append(...cols);
  suggBox.classList.add('open');
}

function refreshSugg() {
  const v = q.value.trim();
  showSugg(v, v ? lastRemote : []);
}

async function suggest(v) {
  const seq = ++suggSeq;
  const tpl = ENGINES[settings.engine].suggest;
  if (!tpl) { lastRemote = []; showSugg(v, []); return; } // this engine has no suggestion service
  try {
    const json = JSON.parse(await fetchText(tpl.replace('%s', encodeURIComponent(v))));
    if (seq !== suggSeq) return;
    lastRemote = (json[1] || []).map((x) => (typeof x === 'string' ? x : x.phrase || '')).filter(Boolean).slice(0, RESULT_SLOTS);
    showSugg(v, lastRemote);
  } catch { /* keep whatever is showing */ }
}

q.addEventListener('input', () => {
  clearTimeout(suggTimer);
  suggSeq++;
  const v = q.value.trim();
  // remembered searches show instantly; engine suggestions follow
  const kept = v ? lastRemote.filter((s) => s.toLowerCase().includes(v.toLowerCase())) : [];
  if (!v) lastRemote = [];
  showSugg(v, kept);
  if (v) suggTimer = setTimeout(() => suggest(v), 150);
});
q.addEventListener('focus', refreshSugg);
q.addEventListener('keydown', (e) => {
  if (e.key === 'Enter' && (e.ctrlKey || e.metaKey)) { e.preventDefault(); goAi(q.value); return; }
  const items = [...suggBox.querySelectorAll('li')];
  if (e.key === 'Escape') { hideSugg(); return; }
  if (e.key === 'ArrowDown' && !suggBox.classList.contains('open')) { refreshSugg(); return; }
  if (!items.length || (e.key !== 'ArrowDown' && e.key !== 'ArrowUp')) return;
  e.preventDefault();
  active = (active + (e.key === 'ArrowDown' ? 1 : -1) + items.length) % items.length;
  items.forEach((li, i) => li.classList.toggle('active', i === active));
  q.value = items[active].querySelector('span').textContent;
});
q.addEventListener('blur', () => setTimeout(hideSugg, 100));

$('#searchForm').addEventListener('submit', (e) => { e.preventDefault(); go(q.value); });

// voice
// 1) With a transcription server set in settings: record, then send the audio to it.
// 2) Otherwise the browser's own speech recognition, which Chrome serves from Google's servers.
//    A self-built Chromium has no Google keys, so it fails with "network"; we say so clearly.
const SR = window.SpeechRecognition || window.webkitSpeechRecognition;
let session = null; // while listening: { stop() }
const setMic = (on) => $('#voice').classList.toggle('rec', on);

function listenWithBrowser() {
  const r = new SR();
  r.lang = navigator.language;
  let heard = false;
  const giveUp = setTimeout(() => {
    if (heard) return;
    r.abort();
    toast('No answer from the speech service. This browser build probably has no Google speech keys; set a transcription server in settings.');
  }, 9000);
  r.onresult = (e) => { heard = true; clearTimeout(giveUp); q.value = e.results[0][0].transcript; go(q.value); };
  r.onerror = (e) => {
    clearTimeout(giveUp);
    heard = true;
    const msg = {
      'network': 'The speech service isn’t reachable (this build has no Google speech keys). Set a transcription server in settings.',
      'service-not-allowed': 'Speech recognition isn’t allowed in this build. Set a transcription server in settings.',
      'not-allowed': 'Microphone access was blocked.',
      'audio-capture': 'No microphone found.',
      'no-speech': 'I didn’t hear anything.',
    }[e.error] || `Voice search failed (${e.error}).`;
    toast(msg);
  };
  r.onend = () => { clearTimeout(giveUp); setMic(false); session = null; };
  session = { stop: () => r.stop() };
  setMic(true);
  toast('Listening…');
  try { r.start(); } catch { setMic(false); session = null; }
}

async function recordAndTranscribe() {
  let stream;
  try { stream = await navigator.mediaDevices.getUserMedia({ audio: true }); }
  catch { toast('Microphone access was blocked.'); return; }

  const chunks = [];
  const recorder = new MediaRecorder(stream);
  const ctx = new AudioContext();
  const analyser = ctx.createAnalyser();
  analyser.fftSize = 1024;
  ctx.createMediaStreamSource(stream).connect(analyser);
  const samples = new Uint8Array(analyser.fftSize);

  const started = Date.now();
  let spoke = false, quietSince = started;
  // stop by itself after 1.5s of silence (once you've spoken) or after 20s; pressing the mic again also stops
  const watcher = setInterval(() => {
    analyser.getByteTimeDomainData(samples);
    let peak = 0;
    for (const v of samples) peak = Math.max(peak, Math.abs(v - 128));
    const now = Date.now();
    if (peak > 12) { spoke = true; quietSince = now; }
    if ((spoke && now - quietSince > 1500) || now - started > 20000) session?.stop();
  }, 100);

  session = { stop: () => { if (recorder.state !== 'inactive') recorder.stop(); } };
  recorder.ondataavailable = (e) => chunks.push(e.data);
  recorder.onstop = async () => {
    clearInterval(watcher);
    stream.getTracks().forEach((t) => t.stop());
    ctx.close();
    session = null;
    setMic(false);
    if (!spoke) { toast('I didn’t hear anything.'); return; }
    toast('Transcribing…');
    try {
      const text = await transcribe(settings.sttUrl, new Blob(chunks, { type: recorder.mimeType }));
      if (text) { q.value = text; go(text); } else toast('Nothing was recognised.');
    } catch { toast('Transcription failed. Check the server address in settings.'); }
  };
  setMic(true);
  toast('Listening… press the mic again to stop');
  recorder.start();
}

$('#voice').addEventListener('click', () => {
  if (session) { session.stop(); return; }
  if (settings.sttUrl) recordAndTranscribe();
  else if (SR) listenWithBrowser();
  else toast('Voice search needs a speech service. Set a transcription server in settings.');
});

// Image button: with text in the box, the selected engine's image results for it;
// with an empty box, the engine's visual (reverse image) search page where it has one.
$('#image').addEventListener('click', () => {
  const e = ENGINES[settings.engine];
  const t = q.value.trim();
  navigate(t ? e.images.replace('%s', encodeURIComponent(t)) : e.visual);
});

// Focus button: hides everything, lightens the wallpaper and moves the search bar to the middle.
$('#focusBtn').addEventListener('click', () => {
  const on = document.documentElement.classList.toggle('focus');
  const btn = $('#focusBtn');
  btn.setAttribute('aria-pressed', String(on));
  btn.title = btn.ariaLabel = on ? 'Show everything' : 'Hide everything';
});

/* ---------------- calendar ---------------- */
const pad = (n) => String(n).padStart(2, '0');
const dayKey = (y, m, d) => `${y}-${pad(m + 1)}-${pad(d)}`;
const marks = { holidays: new Map(), events: new Map() };
const holidayYears = new Set();
const EVENTS_KEY = 'newpage.events';
let custom = load(EVENTS_KEY, {});            // { 'YYYY-MM-DD': [{ id, t }] }, kept in this browser
const customFor = (k) => custom[k] || [];
let view = new Date(); view = new Date(view.getFullYear(), view.getMonth(), 1);

const addMark = (map, k, name) => map.set(k, [...(map.get(k) || []), name]);
const countryCode = () =>
  (settings.country || '').toUpperCase() ||
  (() => { try { return new Intl.Locale(navigator.language).region || 'US'; } catch { return 'US'; } })();

async function loadHolidays(year) {
  const cc = countryCode();
  const id = cc + year;
  if (holidayYears.has(id)) return;
  holidayYears.add(id);
  try {
    const list = JSON.parse(await fetchText(`https://date.nager.at/api/v3/PublicHolidays/${year}/${cc}`));
    for (const h of list) addMark(marks.holidays, h.date, h.localName || h.name);
    renderCalendar();
    updateToday();
  } catch { holidayYears.delete(id); }
}

function icsDate(v) {
  const m = v.match(/^(\d{4})(\d{2})(\d{2})(?:T(\d{2})(\d{2})(\d{2})(Z)?)?$/);
  if (!m) return null;
  if (m[7]) {
    const d = new Date(Date.UTC(+m[1], +m[2] - 1, +m[3], +m[4], +m[5], +m[6]));
    return dayKey(d.getFullYear(), d.getMonth(), d.getDate());
  }
  return `${m[1]}-${m[2]}-${m[3]}`;
}

// Limitation: recurring events (RRULE) and multi-day spans only show on their first day.
function parseIcs(text) {
  const lines = text.replace(/\r?\n[ \t]/g, '').split(/\r?\n/);
  const out = [];
  let ev = null;
  for (const line of lines) {
    if (line === 'BEGIN:VEVENT') ev = {};
    else if (line === 'END:VEVENT') { if (ev && ev.start && ev.summary) out.push(ev); ev = null; }
    else if (ev) {
      const i = line.indexOf(':');
      if (i < 0) continue;
      const name = line.slice(0, i).split(';')[0];
      const val = line.slice(i + 1);
      if (name === 'DTSTART') ev.start = icsDate(val);
      else if (name === 'SUMMARY') ev.summary = val.replace(/\\,/g, ',').replace(/\\n/g, ' ');
    }
  }
  return out;
}

async function loadGcal() {
  marks.events.clear();
  if (settings.gcal) {
    try {
      for (const e of parseIcs(await fetchText(settings.gcal))) addMark(marks.events, e.start, e.summary);
    } catch { toast('Couldn’t load Google Calendar. Check the iCal address in settings.'); }
  }
  renderCalendar();
  updateToday();
}

function renderCalendar() {
  const cal = $('#cal');
  const y = view.getFullYear(), m = view.getMonth();
  const today = new Date();
  const todayKey = dayKey(today.getFullYear(), today.getMonth(), today.getDate());

  const head = document.createElement('div');
  head.className = 'cal-head';
  const prev = Object.assign(document.createElement('button'), { textContent: '‹', ariaLabel: 'Previous month' });
  const next = Object.assign(document.createElement('button'), { textContent: '›', ariaLabel: 'Next month' });
  const title = document.createElement('strong');
  title.textContent = view.toLocaleDateString(undefined, { month: 'long', year: 'numeric' });
  prev.onclick = () => { view = new Date(y, m - 1, 1); loadHolidays(view.getFullYear()); renderCalendar(); };
  next.onclick = () => { view = new Date(y, m + 1, 1); loadHolidays(view.getFullYear()); renderCalendar(); };
  head.append(prev, title, next);

  const grid = document.createElement('div');
  grid.className = 'cal-grid';
  const first = new Date(y, m, 1);
  const weekStart = 0; // 0 = Sunday, 1 = Monday
  for (let i = 0; i < 7; i++) {
    const d = new Date(2024, 0, 7 + weekStart + i); // 2024-01-07 is a Sunday
    const el = document.createElement('div');
    el.className = 'dow';
    el.textContent = d.toLocaleDateString(undefined, { weekday: 'narrow' });
    grid.append(el);
  }
  const offset = (first.getDay() - weekStart + 7) % 7;
  const weeks = Math.ceil((offset + new Date(y, m + 1, 0).getDate()) / 7);
  grid.style.gridTemplateRows = `auto repeat(${weeks}, minmax(26px, 1fr))`;
  for (let i = 0; i < weeks * 7; i++) {
    const d = new Date(y, m, 1 - offset + i);
    const k = dayKey(d.getFullYear(), d.getMonth(), d.getDate());
    const el = document.createElement('button');
    el.type = 'button';
    el.className = 'day';
    el.textContent = d.getDate();
    el.setAttribute('aria-label', d.toLocaleDateString(undefined, { dateStyle: 'full' }));
    if (d.getMonth() !== m) el.classList.add('other');
    if (k === todayKey) el.classList.add('today');

    const mine = customFor(k);
    const names = [...(marks.holidays.get(k) || []), ...(marks.events.get(k) || []), ...mine.map((e) => e.t)];
    // a ring around the number per kind of event: Google Calendar red, your own blue, holidays green
    const rings = [];
    if (marks.events.has(k)) rings.push('var(--gcal)');
    if (mine.length) rings.push('var(--custom)');
    if (marks.holidays.has(k)) rings.push('var(--holiday)');
    if (rings.length) {
      el.style.boxShadow = rings.map((c, j) => (j === 0 ? `inset 0 0 0 2px ${c}` : `0 0 0 ${j * 2}px ${c}`)).join(', ');
    }
    if (names.length) el.title = names.join('\n');
    el.addEventListener('click', () => openDay(k, d));
    grid.append(el);
  }

  const legend = document.createElement('div');
  legend.className = 'cal-legend';
  const dot = (color, label) => {
    const s = document.createElement('span');
    const i = document.createElement('i');
    i.style.borderColor = color;
    s.append(i, label);
    return s;
  };
  legend.append(dot('var(--holiday)', 'Holidays'), dot('var(--gcal)', 'Google Calendar'), dot('var(--custom)', 'My events'));

  cal.replaceChildren(head, grid, legend);
}


/* ---------------- today strip (emoji + name, above the search bar) ---------------- */
const EMOJI_RE = /^(\p{Extended_Pictographic}(?:\uFE0F|\u200D\p{Extended_Pictographic}|\p{Emoji_Modifier})*)\s*(.*)$/u;
function withIcon(text, fallback) {
  const m = text.match(EMOJI_RE);
  return m && m[2] ? { icon: m[1], name: m[2] } : { icon: fallback, name: text };
}
function eventsOn(k) {
  return [
    ...(marks.holidays.get(k) || []).map((t) => withIcon(t, '🎉')),
    ...(marks.events.get(k) || []).map((t) => withIcon(t, '📅')),
    ...customFor(k).map((e) => ({ ...withIcon(e.t, '📌'), id: e.id })),
  ];
}
function updateToday() {
  const box = $('#todayStrip');
  box.replaceChildren();
  for (const ev of eventsOn(todayKey())) {
    const item = document.createElement('span');
    item.className = 't-item';
    const icon = document.createElement('span');
    icon.textContent = ev.icon;
    const name = document.createElement('span');
    name.textContent = ev.name;
    item.append(icon, name);
    box.append(item);
  }
}

/* ---------------- click a day: see and add your own events ---------------- */
const dayDlg = $('#dayDlg');
let openKey = null;

function renderDayList() {
  const ul = $('#dayList');
  ul.replaceChildren();
  for (const ev of eventsOn(openKey)) {
    const li = document.createElement('li');
    const icon = document.createElement('span');
    icon.textContent = ev.icon;
    const name = document.createElement('span');
    name.textContent = ev.name;
    li.append(icon, name);
    if (ev.id) {
      const del = document.createElement('button');
      del.type = 'button';
      del.textContent = '×';
      del.setAttribute('aria-label', 'Delete event');
      del.addEventListener('click', () => {
        custom[openKey] = customFor(openKey).filter((e) => e.id !== ev.id);
        if (!custom[openKey].length) delete custom[openKey];
        save(EVENTS_KEY, custom);
        renderDayList(); renderCalendar(); updateToday();
      });
      li.append(del);
    }
    ul.append(li);
  }
}
function openDay(k, d) {
  openKey = k;
  $('#dayTitle').textContent = d.toLocaleDateString(undefined, { weekday: 'long', day: 'numeric', month: 'long', year: 'numeric' });
  $('#dayInput').value = '';
  renderDayList();
  dayDlg.showModal();
  $('#dayInput').focus();
}
$('#dayForm').addEventListener('submit', (e) => {
  e.preventDefault();
  const t = $('#dayInput').value.trim();
  if (!t || !openKey) return;
  (custom[openKey] ||= []).push({ id: Date.now().toString(36) + Math.random().toString(36).slice(2, 6), t });
  save(EVENTS_KEY, custom);
  $('#dayInput').value = '';
  renderDayList(); renderCalendar(); updateToday();
});
$('#dayClose').addEventListener('click', () => dayDlg.close());
dayDlg.addEventListener('click', (e) => { if (e.target === dayDlg) dayDlg.close(); });

/* ---------------- RSS ---------------- */
const grid = $('#grid');

function textOf(el, name) {
  const n = el.getElementsByTagNameNS('*', name)[0];
  return n ? n.textContent.trim() : '';
}

function parseFeed(xml) {
  const doc = new DOMParser().parseFromString(xml, 'text/xml');
  const out = [];
  for (const e of doc.querySelectorAll('item, entry')) {
    const title = textOf(e, 'title');
    const linkEl = e.querySelector('link[href]');
    const link = linkEl ? linkEl.getAttribute('href') : textOf(e, 'link');
    if (!title || !link) continue;

    const date = Date.parse(textOf(e, 'pubDate') || textOf(e, 'published') || textOf(e, 'updated') || textOf(e, 'date')) || 0;
    const html = textOf(e, 'encoded') || textOf(e, 'description') || textOf(e, 'summary') || textOf(e, 'content');

    let image =
      [...e.getElementsByTagNameNS('*', 'content'), ...e.getElementsByTagNameNS('*', 'thumbnail')]
        .map((n) => n.getAttribute('url')).find(Boolean) ||
      [...e.getElementsByTagName('enclosure')]
        .find((n) => /image/.test(n.getAttribute('type') || ''))?.getAttribute('url') ||
      (html.match(/<img[^>]+src=["']([^"']+)["']/i) || [])[1] || '';
    if (image) {
      try { image = new URL(image, link).href.replace(/^http:/, 'https:'); } catch { image = ''; }
    }

    const desc = (new DOMParser().parseFromString(html, 'text/html').body.textContent || '')
      .replace(/\s+/g, ' ').trim().slice(0, 220);

    out.push({ title, link, date, image, desc });
  }
  return out;
}

function hueFor(s) {
  let h = 0;
  for (const c of s) h = (h * 31 + c.charCodeAt(0)) % 360;
  return h;
}

/* ---------------- pictures for stories that have none ----------------
   1. the article's own og:image (the most relevant picture there is)
   2. otherwise an image search on the story title (Bing, then DuckDuckGo; both unofficial and may break)
   3. otherwise the gradient stays */
const cleanTitle = (t) => t.replace(/\s+[-–—|·]\s+[^-–—|·]{2,40}$/, '').slice(0, 120);

function ldImage(doc) {
  for (const sc of doc.querySelectorAll('script[type="application/ld+json"]')) {
    try {
      const j = JSON.parse(sc.textContent);
      for (const n of Array.isArray(j) ? j : (j['@graph'] || [j])) {
        let im = n && n.image;
        if (Array.isArray(im)) im = im[0];
        if (im && typeof im === 'object') im = im.url;
        if (typeof im === 'string' && im) return im;
      }
    } catch { /* not usable */ }
  }
  return '';
}

async function ogImage(link) {
  // only the first part of the page is needed; the pictures are declared in <head>
  const html = await fetchText(link, { maxBytes: 300000 });
  const doc = new DOMParser().parseFromString(html, 'text/html');
  const meta = doc.querySelector('meta[property="og:image"], meta[property="og:image:url"], meta[name="twitter:image"], meta[name="twitter:image:src"]');
  const src = (meta && meta.getAttribute('content')) ||
    (doc.querySelector('link[rel="image_src"]') || {}).href ||
    ldImage(doc);
  return src ? new URL(src, link).href.replace(/^http:/, 'https:') : '';
}

async function bingImage(title) {
  const q = encodeURIComponent(cleanTitle(title));
  const html = await fetchText(`https://www.bing.com/images/search?q=${q}&first=1`, { maxBytes: 600000 });
  const doc = new DOMParser().parseFromString(html, 'text/html');
  for (const a of doc.querySelectorAll('a.iusc[m]')) {
    try { const m = JSON.parse(a.getAttribute('m')); if (m.turl) return m.turl; } catch { /* skip */ }
  }
  return '';
}

async function ddgImage(title) {
  const q = encodeURIComponent(cleanTitle(title));
  const page = await fetchText(`https://duckduckgo.com/?q=${q}&iax=images&ia=images`);
  const vqd = (page.match(/vqd=["']?([\d-]+)/) || [])[1];
  if (!vqd) return '';
  const json = JSON.parse(await fetchText(`https://duckduckgo.com/i.js?l=us-en&o=json&q=${q}&vqd=${vqd}&f=,,,,,&p=1`));
  const results = json.results || [];
  const hit = results.find((r) => r.width >= 500 && r.thumbnail) || results[0];
  return hit ? hit.thumbnail : '';
}

async function findImage(item) {
  const key = 'newpage.img.' + item.link;
  const c = load(key, null);
  if (c && (c.u || Date.now() - c.t < 3600e3)) return c.u; // retry misses after an hour
  let u = '';
  for (const step of [() => ogImage(item.link), () => bingImage(item.title), () => ddgImage(item.title)]) {
    try { u = await step(); } catch { /* try the next source */ }
    if (u) break;
  }
  save(key, { t: Date.now(), u });
  return u;
}

const imgQueue = [];
let imgActive = 0;
function enqueueImage(item, cover) {
  imgQueue.push({ item, cover });
  setTimeout(pumpImages, 0); // wait until the card has been added to the page
}
function pumpImages() {
  while (imgActive < 4 && imgQueue.length) {
    const { item, cover } = imgQueue.shift();
    if (!cover.isConnected) continue;
    imgActive++;
    findImage(item).then((u) => {
      console.debug('[newpage] picture for', JSON.stringify(item.title.slice(0, 60)), '->', u || 'none found');
      if (!u || !cover.isConnected) return;
      imageUrl(u).then((d) => {
        const img = new Image();
        img.onload = () => { if (cover.isConnected) cover.style.backgroundImage = `url("${d}")`; };
        img.src = d;
      }, () => { /* picture could not be downloaded */ });
    }).finally(() => { imgActive--; pumpImages(); });
  }
}

function renderRssCard(item) {
  const a = document.createElement('a');
  a.className = 'card rss';
  a.href = item.link;
  const cover = document.createElement('div');
  cover.className = 'cover';
  const h = hueFor(item.title);
  const gradient = `linear-gradient(135deg, hsl(${h} 45% 32%), hsl(${(h + 50) % 360} 45% 18%))`;
  const lookup = () => { if (settings.findImages) enqueueImage(item, cover); };
  if (item.image) {
    cover.style.backgroundImage = gradient;
    // a feed picture that is broken or just a tiny icon is replaced by the lookup
    imageUrl(item.image).then((d) => {
      const probe = new Image();
      probe.onload = () => {
        if (probe.naturalWidth && probe.naturalWidth < 200) lookup();
        else if (cover.isConnected) cover.style.backgroundImage = `url("${d}"), ${gradient}`;
      };
      probe.onerror = lookup;
      probe.src = d;
    }, lookup);
  } else {
    cover.style.backgroundImage = gradient;
    lookup();
  }
  const text = document.createElement('div');
  text.className = 'text';
  const h3 = document.createElement('h3');
  h3.textContent = item.title;
  const p = document.createElement('p');
  p.textContent = item.desc;
  text.append(h3, p);
  a.append(cover, text);
  return a;
}

function setRssCards(nodes) {
  grid.querySelectorAll('.rss').forEach((n) => n.remove());
  grid.append(...nodes);
}

// Cards fill the rows top to bottom, then continue to the right; the strip scrolls sideways.
const rowsNow = () => Math.max(1, Number(settings.rows) || 3);
const applyRows = () => document.documentElement.style.setProperty('--rows', String(rowsNow()));
const applyCardW = () => {
  const w = Number(settings.cardW);
  document.documentElement.style.setProperty('--card-w', w > 0 ? `${w}px` : 'clamp(230px, 19vw, 300px)');
};
const colStep = () => (grid.querySelector('.rss')?.getBoundingClientRect().width || 270) + 16;
const firstBatch = () => rowsNow() * (Math.ceil(grid.clientWidth / colStep()) + 2);
const JITTER_HOURS = 72; // bigger = more shuffled order on each load
let pool = [];
let visible = 0;          // RSS cards currently shown
let feedsReady = false;

function renderRss() {
  setRssCards(pool.slice(0, visible).map(renderRssCard));
}

function showMore() {
  const from = visible;
  visible += rowsNow() * 3;
  grid.append(...pool.slice(from, visible).map(renderRssCard));
}

// More stories are added when the strip gets close to its right end (by hand or by auto-scroll).
function maybeLoadMore() {
  if (!feedsReady || visible >= pool.length) return;
  if (grid.scrollLeft + grid.clientWidth > grid.scrollWidth - 3 * colStep()) showMore();
}
grid.addEventListener('scroll', maybeLoadMore, { passive: true });

// The mouse wheel scrolls the strip sideways. Every `scrollSecs` seconds (setting, 0 = off) it moves on
// by one column, unless the pointer is over it or it was used a few seconds ago.
let lastTouch = 0, hovering = false, lastStep = Date.now();
const touched = () => { lastTouch = Date.now(); };
grid.addEventListener('wheel', (e) => {
  touched();
  if (Math.abs(e.deltaY) > Math.abs(e.deltaX)) { e.preventDefault(); grid.scrollLeft += e.deltaY; }
}, { passive: false });
for (const ev of ['pointerdown', 'touchstart', 'keydown']) grid.addEventListener(ev, touched, { passive: true });
grid.addEventListener('mouseenter', () => { hovering = true; });
grid.addEventListener('mouseleave', () => { hovering = false; touched(); });
const behavior = () => (matchMedia('(prefers-reduced-motion: reduce)').matches ? 'auto' : 'smooth');
const scrollEvery = () => { const s = Number(settings.scrollSecs); return s > 0 ? Math.max(1, s) * 1000 : 0; };
setInterval(() => {
  const every = scrollEvery();
  if (!every || Date.now() - lastStep < every) return;
  if (!feedsReady || document.hidden || hovering || Date.now() - lastTouch < Math.max(8000, every + 3000)) return;
  lastStep = Date.now();
  const atEnd = grid.scrollLeft + grid.clientWidth >= grid.scrollWidth - 4;
  if (atEnd && visible >= pool.length) { grid.scrollTo({ left: 0, behavior: behavior() }); return; }
  if (atEnd) showMore();
  grid.scrollBy({ left: colStep(), behavior: behavior() });
}, 250);

const FEED_TTL = 20 * 60e3;
async function getFeed(url) {
  const key = 'newpage.feed.' + url;
  const cached = load(key, null);
  if (cached && Date.now() - cached.t < FEED_TTL) return cached.v;
  try {
    const items = parseFeed(await fetchText(url)).sort((a, b) => b.date - a.date).slice(0, 15);
    save(key, { t: Date.now(), v: items });
    return items;
  } catch (e) {
    if (cached) return cached.v; // stale beats nothing
    throw e;
  }
}

let feedSeq = 0;
async function loadFeeds() {
  const seq = ++feedSeq;
  feedsReady = false;
  visible = firstBatch();

  if (!settings.feeds.length) {
    const empty = document.createElement('div');
    empty.className = 'card rss empty';
    empty.textContent = 'Add RSS feeds in settings to see the latest stories here.';
    setRssCards([empty]);
    return;
  }
  setRssCards(Array.from({ length: visible }, () => Object.assign(document.createElement('div'), { className: 'card rss skeleton' })));

  const results = await Promise.allSettled(settings.feeds.map(getFeed));
  if (seq !== feedSeq) return;

  // Recent stories first, but shuffled a little on every load: each item is penalised by 12h per
  // position in its own feed (so one busy feed can't fill the page) and by a random amount up to
  // JITTER_HOURS.
  const seen = new Set();
  const all = [];
  for (const r of results) {
    if (r.status !== 'fulfilled') continue;
    r.value.forEach((it, rank) => {
      if (seen.has(it.link)) return;
      seen.add(it.link);
      all.push({ ...it, score: it.date - rank * 12 * 3600e3 - Math.random() * JITTER_HOURS * 3600e3 });
    });
  }
  pool = all.sort((a, b) => b.score - a.score);

  if (!pool.length) {
    const empty = document.createElement('div');
    empty.className = 'card rss empty';
    empty.textContent = 'No stories loaded. Check your feed URLs in settings.';
    setRssCards([empty]);
    return;
  }
  visible = firstBatch();
  feedsReady = true;
  renderRss();
}

/* ---------------- settings ---------------- */
const dlg = $('#settings');

let resizeTimer;
window.addEventListener('resize', () => { clearTimeout(resizeTimer); resizeTimer = setTimeout(maybeLoadMore, 150); });

function initSettings() {
  $('#sTheme').value = settings.theme;
  $('#sBg').value = settings.bg;
  $('#sRows').value = String(rowsNow());
  $('#sCountry').value = settings.country;
  $('#sGcal').value = settings.gcal;
  $('#sStt').value = settings.sttUrl;
  $('#sFeeds').value = settings.feeds.join('\n');
  $('#sFindImg').checked = settings.findImages;
  $('#sBgMode').value = settings.bgMode;
  $('#sBgCount').value = String(bgPerDay() === 1 ? (Number(settings.bgCount) || 8) : bgPerDay());
  $('#sBgCount').disabled = settings.bgMode === 'daily';
  $('#sCardAuto').checked = !(Number(settings.cardW) > 0);
  $('#sCardW').value = String(Number(settings.cardW) > 0 ? settings.cardW : 270);
  $('#sCardW').disabled = $('#sCardAuto').checked;
  $('#sCardWOut').textContent = $('#sCardAuto').checked ? 'automatic' : `${$('#sCardW').value}px`;
  $('#sScroll').value = String(Number(settings.scrollSecs) >= 0 ? settings.scrollSecs : 5);
  $('#sIntro').checked = !!settings.intro;
  $('#sGlassAuto').checked = !(Number(settings.glass) > 0);
  $('#sGlass').value = String(Number(settings.glass) > 0 ? settings.glass : 32);
  $('#sGlass').disabled = $('#sGlassAuto').checked;
  $('#sGlassOut').textContent = $('#sGlassAuto').checked ? 'default' : `${$('#sGlass').value}%`;
  $('#sNewTab').checked = !!settings.newTab;

  $('#settingsBtn').addEventListener('click', () => dlg.showModal());
  $('#sClose').addEventListener('click', () => dlg.close());
  dlg.addEventListener('click', (e) => { if (e.target === dlg) dlg.close(); });

  $('#sFindImg').addEventListener('change', (e) => { settings.findImages = e.target.checked; persist(); if (feedsReady) renderRss(); });
  $('#sNewTab').addEventListener('change', (e) => { settings.newTab = e.target.checked; persist(); });
  $('#sTheme').addEventListener('change', (e) => { settings.theme = e.target.value; persist(); applyTheme(); });
  $('#sRows').addEventListener('change', (e) => {
    settings.rows = Number(e.target.value);
    persist();
    applyRows();
    if (feedsReady) { visible = firstBatch(); renderRss(); grid.scrollLeft = 0; }
  });
  $('#sBg').addEventListener('change', (e) => { settings.bg = e.target.value; persist(); loadBackground(); });
  $('#sBgMode').addEventListener('change', (e) => {
    settings.bgMode = e.target.value;
    $('#sBgCount').disabled = settings.bgMode === 'daily';
    persist();
    loadBackground();
  });
  $('#sBgCount').addEventListener('change', (e) => {
    settings.bgCount = Math.min(20, Math.max(2, Math.round(Number(e.target.value)) || 8));
    e.target.value = String(settings.bgCount);
    persist();
    if (settings.bgMode === 'random') loadBackground();
  });

  const cardWChanged = () => {
    const auto = $('#sCardAuto').checked;
    settings.cardW = auto ? 0 : Number($('#sCardW').value);
    $('#sCardW').disabled = auto;
    $('#sCardWOut').textContent = auto ? 'automatic' : `${$('#sCardW').value}px`;
    persist();
    applyCardW();
    if (feedsReady) { visible = firstBatch(); renderRss(); maybeLoadMore(); }
  };
  $('#sCardAuto').addEventListener('change', cardWChanged);
  $('#sCardW').addEventListener('input', () => { $('#sCardWOut').textContent = `${$('#sCardW').value}px`; });
  $('#sCardW').addEventListener('change', cardWChanged);

  $('#sScroll').addEventListener('change', (e) => {
    const s = Math.min(120, Math.max(0, Math.round(Number(e.target.value)) || 0));
    settings.scrollSecs = s;
    e.target.value = String(s);
    lastStep = Date.now();
    persist();
  });
  $('#sIntro').addEventListener('change', (e) => { settings.intro = e.target.checked; persist(); });

  const glassChanged = () => {
    const auto = $('#sGlassAuto').checked;
    settings.glass = auto ? 0 : Number($('#sGlass').value);
    $('#sGlass').disabled = auto;
    $('#sGlassOut').textContent = auto ? 'default' : `${$('#sGlass').value}%`;
    persist();
    applyGlass();
  };
  $('#sGlassAuto').addEventListener('change', glassChanged);
  $('#sGlass').addEventListener('input', glassChanged);
  $('#sCountry').addEventListener('change', (e) => {
    settings.country = e.target.value.trim().toUpperCase();
    persist();
    loadHolidays(view.getFullYear());
  });
  $('#sStt').addEventListener('change', (e) => { settings.sttUrl = e.target.value.trim(); persist(); });
  $('#sGcal').addEventListener('change', (e) => { settings.gcal = e.target.value.trim(); persist(); loadGcal(); });
  $('#sFeeds').addEventListener('change', (e) => {
    settings.feeds = e.target.value.split('\n').map((s) => s.trim()).filter(Boolean);
    settings.feedsCustom = true;
    persist();
    loadFeeds();
  });
}


/* ---------------- to-do list (kept in this browser) ---------------- */
const TODO_KEY = 'newpage.todos';
let todos = load(TODO_KEY, []);   // [{ id, t, done }], newest first
const saveTodos = () => { save(TODO_KEY, todos); renderTodos(); };

function renderTodos() {
  const ul = $('#todoList');
  ul.replaceChildren();
  for (const item of todos) {
    const li = document.createElement('li');
    li.classList.toggle('done', item.done);
    const box = Object.assign(document.createElement('input'), { type: 'checkbox', checked: item.done });
    box.setAttribute('aria-label', 'Done');
    box.addEventListener('change', () => { item.done = box.checked; saveTodos(); });
    const text = document.createElement('span');
    text.textContent = item.t;
    const del = Object.assign(document.createElement('button'), { type: 'button', textContent: '×' });
    del.setAttribute('aria-label', 'Delete task');
    del.addEventListener('click', () => { todos = todos.filter((x) => x.id !== item.id); saveTodos(); });
    li.append(box, text, del);
    ul.append(li);
  }
  $('#todoCount').textContent = todos.length ? `${todos.filter((x) => !x.done).length} left` : '';
  $('#todoClear').hidden = !todos.some((x) => x.done);
}
$('#todoForm').addEventListener('submit', (e) => {
  e.preventDefault();
  const t = $('#todoInput').value.trim();
  if (!t) return;
  todos.unshift({ id: Date.now().toString(36) + Math.random().toString(36).slice(2, 5), t, done: false });
  $('#todoInput').value = '';
  saveTodos();
});
$('#todoClear').addEventListener('click', () => { todos = todos.filter((x) => !x.done); saveTodos(); });

/* ---------------- quote of the day (same quote all day, works offline) ---------------- */
const QUOTES = [
  ['It does not matter how slowly you go as long as you do not stop.', 'Confucius'],
  ['A journey of a thousand miles begins with a single step.', 'Lao Tzu'],
  ['We are what we repeatedly do. Excellence, then, is not an act, but a habit.', 'Will Durant'],
  ['Well done is better than well said.', 'Benjamin Franklin'],
  ['Energy and persistence conquer all things.', 'Benjamin Franklin'],
  ['Lost time is never found again.', 'Benjamin Franklin'],
  ['If I have seen further, it is by standing on the shoulders of giants.', 'Isaac Newton'],
  ['The best way to predict the future is to invent it.', 'Alan Kay'],
  ['Premature optimization is the root of all evil.', 'Donald Knuth'],
  ['Make it work, make it right, make it fast.', 'Kent Beck'],
  ['Programs must be written for people to read, and only incidentally for machines to execute.', 'Abelson and Sussman'],
  ['Plans are worthless, but planning is everything.', 'Dwight D. Eisenhower'],
  ['Genius is one percent inspiration, ninety-nine percent perspiration.', 'Thomas Edison'],
  ['The impediment to action advances action. What stands in the way becomes the way.', 'Marcus Aurelius'],
  ['Waste no more time arguing about what a good man should be. Be one.', 'Marcus Aurelius'],
  ['We suffer more often in imagination than in reality.', 'Seneca'],
  ['The unexamined life is not worth living.', 'Socrates'],
  ['He who has a why to live can bear almost any how.', 'Friedrich Nietzsche'],
  ['Experience is the name everyone gives to their mistakes.', 'Oscar Wilde'],
  ['The only thing we have to fear is fear itself.', 'Franklin D. Roosevelt'],
  ['I learned that courage was not the absence of fear, but the triumph over it.', 'Nelson Mandela'],
  ['Nothing in life is to be feared, it is only to be understood.', 'Marie Curie'],
  ['Strive not to be a success, but rather to be of value.', 'Albert Einstein'],
  ['The only way to do great work is to love what you do.', 'Steve Jobs'],
  ['To thine own self be true.', 'William Shakespeare'],
  ['Be not afraid of going slowly; be afraid only of standing still.', 'Chinese proverb'],
  ['Fall seven times, stand up eight.', 'Japanese proverb'],
  ['Measure twice, cut once.', 'Proverb'],
  ['A smooth sea never made a skilled sailor.', 'Proverb'],
  ['The best time to plant a tree was twenty years ago. The second best time is now.', 'Proverb'],
];
function showQuote() {
  const d = new Date();
  const n = Math.floor(Date.UTC(d.getFullYear(), d.getMonth(), d.getDate()) / 864e5);
  const [text, by] = QUOTES[n % QUOTES.length];
  $('#quoteText').textContent = text;
  $('#quoteBy').textContent = by;
}

/* ---------------- clock ---------------- */
function tickClock() {
  if (document.hidden) return;
  const d = new Date();
  const time = d.toLocaleTimeString(undefined, { hour: '2-digit', minute: '2-digit', second: '2-digit' });
  const date = d.toLocaleDateString(undefined, { weekday: 'long', day: 'numeric', month: 'long', year: 'numeric' });
  if ($('#clockTime').textContent !== time) $('#clockTime').textContent = time;
  if ($('#clockDate').textContent !== date) {
    const first = !$('#clockDate').textContent;
    $('#clockDate').textContent = date;
    if (!first) { updateToday(); showQuote(); } // a new day started
  }
}

/* ---------------- start ---------------- */
applyTheme();
applyTabIcon();
applyGlass();
applyRows();
applyCardW();
tickClock();
setInterval(tickClock, 1000);
document.addEventListener('visibilitychange', tickClock);
fillEngines();
fillAis();
initSettings();
renderCalendar();
loadBackground();
loadHolidays(view.getFullYear());
loadGcal();
updateToday();
renderTodos();
showQuote();
loadFeeds();
