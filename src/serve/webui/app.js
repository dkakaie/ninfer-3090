/* NInfer Studio — served by ninfer-serve itself at /webui/.
 *
 * All endpoints are same-origin: chat goes to /v1/chat/completions, live metrics
 * arrive over the /api/stream SSE feed (the /v1/load payload every two seconds,
 * plus readiness and residency), and suspend/resume hit /v1/models/{id}/… .
 * When the server runs with --api-key, the key is kept in localStorage and sent
 * as a Bearer header (EventSource cannot set headers, so the stream takes it as
 * a ?key= query parameter, which the server redacts from its request log).
 */

/* ─────────────────────────── i18n ─────────────────────────── */
const I18N = {
  en: {
    'a11y.skip': 'Skip to content',
    'a11y.theme': 'Toggle theme',
    'a11y.lang': 'Switch language',

    'brand.sub': 'local inference · RTX 3090',
    'tab.chat': 'Chat', 'tab.live': 'Live', 'tab.engine': 'Engine', 'tab.model': 'Model',

    'status.connecting': 'Connecting…',
    'status.loading': 'loading model',
    'status.ready': 'ready',
    'status.busy': 'generating',
    'status.suspended': 'suspended',
    'status.offline': 'offline',

    'common.retry': 'Retry',
    'common.loading': 'loading…',

    'key.hint': 'This server was started with an API key (--api-key). Enter it to unlock chat and the metrics stream.',
    'key.placeholder': 'API key',
    'key.save': 'Save',
    'key.clear': 'Clear',
    'key.invalid': 'The server rejected that key (401). Try again.',

    'chat.title': 'Chat',
    'chat.sub': 'Streaming from the local engine',
    'chat.new': 'New chat',
    'chat.write': 'Write',
    'chat.ready': 'ready',
    'chat.busy': 'generating',
    'chat.thinking': 'thinking',
    'chat.maxTokens': 'max tokens',
    'chat.temp': 'temp',
    'chat.off': 'off',
    'chat.emptyTitle': 'Ready for your first question',
    'chat.emptyBody': 'Text or image (drag & drop). History stays in this browser tab only.',
    'chat.attach': 'Attach image',
    'chat.placeholder': 'Ask something… (Enter = send, Shift+Enter = new line)',
    'chat.send': 'Send',
    'chat.stop': 'Stop',
    'chat.turns': '{n} messages',
    'chat.turnsOne': '{n} message',
    'chat.you': 'You',
    'chat.ai': 'AI',
    'chat.thinkingTokens': 'thinking · {n} tokens',
    'chat.thinkingOpen': 'thinking',

    'stat.gen': 'gen',
    'stat.decode': 'decode',
    'stat.prefill': 'prefill',
    'stat.accept': 'draft',
    'stat.ttft': 'ttft',
    'stat.total': 'total',
    'stat.genTitle': 'Tokens generated in this reply',
    'stat.decodeTitle': 'Generation speed: output tokens per second, measured from the first token to the last, so prompt reading is excluded.',
    'stat.prefillTitle': 'Prompt reading speed: prompt tokens divided by the time until the first output token',
    'stat.ttftTitle': 'Time to first token: how long the engine spent reading the prompt before answering',
    'stat.totalTitle': 'Total wall-clock time of this request',

    'm.decode': 'decode',
    'm.decodeTitle': 'Decode',
    'm.tprTitle': 'Tok / round',
    'm.tprFoot': 'speculative verification',
    'm.prefill': 'prefill',
    'm.kvTitle': 'KV cache',

    'rail.waiting': 'waiting for a sample',
    'rail.lastReq': 'last request · {n} tok',
    'rail.now': 'Now',
    'rail.tokRound': 'tok / round',
    'rail.generated': 'generated',
    'rail.kvPages': 'KV pages',
    'rail.model': 'model',
    'rail.context': 'context',
    'rail.uptime': 'uptime',
    'rail.state': 'state',
    'rail.suspend': 'Suspend',
    'rail.resume': 'Resume',

    'live.title': 'Live',
    'live.sub': 'Telemetry from',
    'live.sub2': ', sample every',
    'live.refresh': 'Refresh',
    'live.uptime': 'uptime',
    'live.concurrency': 'concurrency',

    'ctx.title': 'Context space',
    'ctx.hint': 'host pinned',
    'ctx.used': 'in use',
    'ctx.total': 'total',
    'ctx.peak': 'peak',
    'ctx.kvTokens': 'KV tokens',

    'q.title': 'Queues',

    'eng.title': 'Engine',
    'eng.sub': 'What the server reports about itself — no estimates.',
    'eng.resume': 'Resume',
    'eng.hint': 'Suspend snapshots the model state to host RAM and releases its device memory. Resume reads the weights back from disk.',
    'eng.process': 'Process',
    'eng.modelId': 'model id',
    'eng.decoded': 'decode tokens',
    'eng.prefilled': 'prefill tokens',
    'eng.reused': 'reused prompt tokens',
    'eng.sleeping': 'sleeping',
    'eng.capacity': 'Capacity',
    'eng.maxConc': 'max concurrency',
    'eng.maxPending': 'max pending',
    'eng.maxCtx': 'max context',
    'eng.kvCap': 'KV capacity',
    'eng.hostCap': 'host context',

    'state.idle': 'Idle',
    'state.generating': 'Generating',
    'state.prefill': 'Prefill',
    'state.suspended': 'Suspended',
    'state.offline': 'Engine down',
    'state.loading': 'Loading…',

    'model.title': 'Model',
    'model.sub': 'The loaded artifact, as the server reports it.',
    'model.loaded': 'Loaded',
    'model.alias': 'alias',
    'model.modalities': 'modalities',
    'model.state': 'state',
    'model.notLoaded': 'not loaded',
    'model.suspended': 'suspended',
    'model.loadedTag': 'loaded',

    'toast.suspended': 'Model suspended — device memory released.',
    'toast.resumed': 'Model resumed.',
    'toast.busy': 'Engine is busy — suspend refused (409).',
    'toast.failed': 'Request failed: {msg}',
    'toast.stopped': 'Generation cancelled.',

    'err.offline': 'The server is not reachable. Make sure ninfer-serve is running.'
  },

  el: {
    'a11y.skip': 'Μετάβαση στο περιεχόμενο',
    'a11y.theme': 'Αλλαγή θέματος',
    'a11y.lang': 'Αλλαγή γλώσσας',

    'brand.sub': 'τοπικό inference · RTX 3090',
    'tab.chat': 'Συνομιλία', 'tab.live': 'Live', 'tab.engine': 'Μηχανή', 'tab.model': 'Μοντέλο',

    'status.connecting': 'Σύνδεση…',
    'status.loading': 'φορτώνει το μοντέλο',
    'status.ready': 'έτοιμο',
    'status.busy': 'παράγει',
    'status.suspended': 'σε αναστολή',
    'status.offline': 'εκτός',

    'common.retry': 'Ξανά',
    'common.loading': 'φόρτωση…',

    'key.hint': 'Ο server ξεκίνησε με API key (--api-key). Εισήγαγέ το για chat και stream μετρήσεων.',
    'key.placeholder': 'API key',
    'key.save': 'Αποθήκευση',
    'key.clear': 'Διαγραφή',
    'key.invalid': 'Ο server απόρριψε το κλειδί (401). Δοκίμασε ξανά.',

    'chat.title': 'Συνομιλία',
    'chat.sub': 'Streaming από το τοπικό engine',
    'chat.new': 'Νέα συνομιλία',
    'chat.write': 'Γράψε',
    'chat.ready': 'έτοιμο',
    'chat.busy': 'παράγει',
    'chat.thinking': 'thinking',
    'chat.maxTokens': 'max tokens',
    'chat.temp': 'θερμοκρασία',
    'chat.off': 'κλειστό',
    'chat.emptyTitle': 'Έτοιμο για την πρώτη ερώτηση',
    'chat.emptyBody': 'Κείμενο ή εικόνα (drag & drop). Το ιστορικό μένει μόνο σε αυτή την καρτέλα.',
    'chat.attach': 'Επισύναψη εικόνας',
    'chat.placeholder': 'Ρώτα κάτι… (Enter = αποστολή, Shift+Enter = νέα γραμμή)',
    'chat.send': 'Αποστολή',
    'chat.stop': 'Διακοπή',
    'chat.turns': '{n} μηνύματα',
    'chat.turnsOne': '{n} μήνυμα',
    'chat.you': 'Εσύ',
    'chat.ai': 'AI',
    'chat.thinkingTokens': 'thinking · {n} tokens',
    'chat.thinkingOpen': 'thinking',

    'stat.gen': 'παραγωγή',
    'stat.decode': 'decode',
    'stat.prefill': 'prefill',
    'stat.accept': 'draft',
    'stat.ttft': 'ttft',
    'stat.total': 'σύνολο',
    'stat.genTitle': 'Tokens που παράχθηκαν σε αυτή την απάντηση',
    'stat.decodeTitle': 'Ταχύτητα παραγωγής: tokens ανά δευτερόλεπτο από το πρώτο ως το τελευταίο token — χωρίς το διάβασμα του prompt.',
    'stat.prefillTitle': 'Ταχύτητα ανάγνωσης prompt: tokens prompt διά τον χρόνο μέχρι το πρώτο token',
    'stat.ttftTitle': 'Χρόνος μέχρι το πρώτο token: πόσο έκανε ο engine να διαβάσει το prompt',
    'stat.totalTitle': 'Συνολικός πραγματικός χρόνος του αιτήματος',

    'm.decode': 'decode',
    'm.decodeTitle': 'Decode',
    'm.tprTitle': 'Tok / round',
    'm.tprFoot': 'speculative verification',
    'm.prefill': 'prefill',
    'm.kvTitle': 'KV cache',

    'rail.waiting': 'αναμονή δείγματος',
    'rail.lastReq': 'τελευταίο αίτημα · {n} tok',
    'rail.now': 'Τώρα',
    'rail.tokRound': 'tok / round',
    'rail.generated': 'παράχθηκαν',
    'rail.kvPages': 'KV pages',
    'rail.model': 'μοντέλο',
    'rail.context': 'context',
    'rail.uptime': 'uptime',
    'rail.state': 'κατάσταση',
    'rail.suspend': 'Αναστολή',
    'rail.resume': 'Επαναφορά',

    'live.title': 'Live',
    'live.sub': 'Telemetry από',
    'live.sub2': ', δείγμα κάθε',
    'live.refresh': 'Ανανέωση',
    'live.uptime': 'uptime',
    'live.concurrency': 'παράλληλα αιτήματα',

    'ctx.title': 'Χώρος context',
    'ctx.hint': 'host pinned',
    'ctx.used': 'σε χρήση',
    'ctx.total': 'σύνολο',
    'ctx.peak': 'κορυφή',
    'ctx.kvTokens': 'KV tokens',

    'q.title': 'Ουρές',

    'eng.title': 'Μηχανή',
    'eng.sub': 'Ό,τι αναφέρει ο server για τον εαυτό του — χωρίς εκτιμήσεις.',
    'eng.resume': 'Επαναφορά',
    'eng.hint': 'Η αναστολή κρατά snapshot της κατάστασης του μοντέλου στη RAM και ελευθερώνει τη μνήμη της κάρτας. Η επαναφορά ξαναδιαβάζει τα weights από τον δίσκο.',
    'eng.process': 'Διεργασία',
    'eng.modelId': 'model id',
    'eng.decoded': 'decode tokens',
    'eng.prefilled': 'prefill tokens',
    'eng.reused': 'ανακυκλωμένα prompt tokens',
    'eng.sleeping': 'sleeping',
    'eng.capacity': 'Χωρητικότητα',
    'eng.maxConc': 'μέγιστη παράλληλη',
    'eng.maxPending': 'μέγιστες αναμονές',
    'eng.maxCtx': 'μέγιστο context',
    'eng.kvCap': 'χωρητικότητα KV',
    'eng.hostCap': 'host context',

    'state.idle': 'Αδρανές',
    'state.generating': 'Παράγει',
    'state.prefill': 'Prefill',
    'state.suspended': 'Σε αναστολή',
    'state.offline': 'Μηχανή εκτός',
    'state.loading': 'Φορτώνει…',

    'model.title': 'Μοντέλο',
    'model.sub': 'Το φορτωμένο artifact, όπως το δίνει ο server.',
    'model.loaded': 'Φορτωμένο',
    'model.alias': 'alias',
    'model.modalities': 'modalities',
    'model.state': 'κατάσταση',
    'model.notLoaded': 'δεν είναι φορτωμένο',
    'model.suspended': 'σε αναστολή',
    'model.loadedTag': 'φορτωμένο',

    'toast.suspended': 'Το μοντέλο μπήκε σε αναστολή — η μνήμη επιστράφηκε.',
    'toast.resumed': 'Το μοντέλο επανήλθε.',
    'toast.busy': 'Η μηχανή είναι απασχολημένη — η αναστολή απορρίφθηκε (409).',
    'toast.failed': 'Αποτυχία: {msg}',
    'toast.stopped': 'Η παραγωγή ακυρώθηκε.',

    'err.offline': 'Ο server δεν αποκρίνεται. Βεβαιώσου ότι το ninfer-serve τρέχει.'
  }
};

