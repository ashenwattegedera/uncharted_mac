# Local changes to gpu/third_party/fsr-vulkan

The submodule points at upstream FireBurn/FSR-Vulkan (`c64f093`). `build.sh` applies the
patches here to its working tree when they are not applied yet:

- `0001-...`: `BB_FSR4_PROFILE` (GPU time per FSR 4 pass) and `BB_FSR4_STATS` (driver
  statistics of each pass) in the FSR 4 v07 provider.
- `0002-...`: `#include <mutex>` in the FFX Vulkan backend; libc++ (the Windows MSYS2 CLANG64
  build) does not include it transitively.

The shared preparation entry point is `bash scripts/apply_gpu_patches.sh`.
It is safe to repeat. `bash scripts/apply_gpu_patches.sh --check` verifies the
patched state without changing files and fails if a patch is missing or incompatible.
The submodule commit stays pinned; its working tree is intentionally dirty only
with these changes. CPU-only CMake targets do not use or require this submodule.
