# Mac port development and verification

The root CMake project is a CPU-only harness. The existing `gpu/` CMake project
and Windows/Linux game build remain separate. No game files, Vulkan SDK, SDL,
FFmpeg, FSR assets, networking libraries or submodules are needed for the harness.

## Build and run the game-free checks

Requirements: GCC/Clang (Apple Clang on macOS), CMake 3.24+, Ninja, Python 3.10+
and host threads. Windows uses MSYS2 CLANG64; run its shell before the commands.

```bash
bash build.sh --cpu-only --test
```

Or select an isolated build tree:

```bash
cmake -S . -B out/cpu -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build out/cpu
ctest --test-dir out/cpu --output-on-failure
```

Configure/build/test are separate operations. CTest fails if a required fixture
binary is missing; it never treats an unbuilt required native test as coverage.
Individual legacy unittest discovery commands retain their optional skips for
developer convenience. CTest supplies `BB_TEST_BIN_DIR` and
`BB_REQUIRED_NATIVE_TESTS` so its results refer to the selected tree, not old
binaries under `out/`.

On arm64 the backend defaults to `unavailable`: the harness compiles native host
contracts for lifecycle, guards, module TLS, memory primitives and AppContent.
Those calls use the host ABI. They do not execute x86 guest instructions, emulate
PS4 threads or verify Darwin VM/fault behavior. Platform-bound providers are
omitted and their imports remain unresolved.

On x86-64 Linux/Windows the default backend is `native-x86` and the root project
also builds a CPU-only loader and existing runtime/semaphore fixtures. It omits
audio, input, ATRAC9, GPU and network registration. Requesting graphics fails with
an explicit diagnostic; synthetic loader fixtures pass `--cpu-only`. The full
game runtime is still built through the existing `build.sh` path. macOS x86-64
can build host contracts, but cannot build the native PS4 loader at this stage.

Explicit configuration examples:

```bash
cmake -S . -B out/host-only -DBB_EXECUTION_BACKEND=unavailable -DBB_BUILD_NATIVE_LOADER=OFF
```

`native-x86` on ARM, unknown backend names and unsupported native-loader hosts
are rejected during configuration. Building an ARM game loader is an M1–M3 task.
The execution API and its current scope are documented in
[execution interface](EXECUTION_INTERFACE.md).

## Native ARM translation experiment (M1)

The independent [M1 experiment](../spikes/cpu/README.md) executes synthetic x86-64
code in a native Apple Silicon process through a pinned Unicorn dependency. It
requires pkg-config and downloads its own source dependency; the root harness
remains dependency-free with respect to Unicorn. It does not enable the ARM
game loader. See the [decision](decisions/0001-cpu-reference-core.md) and
[M1 verification record](M1_VERIFICATION.md) for signing, measurements and limits.

## M2 memory, ABI and fault foundations

The root harness now also tests the portable SysV marshaller and, on macOS,
owned Darwin mappings and protection. The independent CPU experiment includes
compiler-generated x86 ABI callers and an adapter that mirrors owned mappings,
watch changes and fault recovery into translated execution. See
[M2 verification and reproduction](M2_VERIFICATION.md) for the commands and
[ADR 0002](decisions/0002-darwin-memory-and-abi.md) for ownership contracts.

Use `-DBB_PROBE_SANITIZER=undefined` in a separate probe build for fail-fast
project-code UBSan. Vendor-wide UBSan and ASan low-address mappings have explicit
limitations; a successful executable exit code alone does not prove clean
sanitizer output. M2 does not yet enable the ARM game loader or renderer.

## Dependencies for the existing graphics build

```bash
git submodule update --init --recursive
bash scripts/apply_gpu_patches.sh
bash scripts/apply_gpu_patches.sh --check
```

The helper is idempotent. All submodule commit IDs stay pinned. FSR-Vulkan's
working tree is intentionally dirty with the two tracked patch files' changes:
`src/ffx_vk_fsr4_v07.c` and
`upstream/ffx-1.1.4/sdk/src/backends/vk/ffx_vk.cpp`. Do not commit that working
tree as a new submodule revision or reset it to make the parent status look clean.

## Validation status

See [M0 verification record](M0_VERIFICATION.md) for executed commands, results
and pending coverage. The CPU workflow has passed on Linux, Windows CLANG64 and a
macOS arm64 runner; the verification record links the successful code revision.
Runner labels follow [GitHub's runner reference](https://docs.github.com/en/actions/reference/runners/github-hosted-runners),
and the Mac job checks `uname -m` explicitly. CI does not build or run the full
renderer or use game assets.
