# M0 verification record

Date: 2026-10-16. Starting game-runtime revision: `68c8cf9`.

Status: build-harness increment implemented and locally verified. Execution-interface
increment follows on its own feature branch. Final M0 status is recorded below
after both increments are integrated.

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

The build produced an arm64 Mach-O `runtime-core-test`. All 10 build-harness
CTest cases passed: portable runtime contracts and fatal diagnostics, AppContent,
synthetic preparation/linking, mods, patches and harness configuration checks.
The missing-required-binary check verifies a failure rather than a skip.

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
| GPU rendering, audio, input and ATRAC9 | Outside M0 CPU harness; no validation claim |
| x86 guest instruction execution on ARM | Unavailable by design until M1; host-contract results are not CPU-emulation results |
| Gameplay baseline and game compatibility | Deferred until a user-provided dump and reference host are available |

Local commits/integration do not waive these gates. M0's local infrastructure
deliverables can be completed while cross-platform acceptance stays explicitly
pending. Do not describe the entire M0 acceptance matrix as passed.
