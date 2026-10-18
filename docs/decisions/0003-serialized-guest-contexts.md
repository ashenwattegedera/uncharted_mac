# ADR 0003: serialized guest contexts and versioned translated images

Date: 2026-10-18. Status: accepted for the M3 game-free correctness reference.

Use one shared Unicorn engine with opaque CPU contexts and guest-owned stacks.
Guest threads, fibers and module callbacks must not depend on ARM host stacks,
host TLS or native x86 assembly. Preserve complete vendor CPU snapshots; update
FS when a fiber migrates to another guest thread. Main-thread module calls retain
TLS and CPU control state across initialization boundaries.

Native workers serialize instruction execution and HLE. The scheduler mutex and
explicit Darwin VM lease establish ownership and visibility at each handoff.
Publish a suspended fiber as idle only after its CPU state is saved. A lease
cannot be stolen; poisoned leases grant teardown ownership without execution.
Normal fault handling leaves emulation before mappings or virtual CPU state are
changed. This extends ADR 0002's initial fixed-owner rule without enabling
concurrent VM mutation or signal-handler access.

Serial execution gives a stronger reference ordering than x86 TSO for participants
using the lease. It does not establish a parallel x86-on-ARM memory model or make
independent renderer/host memory access safe. Production concurrency must resolve
that contract explicitly rather than remove the serialization opportunistically.
Instruction quanta and a slice budget bound the reference scheduler; guest mutex,
condition and join waits suspend guest state instead of blocking the host CPU.

Translated preparation retains original x86 FS loads; native preparation keeps
its GS rewrite. Backend/policy-versioned artifacts, checked header capabilities
and provenance fingerprints prevent cross-policy cache reuse. Keep guest TSC
state separate from host profiling clocks. The reference uses a deterministic
logical counter, not a calibrated real-time simulation.

The module reader accepts only the declared translated BBPROBE5 subset. Validate
metadata, names, fallback guest exports and relocations before committing maps.
Use separate low guest image storage within an owned non-identity VM window;
reserve CPU scratch stacks outside that range. Initialize linked modules in table
order using the same main guest thread, then execute registered cleanup in reverse
order. Retire continuations before unmapping. Unknown contracts fail with their
name and guest location; native host addresses never become guest targets.

Apply translator-aware guest patches by checking expected bytes, owning the write,
invalidating code and restoring permissions. Actual game hooks need instruction
and ABI evidence before use. Keep native MS-ABI hook refusal intact.

[M3 verification](../M3_VERIFICATION.md) defines the implemented subset, executed
fixtures and remaining implementation/real-input gates. This decision does not
select Unicorn for production, promise parallel TSO, certify all imports or claim
that the game, linked shipped libc, renderer or distribution package works.
