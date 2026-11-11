#!/usr/bin/env bash
#
# Manual smoke for the embedded Studio and the --config flow. Runs against the real engine,
# so it needs the RTX 3090 to have VRAM to spare (the CPU-only build and test suite cover
# everything else; see docs/webui.md).
#
# Scenarios:
#   3a  unkeyed start   --config configs/mtp-long-text.json
#         startup line logs the Studio URL; /webui/ serves the page anonymously; / redirects
#         into it; /health is open; /api/stream streams SSE load frames; one
#         /v1/chat/completions round-trips; SIGINT stops gracefully ("server stopped").
#   3b  keyed start     same profile + --api-key test-key123 (which also proves an explicit
#         CLI flag overrides the profile's api_key: null)
#         the UI stays anonymous; unauthenticated API calls get a 401 in OpenAI shape;
#         Bearer and ?key= are both accepted; the literal key appears nowhere in stderr,
#         request lines print key=****, and the startup record prints <redacted>.
#   3c  webui off       --config configs/mtp-262k-pinned-nowebui.json
#         that profile pins 262k, so the engine needs a near-free card and refuses to start
#         otherwise -- a refusal counts as SKIP, not failure. With webui off there are no
#         /webui routes at all (404), the startup line has no Studio URL, and the API works.
#
# Usage:   bash scripts/smoke-studio.sh [3a 3b 3c]      (default: all three)
# Exit:    0 when every run scenario passed; 1 otherwise.
# Output:  server stderr/chat bodies land in smoke-out/ (local only, git-ignored).

set -u -o pipefail
cd "$(dirname "$0")/.."

SERVE=./build-linux/apps/ninfer-serve
BASE=http://127.0.0.1:8082
OUT=smoke-out
KEY=test-key123
PASS=0 FAIL=0 SKIPS=0 SERVER_PID=

die()  { printf 'FATAL: %s\n' "$*" >&2; exit 1; }
ok()   { printf '  ok   %s\n' "$1"; PASS=$((PASS + 1)); }
bad()  { printf '  FAIL %s\n' "$1"; FAIL=$((FAIL + 1)); }
note() { printf '  note %s\n' "$1"; }

[ -x "$SERVE" ] || die "missing $SERVE (build first: cmake --build build-linux)"
command -v curl >/dev/null || die "curl is required"
command -v python3 >/dev/null || die "python3 is required"
command -v nvidia-smi >/dev/null || die "nvidia-smi is required"
mkdir -p "$OUT"
# Determinism: nothing from the caller's shell may leak a key or a model path into the run.
unset NINFER_API_KEY NINFER_MODEL NINFER_MODEL_DIR 2>/dev/null || true

free_vram() { nvidia-smi --query-gpu=memory.free --format=csv,noheader,nounits 2>/dev/null | head -n1; }

vram_gate() { # $1 min MiB, $2 what it is for; returns 77 (skip) when the card is too busy
  local min=$1 what=$2 free
  free=$(free_vram)
  if [ -z "${free:-}" ]; then
    note "nvidia-smi reported no free-VRAM figure; cannot judge the card -- skipping ($what)"
    return 77
  fi
  if [ "$free" -lt "$min" ]; then
    note "only ${free} MiB free (<${min}): ${what} -- skipping (re-run when the card frees up)"
    return 77
  fi
  return 0
}

port_busy() { curl -s --max-time 1 -o /dev/null "$BASE/health"; }

cleanup() {
  if [ -n "${SERVER_PID:-}" ] && kill -0 "$SERVER_PID" 2>/dev/null; then
    kill -INT "$SERVER_PID" 2>/dev/null
  fi
}
trap cleanup EXIT INT TERM

start_server() { # $1 tag, rest: the engine command; polls /health until the service is published
  local tag=$1; shift
  printf '  start: %s\n' "$*"
  "$@" >"$OUT/$tag.out" 2>"$OUT/$tag.err" &
  SERVER_PID=$!
  local i
  for i in $(seq 1 150); do
    if ! kill -0 "$SERVER_PID" 2>/dev/null; then
      printf '  engine exited during startup; last stderr lines:\n'
      tail -n 25 "$OUT/$tag.err" | sed 's/^/    | /'
      return 1
    fi
    if curl -sf -o /dev/null "$BASE/health"; then return 0; fi
    sleep 2
  done
  note "no /health readiness after 300 s; stopping"
  stop_server "$tag"
  return 1
}

