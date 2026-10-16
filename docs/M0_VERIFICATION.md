# M0 verification record

Date: 2026-10-16. Starting game-runtime revision: `68c8cf9`.

Status: **M0 local implementation complete; cross-platform acceptance pending.**
The build-harness and execution-interface increments are integrated separately
through feature branches. This status does not mark the entire acceptance matrix
as passed.

## Environment and dependency baseline

- Host: Apple Silicon arm64, macOS 26.7.1 (reported during source assessment).
- Compiler: Apple Clang 21.0.0 (`clang-2100.3.34.2`).
- CMake: 4.4.4; Ninja: 1.13.2; Python: 3.15.0.
- No Linux/Windows runtime, VM or container toolchain was available locally.
- No game dump was used.

Initialized recursively at the original pinned revisions:

| Submodule | Commit |
|---|---|
| FSR-Vulkan | `c64f093404125e960813f95f1996c14780f0dd69` |
| ImGui | `f1cc2ae15e53a861a874c3034aae6798fde194ab` |
| magic_enum | `e046b69a3736d314fad813e159b1c192eaef92cd` |
| miniz | `2948eebafc5b9f1c69bebed80333e403a09bd62d` |
| xbyak | `cd6bad3cad2e07a3e277c23d2b1f065320024717` |
| LibAtrac9 | `efca2e3af35562a09a9bb6deed90e45b4b824dc4` |

GPU patch preparation and a repeated application succeeded; `--check` confirmed
both patches. The FSR-Vulkan diff contains two expected files (110 inserted lines).
The parent repository will show that submodule's modified working tree; its
gitlink remains unchanged. Other submodules are clean.

## Executed on the local ARM Mac

```bash
cmake -S . -B out/m0-arm64 -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build out/m0-arm64
ctest --test-dir out/m0-arm64 --output-on-failure
bash -n build.sh scripts/apply_gpu_patches.sh
git diff --check
```

The build produced arm64 Mach-O fixtures. The initial build-harness increment
passed all 10 CTest cases. After execution-interface integration, all 12 CTest
cases passed in both Debug and Release configurations: portable runtime contracts
and fatal diagnostics, AppContent, synthetic preparation/linking, mods, patches,
harness configuration and unavailable-backend behavior.
The missing-required-binary check verifies a failure rather than a skip.

All 12 cases also passed with AddressSanitizer and UndefinedBehaviorSanitizer
enabled for the ARM host fixtures (`out/m0-sanitized`):

```bash
cmake -S . -B out/m0-sanitized -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_FLAGS=-fsanitize=address,undefined \
  -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined
cmake --build out/m0-sanitized
ctest --test-dir out/m0-sanitized --output-on-failure
```

The wrapper command `bash build.sh --cpu-only --test` also passed the final
12-case harness. A Release build used the equivalent commands under
`out/m0-release` with `-DCMAKE_BUILD_TYPE=Release`.

As a supplemental check, the native reference adapter and its fixture compiled
for x86-64 Mach-O and ran using the Rosetta installation already on this machine:

```bash
clang -arch x86_64 -std=c11 -Wall -Wextra -Werror -fno-omit-frame-pointer -Isrc \
  src/runtime_execution.c tests/test_execution.c -o out/m0-native-adapter-x86
out/m0-native-adapter-x86
```

It passed every implemented call shape, import identity, nested callbacks,
signed returns and switched-stack entry. This is adapter-level evidence on macOS
under Rosetta; it does not verify Linux/Windows system calls, the full native
loader, or ARM translation. Option B and the arm64 harness have no Rosetta
dependency. Nothing was installed for this check.
The same fixture also passed after recompilation with `-O2`.

One existing mod fixture compared an unresolved macOS `/var/...` temporary path
to the implementation's canonical `/private/var/...` result. Its assertion now
compares canonical paths. This changes the test expectation, not overlay behavior.

## Coverage that remains unverified

| Check | Status and reason |
|---|---|
| Native x86 Linux loader/runtime fixtures | Not executed: no Linux host locally; CI job supplied |
| Native x86 Windows loader/runtime fixtures | Not executed: no Windows/CLANG64 host locally; CI job supplied |
| Remote macOS CI | Not executed: local checks ran on real ARM hardware, workflow not published/run in this task |
| Existing full Windows/Linux game builds | Not executed: host/dependencies unavailable; CPU CI does not replace this check |
| Windows POSIX-only file/mod fixtures | Not registered in CMake: these use POSIX symlinks/shebang executables and the Linux shell launcher; Linux/macOS jobs cover them |
| GPU rendering, audio, input and ATRAC9 | Outside M0 CPU harness; no validation claim |
| x86 guest instruction execution on ARM | Unavailable by design until M1; host-contract results are not CPU-emulation results |
| Gameplay baseline and game compatibility | Deferred until a user-provided dump and reference host are available |

Local commits/integration do not waive these gates. M0's local infrastructure
deliverables can be completed while cross-platform acceptance stays explicitly
pending. Do not describe the entire M0 acceptance matrix as passed.

## Git integration

- Planning branch: `codex/mac-port-plan`, merged before implementation.
- Build harness: `codex/mac-port-build-harness`, feature commit `8501e1a`, merged
  with a merge commit into `mac-port` before the next feature branch was created.
- Execution boundary: `codex/mac-port-execution-interface`, based on the updated
  integration branch, with its own implementation/documentation commit and merge.
- `main` remains at `68c8cf9`; the original implementation work was integrated locally.
- Local merged implementation branches are removed after integration. The
  previously published planning branch is retained; no remote refs are deleted.

For GitHub review, `codex/mac-port-m0` starts from the planning PR's merge into
`origin/mac-port` (`96d2f51`) and contains two logical commits: the CPU harness
and the execution interface. The first review commit is `8ed6ec0`. The local
integration merges above remain historical development records; they are not
included in this review branch. The PR targets `mac-port` and is left for the
repository owner to merge after reviewing CI results.
