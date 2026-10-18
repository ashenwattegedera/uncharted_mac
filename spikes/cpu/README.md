# M1 native ARM CPU experiment

This independent CMake project runs synthetic x86-64 code in a native arm64 macOS
process. It does not replace the default execution backend or load PS4 game files.
It translates through Unicorn's QEMU-derived TCG engine and provides a narrow
host-call bridge using M0's `BbGuestCall` call descriptions.

Requirements: native Apple Silicon macOS, Clang, CMake 3.24+, Ninja and pkg-config.
No global Unicorn installation is required. Configuring downloads the pinned
source archive below; only its x86 target and static libraries are built locally.

```bash
cmake -S spikes/cpu -B out/m1-cpu -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build out/m1-cpu --parallel 4
ctest --test-dir out/m1-cpu --output-on-failure
```

The dependency is Unicorn 2.1.4, commit
`8028ec436f2d9376525352dd38ed9ed6b9f6be10`, with archive SHA-256
`9d2a2003f0307d0fc239af439654564fe6ef17b67f7c5a39b45fad4c69d6aec2`.
Its [source](https://github.com/unicorn-engine/unicorn/tree/8028ec436f2d9376525352dd38ed9ed6b9f6be10)
includes GPLv2 `COPYING` and per-file notices for QEMU-derived/LGPL components.
Keep those notices and corresponding-source obligations with any distribution.
The parent project is GPLv2-or-later; this experiment does not import proprietary
components or bind a Linux userspace into the process. No upstream files are patched.

For offline builds after obtaining and verifying that exact source tree, CMake
also accepts `-DFETCHCONTENT_SOURCE_DIR_UNICORN=/absolute/path/to/source`. That
override bypasses archive downloading/hashing; the developer is responsible for
matching the pinned revision. Normal CI uses the verified archive URL.

The probe serializes one engine across host callers. Recursive callbacks run
outside the JIT and preserve CPU state with separate stack slices. It is bounded
to eight nested calls, sixteen bridges, sixty-four bridge resumptions per call,
and an instruction/time budget per emulation segment. It deliberately does not
implement concurrent guest CPUs, a PS4 VM, real import signatures or blocking HLE.
Guest `syscall` instructions stop with an unsupported-service result; no host
system call is made on the guest's behalf. Permission changes invalidate cached
blocks before subsequent execution.

CTest covers scalar/control flow, SSE2/double precision/MXCSR, x87, nested host
callbacks with register/flag/SIMD preservation, recoverable/protected guest memory,
host and guest code modification, serialized atomic accesses through aliases,
and limits/unsupported instructions. A second suite runs in an ad-hoc signed
Hardened Runtime executable with only `com.apple.security.cs.allow-jit` enabled.
The separate signed JIT-policy probe verifies public `MAP_JIT`, thread W^X
transitions and instruction-cache invalidation using tiny ARM instructions.

`out/m1-cpu/cpu-probe-suite benchmark` prints JSON for a bounded arithmetic loop
and 100 entry/bridge calls. It includes context-save, watchdog, instruction-budget
and bridge overhead. These numbers have no pass threshold and are not game FPS.

Guest pages here are software mappings, distinct from Darwin VM permissions.
Host-backed mappings borrow memory whose owner must keep it alive until the
probe is closed. Close is allowed only after all callers have joined. Host code
must not mutate shared backing concurrently with guest execution.

Run JIT tests in a normal macOS process. The Codex tool sandbox on the tested M2
rejects a dependency cache-initialization system-register read with SIGILL;
the same binary passes outside that sandbox. This is a recorded environment
limitation, not permission to disable macOS Hardened Runtime protections.

## M2 extension

The same build now has 16 CTest checks, adding ordinary/signed compiler-generated
SysV ABI callers and owned Darwin memory/fault integration. Python 3.10+ and
Clang's x86 object-emission support are needed to regenerate the caller fixture.
No x86 executable runs directly on the host. Root CMake also exposes the reusable
ABI and Darwin memory modules without a Unicorn dependency.

For project-owned probe code, add `-DBB_PROBE_SANITIZER=undefined` to a separate
Debug build. `address,undefined` is also accepted, but the low-address memory
cases cannot reserve their windows under ASan on the tested M2. Vendor-wide
UBSan reports an upstream TCG initialization diagnostic; project-scoped results
do not validate vendor sanitizer cleanliness. See the full
[M2 verification record](../../docs/M2_VERIFICATION.md).

`BbMemoryProbe` owns its engine and Darwin window. Use its invoke/bind APIs;
borrowed CPU/VM/router accessors must not be used to replace its observer, destroy
its resources or mutate raw backing. Keep all operations on its creating thread.
Its GPU callback is an explicit synchronization contract exercised by fixtures,
not a renderer or async-signal handler integration.

## M3 integration

The root `BB_BUILD_TRANSLATED_M2=ON` compatibility profile now adds resumable CPU
contexts, leased worker execution, virtual fibers and translated linked-module
boot. The standalone spike remains independently buildable. See
[the M3 verification record](../../docs/M3_VERIFICATION.md) for synthetic coverage,
ordering guarantees, source/profile versions and explicit production gaps.
