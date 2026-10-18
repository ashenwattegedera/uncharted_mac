# M3 game-free reference runtime: threads, fibers and linked modules

Date: 2026-10-18. Integration base: `mac-port` at `fee6729`, merged PR #5.
No game binaries, game data, Rosetta or renderer are used in these tests.

**Delivered scope:** the M3 synthetic acceptance contracts run through the
opt-in native ARM reference runtime: isolated thread/module TLS, saved CPU and
FP state, native-worker handoff, translated thread/synchronization imports,
virtual fibers, linked-image initialization/cleanup, guest patches and named
import diagnostics. This is a serialized correctness reference. It does not
complete every production thread/fiber/module API or enable the default ARM
application loader. Remaining implementation work is listed separately from
checks that require game files below.

## Branching and commits

```text
mac-port (fee6729, merged M2)
  -> codex/mac-port-guest-threads
     -> codex/mac-port-guest-fibers
        -> codex/mac-port-module-boot (one final PR into mac-port)
```

The preparation-policy and thread/TLS commits form the first checkpoint; the
fiber commit depends on them. The final branch adds linked-image boot, diagnostics,
CI and this verification record. Intermediate branches are dependency checkpoints,
not PRs to merge separately or out of order. Merge the final PR once with
**Create a merge commit** to retain the logical commits. The integration branch
was fast-forwarded before branching. No force-push, local integration merge or
submodule revision change is needed. The existing dirty FSR patch is preserved.

## Execution and memory model

Every runnable guest thread owns an opaque complete Unicorn CPU context and a
private guest stack with inaccessible guard pages. Contexts preserve virtual x86
registers rather than ARM host stacks. Thread and fiber tests check FS, MXCSR,
x87 control/stack values and XMM state across suspension. Repeated module calls
reuse the main thread's TLS and CPU control state instead of resetting it for
each initializer.

Native pthread workers take exclusive turns executing one shared reference CPU.
The scheduler holds a host mutex; the Darwin VM has an explicit release/acquire
lease. Acquire cannot steal a live owner. A handoff occurs only after emulation
has stopped and CPU state has been saved. VM callbacks and HLE execute on the
current owner in normal thread context. Callback entry and module cleanup use
the same guest ABI boundary. There is no ARM signal-context fabrication.

This provides a serial interleaving with stronger ordering than x86 TSO for guest
code and host operations participating in the same lease. It is **not** a proof
of parallel x86-on-ARM ordering or safety of arbitrary host/GPU workers accessing
backing memory independently. Such workers are not enabled by this profile.
The compiled fixture exercises contended mutex/condition waits and locked xadd
while native workers alternate; no guest lock blocks the host CPU needed to run
its owner. Poisoned VM leases remain transferable for teardown only; fault injection verifies final-owner cleanup. CPU instruction quanta provide preemption between HLE calls. A bounded
slice budget and quiescence detection turn infinite execution and deadlocks into
diagnostics rather than unbounded joins.

The VM's logical window now includes low images from `0x400000` through the PS4
user range. General memory-service allocations still begin at `0x1000000000`;
fixed service mappings below that boundary remain refused. A separate sparse
512 MiB backing budget holds images, outside the direct/flexible allocation
budgets. The CPU's internal callback stack is at `0xff00000000`, outside the
runtime window, avoiding collisions with low guest images. No guest address must
equal its host view.

## Supported M3 contracts and fixtures

