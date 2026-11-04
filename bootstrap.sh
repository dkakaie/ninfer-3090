#!/usr/bin/env bash
# bootstrap.sh — start a Qwen3.8-27B serving profile defined in a JSON file.
#
#   ./bootstrap.sh configs/<profile>.json            start (engine, + Studio if the profile enables it)
#   ./bootstrap.sh configs/<profile>.json stop       stop what the profile started
#   ./bootstrap.sh configs/<profile>.json status     engine / Studio state
#
# The JSON is the single source of truth for the serving shape. bootstrap translates it into
# NINFER_* environment variables and hands off to the repo's measured launcher
# (scripts/run.sh `tuned`), so every flag stays exactly what was measured in docs/performance.md.
# When the profile enables the Studio it goes through scripts/start-ui.sh (engine + web UI
# together); when it does not, it runs the engine in the foreground (Ctrl+C stops it).
# See configs/README.md for the full key table.
set -euo pipefail

root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

[[ $# -ge 1 ]] || { printf 'usage: %s <profile.json> [start|stop|status]\n' "${0##*/}" >&2; exit 2; }
profile="$1"
cmd="${2:-start}"
[[ -f "$profile" ]] || { printf 'bootstrap: no such profile: %s\n' "$profile" >&2; exit 1; }

# ---- translate the JSON into NINFER_* env (+ a few bootstrap locals) ------------------
envscript="$(python3 - "$profile" <<'PY'
import json, shlex, sys

cfg = json.load(open(sys.argv[1]))

def export(k, v):
    print("export %s=%s" % (k, shlex.quote(str(v))))

def set(k, v):
    print("%s=%s" % (k, shlex.quote(str(v))))

spec = cfg.get("spec", "mtp")
export("NINFER_SPEC", spec)
export("NINFER_DRAFT_TOKENS", cfg.get("draft_tokens", 7 if spec == "dflash2" else 3))
export("NINFER_CONCURRENCY", cfg.get("lanes", 1 if spec == "dflash2" else 2))
export("NINFER_KV_DTYPE", cfg.get("kv_dtype", "rk4v4"))

vision = bool(cfg.get("vision", False))
export("NINFER_VISION", "on" if vision else "off")
if vision:
    export("NINFER_VISION_RESIDENCY", cfg.get("vision_residency", "overlay"))

export("NINFER_NGRAM", "on" if cfg.get("ngram", spec == "dflash2") else "off")
export("NINFER_MAX_OUTPUT_TOKENS", cfg.get("max_output_tokens", 32768))
export("NINFER_PORT", cfg.get("port", 8082))
export("NINFER_UI_PORT", cfg.get("ui_port", 8090))

# The engine's bind address (default 127.0.0.1) and the key every request must present
# (--api-key). The Studio stays loopback-only; it forwards the key to the engine itself.
if cfg.get("host"):
    export("NINFER_HOST", cfg["host"])
# A value sets the key; null explicitly turns it OFF, clearing any NINFER_API_KEY the
# surrounding shell inherited, so a profile is unambiguous about its own auth.
if cfg.get("api_key"):
    export("NINFER_API_KEY", cfg["api_key"])
elif "api_key" in cfg:
    print("unset NINFER_API_KEY")

# A number pins the context (turns off --fit-context: start at exactly this, or refuse).
# Absent/null leaves it to --fit-context: start at the largest that fits (<= 262,144).
ctx = cfg.get("context")
if ctx:
    export("NINFER_CONTEXT", ctx)
    export("NINFER_KV_CAPACITY", ctx)

# Arbitrary extra environment, applied last (escape hatch for any other NINFER_* knob).
for k, v in (cfg.get("env") or {}).items():
    export(k, v)

# Bootstrap-level values (not passed to the engine).
set("BS_MODEL", cfg.get("model", "qwen38-27b"))
webui = cfg.get("webui") or {}
set("BS_WEBUI", "on" if webui.get("enabled", True) else "off")
set("BS_AUTO_OPEN", "on" if webui.get("auto_open", True) else "off")
cs = cfg.get("context_store", "default")
set("BS_CONTEXT_STORE", "off" if cs == "" else cs)
set("BS_NOTES", cfg.get("notes", ""))
PY
)" || { printf 'bootstrap: failed to parse %s (bad JSON?)\n' "$profile" >&2; exit 1; }
eval "$envscript"

# ---- context store ---------------------------------------------------------------------
case "$BS_CONTEXT_STORE" in
  off)     NINFER_CONTEXT_STORE="" ;;
  default) NINFER_CONTEXT_STORE="$root/.cache/ninfer-context/$BS_MODEL" ;;
  *)       NINFER_CONTEXT_STORE="$BS_CONTEXT_STORE" ;;
esac
[[ -z "$NINFER_CONTEXT_STORE" ]] || mkdir -p -- "$NINFER_CONTEXT_STORE"

# ---- banner ------------------------------------------------------------------------------
HOST="${NINFER_HOST:-127.0.0.1}"
printf '\n%s\n' "$(basename -- "$profile")"
[[ -n "$BS_NOTES" ]] && printf -- '-- %s\n' "$BS_NOTES"
if [[ "$BS_WEBUI" == on ]]; then
  printf 'engine  http://%s:%s/v1     studio  http://127.0.0.1:%s/\n' \
    "$HOST" "$NINFER_PORT" "$NINFER_UI_PORT"
else
  printf 'engine  http://%s:%s/v1     (no Studio — foreground, Ctrl+C stops it)\n' "$HOST" "$NINFER_PORT"
fi
[[ -z "${NINFER_API_KEY:-}" ]] || printf 'auth    API key required on every request (NINFER_API_KEY)\n'
printf '\n'

case "$cmd" in
  start)
    if [[ "$BS_WEBUI" == on ]]; then
      command -v uv >/dev/null 2>&1 || { printf 'bootstrap: uv is needed for the Studio (https://docs.astral.sh/uv/)\n' >&2; exit 1; }
      uiargs=()
      [[ "$BS_AUTO_OPEN" == on ]] || uiargs+=(--no-browser)
      exec "$root/scripts/start-ui.sh" "${uiargs[@]}"
    else
      exec "$root/scripts/run.sh" "$BS_MODEL" tuned
    fi
    ;;
  stop)
    if [[ "$BS_WEBUI" == on ]]; then
      exec "$root/scripts/stop-ui.sh"
    else
      printf 'engine-only profile: it runs in the foreground, so stop it with Ctrl+C (or pkill -f ninfer-serve)\n'
    fi
    ;;
  status)
    if [[ "$BS_WEBUI" == on ]]; then
      command -v uv >/dev/null 2>&1 || { printf 'bootstrap: uv is needed for the Studio\n' >&2; exit 1; }
      exec uv run --no-project --python 3.11 "$root/webui/launcher.py" status
    else
      if curl -s --max-time 2 "http://127.0.0.1:$NINFER_PORT/health" | grep -q '"ok"'; then
        printf 'engine on :%s is up\n' "$NINFER_PORT"
      else
        printf 'engine on :%s is not answering\n' "$NINFER_PORT"
      fi
    fi
    ;;
  *)
    printf 'bootstrap: unknown command %q (expected start, stop or status)\n' "$cmd" >&2; exit 2
    ;;
esac
