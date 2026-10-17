# M2 game-free integration closeout

Date: 2026-10-17. Base: `mac-port` at `e17d818` (merged M2 foundations, PR #4).
This record supersedes the implementation gaps in the historical
[M2 foundation record](M2_VERIFICATION.md). No proprietary files were used.

The game-free memory/ABI acceptance checks now pass through the actual runtime
memory exports and the loader-facing execution interface. This is an opt-in,
single-owner reference runtime, not a working ARM game loader or selected
production translator. M3 has not started. Missing signatures and renderer
validation remain explicit gates before their corresponding services can run.

## Branch and review strategy

```text
mac-port (e17d818)
  -> codex/mac-port-m2-runtime
     -> codex/mac-port-m2-coherency (PR head targeting mac-port)
```

The runtime branch contains the logical memory-service, formatter and translated
import commits. Its dependent branch adds address-space/GPU coherency, inventory,
CI and closeout documentation. Merge the final PR once, using **Create a merge
commit** to retain those commits. Intermediate branches are checkpoints, not
separate PRs to merge out of order. `mac-port` was fast-forwarded before branching;
no integration history was rewritten. The existing dirty FSR submodule and all
pinned third-party revisions are preserved.

## Implemented and exercised

| Area | Evidence and boundary |
|---|---|
| Actual PS4 VM services | All 17 exports in `runtime_memory.c` have typed adapters. Direct/flexible allocation, named aliases, reservation, protection, queries, release and batch operations use owned Darwin storage. Physical contents survive the last virtual unmap until allocation release; released allocations are cleared before reuse. Partial batch processing and flexible-offset splits are exercised. |
| Address space | Runtime defaults to the complete existing PS4 user range `[0x1000000000, 0xfc00000000)`. Logical guest pointers differ from host views. Sorted sparse page metadata scales with live mappings. A separate fixture executes at `0x400000` and through its alias at `1 TiB - 16 KiB`, with insertion/removal/bounds checks. This tests the VM contract, not ELF loading. |
| Physical backing | Sparse file backing covers the existing 5056 MiB direct budget plus 1 GiB flexible storage. Tests check budget, aliases and release/reuse; they do not fill or benchmark every physical byte. |
| Execution/imports | The opt-in `bb-translated-runtime` implements actual `execution.h` entry points. Scoped NIDs resolve only to declared guest bridges/data exports; native host addresses supplied by old import requests are ignored. A compiler-generated relocation-free x86 module invokes actual allocation/map/release and formatting imports in ordinary and Hardened Runtime executables. |
| Relocations/data | Function, image-relative and data relocation requests validate bounds, kinds, addends, write access and overlaps before writing. Negative tests preserve the image. Stack canary and libc marker exports live in guest memory. Parsing real module relocations remains M3. |
| ABI | Existing fixtures retain integer/FP register and stack transport, supported aggregate shapes, hidden sret, nested guest callbacks, promoted varargs and guest va_list. No host va_list or arm64 function address is passed to guest code. |
| Formatting | Checked `snprintf`, `vsnprintf` and bounded `sprintf` adapters support positional/star arguments, promoted doubles, guest LP64 lengths, strings, `%n`, truncation, NUL termination and zero capacity. Invalid/unsupported plans fail before output. Output is capped at 1 MiB, format text at 4096 bytes and calls at 24 ABI operands. Wide strings/chars and long double are unsupported. |
| GPU fault protocol | Actual runtime GPU protection now maintains independent per-alias read/write masks with physical aggregation. The shared guest-PC/access bridge connects the renderer's `ReadMemory`/`OnWriteFault` entry points in source. Synthetic callbacks test missing-handler rejection, acknowledgment, backing synchronization and precise translated restart. Genuine guest protection failures remain failures. **The Vulkan renderer is not built or run by these tests.** |
| Code coherency | Executable physical-reference counts filter data writes. A write through any alias stops execution before the next instruction and invalidates translations outside emulation. A fixture modifies a later instruction in the same cached block through another alias. Existing atomic-fault exactly-once checks remain passing. |
| Renderer copies | Sparse upload copies use checked VMA/backing translation rather than dereferencing logical guest addresses. Renderer-wide pointer/lifetime and worker-thread integration still require M4 validation. |

`BB_DARWIN_VM_MB=64..65536` optionally restricts the runtime's logical window for
experiments. It does not reduce physical budget or change the default range.
`runtime_low_map` retains a separate owned identity window for native metadata;
`image_map` refuses ARM native-image identity loading. M3 must load guest images
using logical addresses and VM accessors.

## Import inventory

[Machine-readable inventory](evidence/m2-import-inventory.json) records **31
contracts** (29 functions, two data exports), all **930 diagnostic NIDs** from
`import_names.inc`, known source export tables and source hashes. Every actual
memory export must be registered. Registry metadata is read from the compiled
binary and compared with checked-in evidence; source-only freshness runs in the
portable harness. Line endings are normalized for cross-platform checks.

Diagnostic names do not provide prototypes. Unregistered names are explicitly
listed as M3 thread/fiber dependencies, M4 device/renderer dependencies, or
`unsupported-needs-contract-review-before-use`. The latter is a real unsupported
surface, not evidence that the game never calls it. Before any such import is
needed, implement its checked pointer/structure/callback contract and tests.
This closeout does not claim that all 930 imports have production adapters.
Linked guest libc, real module lifetimes and imports discovered from binaries
must be checked during M3 and subsequent game validation. Unsupported bindings,
Microsoft-ABI hooks and undeclared ABI classes fail closed.

## Reproduce

Native arm64 macOS, CMake, Clang, Python and pkg-config are required. The root
profile fetches the same checksum-pinned Unicorn 2.1.4 reference as M1; ordinary
root builds do not fetch it. Local runs used the previously hash-verified source
via `FETCHCONTENT_SOURCE_DIR_UNICORN`; CI exercises the pinned download.

```bash
cmake -S . -B out/m2-reference -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DBB_BUILD_TRANSLATED_M2=ON
cmake --build out/m2-reference --parallel 4
ctest --test-dir out/m2-reference --output-on-failure
python3 scripts/audit_m2_imports.py --binary out/m2-reference/translated-runtime-test

cmake -S . -B out/m2-ubsan -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DBB_BUILD_TRANSLATED_M2=ON -DBB_PROBE_SANITIZER=undefined
cmake --build out/m2-ubsan --parallel 4
ctest --test-dir out/m2-ubsan --output-on-failure

cmake -S . -B out/m2-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DBB_BUILD_TRANSLATED_M2=ON -DBB_PROBE_SANITIZER=address,undefined
cmake --build out/m2-asan --parallel 4
ctest --test-dir out/m2-asan \
  -E '^(darwin-guest-memory|cpu-memory-suite|cpu-memory-hardened)$' --output-on-failure
```

Run JIT tests outside a tool sandbox. Inspect `Testing/Temporary/LastTest.log`
for sanitizer diagnostics as well as exit status. The three explicit ASan
exclusions are the older identity-window fixtures: ASan occupies their candidate
addresses on this Mac. They pass normally and under UBSan. **The new non-identity
VM, actual memory service, translated import and GPU protocol tests also pass
under ASan**, including the almost-1-TiB logical window. No passing skip replaces
those tests. Project-scoped sanitizers leave the pinned vendor uninstrumented;
the prior vendor-wide `tcg.h:987` finding is unchanged and not certified clean.

After reviewing a registry/provider change, refresh evidence with:

```bash
python3 scripts/audit_m2_imports.py \
  --binary out/m2-reference/translated-runtime-test --write
```

## Verification record

Local host: Apple M2, native arm64, macOS 26.7.1 (25G241), 16 KiB host pages.

| Check | Result |
|---|---|
| Integrated Release CTest | 38/38 passed |
| Integrated fail-fast project UBSan CTest | 38/38 passed; log inspected, no diagnostics |
| Project ASan+UBSan subset | 35/35 passed with exactly the three exclusions above; log inspected, no diagnostics |
| New ASan memory/runtime/formatter contracts | 6/6 passed, included in the subset |
| Clang static analysis | Formatter, translated runtime, Darwin VM and guest-fault router: no remaining diagnostics |
| Ad-hoc Hardened Runtime | Compiled x86 runtime import/format module passes with allow-jit entitlement; existing signed CPU/ABI/memory suites pass |
| GitHub | PR checks provide revision-specific Linux/Windows/native ARM results; workflow now runs integrated Release and project UBSan and archives logs, signatures and registry evidence |

## Remaining gates before a playable port

- **M3 implementation:** guest scheduling, TLS/fibers, blocking HLE, module loading,
  process lifecycles and multi-thread ordering. The reference runtime is owner-thread
  confined; bridge callbacks cannot migrate or enter from arbitrary workers.
- **M4 implementation/validation:** actual Vulkan/shadPS4 execution, GPU consumer
  lifetime/locking, worker synchronization, native host-fault behavior, graphics,
  audio and input. The new protocol runs in ordinary thread context after the
  translator stops. No ARM signal/Mach handler is installed, and it must not be
  called from a signal handler. Existing native renderer signal code is unchanged.
- **Unsupported ABI/imports:** vectors/SSEUP/x87 return classes, additional
  aggregates, long-double/wide formatting, platform hooks and unregistered names
  need explicit contracts before use. Compiler-generated synthetic coverage does
  not establish the signatures required by the shipped game.
- **Requires game files:** real executable/module layouts, actual imports and
  callbacks, boot, data-dependent VM patterns, saves, graphics/audio/input,
  performance, instruction coverage and playable acceptance.
- **Production/distribution:** translator performance/selection, broader Apple
  hardware/OS coverage, clean-machine tests, Developer ID and notarization.

These are preserved acceptance gates; no game boot, complete renderer integration,
production import completeness or gameplay claim follows from this PR.
