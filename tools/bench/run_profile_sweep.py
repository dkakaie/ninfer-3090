#!/usr/bin/env python3
"""Profile sweep benchmark for the RTX 3090 serving profiles in configs/.

Single command (run after freeing the GPU -- `nvidia-smi` shows who holds it):

    bash tools/bench/run-profile-sweep.sh            # every configs/*.json
    bash tools/bench/run-profile-sweep.sh --profiles configs/dflash2-fast.json
    bash tools/bench/run-profile-sweep.sh --dry-run  # print the commands only

For each profile JSON the sweep:
  1. starts `build-linux/apps/ninfer-serve` with exactly the flags the engine's
     `--config` mapper builds for the profile (the derivation below mirrors
     scripts/run.sh `tuned`, and was verified byte-for-byte against
     `ninfer-serve --config <profile> --print-args`), plus three deterministic
     additions: --greedy (temperature 0), --no-prefix-reuse (no cross-request
     caching, so every measured run prefill is fully cold) and
     --request-log-jsonl (server-measured timings);
  2. waits for /health, does one short warm-up request (CUDA/cuBLAS warm), then
     runs a fixed workload set -- prose, code, a math problem and a long prefill,
     identical for every profile -- one cold run followed by N measured runs;
  3. stops the server (SIGTERM, then SIGKILL) and moves to the next profile.

Results go to benchmark_results/profile_sweep_<timestamp>/ (one directory per
profile with command.json, server.log, request-log.jsonl, responses.jsonl and
summary.json, plus sweep.csv / sweep.json / a printed table at the root). The
table is the artifact to send back for comparison; it reports, per profile and
workload, server-measured prefill tok/s, decode tok/s, TTFT and speculative
acceptance, for the cold run and the warm mean.

Stdlib only (Python >= 3.10). No streaming, no concurrency: one request at a
time, so every number is a single-stream measurement.
"""

from __future__ import annotations

import argparse
import json
import os
import socket
import subprocess
import sys
import threading
import time
import urllib.request
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

# ---------------------------------------------------------------------------
# Workloads. Identical for every profile, greedy, fixed seed. The prose and
# prefill prompts reuse the text of tools/bench/run_qwen38_windows_3090_benchmarks.py
# so numbers stay comparable with the published cohort sweep in docs/performance.md.
# ---------------------------------------------------------------------------
PARAGRAPH = (
    "A reliable GPU inference service separates admission control, prompt ingestion, decode "
    "scheduling, memory accounting, observability, and failure recovery. It measures latency "
    "and throughput independently, bounds concurrency, avoids hidden cache reuse, and records "
    "enough metadata to reproduce every result. Engineers validate correctness before tuning "
    "kernels and preserve memory headroom for transient workspaces. "
)
PREFILL_CHARS = 28000  # ~7k tokens, same as the Windows cohort sweep default