/* ─────────────────────────── basics ─────────────────────────── */
const LS = { lang: 'ninfer.lang', theme: 'ninfer.theme', key: 'ninfer.key' };
const $ = (sel) => document.querySelector(sel);
const $$ = (sel) => Array.from(document.querySelectorAll(sel));

let lang = localStorage.getItem(LS.lang) || 'en';
const t = (key, vars) => {
  const table = I18N[lang] || I18N.en;
  let s = table[key] != null ? table[key] : (I18N.en[key] != null ? I18N.en[key] : key);
  if (vars) for (const k of Object.keys(vars)) s = s.replaceAll('{' + k + '}', vars[k]);
  return s;
};
function applyLang() {
  document.documentElement.lang = lang;
  $('#langLabel').textContent = lang === 'el' ? 'EL' : 'EN';
  $$('[data-i18n]').forEach((el) => { el.textContent = t(el.dataset.i18n); });
  $$('[data-i18n-title]').forEach((el) => { el.title = t(el.dataset.i18nTitle); });
  $$('[data-i18n-placeholder]').forEach((el) => { el.placeholder = t(el.dataset.i18nPlaceholder); });
}
$('#langBtn').addEventListener('click', () => {
  lang = lang === 'en' ? 'el' : 'en';
  localStorage.setItem(LS.lang, lang);
  applyLang();
});

