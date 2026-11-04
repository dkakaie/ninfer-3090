# bootstrap — JSON-driven serving profiles for Qwen3.8-27B

One launcher, [`bootstrap.sh`](../bootstrap.sh), and one JSON per profile:

```sh
./bootstrap.sh configs/mtp-long-vision.json          # start (engine + Studio if the profile enables it)
./bootstrap.sh configs/mtp-long-vision.json stop     # stop what the profile started
./bootstrap.sh configs/mtp-long-vision.json status   # engine / Studio state
```

`bootstrap.sh` reads the JSON, turns it into `NINFER_*` environment variables, and hands off to
the repo's measured launcher (`scripts/run.sh qwen38-27b tuned`) — so every flag stays exactly
what was measured in [docs/performance.md](../docs/performance.md). When the profile enables the
Studio it goes through `scripts/start-ui.sh` (engine + web UI together); when it does not, it
runs the engine in the foreground (**Ctrl+C** stops it).

## Profiles

| profile | spec | context | lanes | vision | Studio |
|---|---|---|---|---|---|
| `mtp-long-vision.json` | mtp | fits to 262k | 2 | on | on, auto-open |
| `mtp-long-text.json` | mtp | fits to 262k | 2 | off | on, auto-open |
| `dflash2-fast.json` | dflash2 | fits (~130k) | 1 | off | on, no auto-open |
| `mtp-262k-pinned-nowebui.json` | mtp | **262k pinned** | 2 | on | off (foreground) |

## JSON keys

Every key is optional; the defaults are shown.

| key | default | meaning |
|---|---|---|
| `notes` | — | printed at startup; free-form description of the profile |
| `model` | `qwen38-27b` | model key passed to `run.sh` |
| `spec` | `mtp` | speculative backend: `mtp` · `dflash2` · `none` |
| `draft_tokens` | 7 (dflash2) / 3 (mtp) | `--draft-tokens` |
| `lanes` | 1 (dflash2) / 2 (mtp) | `--max-concurrency` (1–8) |
| `kv_dtype` | `rk4v4` | KV quantisation |
| `vision` | `false` | enable image input (`--vision`) |
| `vision_residency` | `overlay` | `overlay` (tower in host RAM, ~free) · `resident` (tower in VRAM) |
| `ngram` | true (dflash2) / false | n-gram copy drafting (dflash2 only) |
| `context` | *(unset)* | a number **pins** `--max-context` (turns off `--fit-context`: start at exactly this, or refuse). Omit/null to let it **fit** the card (largest that fits, ≤ 262,144). |
| `max_output_tokens` | `32768` | per-request output cap |
| `port` | `8082` | engine port |
| `ui_port` | `8090` | Studio port |
| `host` | *(unset)* | engine bind address → `NINFER_HOST` (default `127.0.0.1`). The Studio always stays loopback-only; this only changes where the **engine** listens. |
| `api_key` | *(absent)* | a value: required as bearer token or `x-api-key` header on every request → `--api-key`; the Studio forwards it to the engine automatically and `/health` stays open. `null`: explicitly **no** key — it clears any `NINFER_API_KEY` the shell inherited, so the profile is unambiguous about its own auth. Absent: inherits the environment. |
| `context_store` | `default` | `default` → `.cache/ninfer-context/<model>` · a path · `""` to disable |
| `webui.enabled` | `true` | start the Studio (web UI) alongside the engine |
| `webui.auto_open` | `true` | open the browser when the Studio is up (`false` → `--no-browser`) |
| `env` | *(none)* | extra environment passed straight through, e.g. `{"NINFER_MIN_P": "0.05"}` |

## The context knob, in one line

Leave `context` out and the engine starts at **up to 262,144**, degrading to whatever the card
can hold that moment (`--fit-context`) — this is why MTP gives you ~262k while DFlash2, whose
draft model is bigger, lands ~130k on the same card. Set `context: 262144` to **demand** the full
size: the engine then either starts at 262k or refuses to start rather than shrink.

## Notes

* **DFlash2 vs MTP context:** on this 24 GB card, MTP fits the full 262,144 beside a desktop;
  DFlash2's extra draft weights cost ~130k of that. Pick per workload (speed vs context).
* **The Studio reuses a running engine** on its port; stop it first to apply a new profile
  (`<profile>.json stop`). A foreign engine (e.g. from `scripts/run.sh`) must be killed by hand
  (`pkill -f ninfer-serve`).
* **`uv`** is required for the Studio path; engine-only profiles (`webui.enabled: false`) don't need it.
* **Publishing on the network:** `host: "0.0.0.0"` exposes the engine to every network the box is
  on. Pair it with `api_key`, otherwise the API is unauthenticated.