def build_workloads(max_tokens: int) -> list[tuple[str, str, int]]:
    prefill = (PARAGRAPH * (PREFILL_CHARS // len(PARAGRAPH) + 2))[:PREFILL_CHARS]
    return [
        ("prose", "Write a detailed technical guide to reliable local GPU inference. Continue "
                  "until the requested output limit.", max_tokens),
        ("code", "Implement a thread-safe LRU cache in modern C++: a template class "
                 "lru_cache<Key, Value> with get, put, resize and clear, an internal "
                 "doubly-linked list plus unordered map, a single recursive_mutex, and a "
                 "capacity given at construction. Include a small main that exercises it. "
                 "Keep the code compact and idiomatic.", max_tokens),
        ("math", "Compute, showing each step: a server processes requests in batches of 16. "
                 "In one minute it completes 240 batches, and 1/12 of the batches are retried "
                 "once. How many individual requests were accepted in that minute? Then: a "
                 "GPU has 24576 MiB of memory; weights take 18300 MiB, the KV pool takes "
                 "5600 MiB, and the rest is workspace. What fraction of total memory does the "
                 "workspace occupy, as a percentage to one decimal place?", max_tokens),
        ("prefill", prefill, 16),
    ]


SEED = 20261111
THINKING = "none"  # greedy speed sweep; the thinking budget would add nondeterminism and time

# ---------------------------------------------------------------------------
# Flag derivation. Mirrors scripts/run.sh `tuned` (the qwen38-27b arm, 2026-11-11
# state): spec args, KV, prefill chunk, ngram, vision, pending limits, host
# context budget, output cap, sampling guard and the fit-context floor. The
# live engine started by `ninfer-serve --config configs/mtp-long-vision.json`
# carries exactly this flag set (verified against --print-args), so the sweep
# measures the deployment shape, not a re-invention of it. Diverge deliberately
# only through the three deterministic additions in derive_command().
# ---------------------------------------------------------------------------

def _env_int(env: dict, key: str, default: str) -> str:
    return str(env.get(key, default))


def derive_command(cfg: dict, server: Path, model: Path, port: int, host: str,
                   request_log: Path) -> list[str]:
    spec = cfg.get("spec", "mtp")
    draft = cfg.get("draft_tokens", 7 if spec == "dflash2" else 3)
    lanes = cfg.get("lanes", 1 if spec == "dflash2" else 2)
    kv = cfg.get("kv_dtype", "rk4v4")
    env = cfg.get("env") or {}
    chunk = env.get("NINFER_PREFILL_CHUNK", "4096" if spec == "dflash2" else "2048")
    context = cfg.get("context")  # a number pins it; absent lets --fit-context decide

    flags = [
        "--max-concurrency", str(lanes),
        "--max-context", str(context if context else 262144),
        "--kv-capacity", str(context if context else 262144),
        "--kv-dtype", kv,
    ]
    if spec != "none":
        flags += ["--spec", spec, "--draft-tokens", str(draft), "--lm-head-draft"]
    # NINFER_NGRAM defaults to on for dflash2 (the engine's --config mapper does the
    # same), and run.sh honours an explicit on for every lane count.
    if cfg.get("ngram", spec == "dflash2") and spec == "dflash2":
        flags += ["--ngram-draft-tokens", "15"]
    flags += [
        "--gdn-state-fp16",
        "--prefill-cublas", "--prefill-chunk", str(chunk),
        "--max-pending-requests", "16", "--pending-timeout-ms", "600000",
    ]
    if cfg.get("vision", False):
        # run.sh injects the vision pair right after the pending limits (and the engine's --config
        # mapper emits it in the same position); keep all three argv shapes identical.
        flags += ["--vision", "--vision-residency", cfg.get("vision_residency", "overlay")]
    flags += [
        "--host-context-mib", _env_int(env, "NINFER_HOST_CONTEXT_MIB", "8192"),
        "--max-output-tokens", _env_int(env, "NINFER_MAX_OUTPUT_TOKENS",
                                        str(cfg.get("max_output_tokens", 32768))),
        "--min-p", _env_int(env, "NINFER_MIN_P", "0.03"),
        "--presence-penalty", _env_int(env, "NINFER_PRESENCE_PENALTY", "0.5"),
    ]
    if not context:  # run.sh adds --fit-context only when context is not pinned
        flags += ["--fit-context", "32768"]
        # NINFER_FIT_HEADROOM_MIB is exported by some profiles but has no engine
        # consumer and no CLI flag; it stays an inert passthrough, so nothing is
        # emitted for it here.
    # The deliberate divergences: determinism and server-side timing.
    flags += ["--greedy", "--no-prefix-reuse", "--request-log-jsonl", str(request_log)]
    api_key = cfg.get("api_key")
    if api_key:
        flags += ["--api-key", str(api_key)]
    return [str(server), str(model), "--host", str(host), "--port", str(port)] + flags


# ---------------------------------------------------------------------------
# Engine lifecycle
# ---------------------------------------------------------------------------

def gpu_memory_mib() -> tuple[int, int]:
    line = subprocess.run(
        ["nvidia-smi", "--query-gpu=memory.used,memory.total", "--format=csv,noheader,nounits"],
        check=True, capture_output=True, text=True).stdout.strip().splitlines()[0]
    used, total = line.split(",")
    return int(used.strip()), int(total.strip())


def wait_ready(process: subprocess.Popen, port: int, timeout: float) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"server exited during startup with status {process.returncode}")
        try:
            with urllib.request.urlopen(f"http://127.0.0.1:{port}/health", timeout=2):
                return
        except Exception:
            time.sleep(0.25)
    raise TimeoutError(f"server health timeout after {timeout:.0f}s")


def port_open(port: int) -> bool:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.settimeout(0.5)
        return sock.connect_ex(("127.0.0.1", port)) == 0


def wait_port_closed(port: int, timeout: float) -> None:
    deadline = time.monotonic() + timeout
    while port_open(port) and time.monotonic() < deadline:
        time.sleep(0.25)


def stop_server(process: subprocess.Popen) -> None:
    if process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=20)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=10)


