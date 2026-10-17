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

Guest pages here are software mappings, distinct from Darwin VM permissions.
Host-backed mappings borrow memory whose owner must keep it alive until the
probe is closed. Close is allowed only after all callers have joined. Host code
must not mutate shared backing concurrently with guest execution.

Run JIT tests in a normal macOS process. The Codex tool sandbox on the tested M2
rejects a dependency cache-initialization system-register read with SIGILL;
the same binary passes outside that sandbox. This is a recorded environment
limitation, not permission to disable macOS Hardened Runtime protections.
