# M2 verification: Darwin memory, ABI and fault foundations

Date: 2026-10-17. Integration base: `mac-port` at `7d0aae9` (merged M1).

**Delivered:** reusable Darwin memory and SysV ABI modules plus an opt-in adapter
connecting them to M1's reference CPU. Synthetic tests exercise actual Darwin
aliases, independent tracking counts, compiled x86 calling conventions and
fault restart. No game files or Rosetta are used.

**Milestone boundary:** this is the game-free M2 foundation increment. Full
production M2 acceptance remains open: the existing PS4 memory service, full
import/relocation registry and actual GPU/signal path are not wired to these
modules. Those are implementation gaps, not tests that passed or work blocked
solely by missing game files. The default ARM execution backend stays unavailable.

## Branches and commits

The dependent stack starts at the updated integration branch:

```text
mac-port (7d0aae9)
  -> codex/mac-port-darwin-memory (38adb42)
     -> codex/mac-port-guest-abi (29aaa52)
        -> codex/mac-port-faults (remaining ABI, fault, CI and documentation commits)
```

One PR from the final branch into `mac-port` contains the entire stack. The
intermediate branches are dependency checkpoints, not independent PRs to merge
out of order. Use **Create a merge commit** to retain the logical changes. No
integration branch history is rewritten and no local integration merge is made.
The existing dirty FSR working tree and pinned submodule revisions are preserved.

## Reproduce

Local host: Apple M2; macOS 26.7.1 (25G241); Apple Clang 21.0.0
(`clang-2100.3.34.2`), SDK 27; CMake 4.4.4, Ninja 1.13.2, Python 3.15.0,
pkgconf 3.0.8. Native host page size: 16,384 bytes. The normal tested reservation
begins at `0x7000000000`; address selection is not a hardcoded support guarantee.
The reference CPU retains its pinned Unicorn 2.1.4 source and checksum from M1.

```bash
# Root host contracts, portable ABI tests and Darwin memory fixture.
cmake -S . -B out/m2/host -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build out/m2/host --parallel 4
ctest --test-dir out/m2/host --output-on-failure

# Actual x86 translation, compiled callers, mappings and signed executables.
cmake -S spikes/cpu -B out/m2/cpu -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build out/m2/cpu --parallel 4
ctest --test-dir out/m2/cpu --output-on-failure

# Fail-fast UBSan for all project-owned probe targets, excluding vendor code.
cmake -S spikes/cpu -B out/m2/cpu-ubsan -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DBB_PROBE_SANITIZER=undefined
cmake --build out/m2/cpu-ubsan --parallel 4
ctest --test-dir out/m2/cpu-ubsan --output-on-failure

# Root UBSan also includes the native Darwin memory/protection fixture.
cmake -S . -B out/m2/host-ubsan -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  '-DCMAKE_C_FLAGS=-fsanitize=undefined -fno-sanitize-recover=undefined' \
  -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=undefined
cmake --build out/m2/host-ubsan --parallel 4
ctest --test-dir out/m2/host-ubsan --output-on-failure
```

Local probe trees use the exact manually SHA-256-verified M1 source with
`FETCHCONTENT_SOURCE_DIR_UNICORN`. CI exercises the default verified download.
Run JIT tests outside the Codex tool sandbox, as recorded in M1. The root memory
fixture itself also passes in the tool sandbox. Its child processes deliberately
fault on protected memory; SIGBUS/SIGSEGV termination is the expected assertion,
not a swallowed parent failure.

## Executed checks

| Check | Result |
|---|---|
| Root Debug host CTest | **14/14 passed**: existing M0 contracts plus ABI and actual Darwin memory |
| Reference CPU Release CTest | **16/16 passed**: M1 suite plus ordinary/signed ABI and ordinary/signed memory integration |
| Root fail-fast UBSan | **14/14 passed** |
| Project-owned probe fail-fast UBSan | **16/16 passed**; project targets only, vendor core uninstrumented |
| Root ASan+UBSan subset | **13/13 passed**, explicitly excluding `darwin-guest-memory` |
| Probe ASan+UBSan subset | **14/14 passed** with project-scoped instrumentation, explicitly excluding both `cpu-memory-*` cases |
| Clang static analysis | ABI, Darwin memory and fault modules analyzed with no remaining diagnostics |
| Ad-hoc Hardened Runtime | ABI and memory suites pass with the existing allow-jit-only entitlement |
| GitHub Linux/Windows/ARM macOS | Extended checks configured; PR checks record the tested revision and result |

Memory fixtures cover bounded low-address reservation, refusal to overwrite an
existing reservation or map outside ownership, real shared aliases, independent
per-alias permissions, partial watcher removal across four subpages, invalid
watch removals without underflow, last-alias zero-on-reuse, teardown/reuse,
wrong-thread rejection, observer reentry rejection and poisoning after failure.

The independent x86 caller is compiled from freestanding C by Clang targeting
`x86_64-unknown-linux-gnu`. The fixture extractor rejects text relocations; this
avoids smuggling a test linker or host address into the result. Calls cover:

