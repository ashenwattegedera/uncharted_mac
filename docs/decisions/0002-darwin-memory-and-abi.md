# ADR 0002: owned Darwin windows and explicit guest ABI metadata

Date: 2026-10-17. Status: accepted for the game-free M2 foundations.

## Memory and ownership

Use `src/guest_memory.h` with the Darwin implementation under `src/platform`.
Each object owns a bounded address reservation and a private file descriptor for
shared backing. The temporary file is unlinked immediately; its descriptor is
close-on-exec. Views at different guest addresses share those bytes, rather than
copy them. The creating thread owns mutation, observation and destruction.
Other threads receive `BB_VM_WRONG_THREAD`; this is not a concurrent VM service.

Reserve using `mach_vm_allocate` with `VM_FLAGS_FIXED` **without**
`VM_FLAGS_OVERWRITE`. This claims free space without replacing an existing map.
Search sixty aligned candidates below 1 TiB, or require the supplied exact
address. All later `MAP_FIXED` replacements are range-checked inside that owned
reservation. Unmapping a view restores an inaccessible reservation. Closing
releases the entire window and backing. Storage is cleared on reuse only when
its last alias has been released. New aliases of live storage preserve bytes.

The implementation bounds both window and backing to 256 MiB. This deliberately
proves ownership and alias semantics before implementing the existing runtime's
multi-GiB physical pool. It does not reserve the whole PS4 address range or
establish that every real executable can use identity addresses on Darwin.

Track guest pages (16 KiB), queried host pages (16 KiB on the tested M2) and
tracking subpages (4 KiB) separately. Mappings/protection changes use the larger
host/guest granule. Write-watch counts belong to physical backing subpages and
therefore affect every alias. Any watched subpage removes host-page write access;
per-alias guest permissions still restrict that alias independently. Removing
one watcher cannot remove a different subpage's protection. Guest execute
permission is mirrored to the translator; backing bytes are never made native
ARM executable by this module.

Observers mirror committed changes into the reference CPU. An observer failure
poisons subsequent operations; callers must tear down rather than execute a
partially synchronized object. Observers may query but cannot mutate or close
the object while notification is active. This bounds error handling without
claiming transactional rollback of arbitrary external subscribers.

## ABI and imports

Use `src/guest_abi.h` for explicit x86-64 SysV signatures. Integer and SSE register
banks are allocated independently. Whole-aggregate assignments revert to the
stack on exhaustion. Hidden memory returns consume the first integer argument
register. Supported aggregate classes are explicitly declared II, SS, IS, SI
(two eightbytes) and a 24-byte memory aggregate. Unsupported vector/x87/complex
classes are absent and must not be represented using a convenient scalar type.

Host adapters receive copied values and invoke normally typed ARM functions.
Guest pointers remain 64-bit guest addresses until checked through a memory
provider. No ARM function address becomes a relocation target. The reference
probe binds signatures to guest bridge slots and preserves nested contexts.
Memory sessions supply a provider that checks owned views or the probe's stack,
and publishes writes to every code alias. Import metadata/user state must
outlive the binding; names must be unique in the supplied registry.

Declared variadic arguments require promoted types and a valid SysV `%al` vector
count. `bb_abi_va_read` separately reads a guest register-save/overflow area,
with checked offsets and output/state preservation on failure. The existing
name inventory includes `vsnprintf`, `snprintf` and `snprintf_s`; neither a name
nor a guest `va_list` proves a complete formatting adapter. Format parsing,
pointer/string/output validation and per-service wrappers remain required.
No Darwin `va_list` is passed off as a guest one.

## Fault routing and code invalidation

Recover guest memory faults in ordinary thread context after emulation stops.
Capture guest RIP, address, size and access; after accepted recovery explicitly
retry that instruction. The exactly-once `lock xadd` fixture guards against a
skipped or repeated atomic store. Recovery and block resumptions share the
probe's existing sixty-four-segment bound.

For write-watch faults, check guest permission first. A GPU synchronization
callback must acknowledge **every** watched 4 KiB subpage in each affected host
page before any counts are cleared and aliases become writable. Rejected
callbacks retain all counts; callbacks must tolerate repeated acknowledgment
attempts if another callback rejects the batch. Out-of-window addresses, real
read-only violations and unresolved mappings remain faults. Router reentry and
session destruction during execution are rejected.

Host writes invalidate mirrored aliases immediately. Translated writes queue
cache synchronization until emulation exits or before the next translated block
executes. The current reference adapter conservatively flushes all mapped pages;
this is an intentionally expensive correctness experiment. Arbitrary alias
self-modification of later instructions in the same active block is not proven.
A production backend must resolve that case and avoid broad per-write flushing.

This router is not a Darwin SIGSEGV/Mach-exception handler. It allocates, queries
metadata and invokes callbacks; none of those operations may be moved into an
async signal handler. The existing renderer's signal registry/protection hooks
are untouched. A synthetic GPU callback proves the contract, not renderer
integration, GPU coherency, or signal-safe recovery of arbitrary ARM host code.

## Sources and acceptance boundary

The address-reservation decision follows Apple's
[`mach_vm_allocate` API](https://developer.apple.com/documentation/kernel/1402376-mach_vm_allocate),
[XNU VM flag definitions](https://github.com/apple-oss-distributions/xnu/blob/main/osfmk/mach/vm_statistics.h),
and the documented [mapping API](https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man2/mmap.2.html).
ABI rules follow the [x86-64 psABI project](https://gitlab.com/x86-psABIs/x86-64-ABI)
and compiler-generated freestanding SysV callers. Unicorn's
[cache synchronization guidance](https://github.com/unicorn-engine/unicorn/blob/8028ec436f2d9376525352dd38ed9ed6b9f6be10/docs/FAQ.md)
informs the explicit cache boundary.

These foundations are opt-in and have synthetic acceptance tests. Production
runtime/renderer wiring, complete import metadata and the game's actual ABI
remain open M2 integration work; M3 scheduling and game boot are separate gates.
See [M2 verification](../M2_VERIFICATION.md) for executed evidence and limitations.
