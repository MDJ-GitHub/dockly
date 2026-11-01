// The only file that differs between dev and Chromium.
//
//  - Opened from http://localhost:5173 (node server.js): uses the /proxy dev server.
//  - Opened as chrome://newpage: talks to NewPageHandler (C++) via chrome.send.

const IN_CHROMIUM = location.protocol === 'chrome:';

let crModule = null;
async function webui() {
  // Dynamic import so the dev server (where chrome:// can't resolve) never loads it.
  crModule ??= await import('chrome://resources/js/cr.js');
  return crModule;
}

function blobToBase64(blob) {
  return new Promise((resolve, reject) => {
    const r = new FileReader();
    r.onload = () => resolve(String(r.result).split(',')[1] || '');
    r.onerror = () => reject(r.error);
    r.readAsDataURL(blob);
  });
}

export async function fetchText(url, opts = {}) {
  if (IN_CHROMIUM) {
    const { sendWithPromise } = await webui();
    // maxBytes > 0 => truncate at the limit instead of failing.
    const body = await sendWithPromise('fetchText', url, opts.maxBytes || 0);
    if (body === null) throw new Error('fetch failed');
    return body;
  }
  const max = opts.maxBytes ? '&max=' + opts.maxBytes : '';
  const res = await fetch('/proxy?url=' + encodeURIComponent(url) + max);
  if (!res.ok) throw new Error('HTTP ' + res.status);
  return res.text();
}

// Sends recorded speech to an OpenAI-compatible transcription endpoint (for example a local
// Whisper server) and returns the text.
export async function transcribe(url, blob) {
  if (IN_CHROMIUM) {
    const { sendWithPromise } = await webui();
    const text = await sendWithPromise('transcribe', url, await blobToBase64(blob));
    if (text === null) throw new Error('transcription failed');
    return String(text).trim();
  }
  const form = new FormData();
  form.append('file', blob, 'speech.webm');
  form.append('model', 'whisper-1');
  form.append('response_format', 'json');
  const res = await fetch(url, { method: 'POST', body: form });
  if (!res.ok) throw new Error('HTTP ' + res.status);
  const json = await res.json();
  return (json.text || '').trim();
}

// Remote images. A chrome:// page cannot load https images itself in this build, so in Chromium the
// browser process downloads them and hands back a data: URL. In dev the URL is used as it is.
const imgCache = new Map();
let imgActive = 0;
const imgWaiting = [];
const imgSlot = () => new Promise((resolve) => {
  const go = () => { imgActive++; resolve(); };
  if (imgActive < 6) go(); else imgWaiting.push(go);
});
const imgRelease = () => { imgActive--; const next = imgWaiting.shift(); if (next) next(); };

// One download through the browser process (no memory cache). Resolves to a data: URL.
async function download(url) {
  await imgSlot();
  try {
    const { sendWithPromise } = await webui();
    const dataUrl = await sendWithPromise('fetchImage', url);
    if (dataUrl === null) throw new Error('image failed');
    return dataUrl;
  } finally {
    imgRelease();
  }
}

// For small pictures (icons, card covers): remembered in memory for the life of the page.
export function imageUrl(url) {
  if (!IN_CHROMIUM || !/^https:/i.test(url)) return Promise.resolve(url);
  if (!imgCache.has(url)) imgCache.set(url, download(url).catch((e) => { imgCache.delete(url); throw e; }));
  return imgCache.get(url);
}

/* ---- wallpapers: kept on disk (IndexedDB) so a new tab never has to download them again ---- */
let dbPromise;
const db = () => (dbPromise ||= new Promise((resolve, reject) => {
  const req = indexedDB.open('newpage-wallpapers', 1);
  req.onupgradeneeded = () => req.result.createObjectStore('img');
  req.onsuccess = () => resolve(req.result);
  req.onerror = () => reject(req.error);
}));
const idb = (mode, fn) => db().then((d) => new Promise((resolve, reject) => {
  const t = d.transaction('img', mode);
  const r = fn(t.objectStore('img'));
  t.oncomplete = () => resolve(r && r.result);
  t.onerror = t.onabort = () => reject(t.error);
}));

function dataUrlToBlob(dataUrl) {
  const mime = dataUrl.slice(5, dataUrl.indexOf(';'));
  const bin = atob(dataUrl.slice(dataUrl.indexOf(',') + 1));
  const bytes = new Uint8Array(bin.length);
  for (let i = 0; i < bin.length; i++) bytes[i] = bin.charCodeAt(i);
  return new Blob([bytes], { type: mime });
}

// A URL the page can show: from the disk cache when it is there, otherwise downloaded and stored.
export async function wallpaperUrl(url) {
  if (!IN_CHROMIUM) return url;
  let blob;
  try { blob = await idb('readonly', (s) => s.get(url)); } catch { /* storage unavailable */ }
  if (!blob) {
    blob = dataUrlToBlob(await download(url));
    idb('readwrite', (s) => s.put(blob, url)).catch(() => {});
  }
  return URL.createObjectURL(blob);
}

// Downloads every wallpaper of today's list that is not stored yet (one at a time, in the
// background) and removes stored pictures that are no longer in the list.
export async function prefetchWallpapers(urls) {
  if (!IN_CHROMIUM) return;
  const run = async () => {
    let have;
    try { have = new Set(await idb('readonly', (s) => s.getAllKeys())); } catch { return; }
    for (const u of urls) {
      if (have.has(u)) continue;
      try {
        const blob = dataUrlToBlob(await download(u));
        await idb('readwrite', (s) => s.put(blob, u));
      } catch { /* too big or offline: it will be tried again on the next tab */ }
      await new Promise((r) => setTimeout(r, 300));
    }
    for (const k of have) if (!urls.includes(k)) await idb('readwrite', (s) => s.delete(k)).catch(() => {});
  };
  // only one tab at a time does this work
  if (navigator.locks) await navigator.locks.request('newpage-wallpaper-prefetch', { ifAvailable: true }, (lock) => (lock ? run() : undefined));
  else await run();
}

// newTab: open in another tab; if the browser blocks the popup, fall back to this tab.
export function navigate(url, newTab = false) {
  if (IN_CHROMIUM) {
    chrome.send('navigate', [url, !!newTab]);
    return;
  }
  if (newTab) {
    const w = window.open(url, '_blank');
    if (w) { w.opener = null; return; }
  }
  window.location.href = url;
}
