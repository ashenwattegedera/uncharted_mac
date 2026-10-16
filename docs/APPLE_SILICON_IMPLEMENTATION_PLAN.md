# Apple Silicon implementation plan — Option B

Date: 2026-10-16. Baseline: `68c8cf9` (current local `main` and `origin/main` at planning time).

**Decision:** build a native arm64 macOS host with an integrated x86-64 execution backend. Retain the PS4 service layer and shadPS4-derived graphics pipeline, initially using MoltenVK for Metal access. Rosetta is not a product dependency or the implementation strategy.

Status: **planning complete; implementation not started; translator not selected**. The [source assessment](APPLE_SILICON_PORT_ASSESSMENT.md) records the evidence, compatibility risks and external references behind this plan. A compilable harness, a successful game boot and playable performance are separate milestones.

## 1. Scope and initial acceptance target

The first end-to-end target is one recorded U1 dump/version: launch, menu, intro, a playable scene, audio, input, save/reload and clean shutdown. Choose the exact version when a user-provided dump is available; record executable and module hashes privately with the test results. Do not commit game binaries, extracted assets, saves or proprietary shader captures.

Use base rendering with advanced upscaling and frame generation disabled. U2/U3, executable switching, optimized rendering and distribution follow the U1 milestone. Unsupported imports, instructions or GPU features must report actionable failures rather than claim success through dummy results.

Preserve working x86-64 Windows/Linux behavior throughout. Do not change their default execution backend. A native arm64 host still executes translated x86-64 game instructions; this is not a source recompilation of the games.

## 2. Branching and Git workflow

Current topology after this planning change is committed:

```text
main                         68c8cf9  (existing baseline)
  └─ mac-port                68c8cf9  (integration branch)
       └─ codex/mac-port-plan         (assessment + implementation plan)
```

`mac-port` is the exact integration-branch name requested. Working branches use the repository session's `codex/` prefix. Future implementation branches start from the latest integrated `mac-port`, not from arbitrary sibling work.

| Branch | Purpose | Merge target |
|---|---|---|
| `main` | Preserve the existing release lineage | Only a separately reviewed release/upstream change |
| `mac-port` | Integrate validated Mac-port increments | Eventually a reviewed proposal to the chosen mainline |
| `codex/mac-port-plan` | This plan and source assessment | `mac-port` |
| `codex/mac-port-<scope>` | One bounded implementation/review unit | `mac-port` |
| `codex/spike-mac-<scope>` | Disposable feasibility experiment with recorded conclusions | Integrate only reviewed production code or findings |

Workflow for each increment:

1. Check status and preserve unrelated work. Update the local integration branch with a fast-forward from its tracking branch when one exists. Create the feature branch from that base.
2. Implement one coherent change, including its diagnostics and relevant tests. Stage explicit paths and inspect the staged diff; avoid indiscriminate `git add .`.
3. Commit small, meaningful units with imperative messages such as `build: isolate CPU runtime test target` or `feat(cpu): bridge guest imports through execution backend`.
4. Review the diff against `mac-port`; record the behavior change, test evidence and known limitations. All feature PRs target `mac-port`.
5. Before integration, update against the latest target and rerun checks affected by conflicts or changes. Rebase only an unpublished, privately owned branch. For shared branches, merge the integration branch; never rewrite collaborators' history.
6. Integrate with a merge commit (`--no-ff`) to retain the feature boundary and its focused commits. Delete the merged feature branch after confirming integration. Do not combine squash and merge policies unpredictably.
7. Revert a faulty integrated feature using a new revert commit, preserving shared history. Keep `mac-port` buildable for targets declared supported at that stage; incomplete ARM features stay opt-in.

No direct implementation commits to `mac-port` or `main`. No force-pushes to either. No blanket resets, cleans, vendored-code formatting or unrelated dependency upgrades. Prefer an explicit merge when bringing newer upstream `main` changes into `mac-port`, followed by regression checks; do not rebase a published integration branch.

The only configured remote currently points to `Supermedo/uncharted_pc`. Its write permissions and intended contribution destination have not been established. Branches and commits in this task stay local. Before publishing, identify the user's fork or authorized remote; preserve the existing remote configuration until then. Remote branch protection and required checks are recommendations to configure when a destination exists, not settings already applied.

The planning branch should be reviewed and merged into `mac-port` before starting implementation branches. Do not create all planned branches now: they would immediately become stale. Branch names below are proposed scopes, not pre-created branches.

## 3. Architecture boundaries

