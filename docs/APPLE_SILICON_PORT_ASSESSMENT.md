# Apple Silicon port assessment

Assessment date: 2026-10-16. Repository revision: `68c8cf9`.

**Conclusion:** a macOS port is technically plausible, but this is a CPU-execution and graphics-compatibility project, not a recompilation exercise. The repository does not contain the original games' portable source code. It runs their PS4 x86-64 machine code directly. A durable ARM-native runtime needs an x86-64 execution backend as well as a macOS platform layer and renderer adaptations.

This is a source-level feasibility assessment. No game, build, benchmark, or test suite was run. The local host reports arm64 and macOS 26.7.1; its GPU feature set was not measured. All six declared Git submodules are uninitialized in this checkout. No implementation files were changed for this assessment. The older Bloodborne documents describe inherited architecture, not proof that all their claims apply to Uncharted.

## 1. What the project actually does

```text
User-provided decrypted PS4 executables and system modules
    -> Python preparation, relocation and module linking
    -> flat BBPROBE memory image containing x86-64 instructions
    -> C loader maps image, resolves imports and enters guest code
         -> C runtime replaces selected PS4 system services
         -> original PS4 libc and other linked modules still execute as x86-64
         -> shadPS4-derived GNM command processing and shader compiler
         -> SPIR-V + Vulkan rendering
         -> SDL window, input and presentation
```

The repository provides substantial reusable work: executable parsing, import identities, game patches, PS4 service implementations, GPU command decoding, shader translation, caches, input, audio and save handling. It does not provide an x86-to-ARM CPU translator. The shader recompiler translates GPU programs; it cannot translate the game CPU code.

| Area | Evidence in this checkout | Port implication |
|---|---|---|
| Executable preparation | `scripts/prepare.py:33` accepts little-endian x86-64 ELF | Keep the guest format x86-64; compiling the host for ARM does not convert this image |
| Loader | `src/probe.c:11`, `:140`, `:915` onward | Explicit x86-64 build guard, assembly stack entry, generated x86 import traps and direct host-function binding |
| Guest ABI | `src/runtime.h:18` | `sysv_abi` describes the x86 calling convention; it is not an ARM interoperability layer |
| TLS | `scripts/link_modules.py:108`, `src/runtime_thread.c:72` | Rewrites FS loads to GS, then configures Windows TLS or Linux `arch_prctl` |
| Fibers | `src/runtime_fiber.c:91` | Saves x86 registers, stack pointer, MXCSR and x87 control state |
| Memory | `src/runtime_memory.c:1`, `:241`, `:681` | Shared physical backing, aliases, low address requirements and Linux-specific mapping APIs |
| Renderer | `gpu/shadps4/video_core/renderer_vulkan/vk_instance.cpp:266` | Requires particular robustness features, beyond a Vulkan version number |
| Window lifecycle | `gpu/shim/window.cpp:77`, `gpu/shim/bbgpu.cpp:237` | No Cocoa window branch; creates window and pumps events on a worker thread |
| GPU memory tracking | `gpu/shadps4/video_core/page_manager.cpp:44`, `buffer_cache/region_definitions.h:12` | Fixed 4 KB tracking granularity must be reconciled with host protection granularity |
| Launch orchestration | `run.py:20`, `:105` | Win32 process APIs, `.exe` discovery and DLL paths require a macOS path |

## 2. CPU execution is the largest architectural change

### Option A: x86-64 macOS host under Rosetta

Build the loader, runtime, renderer and their application dependencies as x86-64 Mach-O code. Port operating-system calls to Darwin while retaining x86 guest execution and ABI conventions. Test whether dynamically mapped game code, patched TLS accesses, fibers and fault handling behave correctly under Rosetta.

This is a useful bounded feasibility experiment, not demonstrated compatibility. Rosetta runs Mac processes; it does not make this Linux/Windows loader work unchanged or load a PS4 executable by itself. The loader must still prepare and map the game and replace its services.