const theme = localStorage.getItem(LS.theme) || 'dark';
document.documentElement.dataset.theme = theme;
$('#themeBtn').addEventListener('click', () => {
  const next = document.documentElement.dataset.theme === 'dark' ? 'light' : 'dark';
  document.documentElement.dataset.theme = next;
  localStorage.setItem(LS.theme, next);
});

const VIEWS = ['chat', 'live', 'engine', 'model'];
function showView(name) {
  if (!VIEWS.includes(name)) name = 'chat';
  $$('.view').forEach((v) => { v.hidden = v.id !== 'view-' + name; });
  $$('.tabs [role="tab"]').forEach((b) => { b.setAttribute('aria-selected', b.dataset.view === name); });
  if (location.hash.slice(1) !== name) location.hash = name;
}
$$('.tabs [role="tab"]').forEach((b) => b.addEventListener('click', () => showView(b.dataset.view)));
window.addEventListener('hashchange', () => showView(location.hash.slice(1)));

/* ─────────────────────────── formatting ─────────────────────────── */
const fmtInt = (n) => (n == null || !isFinite(n) ? '—' : Math.round(n).toLocaleString(lang === 'el' ? 'el-GR' : 'en-US'));
const fmt1 = (n) => (n == null || !isFinite(n) ? '—' : (Math.round(n * 10) / 10).toFixed(1));
const gib = (bytes) => (bytes == null ? null : bytes / 1073741824);
const pct = (a, b) => (a == null || !b ? null : Math.max(0, Math.min(100, (a / b) * 100)));
const bytesText = (b) => {
  if (b == null) return '—';
  const g = b / 1073741824;
  if (g >= 1) return g.toFixed(g >= 10 ? 0 : 1) + ' GiB';
  return Math.round(b / 1048576) + ' MiB';
};
const dur = (s) => {
  if (s == null || !isFinite(s)) return '—';
  const h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60), sec = Math.floor(s % 60);
  return h ? `${h}h ${m}m` : m ? `${m}m ${sec}s` : `${sec}s`;
};
const num = (v) => (typeof v === 'number' && isFinite(v) ? v : null);

function toast(msg, kind) {
  const el = document.createElement('div');
  el.className = 'toast ' + (kind || '');
  el.textContent = msg;
  $('#toasts').appendChild(el);
  setTimeout(() => el.remove(), 4200);
}

/* ─────────────────────────── auth ─────────────────────────── */
let apiKey = localStorage.getItem(LS.key) || '';
const authHeaders = () => (apiKey ? { Authorization: 'Bearer ' + apiKey } : {});
let authLocked = false; // true while the key banner is open

