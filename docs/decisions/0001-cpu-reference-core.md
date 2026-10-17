# ADR 0001: use Unicorn as an experimental CPU correctness reference

Date: 2026-10-17. Status: accepted for the M1 experiment; production core selection remains open.

## Context

Option B needs native arm64 Darwin execution of x86-64 instructions with this
project's PS4 services. A Linux application launcher alone does not supply that
contract. M0 isolated the execution boundary but intentionally leaves the ARM
runtime backend unavailable. M1 must establish execution feasibility before
production integration, without relying on game files or Rosetta.

## Candidates and evidence

| Candidate / revision | Evidence examined | M1 outcome |
|---|---|---|
| [FEX upstream](https://github.com/FEX-Emu/FEX/tree/5285020de0b30dbdae7b44d0d83541af2d13d002), `5285020de0b30dbdae7b44d0d83541af2d13d002` | MIT license; upstream root CMake permits Linux/Windows, not Darwin | Source assessment only; not built. Adapting upstream FEXCore is separate porting work. |
| [CodeWeavers FEX macOS work](https://www.codeweavers.com/blog/mjohnson/2026/7/31/crossover-preview-the-right-to-bear-arm64-on-mac) | Official announcement describes a custom FEX port in CrossOver's ARM64 preview | Evidence that FEX on Darwin is feasible; Wine/ARM64EC integration does not establish this project's PS4 SysV embedding. Not locally built or audited. |
| [Box64](https://github.com/ptitSeb/box64/tree/4e91870c51f7ce10ff48043e850673a9eff32cb5), `4e91870c51f7ce10ff48043e850673a9eff32cb5` | MIT license; Linux userspace execution and native-library wrapping design | Source assessment only; not built. Darwin embedding/service replacement remains unproven here. |
| [Unicorn 2.1.4](https://github.com/unicorn-engine/unicorn/tree/8028ec436f2d9376525352dd38ed9ed6b9f6be10), `8028ec436f2d9376525352dd38ed9ed6b9f6be10` | C embedding API, QEMU-derived TCG JIT, explicit mappings/hooks/context/cache APIs; GPLv2 and per-file notices | Built as a native ARM static dependency and executed the synthetic suite on an Apple M2, including ad-hoc Hardened Runtime signing. |

Community Darwin/FEX work also exists, for example
[AetherPS4's porting notes](https://github.com/Leviidev/AetherPS4/blob/main/PORTING.md).
Those claims and patches were not audited or executed in this experiment, and
no code was imported from that project. The decision does not claim that the
FEX ecosystem lacks macOS implementations or that untested alternatives fail.

## Decision

Keep a pinned, opt-in Unicorn runner under `spikes/cpu` as a correctness reference
and an executable feasibility result. It accepts M0 `BbGuestCall` descriptions,
runs synthetic x86-64 code, and dispatches its own typed host callbacks without
requiring a Linux userspace. Keep the production ARM backend unavailable.

The pin and archive checksum are enforced by the isolated CMake project. No
vendor patches are applied. Unicorn's GPLv2 source and individual file notices
must be retained, with corresponding source obligations for distribution. The
parent project's GPLv2-or-later license permits this experiment's source setup;
packaging still needs its complete dependency notice/source inventory.

One engine is protected by a recursive mutex. Only one guest instruction stream
executes at a time; callbacks run outside the emulation loop, and nested calls
save/restore complete guest context on distinct stack slices. This provides a
conservative serialized reference for shared-memory ordering and atomic tests.
It does not establish concurrent x86 TSO on ARM. Blocking HLE must yield through
an explicit scheduler/continuation design rather than hold this lock while
waiting for another guest thread. That work belongs to M3.

The signed experiment requests only `com.apple.security.cs.allow-jit`. A separate
probe exercises public `MAP_JIT`, per-thread W^X transitions and ARM instruction
cache synchronization. The dependency's Apple cache/JIT implementation also
contains hardware-specific register inspection; passing on this M2 does not
prove portability across every Apple chip or future macOS release.

## Consequences and next gates

The game-free M1 feasibility acceptance is met by one real ARM candidate,
callbacks, faults, invalidation, signing and a bounded threading design. This
is a provisional reference choice, not a performance endorsement or integration
of the game runtime. See [executed evidence](../M1_VERIFICATION.md).

Before selecting a production implementation on `codex/mac-port-cpu-backend`:

- Validate M2 guest memory, full import ABI signatures and fault recovery, then
  M3 thread/TLS/fiber scheduling against the same execution contract.
- Evaluate useful instruction coverage and performance on representative code;
  compare candidate cores where practical. Tiny fixtures cannot establish AVX,
  all FP/exception semantics or a game's actual requirements.
- Establish supported macOS/chip/signing configurations. Ad-hoc local signing
  does not validate Developer ID, notarization or clean-machine distribution.
- Validate real module loading and gameplay when legally supplied game files
  are available. No game compatibility or FPS claim follows from this decision.