| Area | Implemented and exercised |
|---|---|
| Threads | Typed Sce create/join/detach/exit/self/id/yield plus the declared POSIX aliases. Guest handles are opaque IDs, never host pthread/function pointers. Stack guards use the actual memory-protection service, and query results match translator permissions. Create and join outputs are checked guest memory; self-join and invalid handles return the appropriate Sce/POSIX error forms. |
| TLS | Copied main variant-II static TLS below the TCB; per-thread module templates for IDs 2–7; zero-initialized BSS; guest DTV/self/thread fields; checked `__tls_get_addr`; per-thread errno and key values. Destructors clear values before invocation and permit bounded rearming for four passes. |
| Synchronization | Default nonrecursive mutexes and condition variables, atomic release-and-wait, wakeup/reacquisition, trylock/owner checks and bounded handle/attribute tables. Unsupported non-null mutex/condition attributes are rejected. |
| FP and clocks | Per-context MXCSR/x87/XMM preservation; default guest FPU state and SDK-dependent initial fiber control state. RDTSC/RDTSCP are intercepted before execution and use a deterministic logical counter, not the host profiling clock. RDTSCP reports the logical guest thread ID. |
| Fibers | Typed initialization/options, attach-and-run/switch, run/switch/return, self/info/rename/finalize and stack-size tracking. Ownership is released only after CPU save; running fibers reject concurrent resume/finalize. Resuming on another guest thread installs that thread's FS base while preserving fiber registers/FP state. |
| Migration | One compiled x86 fiber performs 24 handoffs between two guest threads. Both thread and fiber worker masks prove execution on all three distinct native workers. Caller FP state and fiber FP state remain separate. A separate fixture exercises direct fiber-to-fiber switches and resume/return arguments. |
| Linked images | Strict translated-profile-v1 BBPROBE5 reader; bounded little-endian parsing, validated segments, module/TLS metadata, names, fallback bindings, relocation kinds/addends/overlap, executable entry points and no unexpected trailer. Structural/import validation precedes mappings. Unknown named imports fail with their name and relocation address. |
| Module lifecycle | Two compiled synthetic modules initialize in table order through the same backend. Main/module TLS and FP changes persist into the next initializer and entry. Checked host imports coexist with guest function/data export bindings. C++-style finalizers execute once in reverse registration order; failed second initialization unwinds the registered first-module cleanup. Continuations are retired before image unmapping. |
| Patches | Expected-byte guest code patches validate executable ownership, temporarily permit the write, invalidate translated code and restore permissions. Warm-cache execution observes the replacement. Mismatched expected bytes preserve the original code. Native Microsoft-ABI hook addresses remain unsupported. |
| Diagnostics | Separate fixtures reject guest deadlock, instruction budget exhaustion, genuine guest memory faults and invalid typed TLS import pointers. Import failures retain the named bridge and guest PC; malformed images and unresolved names do not become executable mappings. |

The current compiled inventory contains **82 contracts**: the original 31 M2
contracts and 51 M3 additions. [The current inventory](evidence/m2-import-inventory.json)
retains its historical filename, adds milestone labels and hashes the current
scheduler/module/CPU sources. It still lists all 930 diagnostic NIDs; a name or
source table is not an audited prototype. Unregistered imports fail closed.

## Preparation and diagnostic boot harness

Translated preparation preserves x86 FS instructions. Native preparation retains
its existing FS-to-GS rewrite. `--execution-mode unicorn-reference` selects
backend/policy-versioned filenames and sets the translated profile marker. Linkers
reject a prepared input from another execution policy; the native loader rejects
translated capabilities. Reports include backend, schema, source inputs, tool
hashes and a cache key. These fingerprints describe preparation provenance;
there is no persisted production translator cache in this increment.

Without real inputs, the fixture generates `m3-synthetic-linked.bin` entirely
from freestanding C and explicit serialized metadata. This is also exercised
through the actual command-line reference harness. An additional test passes
synthetic parsed-module descriptors through the actual Python multi-module
linker in both execution modes, checks FS/header policy and boots its translated
output through the CLI in Release, UBSan and ASan. This verifies the preparation/
reader boundary without claiming coverage of shipped SELF contents:


```bash
out/m3/reference/translated-modules-test --write-image out/m3/reference/m3-synthetic-linked.bin
out/m3/reference/bb-reference-boot out/m3/reference/m3-synthetic-linked.bin
# Reference entry completed: result=0x2a
```

For later testing with legally obtained inputs, the preparation commands are:

```bash
python3 scripts/prepare.py /path/to/game --out out/prepared \
  --execution-mode unicorn-reference
python3 scripts/link_modules.py /path/to/game --out out/prepared \
  --execution-mode unicorn-reference
# Produces boot-linked.unicorn-reference-v1.bin and its matching report.
```

Those commands with real files have **not** been run. The reference harness
currently accepts translated BBPROBE5 only, with a 512 MiB image limit, 16 KiB
aligned nonoverlapping load granules, at most six linked modules and module TLS
IDs 2–7. The 576 MiB CLI file bound includes descriptors. BBPROBE4 from the
standalone libc linker is not an accepted input. Synthetic entry invocation uses
the declared reference call shape; this is not a verified PS4 process `_start`
stack or a claim that a shipped executable boots.

## Reproduce verification

Native Apple Silicon macOS, Clang, CMake, Python and pkg-config are required.
The compatibility option remains `BB_BUILD_TRANSLATED_M2`; it now includes the
M3 reference modules. Ordinary root builds retain the unavailable ARM backend
and do not fetch Unicorn. Local builds use the previously SHA-256-verified pinned
source via `FETCHCONTENT_SOURCE_DIR_UNICORN`; CI exercises the checksum-pinned
download. No third-party code/revision was changed.

