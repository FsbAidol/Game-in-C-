'use strict';

// Мини-сервер для браузерной версии змейки.
//
// Без npm-зависимостей:
//   * статика из web/public;
//   * каждая игровая сессия — отдельный процесс build/snake-ui;
//   * вывод игры стримится в браузер через SSE (обычный HTTP, работает через
//     любые прокси), а нажатия клавиш уходят обратно короткими POST-запросами.

const http = require('http');
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const { spawn } = require('child_process');

const PORT = Number(process.env.PORT || 8080);
const HOST = process.env.HOST || '0.0.0.0';
const ROOT = path.resolve(__dirname, '..');
const PUBLIC_DIR = path.join(__dirname, 'public');
const BIN = process.env.SNAKE_BIN || path.join(ROOT, 'build', 'snake-ui');

const MIN_COLS = 40;
const MAX_COLS = 140;
const MIN_ROWS = 18;
const MAX_ROWS = 60;
const MAX_SESSIONS = 16;
const SESSION_IDLE_MS = 30_000;
const FRAME_INTERVAL_MS = 16;
const HEARTBEAT_MS = 15_000;
const HISTORY_LIMIT = 256 * 1024;
const ENDED_TTL_MS = 60_000;

const MIME = {
  '.html': 'text/html; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.json': 'application/json; charset=utf-8',
  '.svg': 'image/svg+xml',
  '.png': 'image/png',
  '.jpg': 'image/jpeg',
  '.webp': 'image/webp',
  '.ico': 'image/x-icon',
  '.woff2': 'font/woff2',
  '.map': 'application/json; charset=utf-8',
  '.txt': 'text/plain; charset=utf-8',
};

const COMMANDS = new Set([
  'w', 'a', 's', 'd', 'p', 'r', 'q',
  'up', 'down', 'left', 'right',
  'enter', 'space', 'pause', 'restart', 'quit',
  'ц', 'ф', 'ы', 'в', 'п', 'к', 'й',
]);

const sessions = new Map();

// Завершённые сессии не считаем «живыми» — они просто ждут последнего опроса.
function activeSessionCount() {
  let count = 0;
  for (const session of sessions.values()) {
    if (!session.ended) count += 1;
  }
  return count;
}

// Если бинарь не собран (например, после чистого клонирования) — собираем сами.
let buildPromise = null;

function ensureBinary() {
  if (fs.existsSync(BIN)) return Promise.resolve(true);
  if (buildPromise) return buildPromise;

  console.log('build/snake-ui не найден — запускаю make…');
  buildPromise = new Promise((resolve) => {
    const make = spawn('make', ['-s', 'all'], { cwd: ROOT, stdio: ['ignore', 'pipe', 'pipe'] });
    let log = '';
    make.stdout.on('data', (chunk) => {
      log += chunk;
    });
    make.stderr.on('data', (chunk) => {
      log += chunk;
    });
    make.on('error', (error) => {
      console.error('make не запустился:', error.message);
      buildPromise = null;
      resolve(false);
    });
    make.on('exit', (code) => {
      if (code === 0 && fs.existsSync(BIN)) {
        console.log('сборка готова:', BIN);
        resolve(true);
        return;
      }
      console.error('сборка не удалась (код ' + code + '):\n' + log.slice(-2000));
      buildPromise = null;
      resolve(false);
    });
  });
  return buildPromise;
}

function clamp(value, min, max) {
  return Math.max(min, Math.min(max, value));
}

function sendJson(res, status, data) {
  const body = JSON.stringify(data);
  res.writeHead(status, {
    'Content-Type': 'application/json; charset=utf-8',
    'Content-Length': Buffer.byteLength(body),
    'Cache-Control': 'no-store',
  });
  res.end(body);
}

function readJsonBody(req, limit = 4096) {
  return new Promise((resolve) => {
    let size = 0;
    const chunks = [];
    req.on('data', (chunk) => {
      size += chunk.length;
      if (size > limit) {
        req.destroy();
        resolve(null);
        return;
      }
      chunks.push(chunk);
    });
    req.on('end', () => {
      try {
        resolve(JSON.parse(Buffer.concat(chunks).toString('utf8') || '{}'));
      } catch {
        resolve(null);
      }
    });
    req.on('error', () => resolve(null));
  });
}

