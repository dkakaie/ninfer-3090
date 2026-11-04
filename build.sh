#!/usr/bin/env bash
# NInfer-3090: build on Ubuntu 22.04 / CUDA 12.9 with the pinned vcpkg toolchain.
#   ./build.sh [scripts/build.sh arguments...]
# For example: ./build.sh --test, ./build.sh --target ninfer-serve.

set -euo pipefail

root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
triplet=x64-linux
vcpkg_dir="${NINFER_VCPKG_DIR:-$HOME/.local/share/vcpkg}"
vcpkg_rev=4bca8fd8654e5ba76f92661db7bfe954768ad8ef

ensure_vcpkg() {
  if [[ ! -d "$vcpkg_dir" ]]; then
    printf 'vcpkg not found; cloning into %s\n' "$vcpkg_dir"
    mkdir -p -- "$(dirname -- "$vcpkg_dir")"
    git clone --quiet https://github.com/microsoft/vcpkg.git "$vcpkg_dir"
    git -C "$vcpkg_dir" checkout --quiet "$vcpkg_rev"
  elif [[ -d "$vcpkg_dir/.git" ]]; then
    if [[ "$(git -C "$vcpkg_dir" rev-parse HEAD)" != "$vcpkg_rev" ]]; then
      printf 'vcpkg is at another revision; checking out pinned %s\n' "${vcpkg_rev:0:12}"
      git -C "$vcpkg_dir" checkout --quiet "$vcpkg_rev"
    fi
  else
    printf 'note: %s is not a git checkout; assuming pinned revision\n' "$vcpkg_dir"
  fi
  if [[ ! -x "$vcpkg_dir/vcpkg" ]]; then
    printf 'bootstrapping vcpkg\n'
    "$vcpkg_dir/bootstrap-vcpkg.sh" -disableMetrics
  fi
}

ensure_vcpkg
printf 'vcpkg: %s\n' "$vcpkg_dir"
export CMAKE_TOOLCHAIN_FILE="$vcpkg_dir/scripts/buildsystems/vcpkg.cmake"
export VCPKG_TARGET_TRIPLET="$triplet"
exec "$root/scripts/build.sh" "$@"