function showKeyBanner(invalid) {
  authLocked = true;
  const banner = $('#keyBanner');
  banner.hidden = false;
  $('#keyBannerText').textContent = invalid ? t('key.invalid') : t('key.hint');
  $('#keyClear').hidden = !apiKey;
  const input = $('#keyInput');
  input.value = '';
  input.focus();
}
function hideKeyBanner() {
  authLocked = false;
  $('#keyBanner').hidden = true;
}
function onUnauthorized() {
  if (authLocked) return;
  showKeyBanner(true);
}
$('#keySave').addEventListener('click', () => {
  const value = $('#keyInput').value.trim();
  if (!value) return;
  apiKey = value;
  localStorage.setItem(LS.key, apiKey);
  hideKeyBanner();
  boot();
});
$('#keyInput').addEventListener('keydown', (e) => { if (e.key === 'Enter') $('#keySave').click(); });
$('#keyClear').addEventListener('click', () => {
  apiKey = '';
  localStorage.removeItem(LS.key);
  hideKeyBanner();
  boot();
});

/* ─────────────────────────── live stream ─────────────────────────── */
let stream = null;
let lastFrame = null;
let prevFrame = null, prevFrameAt = 0;
let prevUptime = null;
let peakCtx = 0;
let failStreak = 0;
let modelMeta = null; // {id, modalities:[...], version}

function closeStream() {
  if (stream) { stream.close(); stream = null; }
}

function connectStream() {
  closeStream();
  const url = '/api/stream' + (apiKey ? '?key=' + encodeURIComponent(apiKey) : '');
  stream = new EventSource(url);
  stream.onmessage = (ev) => {
    try { renderState(JSON.parse(ev.data)); } catch (_) { /* ignore a bad frame */ }
  };
  stream.onerror = () => {
    // EventSource retries on its own; a keyed server answering 401 will keep
    // failing, so after a short streak of errors we hand over to the key banner.
    failStreak += 1;
    renderOffline();
    if (failStreak >= 3) {
      closeStream();
      showKeyBanner(!!apiKey);
    }
  };
}

async function probe() {
  try {
    const res = await fetch('/v1/models', { headers: authHeaders() });
    if (res.status === 401) {
      showKeyBanner(true);
      renderOffline();
      return;
    }
    if (!res.ok) throw new Error(res.status);
    const data = await res.json();
    const m = (data.data || [])[0] || {};
    modelMeta = {
      id: m.id || null,
      modalities: (m.architecture && m.architecture.input_modalities) || [],
      version: res.headers.get('X-NInfer-Version') || null
    };
    failStreak = 0;
    hideKeyBanner();
    connectStream();
  } catch (e) {
    failStreak += 1;
    renderOffline();
  }
}

function renderOffline() {
  lastFrame = null;
  const pill = $('#statusPill');
  pill.dataset.state = 'off';
  $('#statusPillText').textContent = t('status.offline');
  // One missed frame is normal while the server restarts or loads a model, so
  // the warning only appears on the second consecutive miss.
  const banner = $('#connBanner');
  banner.hidden = failStreak < 2;
  if (!banner.hidden) $('#connBannerText').textContent = t('err.offline');
}

$('#connRetry').addEventListener('click', () => { failStreak = 0; probe(); });
$('#liveRefresh').addEventListener('click', () => probe());

/* ─────────────────────────── sparklines ─────────────────────────── */
const HIST = { tps: [], tpr: [], pre: [], kv: [] };
const HIST_MAX = 60;
function push(key, value) {
  if (value == null || !isFinite(value)) return;
  const arr = HIST[key];
  arr.push(value);
  if (arr.length > HIST_MAX) arr.shift();
}
function sparkPath(values, w = 100, h = 28) {
  if (values.length < 2) return '';
  const min = Math.min(...values), max = Math.max(...values);
  const span = max - min || Math.max(1, Math.abs(max) * 0.1);
  const step = values.length > 1 ? w / (values.length - 1) : w;
  return values.map((v, i) => {
    const x = (i * step).toFixed(2);
    const y = (h - 2 - ((v - min) / span) * (h - 4)).toFixed(2);
    return (i ? 'L' : 'M') + x + ',' + y;
  }).join(' ');
}
function paintSparks() {
  for (const key of Object.keys(HIST)) {
    const d = sparkPath(HIST[key]);
    $$(`[data-spark="${key}"]`).forEach((p) => { p.setAttribute('d', d); });
  }
  const rail = $('#sparkPath');
  if (rail) rail.setAttribute('d', sparkPath(HIST.tps));
}

/* ─────────────────────────── render ─────────────────────────── */
function renderStatus(up, sleeping, busy) {
  const pill = $('#statusPill');
  let text, state;
  if (!up) { text = t('status.loading'); state = 'warn'; }
  else if (sleeping) { text = t('status.suspended'); state = 'warn'; }
  else if (busy) { text = t('status.busy'); state = 'busy'; }
  else { text = t('status.ready'); state = 'ok'; }
  pill.dataset.state = state;
  $('#statusPillText').textContent = text;

  const tag = $('#chatState');
  tag.className = 'tag ' + (!up ? 'bad' : (sleeping || busyChat) ? 'warn' : 'good');
  tag.querySelector('span:last-child').textContent =
    !up ? t('status.offline') : (sleeping ? t('status.suspended') : (busyChat ? t('chat.busy') : t('chat.ready')));

  const badge = $('#esBadge');
  const bstate = !up ? 'loading' : sleeping ? 'suspended' : busy ? 'generating' : 'idle';
  badge.dataset.state = bstate === 'loading' ? 'offline' : bstate;
  $('#esState').textContent =
    !up ? t('state.loading') : sleeping ? t('state.suspended') : busy ? t('state.generating') : t('state.idle');

  const pill2 = $('#engPill');
  pill2.textContent = up ? (sleeping ? t('state.suspended') : t('state.idle')) : t('state.offline');
  pill2.className = 'tag ' + (!up ? 'bad' : (sleeping ? 'warn' : 'good'));

  const md = $('#mdLoaded');
  md.textContent = up ? (sleeping ? t('model.suspended') : t('model.loadedTag')) : t('model.notLoaded');
  md.className = 'tag ' + (!up ? 'bad' : (sleeping ? 'warn' : 'good'));
}