// ---------------------------------------------------------------------------
// Игровая сессия: один процесс snake-ui
// ---------------------------------------------------------------------------

class Session {
  constructor(cols, rows) {
    this.id = crypto.randomBytes(12).toString('hex');
    this.cols = cols;
    this.rows = rows;
    this.closed = false;
    this.sink = null;
    this.pending = '';
    this.flushTimer = null;
    this.lastSeen = Date.now();
    this.child = null;
    this.onEnd = null;
    this.stderr = '';
    this.history = '';
    this.total = 0;
    this.ended = false;
    this.endedAt = 0;

    this.spawn();
    sessions.set(this.id, this);
  }

  spawn() {
    this.child = spawn(BIN, ['--size', `${this.cols}x${this.rows}`], {
      cwd: ROOT,
      env: {
        ...process.env,
        TERM: 'xterm-256color',
        COLORTERM: 'truecolor',
        LANG: process.env.LANG || 'C.UTF-8',
      },
      stdio: ['pipe', 'pipe', 'pipe'],
    });

    this.child.stdout.setEncoding('utf8');
    this.child.stdout.on('data', (chunk) => this.push(chunk));

    this.child.stderr.setEncoding('utf8');
    this.child.stderr.on('data', (chunk) => {
      this.stderr = (this.stderr + chunk).slice(-2000);
      const text = chunk.trim();
      if (text) console.error('[snake]', text);
    });

    this.child.on('error', (error) => {
      this.push(`\r\n\x1b[31mНе удалось запустить игру:\x1b[0m ${error.message}\r\n`);
      this.finish();
    });

    this.child.on('exit', () => {
      // Даём последнему кадру уйти клиенту.
      setTimeout(() => this.finish(), 40);
    });
  }

  push(text) {
    if (this.closed) return;
    this.pending += text;
    if (this.pending.length > 2 * 1024 * 1024) {
      this.pending = this.pending.slice(-1024 * 1024);
    }
    this.history += text;
    this.total += text.length;
    if (this.history.length > HISTORY_LIMIT) {
      this.history = this.history.slice(-Math.floor(HISTORY_LIMIT / 2));
    }
    if (this.sink && !this.flushTimer) {
      this.flushTimer = setTimeout(() => {
        this.flushTimer = null;
        this.flushNow();
      }, FRAME_INTERVAL_MS);
    }
  }

  attach(sink) {
    this.sink = sink;
    this.lastSeen = Date.now();
    if (this.pending) {
      const chunk = this.pending;
      this.pending = '';
      sink(chunk);
    }
  }

  detach() {
    this.sink = null;
    if (this.flushTimer) {
      clearTimeout(this.flushTimer);
      this.flushTimer = null;
    }
  }

  send(command) {
    if (this.closed) return false;
    const cmd = String(command || '').trim().toLowerCase();
    if (!COMMANDS.has(cmd)) return false;
    this.lastSeen = Date.now();
    if (this.child && this.child.stdin.writable) {
      this.child.stdin.write(cmd + '\n');
      return true;
    }
    return false;
  }

  // Всё, что клиент ещё не видел, начиная с позиции since.
  poll(since) {
    const start = this.total - this.history.length;
    const reset = !Number.isFinite(since) || since < start;
    const from = reset ? 0 : Math.max(0, since - start);
    this.lastSeen = Date.now();
    return {
      seq: this.total,
      d: this.history.slice(from),
      reset,
      ended: this.ended,
    };
  }

  finish() {
    if (this.closed || this.ended) return;
    if (this.sink) this.flushNow();
    this.ended = true;
    this.endedAt = Date.now();
    this.detach();
    // Сессию не удаляем сразу: клиент может забрать последний кадр поллингом.
    if (this.onEnd) this.onEnd();
  }

  flushNow() {
    if (!this.sink || !this.pending) return;
    const chunk = this.pending;
    this.pending = '';
    this.sink(chunk);
  }