Introduce boundaries incrementally; names below express responsibilities, not a mandate for a large framework or premature file hierarchy.

| Boundary | Contract and ownership |
|---|---|
| Execution backend | Enter/resume guest code; create/destroy guest CPU state; deliver callbacks; invalidate translated code; report faults and unsupported instructions |
| Guest ABI bridge | Decode x86 SysV arguments and returns; convert to typed host calls; preserve guest layouts, varargs where needed and nested callbacks |
| Guest thread/fiber state | Own virtual registers, flags, SIMD/FP state, TLS base, guest stack and suspended continuation; distinguish these from host thread state |
| Guest address space | Reserve/map/protect/alias guest memory; translate or validate guest addresses; publish mapping and invalidation events to execution and GPU backends |
| Host platform | Darwin or existing Windows/Linux implementations of VM, clocks, threads, files, faults, executable allocation and process lifecycle |
| GPU compatibility profile | Query exact device features and select tested shader/resource fallbacks; expose failures without pretending unsupported features exist |
| Application lifecycle | Main-thread SDL window/event ownership; worker guest execution; ordered startup, stop, join and resource teardown |

Keep the existing native-x86 backend as the reference implementation. Route guest entry and imported function/callback transitions through the new interfaces before adding translation. Avoid placing CPU-backend-specific conditionals in every PS4 API implementation.

Function imports must resolve to guest-callable bridge entries, not raw arm64 function pointers. Data imports use explicit guest-visible storage and layout validation. Host pointers must not leak into guest descriptors without a deliberate mapping contract. Direct identity mappings are desirable for this codebase but must be proven on macOS, not assumed.

Define an import signature registry from the actual exported functions, including GPU imports. Use explicit metadata for argument/return classes and callbacks; NID names alone do not encode a signature. If code generation becomes useful, generate from this single source and verify the generated outputs instead of maintaining duplicate signatures.

## 4. Ordered implementation backlog

### M0 — Reproducible baseline and test isolation

**Branches:** `codex/mac-port-build-harness`, then `codex/mac-port-execution-interface`.

- Initialize the pinned submodules and record toolchain versions. Preserve submodule commit IDs; make existing FSR patch application reproducible and inspect its expected dirty state.
- Add coherent CMake targets for the loader/runtime and synthetic tests while keeping existing entry scripts functional. Separate graphics and networking registration from CPU-only tests.
- Introduce explicit host/backend selection. Unsupported combinations fail at configure time with a clear diagnostic. ARM CPU harnesses must not require FSR, SDL video, Vulkan or the full renderer.
- Make required tests fail when their binaries were not built. Existing Python native tests can silently skip missing executables; distinguish these skips from successful coverage.
- Extract guest entry/import/callback contracts with a native-x86 adapter. Preserve current ABI behavior before integrating a translator.

**Acceptance:** Windows/Linux reference builds and relevant loader/runtime tests pass; Python preparation/linking tests pass; an arm64 macOS CPU harness configures and builds without executing x86 guest code yet. Capture baseline logs when a supported machine is available; if unavailable, explicitly retain that gate as unverified.

### M1 — Translator selection on Darwin

**Branches:** `codex/spike-mac-cpu-backend`, followed by `codex/mac-port-cpu-backend` for the selected implementation. Depends on M0.

- Evaluate candidate cores for Darwin/arm64 viability, embedding API, x86-64 instruction coverage, license compatibility, maintenance and integration cost. FEX and Box64 are candidates for investigation, not selected dependencies.
- Use tiny synthetic x86 programs to exercise arithmetic, branches, indirect calls, SIMD/FP state, stack operations, atomics and guest-to-host-to-guest calls.
- Prove code-cache allocation and write/execute transitions under the intended macOS signing model. Prove instruction-cache synchronization and guest-code invalidation.
- Prove that the selected core can use this runtime's service layer instead of requiring an incompatible Linux application environment.
- Test instruction faults and memory faults with recoverable guest-PC attribution. Measure throughput and transition overhead; do not extrapolate game FPS from a microbenchmark.
- Record an architecture decision with candidate revisions, experiments, license conclusions, rejected alternatives and remaining limitations. Pin the selected revision.

**Acceptance:** one candidate executes the fixture suite on real Apple Silicon, survives callbacks and faults, and has a credible threading/memory-ordering design. If no candidate passes, stop production integration and re-estimate the missing backend work; do not silently substitute Rosetta.

### M2 — Darwin memory and guest/host ABI

