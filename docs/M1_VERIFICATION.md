# M1 verification: native ARM CPU feasibility

Date: 2026-10-17. Base: `mac-port` at `e559946`. Experiment:
`codex/spike-mac-cpu-backend`; fixture implementation: `5644cb4`.

**Result:** the game-free feasibility gate passes on an Apple M2. A native arm64
process translates synthetic x86-64 instructions without Rosetta or game files.
Unicorn is selected provisionally as an experimental correctness reference,
not as the production performance engine. The default ARM runtime backend
remains unavailable. See [ADR 0001](decisions/0001-cpu-reference-core.md).

## Environment and reproduction

Local host: Apple M2, macOS 26.7.1 (25G241), Apple Clang 21.0.0
(`clang-2100.3.34.2`), SDK 27, CMake 4.4.4, Ninja 1.13.2, pkgconf 3.0.8.
The experiment defaults to a macOS 15 deployment target; the local runtime
result covers macOS 26.7.1 only. Test executables require native ARM compilation
and reject Rosetta via `sysctl.proc_translated`.

```bash
cmake -S spikes/cpu -B out/m1-cpu -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build out/m1-cpu --parallel 4
ctest --test-dir out/m1-cpu --output-on-failure
out/m1-cpu/cpu-probe-suite benchmark

cmake -S spikes/cpu -B out/m1-sanitized -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_FLAGS=-fsanitize=address,undefined \
  -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined
cmake --build out/m1-sanitized --parallel 4
ctest --test-dir out/m1-sanitized --output-on-failure

cmake -S . -B out/cpu -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build out/cpu --parallel 4
ctest --test-dir out/cpu --output-on-failure
```

The local trees were `out/m1/probe` and `out/m1/sanitized`, with
`FETCHCONTENT_SOURCE_DIR_UNICORN` pointing to the manually downloaded source
archive after checking its SHA-256 against the pin in
[the build file](../spikes/cpu/CMakeLists.txt). This override bypasses CMake's
download/hash step; CI uses the default verified archive URL. No global Unicorn
installation or upstream patch is used. pkgconf was installed through Homebrew.

JIT execution ran as normal host processes outside the Codex tool sandbox.
Inside that sandbox, the dependency's cache-initialization register read caused
SIGILL; the identical executable passed outside it. This environment restriction
is not counted as a passing sandbox execution result.

## Executed checks

| Check | Local result and interpretation |
|---|---|
| Release M1 CTest | **12/12 passed**; smoke, scalar/control flow, SIMD, x87, callbacks, faults, invalidation, serialized threads, limits, hardened suite, JIT policy and signature verification |
| Debug ASan + UBSan M1 CTest | **12/12 executable exit statuses passed**; M2 log audit found a recoverable upstream UBSan diagnostic, so this is not a sanitizer-clean result; see [M2 sanitizer limits](M2_VERIFICATION.md#sanitizer-limits-and-corrected-interpretation) |
| Existing M0 ARM harness | **12/12 passed**; default backend behavior preserved |
| Native executable inspection | arm64 Mach-O; Unicorn statically linked; only libSystem reported as a dynamic dependency |
| Ad-hoc Hardened Runtime | codesign flags `0x10002(adhoc,runtime)`; only `com.apple.security.cs.allow-jit=true`; full suite passed in that executable |
| Public JIT policy probe | `MAP_JIT`, supported per-thread protection, ARM instruction rewrite from return 42 to return 43, cache invalidation and execution passed |
| Whitespace/diff checks | `git diff --check` passed |
| GitHub Actions | Native ARM experiment **passed** on `75ed5a3`: [run 39001533127](https://github.com/ashenwattegedera/uncharted_mac/actions/runs/39001533127), including the default archive download/hash path and artifact upload. Final revision checks are attached to [PR #3](https://github.com/ashenwattegedera/uncharted_mac/pull/3). |

The final review also added an explicit nonrecursive bridge-loop regression: exactly
sixty-four callbacks occur before the runner returns its limit error. Release and
ASan/UBSan suites were rerun successfully after this addition.

The synthetic suite specifically checks:

- Arithmetic, branches, stack and indirect calls; signed and void return handling.
- SSE2 integer/double operations, MXCSR save/restore and x87 arithmetic.
- Guest→host→guest callbacks, including restoration after deliberate register,
  flags and SIMD clobbering in the nested guest.
- Guest unmapped-read RIP/address attribution, demand mapping and retry;
  write/execute protection; host patch and guest self-modifying-code invalidation.
- Four host pthread callers performing 400 total serialized `lock xadd`
  operations through low and 64 GB guest aliases of one borrowed 16 KB backing.
  Final value and every old-value sum are checked. No parallel guest vCPUs run.
- UD2, unsupported `syscall`, instruction budget, recursion depth, bridge-loop
  bounds and malformed configuration rejection. A failed call preserves output.

An initial execute-protection test exposed stale translated blocks after a
mapping permission change. The runner now explicitly invalidates the affected
translation-cache range on successful protection changes; the regression passes.

## Measurements

[Raw three-sample M2 evidence](evidence/m1-apple-m2.json) is tied to fixture commit
`5644cb4`. Release results varied substantially:

| Measurement | Observed range |
|---|---|
| Cold 100,000-iteration arithmetic loop | 1.78–4.55 ms |
| Ten warm loops, 3,000,030 guest instructions total | 17.29–47.93 ms |
| Guest entry, mean over 100 calls per sample | 29.83–31.18 µs/call |
| Guest entry plus host bridge, same sampling | 45.23–53.71 µs/call |

These include context save/restore, watchdog, instruction-count budget and
callback machinery. They are a reproducible probe measurement, not bare-core
throughput, a comparison with FEX/Box64, or game FPS. No timing pass threshold
is imposed in CI.

## What remains unverified

- Game binaries, real modules, actual imports, boot, graphics, audio, saves,
  gameplay and performance; no proprietary files were used or committed.
- Production `BbExecutionBackend` integration, PS4 VM ownership, Darwin host
  signal recovery/GPU write tracking, complete scalar/FP/aggregate/varargs ABI.
  Unicorn software guest faults are distinct from Darwin host VM faults.
- True concurrent guest CPUs, x86 memory ordering across ARM cores, TLS/fibers,
  blocking HLE and scheduler shutdown. The mutex serializes the current probe.
- Comprehensive x86/AVX/FP exception compatibility or differential validation
  against native x86. Covered fixtures are deliberately narrow.
- Other Apple chip families, production workloads, Developer ID/notarization,
  clean-machine distribution or App Store eligibility. No broad platform
  support claim follows from setting a deployment target.
- FEX and Box64 execution: they received source assessment only, not local
  builds or performance comparisons. Production translator selection stays open.

The probe owns its engine but borrows host backing storage until close; callers
must join before close and must not mutate backing concurrently. Its limits are
eight nested calls, sixteen bridge slots and sixty-four bridge resumptions per
call. Each emulation segment has a five-million-instruction/one-second budget;
these are segment bounds rather than a whole-invocation real-time guarantee.