  close() {
    if (this.closed) return;
    this.closed = true;
    this.ended = true;
    this.detach();
    sessions.delete(this.id);
    if (this.child && this.child.exitCode === null) {
      try {
        this.child.stdin.end();
      } catch { /* already gone */ }
      this.child.kill('SIGTERM');
      const kill = setTimeout(() => {
        if (this.child && this.child.exitCode === null) this.child.kill('SIGKILL');
      }, 600);
      if (kill.unref) kill.unref();
    }
  }
}

function createSession(cols, rows) {
  if (!fs.existsSync(BIN)) return null;

  // Освобождаем место от уже завершённых сессий.
  for (const [id, session] of sessions) {
    if (session.ended) sessions.delete(id);
  }

  if (sessions.size >= MAX_SESSIONS) {
    // Освобождаем самую старую простаивающую сессию.
    let oldest = null;
    for (const session of sessions.values()) {
      if (oldest === null || session.lastSeen < oldest.lastSeen) oldest = session;
    }
    if (oldest) oldest.close();
    if (sessions.size >= MAX_SESSIONS) return null;
  }
  return new Session(cols, rows);
}

setInterval(() => {
  const now = Date.now();
  for (const session of sessions.values()) {
    if (session.ended) {
      if (now - session.endedAt > ENDED_TTL_MS) session.close();
      continue;
    }
    // Без подключённого стрима и без поллинга сессия считается брошенной.
    if (!session.sink && now - session.lastSeen > SESSION_IDLE_MS) session.close();
  }
}, 10_000).unref();

// ---------------------------------------------------------------------------
// Статика
// ---------------------------------------------------------------------------

function serveStatic(req, res) {
  let pathname;
  try {
    pathname = decodeURIComponent(new URL(req.url, 'http://localhost').pathname);
  } catch {
    res.writeHead(400);
    res.end('Bad request');
    return;
  }

  if (pathname === '/') pathname = '/index.html';

  const filePath = path.resolve(PUBLIC_DIR, '.' + pathname);
  if (filePath !== PUBLIC_DIR && !filePath.startsWith(PUBLIC_DIR + path.sep)) {
    res.writeHead(403);
    res.end('Forbidden');
    return;
  }

  fs.stat(filePath, (err, stat) => {
    if (err || !stat.isFile()) {
      res.writeHead(404, { 'Content-Type': 'text/plain; charset=utf-8' });
      res.end('404 — страница не найдена');
      return;
    }

    const ext = path.extname(filePath).toLowerCase();
    const isVendor = pathname.startsWith('/vendor/');
    res.writeHead(200, {
      'Content-Type': MIME[ext] || 'application/octet-stream',
      'Content-Length': stat.size,
      'X-Content-Type-Options': 'nosniff',
      'Cache-Control': isVendor ? 'public, max-age=3600' : 'no-cache',
    });
    if (req.method === 'HEAD') {
      res.end();
      return;
    }
    fs.createReadStream(filePath).pipe(res);
  });
}

// ---------------------------------------------------------------------------
// HTTP API
// ---------------------------------------------------------------------------

