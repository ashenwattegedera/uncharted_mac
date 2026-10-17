# M0 execution boundary

`src/execution.h` defines the boundary introduced before translator selection.
`src/runtime_execution.c` implements a native-x86 reference adapter and an
unavailable backend for the ARM harness. Selection happens at build time; the
interface does not introduce mutable process-global backend registration.

## Contracts

- `bb_execution_info()` exposes whether guest execution is available.
- `bb_execution_invoke()` accepts an explicit call shape, guest target, up to
  three pointer/integer arguments and an optional entry stack. It updates the
  result only on success. Module initialization preserves signed `int` returns.
- `bb_execution_bind_import()` distinguishes functions, data and the existing
  Microsoft-ABI game hooks. Native x86 retains identity addresses. ARM refuses
  bindings rather than leaking arm64 host function addresses into guest code.
- `bb_execution_invalidate()` validates the range and invalidates instruction
  caches in the native reference adapter. It is an integration point for a future
  translator's code cache, not that cache's implementation.
- Convenience calls stop with exit status 25 and a backend diagnostic on failure.

Validation covers request shape and range arithmetic. It does not prove that a
nonzero native target points to executable code; the loader retains that check.
Callers must supply the appropriate signature and an adequately sized/aligned
stack where used. The current native adapter preserves the original Linux stack
entry assembly and Windows direct-entry behavior.

## Routed transitions

The C loader now binds import relocation targets, unresolved-import handlers,
the process finalizer and game-hook host addresses through this boundary. It
enters the game and linked-module initializers through explicit call shapes and
invalidates the prepared image/trap ranges before execution.

The C runtime routes guest thread entry, fiber entry, `pthread_once` callbacks,
TLS key destructors and process finalization callbacks through the adapter.
The portable core's finalization fixture deliberately invokes registered **host**
callbacks directly. That permits ARM host-contract tests while the ARM guest
backend remains unavailable. It is limited to the core test build profile.

## Deferred work

This is an M0 boundary, not a translator implementation or complete foreign-ABI
marshaller. Function/data/MS-hook categories do not encode the full signatures of
hundreds of PS4 imports. An actual signature registry, FP/aggregate/vararg
marshalling, guest address translation and bridge generation remain M1–M2 work.

Guest TLS, register/FP state, fiber suspension/migration and faults still use the
existing native runtime implementations. Integrating virtual CPU state and
backend-aware fault restart is M1–M3 work. Graphics-side and third-party-library
callbacks also require inventory and integration before those components can run
on ARM. Runtime patches after initial preparation need translator-aware
invalidation when a translator is integrated; the native x86 path remains coherent
under its existing behavior.

The native adapter tests cover every implemented call shape, import identity,
nested callback reentry, signed returns and (on POSIX) stack switching. ARM tests
cover refusal to invoke/bind/invalidate and preserve output sentinels on failure.
See [M0 verification](M0_VERIFICATION.md) for the platforms actually executed.

## M2 opt-in typed bridge

`src/guest_abi.h` now describes supported SysV values and validated memory
providers. It handles independent register banks, stack overflow arguments,
explicit aggregate classes, structure returns and guest variadic state.
`bb_probe_bind_abi` adapts it to M1's reference engine; `memory_probe.h` adds
owned Darwin mappings and a memory-aware binding entry point. These are opt-in
fixtures and reusable modules, not a replacement for `bb_execution_bind_import`
or an audited production registry. The production ARM binding refusal remains.

See [M2 verification](M2_VERIFICATION.md) for supported classes, fault restart,
known sanitizer limits and remaining per-service/renderer integration.
