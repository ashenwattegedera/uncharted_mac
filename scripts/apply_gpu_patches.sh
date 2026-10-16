#!/usr/bin/env bash
# Idempotent patch preparation; --check verifies the expected patched state without writes.
set -euo pipefail
cd -- "$(dirname -- "$0")/.."
mode=${1:-apply}
if [[ $mode != apply && $mode != --check ]]; then
    echo 'Usage: bash scripts/apply_gpu_patches.sh [--check]' >&2
    exit 2
fi
submodule=gpu/third_party/fsr-vulkan
if [[ ! -f $submodule/CMakeLists.txt ]]; then
    echo 'Initialize pinned submodules first: git submodule update --init --recursive' >&2
    exit 1
fi
for patch in gpu/patches/fsr-vulkan/*.patch; do
    if git -C "$submodule" apply --reverse --check "$PWD/$patch" >/dev/null 2>&1; then
        continue
    fi
    if [[ $mode == --check ]]; then
        echo "Missing or incompatible GPU patch: $patch" >&2
        exit 1
    fi
    git -C "$submodule" apply --check "$PWD/$patch"
    git -C "$submodule" apply "$PWD/$patch"
done