class VramPoller:
    """Peak VRAM while a profile runs; the card belongs to the sweep alone here."""

    def __init__(self) -> None:
        self.peak_mib = 0
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._loop, daemon=True)

    def _loop(self) -> None:
        while not self._stop.wait(0.2):
            try:
                used, _ = gpu_memory_mib()
                self.peak_mib = max(self.peak_mib, used)
            except Exception:
                pass

    def start(self) -> None:
        self.peak_mib = gpu_memory_mib()[0]
        self._thread.start()

    def stop(self) -> int:
        self._stop.set()
        self._thread.join()
        return self.peak_mib


def chat(port: int, prompt: str, max_tokens: int, api_key: str | None,
         timeout: float = 900.0) -> dict:
    body = json.dumps({
        "model": "qwen3.8-27b",
        "messages": [{"role": "user", "content": prompt}],
        "max_tokens": max_tokens,
        "temperature": 0,
        "seed": SEED,
        "reasoning_effort": THINKING,
    }).encode("utf-8")
    headers = {"Content-Type": "application/json"}
    if api_key:
        headers["Authorization"] = f"Bearer {api_key}"
    req = urllib.request.Request(f"http://127.0.0.1:{port}/v1/chat/completions",
                                 data=body, headers=headers)
    with urllib.request.urlopen(req, timeout=timeout) as response:
        return json.load(response)


def read_done_records(path: Path) -> list[dict]:
    if not path.exists():
        return []
    records = [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines() if line]
    return [r for r in records if r.get("event") == "request_done"]


def read_rejections(path: Path) -> list[str]:
    if not path.exists():
        return []
    records = [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines() if line]
    out = []
    for r in records:
        if r.get("event") == "request_rejected":
            out.append(json.dumps(r.get("error", {})))
    return out


def run_metrics(record: dict) -> dict:
    t = record["timings_seconds"]
    res = record["result"]
    sp = record.get("speculative") or {}
    prefill_s = t.get("prefill") or 0.0
    decode_s = t.get("decode") or 0.0
    computed = res.get("computed_prefill_tokens", 0)
    finished = res.get("completion_tokens", 0)
    drafted = sp.get("drafted_tokens", 0)
    accepted = sp.get("accepted_tokens", 0)
    ngram_drafted = sp.get("ngram_drafted_tokens", 0)
    ngram_accepted = sp.get("ngram_accepted_tokens", 0)
    return {
        "prompt_tokens": res.get("prompt_tokens", 0),
        "completion_tokens": finished,
        "computed_prefill_tokens": computed,
        "prefix_cache_hit_tokens": res.get("prefix_cache_hit_tokens", 0),
        "finish_reason": res.get("finish_reason"),
        "ttft_ms": 1000.0 * (t.get("ttft") or 0.0),
        "prefill_seconds": prefill_s,
        "decode_seconds": decode_s,
        "total_seconds": t.get("total") or 0.0,
        "prefill_tok_s": computed / prefill_s if prefill_s > 0 else None,
        "decode_tok_s": finished / decode_s if decode_s > 0 else None,
        "drafted_tokens": drafted,
        "accepted_tokens": accepted,
        "ngram_drafted_tokens": ngram_drafted,
        "ngram_accepted_tokens": ngram_accepted,
    }


def summarize(measured: list[dict]) -> dict:
    def stat(key: str) -> dict:
        values = [m[key] for m in measured if m.get(key) is not None]
        if not values:
            return {"mean": None, "min": None, "max": None}
        return {"mean": sum(values) / len(values), "min": min(values), "max": max(values)}

    drafted = sum(m["drafted_tokens"] for m in measured)
    accepted = sum(m["accepted_tokens"] for m in measured)
    ngram_drafted = sum(m["ngram_drafted_tokens"] for m in measured)
    ngram_accepted = sum(m["ngram_accepted_tokens"] for m in measured)
    return {
        "runs": len(measured),
        "prefill_tok_s": stat("prefill_tok_s"),
        "decode_tok_s": stat("decode_tok_s"),
        "ttft_ms": stat("ttft_ms"),
        "total_seconds": stat("total_seconds"),
        "acceptance_percent": 100.0 * accepted / drafted if drafted else None,
        "ngram_acceptance_percent": 100.0 * ngram_accepted / ngram_drafted if ngram_drafted else None,
    }