**Branches:** `codex/mac-port-darwin-memory`, `codex/mac-port-guest-abi`, `codex/mac-port-faults`. Depends on M0; final CPU integration depends on M1.

- Implement owned guest-address reservations, low-address allocation, aliasing backing storage, protection changes, zero-on-reuse and mapping teardown for Darwin.
- Separate guest, host and GPU-tracker page granularities. Preserve watcher counts across subpages sharing one host page.
- Replace direct ARM host-function relocation targets with signature-aware guest bridges. Cover scalar, FP, stack-passed and aggregate values, plus varargs actually used by imports.
- Bridge nested callbacks and data exports. Validate guest-visible structure sizes/offsets and error translations with static assertions and fixtures.
- Connect fault routing to both GPU tracking and translator state; preserve legitimate recovery without swallowing actual guest failures. Audit locking and async-signal safety.

**Acceptance:** aliases observe identical bytes; independently watched subpages remain coherent; unsafe/unowned fixed mappings are rejected; all ABI fixture classes round-trip correctly; a fault reports and resumes the correct guest instruction where recovery is intended.

### M3 — Threads, TLS, fibers and guest modules

**Branches:** `codex/mac-port-guest-threads`, `codex/mac-port-guest-fibers`, `codex/mac-port-module-boot`. Depends on M1–M2.

- Represent guest TLS and FP control state in the execution backend. Make preparation's FS-to-GS rewriting conditional on execution mode and version its cached outputs.
- Integrate translated thread creation, joins, TLS destructors and callbacks. Preserve synchronization semantics, including x86 ordering and atomic accesses shared with host code.
- Implement fiber suspend/resume using complete virtual x86 state, including migration between host workers. Do not merely rewrite the existing assembly into ARM instructions.
- Translate linked PS4 libc/modules through the same backend; preserve initialization order, bindings, cleanup and module TLS.
- Turn game-specific x86 hooks into translator-aware guest patches or bridge traps. Apply code invalidation to changed instructions and include backend identity in relevant caches.
- Separate host profiling clocks from guest-visible TSC behavior.

**Acceptance:** multithreaded TLS isolation, repeated cross-thread fiber migration, nested callbacks, contention and FP-state fixtures pass. A linked synthetic module initializes and calls imports correctly; guest boot reaches named imports with useful diagnostics.

### M4 — macOS graphics feasibility and base renderer

**Branches:** `codex/mac-port-gpu-probe`, `codex/mac-port-window-lifecycle`, `codex/mac-port-gpu-robustness`, `codex/mac-port-gpu-coherency`.

The standalone probe can begin after M0 without waiting for CPU translation. Integration with guest memory depends on M2. These are independent workstreams, not a requirement to delegate to multiple agents.

- Build a small native arm64 SDL/MoltenVK probe. Record the exact API, extensions, limits, formats and feature bits from the target hardware and pinned MoltenVK release.
- Move window creation and event pumping to the main thread, and expose orderly worker startup/shutdown. Wire the surface and portability enumeration/subset requirements deliberately.
- Make FSR/DLSS and unrelated network paths optional build dependencies for the probe/base renderer.
- Implement valid resources for missing descriptors and semantic bounds handling for loads, stores and atomics where robustness was assumed. Do not merely remove required-feature assertions.
- Validate representative shader workloads: graphics, compute, quad/rectangle tessellation, subgroup operations, buffer layouts, depth/stencil and image reinterpretation. Test feature fallbacks already present before introducing new ones.
- Integrate page tracking, CPU writes, GPU readback, aliases and synchronization. Add a simple diagnostic/reference path where needed to distinguish a tracking bug from a rendering bug.

**Acceptance:** queried capabilities are saved as test evidence; unsupported paths fail clearly; representative images/readbacks match references within stated tolerances; validation finds no unexplained errors; protected-page writes and concurrent readback remain correct.

### M5 — U1 end-to-end vertical slice

**Branches:** `codex/mac-port-u1-boot`, `codex/mac-port-u1-playable`. Depends on M3 and M4.

- Integrate startup, game-directory selection, preparation cache identity and runtime configuration without Windows process APIs.
- Implement missing required imports based on actual failures and traces; avoid speculative broad emulation.
- Validate audio decoding/output, controller and keyboard input, video playback, files, saves and shutdown. Fix regressions at the responsible boundary.
- Start with conservative rendering settings. Record CPU translation time, ABI bridge time, GPU time, shader compilation, memory use and stalls independently.