function renderState(f) {
  lastFrame = f;
  const up = !!f.ready;
  if (!up) {
    renderStatus(false, false, false);
    // keep the offline banner quiet while the model legitimately loads
    failStreak = 0;
    $('#connBanner').hidden = true;
    return;
  }
  failStreak = 0;
  $('#connBanner').hidden = true;

  const occ = f.occupancy || {};
  const cap = f.capacity || {};
  const req = f.requests || {};
  const counters = f.counters || {};
  const host = f.host || {};
  const lr = f.last_request || null;
  const sleeping = !!f.suspended;

  // Uptime regression guard: uptime only grows while one engine run serves. If
  // it drops, the process (or the attach) restarted, so the peak-since-start
  // number no longer spans a single run — reset it.
  const uptime = num(f.uptime_seconds);
  if (uptime != null) {
    if (prevUptime != null && uptime < prevUptime) peakCtx = 0;
    prevUptime = uptime;
  }

  // Per-frame rates over the Engine's monotonic counters.
  let tps = null, preDelta = null;
  if (prevFrame && prevFrameAt > 0) {
    const dt = (Date.now() - prevFrameAt) / 1000;
    if (dt > 0.1) {
      const p = prevFrame.counters || {};
      const dec = num(counters.committed_decode_tokens), pdec = num(p.committed_decode_tokens);
      const pre = num(counters.computed_prefill_tokens), ppre = num(p.computed_prefill_tokens);
      if (dec != null && pdec != null && dec > pdec) tps = (dec - pdec) / dt;
      if (pre != null && ppre != null && pre > ppre) preDelta = (pre - ppre) / dt;
    }
  }
  prevFrame = f;
  prevFrameAt = Date.now();

  const lastTps = lr && num(lr.inter_token_seconds) > 0 ? 1 / lr.inter_token_seconds : null;
  const tpsShown = tps != null ? tps : lastTps;
  const decodeTok = num(counters.committed_decode_tokens);
  const rounds = num(counters.decode_rounds);
  const tpr = decodeTok != null && rounds ? decodeTok / rounds : null;
  const busy = (num(req.running) || 0) > 0 || (num(req.prefilling) || 0) > 0;

  const kvUsed = num(occ.device_main_kv_pages);
  const kvTotal = num(cap.kv_capacity_pages);
  const kvPct = pct(kvUsed, kvTotal);
  const ctxUsed = num(occ.host_context_bytes);
  const ctxTotal = num(cap.host_context_bytes);
  if (ctxUsed != null) peakCtx = Math.max(peakCtx, ctxUsed);

  push('tps', tpsShown); push('tpr', tpr); push('pre', preDelta);
  push('kv', kvUsed != null && kvTotal ? (kvUsed / kvTotal) * 100 : null);
  paintSparks();

  renderStatus(true, sleeping, busy);
  const modelId = (modelMeta && modelMeta.id) || f.model || '—';
  $('#chatModel').textContent = f.model || '—';

  // chat rail
  $('#mTps').textContent = tpsShown != null ? fmt1(tpsShown) : '—';
  $('#mTpsFoot').textContent = busy
    ? (tpr != null ? `≈ ${fmt1(tpr)} tok / round` : t('rail.waiting'))
    : (lr ? t('rail.lastReq', { n: fmtInt(lr.completion_tokens) }) : t('rail.waiting'));
  $('#mTpr').textContent = fmt1(tpr);
  $('#mPrefill').textContent = preDelta != null ? fmt1(preDelta) + ' tok/s' : '—';
  $('#mGen').textContent = decodeTok != null ? fmtInt(decodeTok) : '—';
  $('#mKv').textContent = kvTotal ? `${fmtInt(kvUsed)} / ${fmtInt(kvTotal)}` : '—';
  $('#railModel').textContent = f.model || '—';
  $('#railCtx').textContent = num(cap.max_context) != null ? fmtInt(cap.max_context) + ' tok' : '—';
  $('#railUp').textContent = uptime != null ? dur(uptime) : '—';
  $('#railState').textContent = sleeping ? t('state.suspended') : t('state.idle');
  $('#railSpec').textContent = modelMeta ? (modelMeta.version || '—') : '—';

  // engine strip
  $('#esModel').textContent = f.model || '—';
  $('#esUptime').textContent = uptime != null ? dur(uptime) : '—';
  $('#esContext').textContent = num(cap.max_context) != null ? fmtInt(cap.max_context) + ' tok' : '—';
  $('#esKv').textContent = kvTotal ? `${fmtInt(kvUsed)} / ${fmtInt(kvTotal)} pages` : '—';
  $('#esConc').textContent = num(cap.max_concurrency) != null ? fmtInt(cap.max_concurrency) : '—';

  // metric grid
  $('#mgTps').textContent = tpsShown != null ? fmt1(tpsShown) : '—';
  $('#mgTpsFoot').textContent = busy
    ? (tpr != null ? `≈ ${fmt1(tpr)} tok / round` : '')
    : (lr ? t('rail.lastReq', { n: fmtInt(lr.completion_tokens) }) : '');
  $('#mgTpr').textContent = fmt1(tpr);
  $('#mgPre').textContent = preDelta != null ? fmt1(preDelta) : '—';
  $('#mgPreFoot').textContent = num(counters.computed_prefill_tokens) != null
    ? `${fmtInt(counters.computed_prefill_tokens)} tokens total` : '';
  $('#mgKv').textContent = kvTotal ? `${fmtInt(kvUsed)}/${fmtInt(kvTotal)}` : '—';
  $('#mgKvFoot').textContent = kvPct != null ? `${Math.round(kvPct)}% of the KV pool` : '';
  const kvBar = $('#mgKvBar');
  kvBar.style.width = (kvPct ?? 0) + '%';
  kvBar.className = kvPct > 90 ? 'warn' : '';

  // context + queues
  $('#ctxUsed').textContent = ctxUsed != null ? bytesText(ctxUsed) : '—';
  $('#ctxTotal').textContent = ctxTotal != null ? bytesText(ctxTotal) : '—';
  $('#ctxPeak').textContent = peakCtx ? bytesText(peakCtx) : '—';
  $('#ctxKvTok').textContent = num(occ.device_main_kv_tokens) != null
    ? fmtInt(occ.device_main_kv_tokens) + ' tokens' : '—';
  $('#ctxBar').style.width = (pct(ctxUsed, ctxTotal) ?? 0) + '%';

  $('#qRunning').textContent = fmtInt(num(req.running));
  $('#qWaiting').textContent = fmtInt(num(req.waiting));
  $('#qPrefill').textContent = fmtInt(num(req.prefilling));
  $('#qReady').textContent = fmtInt(num(req.decode_ready));
  $('#qHost').textContent = num(host.active_seconds) != null ? fmt1(host.active_seconds) + ' s' : '—';
  $('#qDev').textContent = num(host.device_wait_seconds) != null ? fmt1(host.device_wait_seconds) + ' s' : '—';

  // engine tab
  $('#eUp').textContent = uptime != null ? dur(uptime) : '—';
  $('#eId').textContent = f.model || '—';
  $('#eCtx').textContent = num(cap.max_context) != null ? fmtInt(cap.max_context) + ' tok' : '—';
  $('#eKv').textContent = kvTotal ? `${fmtInt(kvUsed)} / ${fmtInt(kvTotal)}` : '—';
  $('#eDecoded').textContent = decodeTok != null ? fmtInt(decodeTok) : '—';
  $('#ePrefilled').textContent = num(counters.computed_prefill_tokens) != null
    ? fmtInt(counters.computed_prefill_tokens) : '—';
  $('#eReused').textContent = num(counters.reused_prompt_tokens) != null
    ? fmtInt(counters.reused_prompt_tokens) : '—';
  $('#eSleep').textContent = sleeping ? 'true' : 'false';
  $('#eVer').textContent = (modelMeta && modelMeta.version) || '—';
  $('#cConc').textContent = num(cap.max_concurrency) != null ? fmtInt(cap.max_concurrency) : '—';
  $('#cPend').textContent = num(cap.max_pending_requests) != null ? fmtInt(cap.max_pending_requests) : '—';
  $('#cCtx').textContent = num(cap.max_context) != null ? fmtInt(cap.max_context) + ' tok' : '—';
  $('#cKv').textContent = num(cap.kv_capacity_tokens) != null
    ? `${fmtInt(cap.kv_capacity_tokens)} tok · ${fmtInt(cap.kv_capacity_pages)} pages` : '—';
  $('#cHost').textContent = ctxTotal != null ? bytesText(ctxTotal) : '—';

  // model tab
  $('#mdId').textContent = f.model || '—';
  $('#mdCtx').textContent = num(cap.max_context) != null ? fmtInt(cap.max_context) + ' tok' : '—';
  const mods = (modelMeta && modelMeta.modalities.length) ? modelMeta.modalities.join(', ') : 'text';
  $('#mdMod').textContent = mods;
  $('#mdState').textContent = sleeping ? t('model.suspended') : t('state.idle');

  // suspend/resume buttons reflect state
  $('#engSuspend').disabled = sleeping || !up;
  $('#engResume').disabled = !sleeping;
  $('#railSuspend').disabled = sleeping || !up;
  $('#railResume').disabled = !sleeping;
  const pillIcon = $('#pillAction use');
  if (pillIcon) pillIcon.setAttribute('href', 'icons.svg#' + (sleeping ? 'i-play' : 'i-pause'));
}

