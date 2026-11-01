// Dev only. Run: node server.js   then open http://localhost:5173
// Requires Node 18+. Stands in for the C++ PageHandler::FetchText.
const http = require('http');
const fs = require('fs');
const path = require('path');

const PORT = process.env.PORT || 5173;
const MAX_BYTES = 2 * 1024 * 1024;
const MIME = {
  '.html': 'text/html; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
};

http.createServer(async (req, res) => {
  const u = new URL(req.url, `http://localhost:${PORT}`);

  if (u.pathname === '/proxy') {
    try {
      const target = new URL(u.searchParams.get('url'));
      if (target.protocol !== 'https:') throw new Error('https only');
      const r = await fetch(target, {
        signal: AbortSignal.timeout(10000),
        headers: { 'user-agent': 'Mozilla/5.0 NewPageDev', referer: target.origin + '/', 'accept-language': 'en-US,en;q=0.9' },
      });
      // ?max=N reads only the first N bytes (enough for a page's <head>); otherwise too big is an error
      const explicit = Math.min(Number(u.searchParams.get('max')) || 0, MAX_BYTES);
      const limit = explicit || MAX_BYTES;
      const chunks = [];
      let total = 0;
      const reader = r.body.getReader();
      for (;;) {
        const { done, value } = await reader.read();
        if (done) break;
        chunks.push(Buffer.from(value));
        total += value.length;
        if (total > limit) {
          if (!explicit) throw new Error('too large');
          await reader.cancel();
          break;
        }
      }
      const buf = Buffer.concat(chunks).subarray(0, limit);
      res.writeHead(r.status, { 'content-type': r.headers.get('content-type') || 'text/plain' });
      res.end(buf);
    } catch (e) {
      res.writeHead(502);
      res.end('proxy error: ' + e.message);
    }
    return;
  }

  const file = path.join(__dirname, u.pathname === '/' ? 'index.html' : decodeURIComponent(u.pathname));
  if (!file.startsWith(__dirname)) { res.writeHead(403); res.end(); return; }
  fs.readFile(file, (err, data) => {
    if (err) { res.writeHead(404); res.end('not found'); return; }
    res.writeHead(200, { 'content-type': MIME[path.extname(file)] || 'application/octet-stream' });
    res.end(data);
  });
}).listen(PORT, () => console.log(`New page dev server: http://localhost:${PORT}`));