```bash
cmake -S . -B out/m3/reference -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DBB_BUILD_TRANSLATED_M2=ON
cmake --build out/m3/reference --parallel 4
ctest --test-dir out/m3/reference --output-on-failure

cmake -S . -B out/m3/ubsan -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DBB_BUILD_TRANSLATED_M2=ON -DBB_PROBE_SANITIZER=undefined
cmake --build out/m3/ubsan --parallel 4
ctest --test-dir out/m3/ubsan --output-on-failure

cmake -S . -B out/m3/asan -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DBB_BUILD_TRANSLATED_M2=ON -DBB_PROBE_SANITIZER=address,undefined
cmake --build out/m3/asan --parallel 4
ctest --test-dir out/m3/asan \
  -E '^(darwin-guest-memory|cpu-memory-suite|cpu-memory-hardened)$' --output-on-failure

python3 scripts/audit_m2_imports.py --binary out/m3/reference/translated-runtime-test
```

Run JIT tests outside a restrictive tool sandbox and inspect CTest logs for
sanitizer diagnostics as well as exit status. The three ASan exclusions are
unchanged historical identity-window fixtures; they pass in Release and UBSan.
**Every new M3 thread, fiber, module and CLI test remains in the ASan subset.**
Project sanitizers do not instrument the pinned vendor. The existing vendor-wide
Unicorn `tcg.h:987` UBSan finding remains open; these checks do not certify the
vendor core sanitizer-clean.

## Verification record

Native Apple M2, arm64 macOS 26.7.1 (25G241), 16 KiB host pages.

| Check | Result |
|---|---|
| Integrated Release CTest | 55/55 passed |
| Integrated fail-fast project UBSan CTest | 55/55 passed; log inspected with no sanitizer diagnostics |
| Project ASan+UBSan subset | 52/52 passed with exactly the three exclusions above; log inspected with no sanitizer diagnostics |
| Signed Hardened Runtime | Thread, fiber and linked-module fixtures pass with the existing allow-jit-only entitlement; existing signed M1/M2 fixtures remain passing |
| Static analysis | VM, CPU continuation, scheduler, import runtime, linked-image reader and reference CLI analyzed; no remaining diagnostics |
| GitHub | PR checks give revision-specific Linux, Windows CLANG64, native ARM host and integrated ARM Release/UBSan results; artifacts include logs, signed binary metadata, compiled import contracts and synthetic headers/image |

## Remaining implementation work

These are implementation gaps, **not** checks blocked only by missing game files:

- Parallel guest execution, a production x86 TSO strategy, arbitrary worker HLE
  callbacks, cancellation, timed waits, priorities/affinity, recursive/custom
  mutex attributes, rwlocks/semaphores and unregistered thread/lifecycle services
  still require implementation and contract tests. The current scheduler is
  serialized and synchronous at its host API boundary.
- Context-less fibers can be initialized and attached later. Running one without
  attached guest storage is explicitly rejected; borrowing the calling guest
  thread's stack and restart semantics remain unsupported. No host-stack shortcut
  is used. Additional SDK/error edge cases require broader contract tests.
- Limits are explicit: 64 thread records, 32 keys, 64 mutexes/conditions, 32
  attributes/fibers and 256 finalizers per reference process. Handles are not
  recycled during a process. This is not a production unbounded job system.
- The loader supports the current prepared-image subset, not arbitrary SELF/ELF
  dynamic loading, unload/reload, all relocation/TLS models or module stop arrays.
  Registered cleanup is exercised for normal return and failed initialization;
  arbitrary guest-fault cleanup/exception semantics remain unverified.
- The guest TSC is a deterministic counter stepped by timestamp instructions.
  Real-time timer fidelity, scheduling fairness guarantees and calibrated PS4
  frequency/latency are not implemented. Host profiling clocks remain separate.
- Actual game-specific hooks still need guest instruction/ABI contracts. The
  checked guest patch API and invalidation are verified; native host hook
  addresses are not reclassified as safe translated hooks.
- The full application, graphics/audio/input pipeline, concurrent renderer memory
  access, platform exceptions and distribution remain outside this increment.

## Checks requiring game files or broader environments

Real executable/libc/module layouts, actual import signatures and lifetimes,
initialization dependencies, module TLS patterns, the real process entry stack,
job/fiber behavior, instruction coverage, game-specific patches, boot, saves,
graphics/audio/input and performance have not been verified. Actual shadPS4/
Vulkan execution is not inferred from the CPU protocol tests. Other Apple chips,
OS releases, clean-machine installation, Developer ID and notarization remain
separate validation gates. Review this PR and these limits before choosing the
next M3 expansion or starting M4.

CPU-context and hook contracts were checked against the pinned Unicorn headers
and [upstream API declarations](https://github.com/unicorn-engine/unicorn/blob/8028ec436f2d9376525352dd38ed9ed6b9f6be10/include/unicorn/unicorn.h).
The [upstream FAQ](https://github.com/unicorn-engine/unicorn/blob/8028ec436f2d9376525352dd38ed9ed6b9f6be10/docs/FAQ.md)
limits instruction hooks; timestamp interception therefore uses the existing
instruction callback and stops execution before updating virtual CPU state.