/* ─────────────────────────── suspend / resume ─────────────────────────── */
async function modelAction(action, okKey) {
  const id = (lastFrame && lastFrame.model) || (modelMeta && modelMeta.id);
  if (!id) { toast(t('toast.failed', { msg: 'no model' }), 'err'); return; }
  const btns = ['#engSuspend', '#engResume', '#railSuspend', '#railResume', '#pillAction'].map((s) => $(s));
  btns.forEach((b) => { b.disabled = true; });
  try {
    const res = await fetch(`/v1/models/${encodeURIComponent(id)}/${action}`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json', ...authHeaders() },
      body: '{}'
    });
    if (res.ok) {
      toast(t(okKey), 'ok');
    } else if (res.status === 401) {
      onUnauthorized();
    } else if (res.status === 409) {
      toast(t('toast.busy'), 'err');
    } else {
      let msg = res.status;
      try { const e = await res.json(); msg = (e.error && e.error.message) || msg; } catch (_) {}
      toast(t('toast.failed', { msg }), 'err');
    }
  } catch (e) {
    toast(t('toast.failed', { msg: e.message }), 'err');
  } finally {
    btns.forEach((b) => { b.disabled = false; });
  }
}
$('#engSuspend').addEventListener('click', () => modelAction('suspend', 'toast.suspended'));
$('#engResume').addEventListener('click', () => modelAction('resume', 'toast.resumed'));
$('#railSuspend').addEventListener('click', () => modelAction('suspend', 'toast.suspended'));
$('#railResume').addEventListener('click', () => modelAction('resume', 'toast.resumed'));
$('#pillAction').addEventListener('click', () => {
  const sleeping = !!(lastFrame && lastFrame.suspended);
  modelAction(sleeping ? 'resume' : 'suspend', sleeping ? 'toast.resumed' : 'toast.suspended');
});