# ---------------------------------------------------------------------------
# One profile
# ---------------------------------------------------------------------------

def run_profile(cfg: dict, name: str, out_dir: Path, args: argparse.Namespace,
                server: Path, model: Path) -> dict:
    port = int(cfg.get("port", 8082))
    host = str(cfg.get("host") or "127.0.0.1")
    profile_dir = out_dir / name
    profile_dir.mkdir(parents=True)
    request_log = profile_dir / "request-log.jsonl"
    request_log.write_text("", encoding="utf-8")
    api_key = cfg.get("api_key")

    command = derive_command(cfg, server, model, port, host, request_log)
    (profile_dir / "command.json").write_text(json.dumps(command, indent=2), encoding="utf-8")
    print(f"\n=== {name} ===")
    print("  " + " ".join(command))

    started = time.monotonic()
    if port_open(port):
        raise RuntimeError(f"port {port} is busy before launch; refusing to start beside it")
    server_log = (profile_dir / "server.log").open("w", encoding="utf-8")
    process = subprocess.Popen(command, stdout=server_log, stderr=subprocess.STDOUT)
    poller = VramPoller()
    summary: dict = {
        "profile": name,
        "config": {k: v for k, v in cfg.items() if k != "notes"},
        "notes": cfg.get("notes"),
        "launch_seconds": None,
        "peak_gpu_memory_mib": None,
        "workloads": {},
        "server_tail": None,
        "rejections": [],
    }
    responses: list[dict] = []
    try:
        wait_ready(process, port, args.startup_timeout)
        summary["launch_seconds"] = round(time.monotonic() - started, 1)
        print(f"  up in {summary['launch_seconds']} s (pid {process.pid})")
        poller.start()

        chat(port, "Warm up the inference path with a short response.", 32, api_key)
        base_done = len(read_done_records(request_log))
        if base_done < 1:
            raise RuntimeError(
                "warm-up request never completed; rejections="
                f"{read_rejections(request_log)[-3:]}")

        for workload, prompt, max_tokens in build_workloads(args.max_tokens):
            runs: list[dict] = []
            for attempt in range(args.cold + args.reps):
                t0 = time.monotonic()
                response = chat(port, prompt, max_tokens, api_key)
                wall = time.monotonic() - t0
                done = read_done_records(request_log)
                if len(done) <= base_done:
                    rejections = read_rejections(request_log)
                    raise RuntimeError(
                        f"{workload}: no request_done record; rejections={rejections[-3:]}")
                record = done[-1]
                base_done = len(done)
                metrics = run_metrics(record)
                metrics["wall_seconds"] = round(wall, 3)
                metrics["cold"] = attempt < args.cold
                runs.append(metrics)
                responses.append({"profile": name, "workload": workload, "attempt": attempt,
                                  "response": response, "metrics": metrics})
                shown = metrics["decode_tok_s"]
                print(f"  {workload:<8} {'cold ' if metrics['cold'] else 'warm '}"
                      f"pre={metrics['prefill_tok_s'] and round(metrics['prefill_tok_s'], 1)} "
                      f"dec={shown and round(shown, 1)} tok/s  ttft={metrics['ttft_ms']:.0f} ms  "
                      f"({wall:.1f} s wall)")
            measured = [m for m in runs if not m["cold"]]
            summary["workloads"][workload] = {
                "cold": runs[0] if args.cold else None,
                "mean": summarize(measured),
                "runs": runs,
            }
    finally:
        stop_server(process)
        wait_port_closed(port, 15)
        peak = poller.stop()
        summary["peak_gpu_memory_mib"] = peak
        server_log.close()
        tail = (profile_dir / "server.log").read_text(encoding="utf-8", errors="replace")
        summary["server_tail"] = tail[-4000:]
        (profile_dir / "responses.jsonl").write_text(
            "\n".join(json.dumps(r) for r in responses) + "\n", encoding="utf-8")
        (profile_dir / "summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
    print(f"  stopped; peak VRAM {peak // 1024} GiB")
    return summary


# ---------------------------------------------------------------------------
# Sweep
# ---------------------------------------------------------------------------

def print_table(results: list[dict]) -> None:
    header = (f"{'profile':<28} {'workload':<9} {'kind':<5} "
              f"{'prefill tok/s':>14} {'decode tok/s':>13} {'TTFT ms':>9} {'accept %':>9}")
    print("\n" + header)
    print("-" * len(header))
    for result in results:
        name = result["profile"]
        if result.get("error"):
            print(f"{name:<28} {'(failed)':<9}  {result['error']}")
            continue
        for workload in ("prose", "code", "math", "prefill"):
            block = result["workloads"].get(workload)
            if not block:
                continue
            for kind, block_key, src in (("cold", "cold", lambda b: b["cold"]),
                                         ("mean", "mean", lambda b: b["mean"])):
                row = src(block)
                if row is None:
                    continue
                if kind == "cold":
                    pre = row["prefill_tok_s"]
                    dec = row["decode_tok_s"]
                    ttft = row["ttft_ms"]
                    acc = None
                else:
                    pre = row["prefill_tok_s"]["mean"]
                    dec = row["decode_tok_s"]["mean"]
                    ttft = row["ttft_ms"]["mean"]
                    acc = row["acceptance_percent"]
                fmt = lambda v: "—" if v is None else f"{v:,.0f}"
                acc_txt = "—" if acc is None else f"{acc:.0f}"
                print(f"{name:<28} {workload:<9} {kind:<5} "
                      f"{fmt(pre):>14} {fmt(dec):>13} {fmt(ttft):>9} {acc_txt:>9}")
    print()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--profiles", nargs="*", default=None,
                        help="profile JSONs (default: every configs/*.json, sorted)")
    parser.add_argument("--reps", type=int, default=3,
                        help="measured (warm) runs per workload (default 3)")
    parser.add_argument("--cold", type=int, default=1,
                        help="discarded cold runs per workload after the warm-up (default 1)")
    parser.add_argument("--max-tokens", type=int, default=512,
                        help="generation length for the prose/code/math workloads (default 512)")
    parser.add_argument("--startup-timeout", type=float, default=300.0,
                        help="seconds to wait for /health (default 300)")
    parser.add_argument("--min-free-gib", type=float, default=20.0,
                        help="abort unless this much VRAM is free before the first profile "
                             "(default 20; the 27B needs ~17-19 GiB for weights)")
    parser.add_argument("--force", action="store_true", help="skip the free-VRAM pre-check")
    parser.add_argument("--out", default=None, help="results directory (default under benchmark_results/)")
    parser.add_argument("--dry-run", action="store_true", help="print the derived commands, start nothing")
    args = parser.parse_args()

    if args.profiles:
        profiles = [Path(p) for p in args.profiles]
    else:
        profiles = sorted((ROOT / "configs").glob("*.json"))
    if not profiles:
        parser.error("no profiles found")

    server = Path(os.environ.get("NINFER_SERVER", ROOT / "build-linux" / "apps" / "ninfer-serve"))
    model = Path(os.environ.get("NINFER_MODEL", ROOT / "scripts" / "models" / "qwen3_8_27b.ninfer"))
    if not model.exists():
        alt = ROOT / "models" / "qwen3_8_27b.ninfer"
        if alt.exists():
            model = alt
        else:
            raise SystemExit(f"model not found: {model} (set NINFER_MODEL)")
    if not server.exists():
        raise SystemExit(f"server not found: {server} (build first: cmake --build build-linux -j6)")

    run_id = datetime.now().strftime("%Y%m%d_%H%M%S")
    out_dir = Path(args.out) if args.out else ROOT / "benchmark_results" / f"profile_sweep_{run_id}"

    used, total = gpu_memory_mib()
    free_gib = (total - used) / 1024
    print(f"RTX 3090 profile sweep: {len(profiles)} profile(s), {args.cold} cold + "
          f"{args.reps} measured run(s) per workload")
    print(f"  VRAM: {used / 1024:.1f} / {total / 1024:.1f} GiB used ({free_gib:.1f} free)")
    print(f"  model: {model}")
    print(f"  results: {out_dir}")

    if args.dry_run:
        for profile in profiles:
            cfg = json.loads(profile.read_text(encoding="utf-8"))
            command = derive_command(cfg, server, model, int(cfg.get("port", 8082)),
                                     str(cfg.get("host") or "127.0.0.1"),
                                     out_dir / profile.stem / "request-log.jsonl")
            print(f"\n{profile.name}:")
            print("  " + " ".join(command))
        return

    out_dir.mkdir(parents=True, exist_ok=False)

    if not args.force and free_gib < args.min_free_gib:
        raise SystemExit(
            f"only {free_gib:.1f} GiB of VRAM is free (< {args.min_free_gib} GiB). "
            "Stop whatever holds the card (nvidia-smi lists it) and re-run, "
            f"or pass --force / --min-free-gib {free_gib:.1f}.")

    # The engine port must be free before the first launch: if something else answers on it,
    # the health poll would succeed against a foreign engine and the request log would never
    # fill, which reads as a workload failure instead of "you did not stop your server".
    for port in sorted({int(cfg.get("port", 8082)) for cfg in
                        (json.loads(p.read_text(encoding="utf-8")) for p in profiles)}):
        if port_open(port):
            raise SystemExit(
                f"port {port} already serves something (an engine left from before?). "
                "Stop it first, then re-run.")

    git_rev = subprocess.run(["git", "-C", str(ROOT), "rev-parse", "--short", "HEAD"],
                             check=False, capture_output=True, text=True).stdout.strip() or "?"
    (out_dir / "configuration.json").write_text(json.dumps({
        "argv": sys.argv, "git": git_rev, "server": str(server), "model": str(model),
        "free_vram_gib_at_start": round(free_gib, 2),
        "seed": SEED, "thinking": THINKING, "prefill_chars": PREFILL_CHARS,
        "workload_max_tokens": args.max_tokens,
        "workloads": [{ "name": w, "prompt_chars": len(p), "max_tokens": m }
                       for w, p, m in build_workloads(args.max_tokens)],
    }, indent=2), encoding="utf-8")

    results = []
    try:
        for profile in profiles:
            cfg = json.loads(profile.read_text(encoding="utf-8"))
            print(f"\n# {profile.name} — {cfg.get('notes', '')[:110]}")
            try:
                results.append(run_profile(cfg, profile.stem, out_dir, args, server, model))
            except Exception as exc:  # a profile that fails to start must not kill the sweep
                results.append({"profile": profile.stem, "error": f"{type(exc).__name__}: {exc}",
                                "workloads": {}})
                print(f"  FAILED: {exc}")
                tail = (out_dir / profile.stem / "server.log")
                if tail.exists():
                    print("  --- server.log tail ---")
                    print(tail.read_text(encoding="utf-8", errors="replace")[-2000:])
    except KeyboardInterrupt:
        print("\ninterrupted — stopping any running server")
    finally:
        (out_dir / "sweep.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
        rows = []
        for result in results:
            for workload, block in (result.get("workloads") or {}).items():
                for kind in ("cold", "mean"):
                    row = block.get(kind)
                    if row is None:
                        continue
                    if kind == "cold":
                        rows.append([result["profile"], workload, "cold",
                                     row.get("prefill_tok_s"), row.get("decode_tok_s"),
                                     row.get("ttft_ms"), "", row.get("total_seconds"),
                                     result.get("peak_gpu_memory_mib")])
                    else:
                        rows.append([result["profile"], workload, "mean",
                                     row["prefill_tok_s"]["mean"], row["decode_tok_s"]["mean"],
                                     row["ttft_ms"]["mean"], row.get("acceptance_percent"),
                                     row["total_seconds"]["mean"], result.get("peak_gpu_memory_mib")])
        columns = ["profile", "workload", "kind", "prefill_tok_s", "decode_tok_s",
                   "ttft_ms", "acceptance_percent", "total_seconds", "peak_gpu_memory_mib"]
        csv = [",".join(columns)]
        for row in rows:
            csv.append(",".join("" if v is None else (f"{v:.3f}" if isinstance(v, float) else str(v))
                                for v in row))
        (out_dir / "sweep.csv").write_text("\n".join(csv) + "\n", encoding="utf-8")
        print_table(results)
        print(f"Done. Send {out_dir}/sweep.csv (and the per-profile server.log tails if anything failed).")
        for result in results:
            if result.get("error"):
                print(f"  ! {result['profile']}: {result['error']}")


if __name__ == "__main__":
    main()
