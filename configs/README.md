# configs — JSON serving profiles for Qwen3.8-27B

One engine command, one JSON per profile:

```sh
./build-linux/apps/ninfer-serve --config configs/mtp-long-vision.json   # start (Ctrl+C stops)
./build-linux/apps/ninfer-serve --config configs/mtp-long-vision.json --print-args
```

`--config` resolves the profile into the exact flag set the measured launcher built
(`scripts/run.sh qwen38-27b tuned`, whose branch the engine's `--config` mapper now reproduces
directly) — the same configuration, with no shell and no Python in between. Every profile runs
in the foreground: **Ctrl+C** stops the engine gracefully (which flushes the context store).
`--print-args` prints the derived command line (any API key redacted) and exits without loading
or binding — diff it to see exactly what a profile means. Any explicit CLI flag overrides the
profile's value (the profile supplies the base, your flags win).

## Profiles

| profile | spec | context | lanes | vision | Web UI |
|---|---|---|---|---|---|
| `mtp-long-vision.json` | mtp | fits to 262k | 2 | on | on |
| `mtp-long-text.json` | mtp | fits to 262k | 2 | off | on |
| `code-dflash2.json` | dflash2 | fits to 262k | 1 | on | on |
| `code-mtp.json` | mtp | fits to 262k | 2 | on | on |
| `coding-long-vision.json` | dflash2 | fits (~205k) | 1 | on | on |
| `coding-long-text.json` | dflash2 | **192k pinned** | 1 | off | on |
| `dflash2-fast.json` | dflash2 | fits (~197k) | 1 | off | on |
| `mtp-262k-pinned-nowebui.json` | mtp | **262k pinned** | 2 | on | **off** |

The Web UI is the embedded Studio: served by the engine itself at `/webui/` on the same port as
the API (see [docs/webui.md](../docs/webui.md)).

## JSON keys

Every key is optional; the defaults are shown.

| key | default | meaning |
|---|---|---|
| `notes` | — | free-form description of the profile, for humans and tooling; the engine does not read it |
| `model` | `qwen38-27b` | model key (`qwen38-27b` · `qwen36-35b-a3b`) or an explicit artifact path. Keys resolve under `models/`, then `scripts/models/`, as `run.sh` does; `NINFER_MODEL` (path) or `NINFER_MODEL_DIR` (directory) override resolution |
| `spec` | `mtp` | speculative backend: `mtp` · `dflash2` · `none` |
| `draft_tokens` | 7 (dflash2) / 3 (mtp) | `--draft-tokens` |
| `lanes` | 1 (dflash2) / 2 (mtp) | `--max-concurrency` (1–8) |
| `kv_dtype` | `rk4v4` | KV quantisation |
| `vision` | `false` | enable image input (`--vision`) |
| `vision_residency` | `overlay` | `overlay` (tower in host RAM, ~free) · `resident` (tower in VRAM) |
| `ngram` | true (dflash2, 1–2 lanes) / false | n-gram copy drafting (dflash2 only) |
| `context` | *(unset)* | a number **pins** `--max-context`/`--kv-capacity` (turns off `--fit-context`: start at exactly this, or refuse). Omit/null to let it **fit** the card (largest that fits, ≤ 262,144). |
| `max_output_tokens` | `32768` | per-request output cap |
| `port` | `8082` | the single listener port: API and Web UI share it |
| `host` | `127.0.0.1` | bind address → `--host` |
| `api_key` | *(absent)* | a value: required as bearer token or `x-api-key` header on every request → `--api-key` (the Studio page has a key field and forwards it; `/health` stays open). `null`: explicitly **no** key — it clears any `NINFER_API_KEY` the shell inherited, so the profile is unambiguous about its own auth. Absent: inherits the environment. |
| `context_store` | `default` | `default` → `configs/context-store/<model>` · a path (relative to the repo root, or absolute) · `""` to disable. Budget: 2 GiB by default (`--context-store-max-gib`), override with `env: {"NINFER_CONTEXT_STORE_MAX_GIB": "8"}`. |
| `webui.enabled` | `true` | serve the Studio at `/webui/` on the engine's listener (anonymous, like `/health`). `false` → no `/webui` routes at all (API only). |
| `webui.auto_open` | *(ignored)* | the engine does not open browsers; it logs the Studio URL in the startup line instead (`listening on http://… | Studio http://…/webui/`) |
| `env` | *(none)* | extra environment applied with `setenv` before the engine starts, e.g. `{"NINFER_PREFILL_CHUNK": "2048"}`. Any `NINFER_*` knob the engine reads straight from the environment (e.g. `NINFER_CUDA_SYNC`) keeps working this way. An `env` entry also wins over the profile's own keys where they overlap (e.g. `NINFER_API_KEY` over `api_key`). |

## The context knob, in one line

Leave `context` out and the engine starts at **up to 262,144**, degrading to whatever the card
can hold that moment (`--fit-context`) — this is why MTP gives you ~262k while DFlash2, whose
draft model is bigger, lands ~197k beside a 0.9 GiB desktop (measured 2026-11-11). Set
`context: 262144` to **demand** the full size: the engine then either starts at 262k or refuses
to start rather than shrink. `coding-long-text.json` pins 192,000 — the widest fixed context the
sweep left headroom for the store — while `coding-long-vision.json` leaves it out and **fits**
instead (~205k beside a light desktop, per its notes); both use a 2048-token prefill chunk
(~300 MiB less cuBLAS workspace). Note the `coding-long-*` profiles also carry
`NINFER_FIT_HEADROOM_MIB` in `env` — this engine has no consumer for that variable (it came
along from the upstream fork); it is inert here and harmless to keep.

## Notes

* **DFlash2 vs MTP context:** on this 24 GB card, MTP fits the full 262,144 beside a desktop;
  DFlash2's extra draft weights put its fit at ~197k on a nearly empty card (the 2026-11-11
  profile sweep; ~130k beside a heavily loaded desktop). DFlash2 decodes code ~1.45x faster
  (226 vs 152 tok/s), so `coding-long-*` are DFlash2 tuned for the widest context:
  `coding-long-text` pins 192k (deterministic, leaves headroom for the store) and
  `coding-long-vision` fits (~205k beside a light desktop, room to paste screenshots); pick MTP
  only if you need the full 262k.
* **Everything runs in the foreground:** the command stays open after the banner; **Ctrl+C**
  stops the engine (a graceful stop, which flushes the context store) and returns you to the
  prompt. There is no separate launcher process to manage — the Studio page and the API live in
  the same process. An engine started some other way (e.g. `scripts/run.sh`) is stopped by hand
  (`pkill -f ninfer-serve`).
* **Host-pin retry:** if the engine fails to pin its default 8 GiB host-context budget
  (`cudaMallocHost failed`), the launcher retries halving the budget down to 0, mirroring what
  `scripts/run.sh` did around the server. A budget you chose yourself (`env`/CLI) fails instead.
* **Publishing on the network:** `host: "0.0.0.0"` exposes the engine (and the Studio) to every
  network the box is on. Pair it with `api_key` — the Studio page asks for the key and sends it
  as a bearer token — otherwise the API is unauthenticated.
