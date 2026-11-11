# Web UI (Studio)

NInfer Studio is the browser front end for a `ninfer-serve` instance: a streaming
chat client, live load telemetry, and model suspend/resume. It is plain static
files (HTML, CSS, JS, fonts) **embedded in the `ninfer-serve` binary at build
time** and served by the engine's own HTTP listener — there is no separate
process, no extra port, no Python and no build step at runtime.

## Start

The Studio comes up with the engine whenever it is enabled:

* a serving profile with `webui.enabled` left at its default (`true`) — see
  [configs/README.md](../configs/README.md);
* an explicit `--webui` flag.

`webui.enabled: false` (or no flag on a plain `--artifact` start) serves no
`/webui` routes at all — the path answers `404` — and the API-only shape is
exactly the pre-Studio server. The engine prints the address in its startup
line:

```
listening on http://127.0.0.1:8082 | model qwen3_8_27b.ninfer | auth disabled | Studio http://127.0.0.1:8082/webui/
```

Open that URL. `http://127.0.0.1:8082/` redirects to `/webui/`.

## Access policy

* `/webui/` (all assets) and `/` follow `/health`'s policy: **anonymous**, even
  on a keyed server.
* The API calls the page makes (chat, suspend/resume, model list) send the key
  from the page's key field as `Authorization: Bearer <key>`. On a keyed server
  the page shows a banner until a key is saved; the key is stored in the
  browser's `localStorage` (`ninfer.key`) and never sent anywhere but this
  origin.
* Live telemetry arrives over an SSE stream, `GET /api/stream` (the `/v1/load`
  payload every two seconds, plus readiness and residency state).
  `EventSource` cannot set headers, so the stream takes the key as `?key=`, an
  `x-api-key` header, or a bearer — without one, a keyed server answers `401`.
* The key value is redacted in every log surface: the server's request logger
  prints `…?key=****` for the stream query parameter, and `--api-key` is
  redacted in the recorded startup command line.

## What the page does

Four tabs:

* **Chat** — streaming completions (`POST /v1/chat/completions`): image
  attachment when the model reports `input_modalities: ["text","image"]`,
  reasoning shown in a collapsed block (`reasoning_content`), per-completion
  stats (wall time, tok/s, usage), temperature / max tokens / reasoning effort
  controls.
* **Live** — the SSE feed: throughput sparklines (decode tok/s, prefill tok/s),
  request lanes, device/host KV and state occupancy, the host-context peak
  since start (with an uptime-regression guard so a restart never inherits an
  old peak), and host/device timing.
* **Engine** — capacity and model identity: max concurrency, pending limit,
  context and KV capacity, host context budget.
* **Model** — suspend/resume buttons (`POST /v1/models/{id}/suspend|resume`).
  Suspend releases the model's device memory; a busy engine refuses with `409`.
  The status pill in the header reflects `ready` / `loading` / `suspended`.

English and Greek labels; light/dark theme; both remembered in `localStorage`.
All URLs are same-origin relative; nothing phones home.

## What the old Studio had that this one doesn't

The previous Studio was a separate Python process that launched the engine and
proxied it. With the UI folded into the engine those launcher features are gone,
along with the panels that depended on them:

* engine **start/stop** from the page (the engine is the page's process — stop
  it with Ctrl+C, as you would any terminal program);
* the engine **log tail** and the **artifacts-on-disk** table (the separate
  process's own files are the engine's now);
* the **NVML temperatures / power / fan** panel and VRAM utilisation tile —
  the engine publishes no GPU sensor series;
* **capability probing** of an external server — this UI talks to the engine
  it is embedded in.

## Verification

The config-profile mapper (JSON → engine invocation) and the serve options are
covered by CPU-only C++ tests in the regular suite (`ninfer_config_profile_test`,
`ninfer_serve_options_test`): `ctest -R "config_profile|serve_options"` after
building the `ninfer_tests` target. They need no model and no GPU. The old
`webui/test_studio.py` (and the CI job that ran it) is gone with the Python
layer; GitHub-hosted runners cannot build this repository (no CUDA toolchain),
so no CI job covers the C++ tests — run them on the dev box.

## Troubleshooting

| Symptom | Meaning |
|---|---|
| Key banner on open | the server runs with `--api-key`; enter it in the banner. A wrong key gives `401`s on chat. |
| status pill `loading model` | `/health` answers `503`; the artifact is still loading. Large models take minutes. |
| `/webui/` answers `404` | the engine was started without the Studio (`webui.enabled: false`, no `--webui`). Restart with it enabled. |
| SSE reconnecting (pill flickers) | transient; the page retries. A persistent failure means the key is wrong (keyed server) or the port changed. |
| suspend refused, `409` toast | a request is in flight; suspend applies between requests. |