Rosetta translates an entire process, including dynamically loaded modules. An arm64 executable cannot simply call a mapped x86 function and ask Rosetta to translate that call. A separate x86 process with an ARM renderer would require IPC, shared-memory ownership and synchronization redesign; this project currently passes pointers directly across those boundaries. See [Apple's translation environment documentation](https://developer.apple.com/documentation/apple-silicon/about-the-rosetta-translation-environment).

There is also a lifecycle constraint: Apple says general Rosetta availability extends through macOS 27; macOS 28 restricts it to certain older games. Do not assume this new runtime qualifies. This makes Rosetta a weak foundation for a durable product. See [Apple's current Rosetta support policy](https://support.apple.com/en-us/102527).

**Decision gate:** first prove synthetic mapped-code execution, guest-to-host calls and returns, TLS isolation, cross-thread fiber resumption, executable patch invalidation and recoverable memory faults. Failure here should stop a full Rosetta port until its cause is understood.

### Option B: arm64 host with an integrated x86-64 translator

This is the recommended long-term architecture:

```text
x86-64 game + linked PS4 modules
    -> x86-64-to-AArch64 execution backend
    -> explicit PS4 import/callback bridges
    -> native arm64 runtime and renderer
    -> Vulkan compatibility path through MoltenVK
    -> Metal / Apple GPU
```

Introduce an execution interface for guest entry, host calls, callbacks, thread state, fiber state, faults and code invalidation. Today `runtime_resolve()` yields raw host addresses that the loader writes into guest relocations. In a translated runtime, function imports must instead target translator-recognized bridges. Data imports need separate address and layout handling.

The bridge must preserve integer and floating-point arguments, stack arguments, aggregate returns, varargs where used, guest-visible structures, callback reentry and guest error values. The guest x86 SysV ABI and Apple's arm64 ABI are distinct contracts. Replacing the `ABI` macro is insufficient.

Guest fibers need virtual x86 register/stack state. Rewriting `fiber_swap` in ARM assembly alone would only switch host state; it would not preserve a translated guest correctly. Guest TLS likewise belongs in the virtual CPU/thread model. Preparation must make the current FS-to-GS rewrite an execution-backend policy instead of an unconditional assumption.

CPU correctness includes x86 memory ordering, atomic operations, SIMD, floating-point rounding/denormals, timing instructions, fault restart and self-modifying or patched code. Validate the actual guest instruction set rather than assuming SSE or AVX requirements from the PS4 hardware specification alone. Preserve the game address space when possible because CPU code, GPU descriptors and HLE services share pointers.

Evaluate existing translator cores before writing a new one. [FEX](https://github.com/FEX-Emu/FEX) and [Box64](https://github.com/ptitSeb/box64) are relevant engineering references, but their documented targets are Linux. Neither is an established drop-in macOS backend for this loader. Selection requires a Darwin feasibility spike, embeddability review, callback and fault tests, license review and measured throughput. This assessment does not select a core.

Apple's JIT requirements affect generated ARM code: plan a code-cache allocator with the required permissions, write/execute transitions and instruction-cache synchronization. Keep translated host instructions separate from writable guest data and guest instruction bytes. See [Apple's JIT porting guidance](https://developer.apple.com/documentation/apple-silicon/porting-just-in-time-compilers-to-apple-silicon).

### Other approaches

- **Static binary recompilation:** potentially valuable later for hot blocks or known executables, but indirect calls, relocation, module initialization, patches and callbacks still need runtime support. The preparation scripts currently do not perform this translation.
- **Windows compatibility environment:** an optional external experiment, not a native port. This runtime's low-address mappings, exceptions and Vulkan requirements still need validation; a working launcher would prove little about game execution.
- **Direct Metal renderer:** defer unless a measured MoltenVK limitation makes it necessary. It replaces much more of the renderer and does not solve CPU execution.

## 3. Graphics: reuse the renderer, but add a real compatibility path

There is already a Metal surface branch in `vk_platform.cpp:111`, plus Apple branches in common thread, clock and signal helpers. These are useful inherited pieces, not an operational macOS port. `gpu/shim/window.cpp` selects Windows, X11 or Wayland only, and otherwise aborts.

Move window creation and event pumping to the application main thread, with guest execution on workers. SDL documents this main-thread requirement. Use SDL's Vulkan surface API, or deliberately create and retain the Metal layer required by the existing surface code. Review Vulkan portability enumeration/subset enablement and ICD discovery; the existing Apple path forces `VK_DRIVER_FILES` from the executable directory. See [SDL window threading requirements](https://wiki.libsdl.org/SDL3/SDL_CreateWindow).

**Confirmed source-level mismatch:** this renderer asserts `robustBufferAccess2`, `robustImageAccess2` and `nullDescriptor`. Current upstream MoltenVK source returns false for `robustBufferAccess2` and `nullDescriptor`. Its supported API version alone therefore cannot satisfy this initialization contract. See [MoltenVK feature reporting](https://github.com/KhronosGroup/MoltenVK/blob/main/MoltenVK/MoltenVK/GPUObjects/MVKDevice.mm). These are upstream observations, not a query of an installed MoltenVK binary; pin and inspect the chosen release.

Implement valid fallback resource descriptors for unbound slots, plus correct read, write and atomic bounds behavior wherever the existing renderer relies on robustness. Simply deleting assertions can turn an initialization failure into corruption or GPU faults. Also check whether the chosen release advertises the exact EXT/KHR extension names requested.

Build a feature-and-workload matrix for push descriptors, vertex divisors, storage formats, depth/stencil reinterpretation, descriptor limits, scalar buffer layouts, subgroup operations, integer widths, atomics and synchronization. Some shader capability fallbacks already exist in `profile.h` and `vk_pipeline_cache.cpp:291`; test their actual coverage. Quad/rectangle emulation uses auxiliary tessellation shaders in `vk_graphics_pipeline.cpp`, so a triangle demo is not representative enough.

Start with base rendering, no frame generation and no advanced upscaling. Make the FSR dependencies optional at build time: CMake currently adds and links FSR providers unconditionally. Later assess TAA and FSR 3.1 separately. Exclude the NVIDIA DLSS path from the Apple-GPU target. Do not promise FSR 4 compatibility from general Vulkan support. MoltenVK translates SPIR-V to Metal, which adds another compilation and semantic boundary; see its [runtime guide](https://github.com/KhronosGroup/MoltenVK/blob/main/Docs/MoltenVK_Runtime_UserGuide.md).

## 4. Memory and operating-system work

The guest memory model uses 16 KB pages, but GPU tracking uses 4 KB pages. Separate guest page size, tracking granularity and actual host VM protection granularity. Query the host at runtime, including the translated-process configuration. A write to one tracked subpage must not accidentally release protection for other still-watched subpages sharing a host page. Test read watchers and CPU/GPU readback as well as writes.

Replace Linux `memfd_create`, hole punching and `MAP_FIXED_NOREPLACE` with a designed Darwin backend, preserving shared aliases and zero-on-reuse behavior. Do not substitute destructive `MAP_FIXED` calls without first owning the address range. The runtime explicitly constrains guest-visible addresses below 1 TiB because PS4 GPU descriptors encode 40-bit addresses; allowing arbitrary host mappings is not a valid fallback.

Port crash and recovery handlers from Linux register contexts and `/proc/self/task` inspection. Recheck fault-handler locking and reentrancy: the GPU page tracker intentionally faults on guest memory accesses during normal operation. For translated execution, recover the guest PC and restart the correct translated access.

Audit pthread attributes, timed waits, thread identity/affinity, restart logic and networking dependencies. `bbnet` is built unconditionally even though it includes inherited Bloodborne co-op functionality; inventory Uncharted imports before deciding which services are necessary. Preserve guest ABI structures and error codes while replacing host APIs.

Replace unconditional x86 timing intrinsics in `runtime_kernel.c`, `bbport_write_log.cpp`, `vk_draw_pipe.h` and `vk_bind_helper.h`. Distinguish guest-visible TSC semantics from host profiling clocks. Existing architecture guards and portable implementations should be reused where present; not every occurrence of an x86 header is an unconditional blocker.

## 5. Build, launcher and packaging

`build.sh` handles Windows specially and treats other systems as Linux. The latter path expects `.so` libraries, ELF `$ORIGIN` rpaths and Linux linker behavior. `gpu/CMakeLists.txt:66` requires X11 on every non-Windows platform, and `:221` applies a GNU shared-library linker option. Inline assembly also needs Mach-O symbol/section conventions, including the embedded font in `bbport_overlay.cpp`.

Add explicit Darwin targets with architecture-specific dependency trees. Prefer a shared CMake target model for loader, runtime and renderer over extending the shell compilation command. Review how runtime symbols resolve from the renderer; static linkage or an explicit runtime interface is cleaner than assuming Linux shared-library resolution. Audit C++23 library availability with the selected compiler.

Create a CPU test target that does not pull in Vulkan, FSR, networking and the entire renderer. Existing `tests/test_probe.py` synthetic x86 fixtures and TLS/module tests are valuable starting points, but their 4 KB image layouts need page-size-aware handling. They are not currently ARM execution tests.

Keep Python preparation tools where possible. Port `run.py` process management and executable discovery, preserving the Uncharted-specific per-game settings. Do not substitute the older Bloodborne launcher path without checking behavior. Package a macOS app with runtime libraries under appropriate rpaths, user-writable saves/cache locations, controller permissions as applicable, signing and JIT entitlements. Packaging follows correctness, rather than preceding it.

## 6. Recommended implementation sequence and acceptance gates

| Stage | Deliverable | Required evidence before continuing |
|---|---|---|
| 0: reproducible baseline | Pin submodules, record compiler/dependencies and game version/hash; establish Windows or Linux baseline | One known-good scene and logs, with inherited defects recorded |
| 1: independent feasibility probes | Minimal Mac CPU execution harness and independent SDL/MoltenVK capability/render harness | Guest call/return, callbacks, TLS, fibers, faults and low mappings; queried features and representative shaders |
| 2: select CPU strategy | Choose short-lived Rosetta experiment or translator integration based on stage 1 | Written scope, measured limitations, supported OS range; no assumption that either route already works |
| 3: Darwin runtime | Memory, threading, files, timing, signals and execution bridge | Synthetic runtime tests, alias/coherency tests, concurrency stress and correct guest errors |
| 4: base renderer | Cocoa/main-thread lifecycle, portability setup and robustness substitutes | Representative graphics/compute, quad/rect paths, buffer faults, readback and image comparisons |
| 5: one game end to end | Start with one exact U1 dump/version and conservative settings | Menu, intro, playable scene, audio, input, save/reload and stable streaming |
| 6: collection support | U2/U3, executable switching, multiple scenes and versions | Per-game regression coverage, long sessions, memory-pressure and shutdown tests |
| 7: performance and distribution | Profiling, optional TAA/FSR, signed app bundle | CPU/GPU frame times, peak memory, thermal behavior and clean-machine installation |

Stage 1 is deliberately split: a CPU-only success cannot prove graphics compatibility, and a rendering demo cannot prove guest execution. Run implementation work in small reviewable changes that retain the x86 Windows/Linux baseline.

Record exact SoC, unified-memory capacity, macOS version, translator revision and MoltenVK revision for results. Memory use includes the guest pool, GPU caches, decoded assets and translated code; the README's discrete-GPU VRAM guidance cannot directly be converted into a Mac RAM requirement. No credible FPS target or minimum Mac can be stated from source inspection alone.

## 7. Effort and recommendation

These are planning judgments, not delivery commitments: budget an initial one-to-two-week feasibility investigation for someone experienced in systems and graphics work. A passing probe is not a playable port. Even the Rosetta route could take months once memory tracking and renderer semantics are included. An integrated native ARM execution backend is a many-month effort, potentially longer, with specialist CPU-translation and graphics expertise required. Re-estimate only after the first execution and renderer gates.

**Recommended next work:** implement the two isolated feasibility harnesses, with a durable arm64 runtime as the architectural target and a tightly bounded Rosetta experiment if short-term playability matters. Reuse the current GNM/shader pipeline through MoltenVK initially. Do not begin with a full Metal rewrite or a promise that changing build flags will boot the games.