async function handleApi(req, res, url) {
  const route = url.pathname;
  await ensureBinary();

  if (route === '/api/health') {
    sendJson(res, 200, {
      ok: true,
      game: 'snake',
      binary: fs.existsSync(BIN),
      sessions: activeSessionCount(),
      transport: 'sse+poll',
    });
    return true;
  }

  if (route === '/api/session' && req.method === 'POST') {
    const body = await readJsonBody(req);
    if (!body) {
      sendJson(res, 400, { error: 'bad json' });
      return true;
    }
    const cols = Math.floor(clamp(Number(body.cols) || 100, MIN_COLS, MAX_COLS));
    const rows = Math.floor(clamp(Number(body.rows) || 38, MIN_ROWS, MAX_ROWS));

    if (!fs.existsSync(BIN)) {
      sendJson(res, 503, {
        error: 'binary-missing',
        message: 'Не найден build/snake-ui. Соберите игру: make',
      });
      return true;
    }

    const session = createSession(cols, rows);
    if (!session) {
      sendJson(res, 503, { error: 'busy', message: 'Слишком много игровых сессий' });
      return true;
    }
    sendJson(res, 200, { id: session.id, cols, rows });
    return true;
  }

  if (route === '/api/stream') {
    const session = sessions.get(url.searchParams.get('id'));
    if (!session) {
      res.writeHead(404, { 'Content-Type': 'text/plain; charset=utf-8' });
      res.end('session not found');
      return true;
    }

    res.writeHead(200, {
      'Content-Type': 'text/event-stream; charset=utf-8',
      'Cache-Control': 'no-store, no-transform',
      Connection: 'keep-alive',
      'X-Accel-Buffering': 'no',
    });
    if (typeof res.flushHeaders === 'function') res.flushHeaders();

    const writeEvent = (payload) => {
      if (res.writableEnded) return;
      res.write(`data: ${JSON.stringify(payload)}\n\n`);
    };

    writeEvent({ t: 'ready', cols: session.cols, rows: session.rows });

    session.attach((chunk) => writeEvent({ t: 'out', d: chunk }));

    const heartbeat = setInterval(() => {
      if (res.writableEnded) return;
      res.write(': ping\n\n');
    }, HEARTBEAT_MS);
    if (heartbeat.unref) heartbeat.unref();

    let ended = false;
    const cleanup = () => {
      if (ended) return;
      ended = true;
      clearInterval(heartbeat);
      session.detach();
    };

    session.onEnd = () => {
      writeEvent({ t: 'end' });
      cleanup();
      if (!res.writableEnded) res.end();
    };

    // Важно: слушаем именно ответ. Событие 'close' у запроса срабатывает
    // сразу после того, как прочитано тело, — это не разрыв соединения.
    res.on('close', () => {
      // Клиент мог просто переключиться на поллинг — сессию не убиваем,
      // её заберёт сборщик брошенных сессий.
      cleanup();
    });
    return true;
  }

  if (route === '/api/poll') {
    const session = sessions.get(url.searchParams.get('id'));
    if (!session) {
      sendJson(res, 404, { error: 'session-not-found' });
      return true;
    }
    const since = Number(url.searchParams.get('since'));
    const state = session.poll(since);
    sendJson(res, 200, { t: 'frames', seq: state.seq, d: state.d, reset: state.reset, ended: state.ended });
    return true;
  }

  if (route === '/api/input' && req.method === 'POST') {
    const body = await readJsonBody(req);
    const session = body && sessions.get(body.id);
    if (!session) {
      sendJson(res, 404, { error: 'session-not-found' });
      return true;
    }
    const ok = session.send(body.cmd);
    sendJson(res, ok ? 200 : 400, { ok });
    return true;
  }

  if (route === '/api/close' && req.method === 'POST') {
    const body = await readJsonBody(req);
    const session = body && sessions.get(body.id);
    if (session) session.close();
    sendJson(res, 200, { ok: true });
    return true;
  }

  return false;
}

const server = http.createServer(async (req, res) => {
  let url;
  try {
    url = new URL(req.url, 'http://localhost');
  } catch {
    res.writeHead(400);
    res.end('Bad request');
    return;
  }

  if (url.pathname.startsWith('/api/')) {
    try {
      const handled = await handleApi(req, res, url);
      if (!handled) sendJson(res, 404, { error: 'not-found' });
    } catch (error) {
      console.error('[api]', error);
      if (!res.headersSent) sendJson(res, 500, { error: 'internal' });
      else res.end();
    }
    return;
  }

  if (req.method !== 'GET' && req.method !== 'HEAD') {
    res.writeHead(405);
    res.end('Method not allowed');
    return;
  }
  serveStatic(req, res);
});

ensureBinary();

server.listen(PORT, HOST, () => {
  console.log('SNAKE · C++17 terminal edition');
  console.log(`  сервер:  http://${HOST}:${PORT}`);
  console.log(`  бинарь:  ${BIN}${fs.existsSync(BIN) ? '' : '  (не собран — make)'}`);
  console.log('  стрим:   SSE (/api/stream) + POST /api/input');
});

function shutdownServer() {
  console.log('\nОстанавливаюсь…');
  for (const session of sessions.values()) session.close();
  server.close(() => process.exit(0));
  setTimeout(() => process.exit(0), 800);
}

process.on('SIGINT', shutdownServer);
process.on('SIGTERM', shutdownServer);