- Eight integer arguments, including stack overflow; nine doubles; float.
- II, SS, IS and SI aggregates in both arguments and returns.
- Whole-aggregate register-allocation rollback with a later register argument.
- A 24-byte stack aggregate and hidden structure-result pointer.
- Nested guest→typed host→guest→typed host calls and guest data reads.
- Declared promoted varargs and a compiler-created guest `va_list` consuming
  both register-save and overflow areas; malformed offsets and bounds fail.
- Checked pointer access to owned memory and a signed PS4 EINVAL return, plus
  guest layout size/offset assertions. This is not every service error mapping.

The memory/CPU integration fixture covers demand mapping with exact RIP/address,
refusal outside ownership, callback acknowledgment of all watched subpages,
retention after rejection, genuine read-only/fetch/unmapped failures, explicit
restart, **exactly-once atomic xadd recovery**, host patches through aliases,
guest patches visible through another executable alias, and unmap/remap cache
invalidation. Tests run in ordinary and signed Hardened Runtime processes.

For the ASan subset, configure a separate probe tree with
`-DBB_PROBE_SANITIZER=address,undefined`, build, then run
`ctest --test-dir BUILD_DIR -E '^cpu-memory-' --output-on-failure`.
The root subset uses `-fsanitize=address,undefined` for compile/link and excludes
`^darwin-guest-memory$`. Counts above describe these explicit subsets, not full
memory coverage. The final selected logs were inspected for sanitizer diagnostics
as well as exit status and contained none.

## Failures found and resolved

- Advisory `mmap` hints did not return the initially assumed low address. The
  final allocator claims free candidate ranges with Mach fixed allocation,
  without the overwrite flag, and verifies ownership before fixed replacements.
- Mutating protection/cache state inside Unicorn's memory-fault callback could
  skip a recovered store. Recovery now occurs after emulation returns and retries
  captured RIP explicitly. The atomic fixture checks that recovery neither skips
  nor doubles the operation.
- Cache mutations during write hooks are deferred until a block boundary or
  emulation exit. This avoids executing a stale next block after an alias patch.

## Sanitizer limits and corrected interpretation

ASan prevents this bounded allocator from finding a low window on the tested
host. A minimal Mach reservation diagnostic returned `KERN_NO_SPACE` for all
sixty candidates. Full ASan attempts therefore failed at memory creation: root
13/14 and probe 14/16 by exit status. The memory cases are **not ASan verified**;
they are tested normally and with UBSan. No higher address, unsafe overwrite or
passing skip was substituted to hide the limitation.

Vendor-wide fail-fast UBSan reports `tcg.h:987`, nonzero offset from a null pool
pointer in pinned Unicorn's `tcg_malloc`, before guest execution. The original
recoverable ASan+UBSan run also printed this diagnostic despite CTest passing
its executable exit statuses. Those runs are not sanitizer-clean results.
This also narrows the interpretation of the historical M1 sanitizer count.

`BB_PROBE_SANITIZER` now explicitly instruments **project-owned** code, with
nonrecoverable UBSan errors; the unmodified pinned vendor core is uninstrumented
for that gate. This does not fix, suppress in vendor source, or validate the
vendor diagnostic. Vendor-wide sanitizer cleanliness remains unverified.

## Remaining implementation and verification gates

| Gate | Status / what is still needed |
|---|---|
| Full PS4 VM services | Existing runtime memory pool, flexible/direct allocation rules, batch APIs and entire address range are not replaced by the bounded 256 MiB foundation |
| Full import registry and relocations | Production loader still refuses ARM bindings; populate audited signatures and typed per-service wrappers, including pointer/data/callback lifetimes |
| Formatting/varargs imports | Guest va_arg transport works; actual `vsnprintf`/`snprintf` formatting, positional/length specifiers and output/string validation are not implemented |
| Complete ABI surface | Vector/SSEUP/x87 returns, unusual alignment, aggregates outside the declared shapes, platform hooks and real module metadata remain unsupported |
| Renderer/GPU hookup | GPU synchronization is a test callback, not shadPS4 cache/coherency validation; multi-consumer watches must coordinate lifetime and acknowledgment |
| Host exceptions | No ARM SIGSEGV or Mach exception restart handler is installed. New router APIs are not async-signal-safe; existing renderer signal-handler locking remains unaudited for Darwin use |
| Code modification | Tested alias patches take effect by the next block; arbitrary alias changes to later instructions within the same active block are not proven |
| Concurrency | VM objects are owner-thread confined; concurrent mutation, GPU workers, TLS/fibers, blocking HLE and parallel guest TSO require M3/M4 design |
| Game acceptance | Real binaries/modules, actual import calls, boot, graphics/audio/input/save/gameplay/performance remain untested; no proprietary content used |
| Platform/distribution | Local M2 runtime and CI configurations only; no universal Apple-chip support, Developer ID/notarization or clean-machine release claim |

See [ADR 0002](decisions/0002-darwin-memory-and-abi.md) for ownership, ABI and fault
contracts. Production integration must resolve the implementation gaps above
before marking the broad M2 milestone complete.
