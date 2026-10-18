(() => {
  'use strict';

  const terminalEl = document.getElementById('terminal');
  const overlay = document.getElementById('overlay');
  const overlayTitle = document.getElementById('overlayTitle');
  const overlayText = document.getElementById('overlayText');
  const overlayBtn = document.getElementById('overlayBtn');
  const overlaySpinner = document.getElementById('overlaySpinner');
  const statusBadge = document.getElementById('statusBadge');
  const statusText = document.getElementById('statusText');
  const screenMeta = document.getElementById('screenMeta');
  const sizeInfo = document.getElementById('sizeInfo');

  const MONO =
    '"JetBrains Mono", "Cascadia Code", "SFMono-Regular", Menlo, Consolas, "Liberation Mono", monospace';

  const STREAM_GRACE_MS = 4500;   // сколько ждём первый кадр по SSE
  const POLL_INTERVAL_MS = 55;    // период опроса, если стрим не прошёл через прокси
  const RECONNECT_DELAY_MS = 2200;

  function fontSizeForViewport() {
    const width = window.innerWidth;
    if (width < 560) return 8.5;
    if (width < 820) return 10;
    if (width < 1100) return 12.5;
    return 14.5;
  }

  const term = new Terminal({
    cols: 100,
    rows: 38,
    fontFamily: MONO,
    fontSize: fontSizeForViewport(),
    lineHeight: 1.12,
    letterSpacing: 0,
    cursorBlink: false,
    cursorStyle: 'bar',
    disableStdin: true,
    scrollback: 0,
    allowTransparency: false,
    theme: {
      background: '#0b100e',
      foreground: '#dcece2',
      cursor: '#4ef08b',
      cursorAccent: '#0b100e',
      selectionBackground: 'rgba(78, 240, 139, 0.25)',
      black: '#101a15',
      red: '#ff6b6b',
      green: '#4ef08b',
      yellow: '#ffd166',
      blue: '#6ec3ff',
      magenta: '#d19cff',
      cyan: '#66e0d0',
      white: '#e7f2ea',
      brightBlack: '#4a6156',
      brightRed: '#ff8a8a',
      brightGreen: '#a6ffb8',
      brightYellow: '#fff2a3',
      brightBlue: '#9ed8ff',
      brightMagenta: '#e2b9ff',
      brightCyan: '#9ff5e8',
      brightWhite: '#ffffff',
    },
  });

  const fitAddon = new FitAddon.FitAddon();
  term.loadAddon(fitAddon);
  term.open(terminalEl);

  // ---------------------------------------------------------------------------
  // Состояние соединения
  // ---------------------------------------------------------------------------

  const conn = {
    id: null,
    mode: null,          // 'stream' | 'poll' | null
    reader: null,
    abort: null,
    pollTimer: null,
    seq: 0,
    hasOutput: false,
    stopped: false,      // соединение закрыто намеренно (или уже обработано)
    graceTimer: null,
    retryTimer: null,
  };

  function updateSizeLabel() {
    screenMeta.textContent = `${term.cols} × ${term.rows}`;
    sizeInfo.textContent = `${term.cols}×${term.rows}`;
  }

  function setStatus(kind, label) {
    statusBadge.classList.remove('online', 'offline', 'connecting');
    statusBadge.classList.add(kind);
    statusText.textContent = label;
  }

  function showOverlay(title, text, buttonLabel, spinning) {
    overlay.hidden = false;
    overlayTitle.textContent = title;
    overlayText.textContent = text;
    overlaySpinner.hidden = !spinning;
    overlayBtn.hidden = !buttonLabel;
    if (buttonLabel) overlayBtn.textContent = buttonLabel;
  }

  function hideOverlay() {
    overlay.hidden = true;
  }

  function markOnline() {
    if (conn.hasOutput) return;
    conn.hasOutput = true;
    clearTimeout(conn.graceTimer);
    hideOverlay();
    setStatus('online', 'онлайн');
    term.focus();
  }

  async function postJson(url, payload) {
    return fetch(url, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload),
      cache: 'no-store',
    });
  }

  function send(command) {
    if (!conn.id) return;
    postJson('/api/input', { id: conn.id, cmd: command }).catch(() => {
      /* сеть мигнула — следующая клавиша всё равно уйдёт */
    });
  }

  function teardown(notifyServer) {
    conn.stopped = true;
    clearTimeout(conn.graceTimer);
    clearTimeout(conn.retryTimer);
    clearInterval(conn.pollTimer);
    conn.pollTimer = null;
    conn.graceTimer = null;
    conn.retryTimer = null;

    if (conn.reader) {
      try {
        conn.reader.cancel();
      } catch { /* уже закрыт */ }
      conn.reader = null;
    }
    if (conn.abort) {
      conn.abort.abort();
      conn.abort = null;
    }
    if (notifyServer && conn.id) {
      postJson('/api/close', { id: conn.id }).catch(() => {});
    }
    conn.id = null;
    conn.mode = null;
    conn.hasOutput = false;
  }

  function gameOverOverlay() {
    setStatus('offline', 'сессия завершена');
    showOverlay(
      'Игра окончена',
      'Процесс змейки завершился. Запустите новую сессию, чтобы сыграть ещё.',
      'Сыграть ещё',
      false
    );
  }

  function connectionLostOverlay(message) {
    setStatus('offline', 'нет связи');
    showOverlay(
      'Соединение потеряно',
      message || 'Проверяем связь с сервером…',
      'Переподключиться',
      true
    );
    conn.retryTimer = setTimeout(connect, RECONNECT_DELAY_MS);
  }

  // ---------------------------------------------------------------------------
  // Транспорт 1: SSE-стрим
  // ---------------------------------------------------------------------------

  function handleStreamEvent(event) {
    if (!event || typeof event.t !== 'string') return;

    if (event.t === 'ready') {
      setStatus('connecting', 'сессия запускается');
      return;
    }
    if (event.t === 'out' && typeof event.d === 'string') {
      markOnline();
      term.write(event.d);
      return;
    }
    if (event.t === 'end') {
      conn.stopped = true;
      clearInterval(conn.pollTimer);
      gameOverOverlay();
    }
  }

  async function readStream(response) {
    const reader = response.body.getReader();
    conn.reader = reader;
    const decoder = new TextDecoder();
    let buffer = '';

    for (;;) {
      const { value, done } = await reader.read();
      if (done) break;
      buffer += decoder.decode(value, { stream: true });

      let boundary = buffer.indexOf('\n\n');
      while (boundary >= 0) {
        const block = buffer.slice(0, boundary);
        buffer = buffer.slice(boundary + 2);
        for (const line of block.split('\n')) {
          if (!line.startsWith('data:')) continue;
          try {
            handleStreamEvent(JSON.parse(line.slice(5).trim()));
          } catch {
            /* мусорный кадр пропускаем */
          }
        }
        boundary = buffer.indexOf('\n\n');
      }
    }
  }

  async function openStream() {
    conn.mode = 'stream';
    conn.abort = new AbortController();

    const response = await fetch(`/api/stream?id=${encodeURIComponent(conn.id)}`, {
      headers: { Accept: 'text/event-stream' },
      signal: conn.abort.signal,
      cache: 'no-store',
    });
    if (!response.ok || !response.body) {
      throw new Error(`Стрим недоступен (${response.status})`);
    }

    // Если прокси буферизует ответ — переключаемся на поллинг.
    conn.graceTimer = setTimeout(() => {
      if (!conn.hasOutput && conn.mode === 'stream') {
        switchToPolling();
      }
    }, STREAM_GRACE_MS);

    try {
      await readStream(response);
    } catch (error) {
      // Отмена при переходе на поллинг — это не ошибка.
      if (conn.stopped || conn.mode === 'poll') return;
      throw error;
    }
    conn.reader = null;

    if (conn.stopped) return;

    // Стрим закрылся. Если это был штатный конец игры, клиент уже получил
    // событие 'end'; иначе проверяем состояние поллингом (прокси мог обрезать).
    switchToPolling();
  }

  // ---------------------------------------------------------------------------
  // Транспорт 2: поллинг (страховка для прокси, буферизующих SSE)
  // ---------------------------------------------------------------------------

  function applyFrames(d, reset) {
    if (!d) return;
    let data = d;
    if (reset) {
      // Пересинхронизация: показываем последний целый кадр.
      const last = data.lastIndexOf('\x1b[H');
      if (last > 0) data = data.slice(last);
    }
    markOnline();
    term.write(data);
  }

  async function pollOnce() {
    if (!conn.id || conn.stopped) return;
    try {
      const response = await fetch(
        `/api/poll?id=${encodeURIComponent(conn.id)}&since=${conn.seq}`,
        { cache: 'no-store' }
      );

      if (response.status === 404) {
        if (!conn.stopped) {
          conn.stopped = true;
          teardown(false);
          connectionLostOverlay('Игровая сессия не найдена. Пробуем создать новую.');
        }
        return;
      }
      if (!response.ok) throw new Error(`HTTP ${response.status}`);

      const data = await response.json();
      if (conn.stopped || !data) return;

      applyFrames(data.d, Boolean(data.reset));
      conn.seq = data.seq;

      if (data.ended) {
        conn.stopped = true;
        clearInterval(conn.pollTimer);
        conn.pollTimer = null;
        gameOverOverlay();
      }
    } catch (error) {
      if (conn.stopped) return;
      conn.stopped = true;
      teardown(false);
      connectionLostOverlay(error && error.message ? error.message : undefined);
    }
  }

  function startPolling() {
    conn.mode = 'poll';
    conn.seq = 0;
    conn.pollTimer = setInterval(pollOnce, POLL_INTERVAL_MS);
    pollOnce();
  }

  function switchToPolling() {
    clearTimeout(conn.graceTimer);
    conn.graceTimer = null;

    if (conn.reader) {
      try {
        conn.reader.cancel();
      } catch { /* уже закрыт */ }
      conn.reader = null;
    }
    if (conn.abort) {
      conn.abort.abort();
      conn.abort = null;
    }
    if (conn.stopped) return;

    // Сессия на сервере живёт ещё немного, поэтому просто продолжаем её читать.
    startPolling();
  }

  // ---------------------------------------------------------------------------
  // Запуск
  // ---------------------------------------------------------------------------

  async function connect() {
    teardown(true);

    conn.stopped = false;
    conn.hasOutput = false;
    conn.seq = 0;
    conn.mode = null;

    setStatus('connecting', 'подключение…');
    showOverlay('Подключаемся…', 'Запускаем змейку на C++', null, true);

    try {
      const sessionResponse = await postJson('/api/session', {
        cols: term.cols,
        rows: term.rows,
      });

      if (!sessionResponse.ok) {
        let message = 'Сервер не принял сессию.';
        try {
          const data = await sessionResponse.json();
          if (data && data.message) message = data.message;
        } catch { /* без деталей */ }
        throw new Error(message);
      }

      const session = await sessionResponse.json();
      if (conn.stopped) return;
      conn.id = session.id;

      await openStream();
    } catch (error) {
      if (conn.stopped) return;
      teardown(false);
      connectionLostOverlay(error && error.message ? error.message : undefined);
    }
  }

  overlayBtn.addEventListener('click', () => {
    hideOverlay();
    connect();
  });

  document.querySelectorAll('[data-cmd]').forEach((button) => {
    button.addEventListener('click', () => send(button.dataset.cmd));
  });

  const ARROWS = {
    ArrowUp: 'up',
    ArrowDown: 'down',
    ArrowLeft: 'left',
    ArrowRight: 'right',
  };
  const LETTERS = new Set(['w', 'a', 's', 'd', 'p', 'r', 'q', 'ц', 'ф', 'ы', 'в', 'п', 'к', 'й']);

  document.addEventListener('keydown', (event) => {
    if (event.defaultPrevented || event.ctrlKey || event.metaKey || event.altKey) return;

    if (!overlay.hidden && !overlayBtn.hidden &&
        (event.key === 'Enter' || event.key === ' ' || event.key === 'r' || event.key === 'R' ||
         event.key === 'к' || event.key === 'К')) {
      event.preventDefault();
      overlayBtn.click();
      return;
    }

    const arrow = ARROWS[event.key];
    if (arrow) {
      event.preventDefault();
      send(arrow);
      return;
    }
    if (event.key === 'Enter' || event.key === ' ') {
      event.preventDefault();
      send('enter');
      return;
    }
    if (event.key.length === 1) {
      const key = event.key.toLowerCase();
      if (LETTERS.has(key)) {
        event.preventDefault();
        send(key);
      }
    }
  });

  window.addEventListener('pagehide', () => {
    const id = conn.id;
    teardown(false);
    if (id && navigator.sendBeacon) {
      navigator.sendBeacon('/api/close', JSON.stringify({ id }));
    }
  });

  requestAnimationFrame(() => {
    fitAddon.fit();
    updateSizeLabel();
    connect();
  });

  window.addEventListener('resize', () => {
    if (!conn.id) {
      fitAddon.fit();
      updateSizeLabel();
    }
  });
})();