stop_server() { # $1 tag; SIGINT (the Ctrl+C path) and verify the graceful stop
  local tag=$1 rc i
  kill -INT "$SERVER_PID" 2>/dev/null
  for i in $(seq 1 30); do
    kill -0 "$SERVER_PID" 2>/dev/null || break
    sleep 1
  done
  if kill -0 "$SERVER_PID" 2>/dev/null; then
    bad "$tag: did not stop within 30 s of SIGINT; sending SIGKILL"
    kill -9 "$SERVER_PID" 2>/dev/null
    return 1
  fi
  wait "$SERVER_PID"; rc=$?
  if grep -q 'server stopped' "$OUT/$tag.err"; then
    ok "$tag: graceful stop ('server stopped' logged, exit $rc)"
  else
    bad "$tag: no 'server stopped' in the log (exit $rc); see $OUT/$tag.err"
  fi
}

status_of() { curl -s -o /dev/null -w '%{http_code}' --max-time 10 "$@"; }

expect_status() { # $1 what, $2 expected code, rest: curl args
  local what=$1 want=$2 got
  shift 2
  got=$(status_of "$@")
  if [ "$got" = "$want" ]; then ok "$what -> $got"; else bad "$what: expected $want, got $got"; fi
}

expect_sse() { # $1 what, rest: curl args; wants at least one 'data: ' frame within 5 s
  local what=$1 frame
  shift
  frame=$(curl -sN --max-time 5 "$@" || true)
  if printf '%s' "$frame" | grep -q '^data: '; then
    ok "$what emits SSE frames"
    if printf '%s' "$frame" | grep -q 'uptime_seconds'; then ok "$what frames carry load counters"; else bad "$what frame lacks uptime_seconds"; fi
  else
    bad "$what: no SSE frame within 5 s"
  fi
}

expect_chat() { # $1 tag, $2 what, rest: curl args for POST /v1/chat/completions
  local tag=$1 what=$2 code content
  shift 2
  code=$(curl -s -o "$OUT/$tag.chat" -w '%{http_code}' --max-time 120 -X POST "$BASE/v1/chat/completions" \
    -H 'content-type: application/json' "$@")
  if [ "$code" != 200 ]; then
    bad "$what: HTTP $code: $(head -c 300 "$OUT/$tag.chat" 2>/dev/null)"
    return 1
  fi
  content=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["choices"][0]["message"]["content"] or "")' \
    "$OUT/$tag.chat" 2>/dev/null) || { bad "$what: 200 but body is not a chat completion: $(head -c 300 "$OUT/$tag.chat")"; return 1; }
  if [ -n "$content" ]; then ok "$what replied: ${content:0:60}"; else bad "$what: 200 but empty content"; fi
}

model_id() { # $1 tag, rest: extra curl headers; prints the served model id
  local tag=$1
  shift
  curl -s --max-time 10 "$@" "$BASE/v1/models" | python3 -c 'import json,sys; print(json.load(sys.stdin)["data"][0]["id"])' \
    || { bad "could not read the model id from /v1/models"; return 1; }
}

scenario_3a() {
  printf '\n== 3a: unkeyed start, Studio on (configs/mtp-long-text.json) ==\n'
  if port_busy; then die "something already answers on :8082; stop it first"; fi
  vram_gate 20500 "weights + floor context need ~20.5 GiB" || return 77
  start_server 3a "$SERVE" --config configs/mtp-long-text.json || { bad "3a: engine failed to start"; return 1; }
  if grep -q 'Studio http://127.0.0.1:8082/webui/' "$OUT/3a.err"; then
    ok 'startup line logs the Studio URL'
  else
    bad "startup line missing '| Studio http://127.0.0.1:8082/webui/' (see $OUT/3a.err)"
  fi
  expect_status '/webui/ is anonymous' 200 "$BASE/webui/"
  if curl -s --max-time 5 "$BASE/webui/" | grep -qi '<html'; then
    ok '/webui/ serves the Studio page'
  else
    bad '/webui/ body is not the Studio page'
  fi
  local loc
  loc=$(curl -s -o /dev/null -w '%{http_code} %{redirect_url}' --max-time 5 "$BASE/")
  case $loc in
    3*'*/webui/'*) ok "/ redirects into /webui/ ($loc)" ;;
    *) bad "/ did not redirect into /webui/ ($loc)" ;;
  esac
  expect_status '/health is open' 200 "$BASE/health"
  expect_sse '/api/stream' "$BASE/api/stream"
  local mid
  mid=$(model_id 3a) || return 1
  expect_chat 3a "one chat completion (no key)" -d "{\"model\":\"$mid\",\"messages\":[{\"role\":\"user\",\"content\":\"Reply with exactly: OK\"}],\"max_tokens\":16}"
  stop_server 3a
}