/* ─────────────────────────── chat ─────────────────────────── */
const messages = [];          // {role, content, images?: [dataUrl]}
let attachments = [];         // pending images
let busyChat = false;
let abortCtrl = null;

function renderTurns() {
  const n = messages.length;
  $('#chatTurns').textContent = n ? t(n === 1 ? 'chat.turnsOne' : 'chat.turns', { n }) : '';
}

function el(tag, cls, text) {
  const node = document.createElement(tag);
  if (cls) node.className = cls;
  if (text != null) node.textContent = text;
  return node;
}

function messageNode(msg) {
  const wrap = el('div', 'msg ' + (msg.role === 'user' ? 'user' : 'assistant'));
  wrap.appendChild(el('div', 'avatar', msg.role === 'user' ? 'EU' : 'AI'));
  const body = el('div', 'body');
  body.appendChild(el('div', 'who', msg.role === 'user' ? t('chat.you') : t('chat.ai')));

  if (msg.images && msg.images.length) {
    const att = el('div', 'attachments');
    msg.images.forEach((src) => {
      const img = document.createElement('img');
      img.src = src;
      att.appendChild(img);
    });
    body.appendChild(att);
  }
  if (msg.reasoning) {
    const det = document.createElement('details');
    det.className = 'thinking';
    det.appendChild(el('summary', null, t('chat.thinkingTokens', { n: msg.reasoning.length })));
    det.appendChild(el('div', 'think-body', msg.reasoning));
    body.appendChild(det);
  }
  const bubble = el('div', 'bubble', msg.content || '');
  body.appendChild(bubble);
  if (msg.stats) body.appendChild(statsRow(msg.stats));
  wrap.appendChild(body);
  return { wrap, bubble, body };
}

function statsRow(stats) {
  const row = el('div', 'stats');
  const chip = (label, value, unit, title, cls) => {
    const c = el('span', 'chip' + (cls ? ' ' + cls : ''));
    c.title = title || '';
    c.innerHTML = `<span class="k">${label}</span><b>${value}</b>${unit ? ' ' + unit : ''}`;
    row.appendChild(c);
  };
  if (stats.outputTokens != null) chip(t('stat.gen'), fmtInt(stats.outputTokens), 'tok', t('stat.genTitle'));
  if (stats.tps != null) chip(t('stat.decode'), fmt1(stats.tps), 'tok/s', t('stat.decodeTitle'), 'good');
  if (stats.prefill != null) chip(t('stat.prefill'), fmt1(stats.prefill), 'tok/s', t('stat.prefillTitle'));
  if (stats.ttft != null) chip(t('stat.ttft'), fmt1(stats.ttft), 's', t('stat.ttftTitle'));
  if (stats.total != null) chip(t('stat.total'), fmt1(stats.total), 's', t('stat.totalTitle'));
  return row;
}

function scrollLog() {
  const log = $('#log');
  log.scrollTop = log.scrollHeight;
}

function renderChat() {
  const log = $('#log');
  log.innerHTML = '';
  if (!messages.length) {
    const empty = el('div', 'empty');
    empty.innerHTML = `<svg class="ico"><use href="icons.svg#i-hub"></use></svg>`;
    empty.appendChild(el('strong', null, t('chat.emptyTitle')));
    empty.appendChild(el('span', null, t('chat.emptyBody')));
    log.appendChild(empty);
  } else {
    messages.forEach((m) => log.appendChild(messageNode(m).wrap));
  }
  renderTurns();
  scrollLog();
}

function renderAttachments() {
  const row = $('#attachRow');
  row.innerHTML = '';
  row.hidden = attachments.length === 0;
  attachments.forEach((src, i) => {
    const chip = el('div', 'attach-chip');
    const img = document.createElement('img');
    img.src = src;
    chip.appendChild(img);
    const x = el('button', null, '×');
    x.type = 'button';
    x.addEventListener('click', () => { attachments.splice(i, 1); renderAttachments(); });
    chip.appendChild(x);
    row.appendChild(chip);
  });
}

async function addFiles(fileList) {
  const files = Array.from(fileList || []).slice(0, 4);
  for (const f of files) {
    if (!f.type.startsWith('image/')) continue;
    const dataUrl = await new Promise((resolve, reject) => {
      const fr = new FileReader();
      fr.onload = () => resolve(fr.result);
      fr.onerror = reject;
      fr.readAsDataURL(f);
    });
    attachments.push(dataUrl);
  }
  renderAttachments();
  $('#input').focus();
}

$('#btnImage').addEventListener('click', () => $('#file').click());
$('#file').addEventListener('change', (e) => { addFiles(e.target.files); e.target.value = ''; });
$('#input').addEventListener('paste', (e) => {
  const items = Array.from(e.clipboardData?.items || []).filter((i) => i.type.startsWith('image/'));
  if (items.length) addFiles(items.map((i) => i.getAsFile()).filter(Boolean));
});
document.addEventListener('dragover', (e) => { if (!$('#view-chat').hidden) e.preventDefault(); });
document.addEventListener('drop', (e) => {
  if ($('#view-chat').hidden) return;
  e.preventDefault();
  addFiles(e.dataTransfer?.files);
});

function autoGrow() {
  const ta = $('#input');
  ta.style.height = 'auto';
  ta.style.height = Math.min(180, ta.scrollHeight) + 'px';
}
$('#input').addEventListener('input', autoGrow);
$('#input').addEventListener('keydown', (e) => {
  if (e.key === 'Enter' && !e.shiftKey) { e.preventDefault(); $('#form').requestSubmit(); }
});
$('#btnFocus').addEventListener('click', () => { showView('chat'); $('#input').focus(); });
$('#btnClear').addEventListener('click', () => {
  if (busyChat) return;
  messages.length = 0;
  attachments = [];
  renderAttachments();
  renderChat();
});

