#!/usr/bin/env bash
# One-command profile sweep: benchmark every serving profile in configs/ on this card.
#
#   bash tools/bench/run-profile-sweep.sh                    # all profiles in configs/
#   bash tools/bench/run-profile-sweep.sh --dry-run          # print the commands only
#   bash tools/bench/run-profile-sweep.sh --reps 5           # 1 cold + 5 measured runs
#   bash tools/bench/run-profile-sweep.sh --profiles configs/dflash2-free.json
#
# Free the GPU first -- the script aborts if less than 20 GiB is free (nvidia-smi
# shows who holds it). Results land in benchmark_results/profile_sweep_<timestamp>/;
# send back sweep.csv from there.
set -euo pipefail
repo="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
exec python3 "$repo/tools/bench/run_profile_sweep.py" "$@"