scenario_3b() {
  printf '\n== 3b: keyed start, --api-key overrides the profile ==\n'
  if port_busy; then die "something already answers on :8082; stop it first"; fi
  vram_gate 20500 "weights + floor context need ~20.5 GiB" || return 77
  start_server 3b "$SERVE" --config configs/mtp-long-text.json --api-key "$KEY" || { bad "3b: engine failed to start"; return 1; }
  if grep -q 'auth bearer' "$OUT/3b.err"; then
    ok 'startup line reports auth bearer'
  else
    bad "startup line does not report auth bearer (see $OUT/3b.err)"
  fi
  if grep -q '<redacted>' "$OUT/3b.err"; then
    ok 'startup record redacts --api-key'
  else
    bad "startup record lacks '<redacted>' next to --api-key (see $OUT/3b.err)"
  fi
  expect_status '/webui/ stays anonymous' 200 "$BASE/webui/"
  expect_status '/health stays open' 200 "$BASE/health"
  local code
  code=$(curl -s -o "$OUT/3b.unauth" -w '%{http_code}' --max-time 10 -X POST "$BASE/v1/chat/completions" \
    -H 'content-type: application/json' -d '{"model":"x","messages":[{"role":"user","content":"hi"}],"max_tokens":8}')
  if [ "$code" = 401 ]; then
    ok 'unauthenticated chat -> 401'
    if python3 -c 'import json,sys; e=json.load(open(sys.argv[1]))["error"]; assert e.get("message") and e.get("type"), e' "$OUT/3b.unauth" 2>/dev/null; then
      ok '401 body is OpenAI-shaped (error.message/type)'
    else
      bad "401 body is not OpenAI-shaped: $(head -c 300 "$OUT/3b.unauth")"
    fi
  else
    bad "unauthenticated chat: expected 401, got $code"
  fi
  local mid
  mid=$(model_id 3b -H "Authorization: Bearer $KEY") || return 1
  expect_chat 3b "chat with Bearer key" -H "Authorization: Bearer $KEY" \
    -d "{\"model\":\"$mid\",\"messages\":[{\"role\":\"user\",\"content\":\"Reply with exactly: OK\"}],\"max_tokens\":16}"
  expect_status '/api/stream without key' 401 "$BASE/api/stream"
  expect_sse '/api/stream with ?key=' "$BASE/api/stream?key=$KEY"
  if grep -q "$KEY" "$OUT/3b.err"; then
    bad "the literal key appears in the server's stderr log (grep $OUT/3b.err for it)"
  else
    ok 'the literal key appears nowhere in stderr'
  fi
  if grep -q 'key=\*\*\*\*' "$OUT/3b.err"; then
    ok 'request lines print key=**** (redacting logger)'
  else
    bad 'no key=**** request line found in stderr'
  fi
  stop_server 3b
}

scenario_3c() {
  printf '\n== 3c: webui disabled, 262k pinned (configs/mtp-262k-pinned-nowebui.json) ==\n'
  if port_busy; then die "something already answers on :8082; stop it first"; fi
  vram_gate 22000 "the pinned 262k needs a near-free card" || return 77
  if ! start_server 3c "$SERVE" --config configs/mtp-262k-pinned-nowebui.json; then
    if grep -q 'does not fit beside the expert cache' "$OUT/3c.err"; then
      note 'engine refused the pinned 262k (card too busy) -- SKIP 3c, re-run when the card is free'
      return 77
    fi
    bad "3c: engine failed to start (see $OUT/3c.err)"
    return 1
  fi
  if grep -q '| Studio' "$OUT/3c.err"; then
    bad 'startup line still advertises a Studio URL'
  else
    ok 'startup line has no Studio URL'
  fi
  expect_status '/webui/ is absent' 404 "$BASE/webui/"
  expect_status '/ is absent' 404 "$BASE/"
  expect_status '/api/stream is absent' 404 "$BASE/api/stream"
  expect_status 'the API still works (/v1/models)' 200 "$BASE/v1/models"
  stop_server 3c
}

scenarios=()
for s in "$@"; do scenarios+=("$s"); done
[ ${#scenarios[@]} -eq 0 ] && scenarios=(3a 3b 3c)
for s in "${scenarios[@]}"; do
  case $s in
    3a) scenario_3a ;;
    3b) scenario_3b ;;
    3c) scenario_3c ;;
    *) die "unknown scenario '$s' (use 3a 3b 3c)" ;;
  esac
  [ $? -eq 77 ] && SKIPS=$((SKIPS + 1))
done

printf '\n== result: %d ok, %d failed, %d skipped ==\n' "$PASS" "$FAIL" "$SKIPS"
[ "$FAIL" -eq 0 ]