**Acceptance:** the recorded U1 version reaches menu and gameplay, completes a repeatable scene, saves/reloads and shuts down. Repeat a 30-minute session as an initial stability gate; this is not evidence of full-game compatibility. Compare with the recorded x86 baseline and distinguish inherited defects from new ones.

### M6 — Collection support and performance

**Branches:** scoped `codex/mac-port-u2-*`, `codex/mac-port-u3-*`, `codex/mac-port-perf-*` branches. Depends on M5.

- Support executable switching and restart without losing configuration or corrupting saves. Validate U2 and U3 individually, including streaming-heavy scenes.
- Profile before optimizing. Prioritize translation hot paths, bridge overhead, memory tracking and excessive copies according to measurements.
- Add TAA and then evaluate FSR 3.1 in separate changes. Keep frame generation and advanced upscalers out of the minimum supported feature set until independently validated.
- Test several SoC/memory configurations and sustained thermal behavior before declaring hardware requirements or FPS expectations.

**Acceptance:** per-game regression matrix, repeatable performance measurements and documented supported combinations. Performance changes must preserve fixture correctness and scene output.

### M7 — Distribution and supportability

**Branches:** `codex/mac-port-packaging`, `codex/mac-port-release-validation`. Depends on M5; collection-wide claims depend on M6.

- Produce an arm64 app bundle with correct library rpaths, writable Application Support/cache locations, dependency notices and actionable diagnostics.
- Establish minimum macOS/toolchain versions from the selected translator and MoltenVK requirements. Configure appropriate signing/JIT entitlements and test the signed build, not only an unsigned development executable.
- Keep caches versioned by game/module identity, backend revision and graphics compatibility profile as relevant. Recover safely from stale caches.
- Test installation, launch, save retention, restart and removal on a clean Mac. Document limitations and how to collect logs without uploading proprietary content.

**Acceptance:** a reproducible signed release candidate runs on a clean supported machine and passes the declared support matrix. Signing, notarization and publishing credentials are a release-stage dependency, not required to start local development.

## 5. Validation and coding standards

| Change type | Required evidence |
|---|---|
| Preparation/linking | Python fixture tests, malformed-image rejection, backend-specific TLS and cache-version tests |
| ABI/execution | Hand-auditable synthetic guest programs, differential results against native x86 where possible, all relevant register and FP state checks |
| Memory/faults | Aliasing, boundaries, permission changes, invalidation, watcher aggregation, concurrent access and fault restart tests |
| Runtime services | Guest error codes/layouts, lifecycle and failure cases; sanitizers on compatible host-only targets |
| GPU | Feature logs, Vulkan/Metal validation as applicable, representative image/readback comparisons and coherency tests |
| Integration | Existing Windows/Linux build/tests plus macOS jobs appropriate to the stage; separately identified manual game coverage |

Use real ARM Mac hardware for execution tests. Cross-compilation alone cannot pass a runtime gate. Do not count skipped tests as passing backend coverage. Keep CI game-free; proprietary-content tests run locally with recorded inputs and outcomes. Add CI configuration when the targets exist, using available Linux/Windows runners and a confirmed real arm64 macOS runner rather than assuming a runner label or entitlement setup.

Prefer small C interfaces at the C/C++ boundary, explicit ownership, checked arithmetic and fixed-width guest types. Use RAII for new C++ resource ownership and explicit cleanup paths in C. Keep host-specific code in platform modules; isolate new vendored changes and document their upstream revision. Follow each touched file's existing style, avoid broad refactors, and test behavior rather than mirroring implementation.

Every PR describes the concrete before/after behavior, affected platforms, executed checks, skipped coverage and remaining risk. Update this plan's milestone status only when the acceptance evidence exists. Add an architecture decision for translator choice, address-space mapping, bridge signatures and GPU robustness strategy as each is resolved.

## 6. Decision gates and scheduling

The critical path is M0 → M1/M2 → M3 → M5; M4 is a separate prerequisite for M5. M6 and M7 build on actual gameplay results. Packaging work cannot resolve CPU or GPU correctness failures.

Reserve the first one to two weeks as a feasibility timebox, not a promise that M0–M4 will finish. At its end, report what executes on Darwin, measured GPU incompatibilities, candidate-core suitability, and revised scope. Do not promise a playable date until CPU/ABI, VM/fault and representative-renderer gates have passed. A custom translator or major shader-semantic gaps can turn this into a much longer systems project.

The next implementation change is **M0: isolate a CPU-only build/test harness and define the execution interface while preserving native x86 behavior**. Translator selection follows measured Darwin experiments; no game porting code should be built around an untested core choice.