function buildPayload(text) {
  const content = [];
  if (text) content.push({ type: 'text', text });
  attachments.forEach((src) => content.push({ type: 'image_url', image_url: { url: src } }));
  const history = messages
    .filter((m) => m.content || (m.images && m.images.length))
    .map((m) => {
      if (m.role === 'user' && m.images && m.images.length) {
        const parts = [];
        if (m.content) parts.push({ type: 'text', text: m.content });
        m.images.forEach((src) => parts.push({ type: 'image_url', image_url: { url: src } }));
        return { role: m.role, content: parts };
      }
      return { role: m.role, content: m.content };
    });
  history.push({ role: 'user', content: content.length > 1 || (content[0] && content[0].type !== 'text') ? content : text });
  return {
    messages: history,
    stream: true,
    stream_options: { include_usage: true },
    temperature: Number($('#optTemp').value) || 0,
    max_tokens: Number($('#optMax').value) || 1024,
    reasoning_effort: $('#optThink').value === 'none' ? 'none' : $('#optThink').value
  };
}

$('#form').addEventListener('submit', async (e) => {
  e.preventDefault();
  if (busyChat) { if (abortCtrl) abortCtrl.abort(); return; }
  const text = $('#input').value.trim();
  if (!text && !attachments.length) return;

  const images = attachments.slice();
  const payload = buildPayload(text);
  messages.push({ role: 'user', content: text, images });
  attachments = [];
  renderAttachments();
  $('#input').value = '';
  autoGrow();
  renderChat();

  const target = { role: 'assistant', content: '', reasoning: '', stats: null };
  messages.push(target);
  renderChat();

  const nodes = $$('#log .msg');
  const node = nodes[nodes.length - 1];
  const bubble = node.querySelector('.bubble');
  bubble.classList.add('streaming');

  busyChat = true;
  abortCtrl = new AbortController();
  const btn = $('#btnSend');
  btn.classList.remove('btn-primary');
  btn.innerHTML = `<svg class="ico"><use href="icons.svg#i-stop"></use></svg><span>${t('chat.stop')}</span>`;

  const started = performance.now();
  let firstTokenAt = null;
  let outTokens = 0;
  let usage = null;

  try {
    const res = await fetch('/v1/chat/completions', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json', ...authHeaders() },
      body: JSON.stringify(payload),
      signal: abortCtrl.signal
    });
    if (res.status === 401) { onUnauthorized(); throw new Error('unauthorized'); }
    if (!res.ok) {
      const body = await res.text();
      let msg = body.slice(0, 300) || res.status;
      try { msg = (JSON.parse(body).error || {}).message || msg; } catch (_) {}
      throw new Error(msg);
    }
    const reader = res.body.getReader();
    const decoder = new TextDecoder();
    let buf = '';
    let reasoningEl = null, thinkBody = null;

    while (true) {
      const { value, done } = await reader.read();
      if (done) break;
      buf += decoder.decode(value, { stream: true });
      const lines = buf.split('\n');
      buf = lines.pop();
      for (const raw of lines) {
        const line = raw.trim();
        if (!line || line.startsWith(':') || line.startsWith('event:')) continue;
        if (!line.startsWith('data:')) continue;
        const data = line.slice(5).trim();
        if (data === '[DONE]') continue;
        let json;
        try { json = JSON.parse(data); } catch (_) { continue; }
        if (json.usage) usage = json.usage;
        const delta = json.choices?.[0]?.delta || {};
        if (delta.reasoning_content) {
          target.reasoning += delta.reasoning_content;
          if (!reasoningEl) {
            const det = document.createElement('details');
            det.className = 'thinking';
            det.open = true;
            det.appendChild(el('summary', null, t('chat.thinkingOpen')));
            thinkBody = el('div', 'think-body');
            det.appendChild(thinkBody);
            node.querySelector('.body').insertBefore(det, bubble);
            reasoningEl = det;
          }
          thinkBody.textContent = target.reasoning;
        }
        if (delta.content) {
          if (firstTokenAt == null) {
            firstTokenAt = performance.now();
            if (reasoningEl) reasoningEl.open = false;
          }
          target.content += delta.content;
          bubble.textContent = target.content;
          scrollLog();
        }
        outTokens = json.choices?.[0]?.usage?.completion_tokens ?? outTokens;
      }
    }
    bubble.classList.remove('streaming');
    const elapsed = (performance.now() - started) / 1000;
    const out = usage?.completion_tokens ?? outTokens ?? null;
    const prompt = usage?.prompt_tokens ?? null;
    const ttft = firstTokenAt ? (firstTokenAt - started) / 1000 : null;
    const tps = out && firstTokenAt ? out / ((performance.now() - firstTokenAt) / 1000) : null;
    const prefill = prompt && ttft && ttft > 0.01 ? prompt / ttft : null;
    target.stats = {
      outputTokens: out,
      tps,
      prefill,
      ttft,
      total: elapsed
    };
    node.querySelector('.body').appendChild(statsRow(target.stats));
  } catch (err) {
    bubble.classList.remove('streaming');
    if (err.name === 'AbortError') {
      toast(t('toast.stopped'));
    } else {
      bubble.textContent = (target.content ? target.content + '\n\n' : '') + '⚠ ' + err.message;
      if (err.message === 'unauthorized') {
        // onUnauthorized already raised the banner
      } else {
        toast(t('toast.failed', { msg: err.message }), 'err');
      }
    }
  } finally {
    busyChat = false;
    abortCtrl = null;
    btn.classList.add('btn-primary');
    btn.innerHTML = `<svg class="ico"><use href="icons.svg#i-send"></use></svg><span>${t('chat.send')}</span>`;
    if (lastFrame) renderState(lastFrame); // restore the ready/busy tag
    scrollLog();
  }
});

/* ─────────────────────────── boot ─────────────────────────── */
function boot() {
  applyLang();
  renderChat();
  probe();
}
boot();
showView(location.hash.slice(1) || 'chat');
