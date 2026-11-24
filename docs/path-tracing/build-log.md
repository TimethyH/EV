# Path tracing build log

One entry per step: date, milestone, what was built (and by whom), results/measurements, problems, next step.
Newest entry at the bottom. Decisions and roadmap: `plan.md`. Tutor reference: `knowledge.md`.

## 2026-11-18 – Research, decisions, branch setup (before M0)
- **Research**: industry real-time path tracing, offline/spectral rendering (hero wavelength sampling), OptiX denoiser
  and CUDA/D3D12 interop, Neural Radiance Cache, path tracing an FFT ocean, and a full read of EV's architecture.
  Report and notes in `research/`.
- **Follow-up research** on the uncertain points (AMD HIP compute + OIDN on Windows, RTX 3070 / OptiX specifics,
  spectral implementation details, DX12/DXR practicalities, EV architecture on this branch): `research/followup_*.md`.
- **Decisions D1–D18** made (see `plan.md`). Key changes against the report: development mainly on the AMD RX 9070 XT,
  so GPU compute is one CUDA-style source built for HIP and CUDA, OIDN is the first denoiser, the NRC is self-written
  (NVIDIA's SDK only as reference on the RTX 3070).
- **Branch** `path-tracing` created from `origin/scene-save-load`; the handoff note about the IBL bake fix
  (`e738ba0`, from `render-graph`) was cherry-picked so the TODO is not lost.
- **Plan additions** (same day): milestone **M5c** software coherence (wavefront + material sorting, benchmarked with
  1/4/16/64 materials), optimization phases **O1**/**O2**, profiling after every milestone on both GPUs plus A/B checks
  on hot-path changes, logged in `performance.md`; SER stays as a later milestone (raygen-only API). Research:
  `research/followup_performance_optimization.md`; `knowledge.md` §10.1 profiling routine, §10.2 strategy catalogue.
- **More research** (same day): gaps from an interrupted run closed (`research/followup_gaps_completion.md`: Laine 2013
  numbers, Blender sorting figures, Ampere and RDNA 4 RT hardware; correction: fp16 is not faster than fp32 on the
  RTX 3070) and Jacco Bikker / TinyBVH (`research/followup_bikker_tinybvh.md`). Chosen additions: **M1b** hand-built
  SAH BVH on the CPU (checked against TinyBVH) and **M3-ref** CPU reference bench (BSDF/pdf tests + tiny CPU tracer
  on TinyBVH). Not chosen: M5d compute traversal vs DXR.
- **Working agreement**: the developer writes all logic; Claude may provide skeletons (signatures, step comments,
  `TODO(human)` markers) and tutors with `knowledge.md`.
- **Next**: M0 – capabilities and plumbing (written by the developer).

## 2026-11-19 – M0 step 1: Agility SDK 1.620.2 loaded
- **Vendored** (Claude): trimmed NuGet `Microsoft.Direct3D.D3D12` 1.620.2 in `EV-Engine/EV-Engine/thirdparty/agility/`
  (`include/`, flat `redist/` – EV's `.gitignore` ignores `bin/` and `x64/` – plus licenses and README).
- **Developer**: Agility `include` first in `IncludePath` (EV-Engine + OceanRenderer, x64); post-build
  `xcopy /Y /I /D` of `redist\*.dll` to `$(OutDir)D3D12\`; `EV_AGILITY_SDK_EXPORTS(version)` macro in
  `header/core/agility_sdk.h` (skeleton by Claude), expanded in `OceanRenderer/source/main.cpp`;
  `LogLoadedD3D12Core()` in `application.cpp` (GetModuleHandleW + GetModuleFileNameW, warns when the path is not
  `\D3D12\`). ShaderArt skipped (unused project).
- **Problems hit**: dumped an old Debug exe (the earlier build was Release) → check timestamps; missing include of
  `agility_sdk.h`; `u8".\\D3D12\\"` is `const char8_t[]` in C++20 (C2440) → plain `".\\D3D12\\"`.
- **Verified** (RX 9070 XT, Debug): `dumpbin /exports` lists `D3D12SDKPath` + `D3D12SDKVersion`; at runtime
  `Loaded D3D12Core.dll from: T:\Projects\DX12\EV\EV-Engine\x64\Debug\D3D12\D3D12Core.dll`, no warning.
- **New issue**: the 620 runtime raises debug-layer warning **#989 DEPRECATED_API** ("`ID3D12Device::CheckFeatureSupport`
  is superseded by `ID3D12Device16::CheckFeatureSupport1`"); EV breaks on warnings → stops in `Initialize`.
  EV's own D3D12 calls: `application.cpp` root-signature check + MSAA quality loop, `resource.cpp` format support;
  third-party code is not involved: the split `thirdparty/d3dx12*.h` are on no include path (unused – EV's
  `<d3dx12.h>` resolves to `thirdparty/DirectXTex/d3dx12.h`), and the DirectXTex functions that call
  `CheckFeatureSupport` (`CaptureTexture`, `IsSupportedTexture`) are never called by EV.
- **Decision**: migrate (not deny-list): `Application::CheckFeatureSupport()` = `ID3D12Device16::CheckFeatureSupport1`
  clamped to the SDK version, classic call as fallback; EV's 3 call sites switch to it. Skeleton by Claude, logic by
  the developer.
- **Next**: finish #989, then M0 step 2 (device creation).

## 2026-11-22 – Planning: GI techniques track (no code)
- **Scope change (developer)**: the ocean is no longer the main target; proper test environments come later. New goal:
  learn and fairly compare real-time GI techniques on performance and visual fidelity.
- **Research** (Claude, 4 notes in `research/`): `followup_gi_world_caches.md` (surfels, kajiya, DDGI, GI-1.0, Lumen),
  `followup_restir.md` (RIS → ReSTIR DI/GI/PT, spectral ReSTIR), `followup_radiance_cascades.md` (2D/3D RC, Split RC,
  "Spatium" in Black Engine), `followup_gi_comparison_method.md` (metrics, protocol, test scenes, EV loader gaps).
- **Plan**: new section "GI techniques track" in `plan.md` with milestones RC-0, R0/R1, M8c (harness), M8d (SPWI RC),
  M8e (ReSTIR GI), M9b (DDGI), M9r (Split RC), M9c (surfels), M9x (bake-off) and optional M9r-b/M9d/M9e/R4–R5.
- **Decisions (developer)**: G1 core = DDGI, radiance cascades (SPWI + Split RC), ReSTIR GI, surfel GI; G2 temporal
  ReSTIR DI moves into M8b; G3 harness steps 1–3 in M7; G5 counter-based replayable RNG from M3. G4 (cache order) and
  G6 (loader fixes inside M8c) keep their defaults for now.
- **Still open**: #989 (M0), unchanged since 2026-11-19.

## 2026-11-22 – Patroni board of the plan (no code)
- **Built** (Claude): Patroni Home → `EV (Claude)` → `path-tracing`. Overview board with the "you are here" note;
  Roadmap with all 31 milestones / side tracks as nodes in four phase rows (dependencies as arrows); one board per
  milestone in the fixed order 1 what + why → 2 knowledge + reading links → visual → 3 tasks → 4 verify / pitfalls;
  Architecture (built = green, planned = blue, data-flow links); Decisions D1–D18 + G1–G6 (G4, G6 marked open);
  GI techniques track; Profiling & performance; Hardware & documents.
- **Visuals added** (interactive where useful): SAH split cost with sliders + expected refit curve (M1b), noise vs spp
  and MIS weights (M3), SampleVisible pdf with a draggable hero wavelength (M4), Fresnel/TIR + water transmittance
  (M5), divergence per wave vs material count (M5c), D3D12 ↔ HIP fence sequence (M6), occupancy vs registers + Amdahl
  (O1), log error vs log time with a bias floor (M8c), RC cascade ladder (RC-0) and reach (M8d), Chebyshev visibility
  + atlas memory (M9b), published GI costs (M9x, GI track), async-compute timeline (O2).
- **Status on the board**: M0 = current; task 1 (Agility) done, #989 parts A/B "doing", everything else todo.
- From now on the board is updated together with this log after every step, decision or plan change.

## 2026-11-22 – Plan change: reprojection texture in M8b (no code)
- **Decision (developer)**: M8b gets a step right after the motion vectors: a reprojection texture (previous uv +
  4-bit bilinear-tap validity + accuracy), shared by every temporal pass (R2, temporal denoiser, M8d, M8e, TAA).
- **Why**: EV will have 4–5 temporal consumers; one shared validity test avoids duplicated cost and inconsistent
  history decisions, and keeps the M9x GI comparison fair. Updated in `plan.md` (M8b row) and on the Patroni board.

## 2026-11-23 – M0: warning #989 code (CheckFeatureSupport wrapper)
- **Part A** (developer): `m_device.As(&m_device16)` right after `LogLoadedD3D12Core()` in `Application::Initialize`;
  HRESULT ignored on purpose (OS runtime → `m_device16` stays null, commented).
- **Part B** (developer): `Application::CheckFeatureSupport` → `m_device16->CheckFeatureSupport1(…, D3D12_SDK_VERSION)`
  when the Device16 view exists, else classic `m_device->CheckFeatureSupport`.
- **Call sites** (developer): root-signature check in `Initialize`, `GetMultisampleQualityLevels`, and
  `Resource::CheckFeatureSupport` now go through the wrapper (no direct device capability query left in EV; the two
  `IDXGIFactory5::CheckFeatureSupport` tearing checks are DXGI and not affected). Claude removed the unused
  `d3d12Device` local in `resource.cpp`.
- **Bumps on the way** (worth remembering): calling `CheckFeatureSupport1()` as a free function (it is a method of
  `ID3D12Device16`, needs `m_device16->`); hardcoding `620` instead of `D3D12_SDK_VERSION`; calling `m_device16`
  directly at a call site (crashes when Agility is not loaded – call sites must only know the wrapper).
- **Verified** (developer, Debug run): #989 is gone. Committed as `a0bdeda` (together with M0 step 1 and the docs).
- **Next**: M0 step 2 (device creation); task 4 (`EV_AGILITY_SDK_EXPORTS(D3D12_SDK_VERSION)`) optional. Claude's
  queued IBL bake fix is now unblocked.

## 2026-11-23 – IBL bake fix ported (Claude, at the developer's request)
- **What**: the sky-lighting fix from `Dads-Birthday` commit `11bf7d9` (see `docs/editor-handoff.md`, "TODO: sky lighting
  streaks and blocks"). Only `OceanRenderer/shaders/ibl_specular.hlsl` and `ibl_convolution.hlsl` were taken from that
  commit; both were byte-identical to its parent here, so nothing else from `Dads-Birthday` came along.
- **Why it matters for the path tracer**: rough objects had view-dependent streaks and blotches baked into the IBL
  cubemaps. That noise would have been in the raster reference every path-traced image is compared against.
- **Cause, in short**: both bakes read far too detailed a skybox mip. The specular prefilter computed the texel solid
  angle from the size of the cubemap being *written* instead of the one being *sampled*; the irradiance convolution
  always read mip 0 with samples ~0.025 rad apart, so whether a sample hit the sun was luck. Fix: pick the mip whose
  texels match the sample spacing (+1 mip for margin), clamped to the mip chain.
- **Checked**: both shaders compile with DXC `cs_6_0` (Windows SDK 10.0.26100), no warnings. Visual check by the
  developer: works (rough objects shade smoothly in front of the default sky).

## 2026-11-23 – M0 task 4 + step 2: SDK version macro, two-step device creation
- **Task 4** (developer): `OceanRenderer/source/main.cpp` exports `EV_AGILITY_SDK_EXPORTS(D3D12_SDK_VERSION)` instead of
  a hardcoded `620`, so the exported version always equals the Agility `d3d12.h` EV compiles against (works because
  `core/application.h` pulls in `d3d12.h` before the macro line).
- **Step 2** (developer): `Application::CreateDevice` creates a base `ID3D12Device` first (`ThrowIfFailed` = adapter /
  driver can't do FL 11_0), then `As<ID3D12Device13>` with its own `std::runtime_error` ("runtime too old, Agility SDK
  not loaded?") and `LogLoadedD3D12Core()` printed just before it. Both pointers are locals; `Initialize` stores the
  result in `m_device` (unchanged type: Device13).
- **Plan change**: no separate `As<ID3D12Device5>` – Device13 inherits it, so the DXR device methods are already on
  `m_device`. Whether DXR works is decided by `OPTIONS5.RaytracingTier` in step 3 (interface = runtime, tier = hardware).
- **Bumps on the way**: `IID_PPV_ARGS(&m_device)` still requested Device13 (the pointer's type picks the interface);
  the unchecked first `D3D12CreateDevice` (null `ComPtr` → access violation in `.As`); writing `m_device` inside
  `CreateDevice` (hidden side effect, would bite in M6 when probing other adapters); changing `m_device` to the base
  type broke `GetDevice()` (`ComPtr` only converts derived → base); `LogLoadedD3D12Core()` placed before the first
  device call (D3D12Core is loaded lazily) and logged twice → moved into the failure branch.
- **Verified** (developer): Debug run works (EV breaks on debug-layer warnings, so none fired). Not committed yet.
- **Next**: M0 step 3 – `RenderCaps` struct + one log line.

## 2026-11-23 – Plan change: optional milestones are considered, not skipped silently (no code)
- **Decision (developer)**: build **RC-0** (2D radiance cascades toy) after M0 and before M1, and **M1b** (hand-built
  SAH BVH) after M1 and before M2. Updated in `plan.md` (RC-0 row, M1b row, GI-track order).
- **Rule from now on**: every optional item (M3-ref, R0, R1, M5b, M9r-b/M9d/M9e, R4/R5, M11 extras, small optional
  tasks like M0's DXC) is offered to the developer at the right moment – do it now or skip for now – before the plan
  moves past the point where it fits. Nothing optional is passed without that question.

## 2026-11-23 – M0 step 3: RenderCaps + one log line
- **What** (developer): `Application::QueryRenderCaps()`, called at the end of `Initialize`, fills `m_renderCaps`
  (`struct RenderCaps` in `application.h`, getter `GetRenderCaps()`): adapter name / VendorId / LUID from
  `m_dxgiAdapter->GetDesc1`, then `OPTIONS5.RaytracingTier`, `OPTIONS.ResourceBindingTier`,
  `OPTIONS22.ShaderExecutionReorderingActuallyReorders` and `SHADER_MODEL` – all through the `CheckFeatureSupport`
  wrapper, each stored only on `SUCCEEDED`, so a failed query keeps the safe default from the struct (never throws).
  Shader model: asks `D3D_HIGHEST_SHADER_MODEL` (6.10 in this header) and steps down while `E_INVALIDARG`, floor 6.0
  (MS Learn `D3D12_FEATURE_DATA_SHADER_MODEL`, Remarks → Note). One `std::format` line via `OutputDebugStringW`.
- **Plan change**: adapter info is read inside `QueryRenderCaps`, not stored by `CreateAdapter` (no hidden side
  effects; `CreateAdapter` runs twice with the WARP fallback). knowledge.md §2.5 + M0 entry updated.
- **Result, main PC**:
  `RenderCaps: AMD Radeon RX 9070 XT | vendor: 0x1002 | LUID: 00000000:0000f5ec | RT: 1.1 | SM: 6.9 | binding tier: 3 | SER reorders: no | SDK: 620`
  – matches the expectation (RT ≥ 1.1, SM 6.9, tier 3, SER FALSE). The current AMD driver reports RT tier 1.1, not
  1.2 (DXR 1.2).
- **Result, laptop** (developer, Debug): `NVIDIA GeForce RTX 3070 Ti Laptop GPU | vendor: 0x10de | LUID:
  00000000:00013697 | RT: 1.1 | SM: 6.8 | binding tier: 3 | SER reorders: no | SDK: 620`. **SM 6.8, not 6.9**:
  the driver doesn't report SM 6.9 yet, so SM 6.9 shaders (D3) won't run there until a driver update – recheck then.
  SER "no" as expected on RTX 30.
- **Open (developer: not now)**: on the laptop the Debug run then **crashes at swapchain creation**: `dxgi.dll`
  itself calls the deprecated `ID3D12Device::CheckFeatureSupport` on EV's device → debug layer reports #989 → EV's
  info queue breaks on warnings. Not EV code, so the wrapper can't help. Not seen on the 9070 XT desktop (why the
  laptop's DXGI path differs is unconfirmed; hybrid-GPU presentation is a guess). Options when it's picked up: deny
  #989 in the info queue with a written reason (loses the check for EV's own calls), or don't break on that one ID;
  also check whether CI (WARP) hits it.
- **Hardware fix in the docs**: the NVIDIA machine is a laptop with an RTX 3070 Ti Laptop GPU, not a desktop RTX 3070
  (`plan.md` Hardware table + `CLAUDE.md` corrected; "the 3070" in the docs means that GPU).

## 2026-11-23 – M0 task 8: ID3D12GraphicsCommandList4
- **What** (developer): `CommandList::m_commandList` and `GetCommandList()` are now `ID3D12GraphicsCommandList4`
  (adds `BuildRaytracingAccelerationStructure`, `EmitRaytracingAccelerationStructurePostbuildInfo`,
  `CopyRaytracingAccelerationStructure`, `SetPipelineState1`, `DispatchRays`, render passes, meta commands).
- **Nothing else changed, on purpose**: `CreateCommandList(…, IID_PPV_ARGS(&m_commandList))` asks for the IID of the
  member's type, so it now requests List4 by itself; the 41 `GetCommandList()` callers (17 files) use `auto` / `->` /
  `.Get()`, and List4 → List2 / `ID3D12GraphicsCommandList*` converts automatically (derived → base). `demo.cpp`'s
  List2 parameters are not compiled (not in the project). No fallback needed: every runtime with Device13 has List4.
- **Predictions first** (developer): both right (creation line unchanged, callers compile); the reason for the first
  was "a general wrapper" → the real reason is `IID_PPV_ARGS` taking the IID from the pointer type.
- **Verified** (developer): Debug build + run on the 9070 XT, scene comes up (creation is wrapped in `ThrowIfFailed`).
- **Cleanup (Claude)**: `TODO(human)` replaced by an explanatory comment; the file-header doc comment still said List2.

## 2026-11-23 – M0 task 9: DRED names for DXR ops
- **What** (developer): `GetBreadcrumbOpName` names BuildRaytracingAccelerationStructure,
  EmitRaytracingAccelerationStructurePostbuildInfo, CopyRaytracingAccelerationStructure, DispatchRays,
  SetPipelineState1, Barrier, BeginCommandList, DispatchGraph, SetProgram (names spelled like the D3D12 methods, so
  they can be searched). Unknown ops return "Unknown Operation", and the breadcrumb line in
  `GetDeviceRemovedReport` now also prints the raw op number (option "format in the caller": the lookup keeps
  returning only string literals).
- **Key fact**: inline RayQuery has no breadcrumb op of its own – a path-tracing crash will point at **Dispatch**. The
  useful new names are the AS ops (M1). To tell dispatches apart, pass names would need DRED BreadcrumbContext
  (`ID3D12DeviceRemovedExtendedDataSettings1`, knowledge.md §6.7) – consider at the first M3 crash.
- **Bumps on the way**: `return "[{}] Unknown Operation", op;` (comma operator → returns the enum → C2440; `{}`
  only means something inside `std::format`; a formatted string can't be returned as `const char*` without
  dangling); then a `snprintf` whose `%d`/`%s` slots didn't match the argument order (`%s` would have read address
  31 → the crash reporter itself would crash; MSVC warning C4477 catches this).
- **Verified**: Debug build + run on the 9070 XT, unchanged behaviour. The names can't appear before M1 (no DXR
  commands yet); the optional forced `RemoveDevice()` test was offered, not taken.
- **Cleanup (Claude)**: `TODO(human)` → explanatory comment; removed extra empty lines.

## 2026-11-23 – M0 task 10: frame counter running again
- **What** (developer): `WndProc`'s `WM_PAINT` case (EV's once-per-frame tick: the window is never validated, so
  Windows keeps sending `WM_PAINT`) calls the private `Application::IncrementFrame()` **before** `OnUpdate` →
  the first frame sees `m_frameCount = 1`; 0 means "no frame has run yet". The unused per-window
  `Window::m_frameCounter` was deleted (one source of truth for M3's RNG seed). Also fixed: `WM_DPICHANGED` fell
  through into `WM_PAINT` (missing `break`), which ran an extra frame on every DPI change.
- **Checked first**: the only live reader, `DescriptorAllocation::Free`, stores the frame next to each stale
  descriptor, but `DescriptorAllocatorPage::ReleaseStaleDescriptors` ignores it and frees everything after each
  `Present` → enabling the counter changes no behaviour. Note for M3: the persistent bindless heap needs real
  frame-based deferred release (the original tutorial design EV dropped).
- **Found on the way**: the `friend WndProc` in `Application` had never worked – inside `namespace EV` an
  unqualified friend names `EV::WndProc`, but the real one is global. Fix (developer): declare the global `WndProc`
  before `namespace EV` and befriend `::WndProc` (same pattern as `window.h`).
- **Verified** (developer): Watch window `EV::Application::m_frameCount` grows between two pauses.
- **Cleanup (Claude)**: `TODO(human)` removed, comments put above their own lines, explanatory comments on the global
  `WndProc` declaration and `IncrementFrame`.

## 2026-11-23 – M0 task 11: "can we path trace here?" rule + Path Tracing panel
- **Part A** (developer): `RenderCaps::GetPathTracingUnsupportedReason()` (application.cpp) is the one place that
  decides; `SupportsPathTracing()` is derived from it (header). **Minimums (decision)**: RT tier ≥ 1.1 (inline
  RayQuery), **SM ≥ 6.6** (RayQuery needs 6.5, `ResourceDescriptorHeap` bindless 6.6), resource binding tier 3
  (bindless materials); checked in that order, each with its own reason text. SM 6.9 extras (SER, OMM) are optional
  and get checked where they're used, so the laptop (SM 6.8) stays supported.
- **Part B** (developer): `Ocean::DrawPathTracingPanel` (registered next to the Ocean/Light panels): "Enable Path
  Tracing" checkbox (`m_pathTracingEnabled`, read by nothing until M2/M3) inside `BeginDisabled(!supported)`, the
  reason as wrapped text outside the disabled block, and read-only adapter / RT tier / SM lines.
- **Bumps on the way**: `m_renderCaps.x` inside a `RenderCaps` method (the struct is `this`); SM minimum 6.0 and
  reason texts blaming "DXR 1.1" for every check; first panel version used a toggle `Button` (shows no state), an
  `if/else` duplicating the widget with an always-true inner check, a generic "not supported" text instead of the
  reason, `%s` for the `std::wstring` name (prints only "A"; ImGui gets no C4477 check), and a per-frame copy of
  `RenderCaps`.
- **Verified** (developer): panel shows on the 9070 XT with an active checkbox. WARP run (`EV_USE_WARP=1`, main PC,
  **inbox** WARP – no `D3D10Warp.dll` in the repo): `Microsoft Basic Render Driver | vendor: 0x1414 | RT: 1.1 |
  SM: 6.8 | binding tier: 3 | SER reorders: no | SDK: 620` → **supported**, not "unsupported" as planned. The rule is
  right; the plan's assumption "inbox WARP has no DXR" (knowledge.md §1 matrix, marked [I]) was wrong for this
  Windows build. The CI runner's WARP (windows-2025 image) may still differ – the manual CI run will show it.
  **Decision**: no override switch to force "unsupported" (no machine we use lacks the minimums); M0 verify changed to
  "CI green on WARP, panel shows what the runner reports".
- **Cleanup (Claude)**: `TODO(human)`s removed; `if (ImGui::Checkbox(...));` → plain call (the empty `if` did nothing,
  MSVC C4390).
- **Task 12 (optional, newer DXC next to the exe): skipped for now** (developer). The Windows SDK DXC covers SM 6.6,
  enough through M3; pick it up with the first SM 6.9 shader (knowledge.md §6.2: DXC v1.9.x).

- **Bumps on the way**: the OPTIONS5 struct used for all three queries (runtime only checks the size → silent
  `E_INVALIDARG`); the shader-model input started at 0 (`= {}`) and then at the 5.1 default, and the loop lowered
  `m_renderCaps` instead of the struct field the runtime reads; printing SM with `/ 10`, `% 10` ("10.5") instead of
  the hex digits `>> 4`, `& 0xF`.
- **Cleanup (Claude, after review)**: removed the `TODO(human)` + step comments, added a comment explaining the
  shader-model loop, added the missing trailing `\n` to the log line.

## 2026-11-23 – Laptop Debug crash fixed: #989 from dxgi.dll denied (Claude, at the developer's request)
- **Problem** (step-3 entry): on the laptop, `dxgi.dll` calls the deprecated `ID3D12Device::CheckFeatureSupport` on
  EV's device during swapchain creation → `D3D12_MESSAGE_ID_DEPRECATED_API` (#989) → break-on-warning → crash.
- **Fix**: `D3D12_MESSAGE_ID_DEPRECATED_API` added to the storage-filter `DenyIds` in `Application::CreateDevice`
  (denied messages are never stored, so they can't break), with the reason in a comment. Why not
  `SetBreakOnID(989, FALSE)`: the debug layer breaks if category OR severity OR ID is set, so break-on-WARNING still
  fires. Why the whole run and not only around swapchain creation: DXGI may make the same call on `ResizeBuffers` /
  fullscreen switches. Cost: a new deprecated call in EV's own code would go unnoticed (all capability queries go
  through the `CheckFeatureSupport` wrapper → `CheckFeatureSupport1`, so none today).
- **Checked**: EV-Engine Debug x64 builds (MSBuild, no new warnings). **Verified on the laptop** (developer, Debug):
  reaches the scene, no crash after resizing the window. Commit `5caa166`.

## 2026-11-23 – M0 task 13, round 1: VRAM line in the Stats window
- **What** (developer): `Editor::DrawStats` prints `VRAM: <usage> / <budget> MB` from
  `IDXGIAdapter3::QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, …)` via `Application::GetAdapter()`
  (Adapter4 inherits Adapter3, no `As<>`); printed only on `SUCCEEDED`, `UINT64` bytes → MB with `%llu`.
- **Why**: the `performance.md` baseline has a VRAM column; from M1 on it shows what acceleration structures cost,
  and on the 8 GB laptop usage close to Budget explains sudden frame-time jumps (paging).
- **Checked**: Claude review passed first time; EV-Engine Debug x64 compiles. `TODO(human)` removed, comment kept.
- **Next**: round 2 – measure the raster baseline on both PCs (Release, vsync off, normal clocks) → `performance.md`.

## 2026-11-23 – M0 task 13, round 2: raster baseline, main PC
- **Measured** (developer; Release, vsync off, normal clocks, 30 s warm-up, default ocean scene, viewport 1237×712):
  ~1200–1300 FPS, frame avg 0.8 ms / max 1.6 ms; Render Graph GPU total 0.35 ms (ocean surface 0.18, skybox 0.013,
  tonemap 0.012, simulation passes ≤ 0.008 each); VRAM 1351 / 15419 MB. RX 9070 XT, Adrenalin 26.11.2, Ryzen 5 3600.
- **Reading**: CPU-bound (frame 0.8 ms vs GPU 0.35 ms); numbers read by eye jump a lot at this frame rate (120-frame
  average = 0.1 s) – the `--benchmark` CSV (M1–M3) replaces this. Render Graph lists passes in run order, not by cost.
- **Note**: CPU upgrade planned on the main PC → re-measure this row afterwards (new machine line in performance.md).
- **Open**: laptop row (same procedure, similar viewport).

## 2026-11-23 – M0 task 13, round 2: raster baseline, laptop
- **Measured** (developer; Release, vsync off, on the charger, viewport 1212×681): ~620 FPS, frame avg 1.6 ms / max
  3.0 ms; Render Graph GPU total ≈ 0.9 ms (ocean surface 0.26, skybox 0.030, slope/displacement FFT passes ≈ 0.029
  each); VRAM 1366 / 7250 MB. Driver + CPU name still to add to the Machines table.
- **Reading**: the laptop GPU is ~2.6× slower than the 9070 XT on this scene (0.9 vs 0.35 ms) and the FFT passes
  are ~3.5× slower (0.029 vs 0.008 ms); frame time is less CPU-bound (GPU 0.9 of 1.6 ms).
- **Open**: main-PC row again after its CPU upgrade (new machine line).

## 2026-11-23 – RC-0 started: tutor entry + skeleton (Claude)
- **Decision (developer)**: start RC-0 now; M0 leftovers (manual CI run on WARP, laptop driver/CPU, main-PC baseline
  after the CPU upgrade) are done alongside. "Continue EV" = continue the path-tracing work.
- **Tutor entry**: `knowledge.md` "### RC-0" written (goal, where it lives in EV, fixed conventions – canvas rgb =
  emission / a = opacity, cascade texel rgb = radiance / a = transmittance β, c0 2 px × 4 directions, ×2 / ×4 / ×4,
  t0 = 2 px, 6 cascades, ping-pong – order in 7 steps, verify, pitfalls, reading with exact section headings checked
  on the live pages).
- **Where it lives (decision)**: OceanRenderer, class `RcToy` (`include/rc_toy.h`, `source/rc_toy.cpp`), panel "RC-0"
  in the Center dock (tab next to the Scene view), passes only while "Enable" is ticked. Skeleton by Claude: class
  declaration, wiring in `Ocean::LoadContent` / `RecordRenderGraph` / `UnloadContent`, project entries, and the panel
  showing the canvas with `ImGui::Image`. OceanRenderer Debug x64 compiles. First `TODO(human)`: step 1a, creating the
  canvas texture in `RcToy::Initialize`.

## 2026-11-23 – RC-0 step 1a: canvas texture
- **What** (developer): `RcToy::Initialize` creates `m_canvas` – 1024² `R16G16B16A16_FLOAT`, `ALLOW_UNORDERED_ACCESS`,
  via `Application::CreateTexture`, named "RC-0 Canvas".
- **Bumps on the way**: copied the BRDF LUT's `R16G16_FLOAT` (2 channels; the canvas needs 4: rgb light + a opacity)
  and its variable names; tried `std::make_shared<Texture>(w, h, format, name)` – no such constructor (only
  `Texture(desc, clearValue)`), and a direct `make_shared` would skip `Application::TrackLiveTexture` (editor texture
  browser). Factory functions are where the bookkeeping lives.
- **Checked**: Claude review; OceanRenderer Debug x64 compiles. Cleanup (Claude): removed the leftover `make_shared`
  line and the `TODO(human)`, added an explanatory comment.
- **Question (developer)**: a simpler texture-creation wrapper? → see the session answer: `CreateTexture2D(name, w, h,
  format, flags)` helper vs. letting the render graph own persistent textures (offered as optional).
- **Decision (developer)**: option C – the render graph will own persistent textures (`CreatePersistent`, flags
  derived from passes) and all EV texture creation moves to it. Handed to Claude as a queued task for a session when
  the developer is not developing: `docs/tasks/persistent-render-graph-textures.md` (listed in `CLAUDE.md`). RC-0
  continues with the current `CreateTexture` code until then.

## 2026-11-23 – RC-0 step 1b: mouse → canvas pixel
- **What** (developer): `RcToy::DrawPanel` reads `GetItemRectMin/Max` right after `ImGui::Image`, maps
  `(mouse − min) / (max − min) · CanvasSize` into `m_brushPixelX/Y` (float canvas pixels), `m_painting =
  IsItemHovered() && IsMouseDown(Left)`, and shows them as one text line.
- **Review**: correct first time. No clamp needed – the brush is only used while hovered and ImGui's hover test is
  `min ≤ mouse < max`, so 0 ≤ pixel < 1024 whenever it matters. Comments said "panel space" for the item rectangle; it
  is screen space like `GetMousePos` (that's why the subtraction works) – fixed in cleanup.
- **Checked**: OceanRenderer Debug x64 compiles; `TODO(human)` removed. Developer to confirm the corners read ≈ 0,0 and
  ≈ 1023,1023.
- **Fix (Claude, skeleton code)**: the canvas wasn't visible – it starts as (0,0,0,0) and ImGui alpha-blends images,
  so alpha = 0 ("empty") drew fully transparent. The panel now uses `ImGui::ImageWithBg` over dark grey: empty = grey,
  wall = black, light = its colour. Hover/coordinates were unaffected (the item rectangle existed all along).

## 2026-11-23 – RC-0 step 1c round 1: the brush shader (rc_paint.hlsl)
- **Skeleton (Claude)**: `shaders/rc_paint.hlsl` (cbuffer `PaintConstants` b0 + `RWTexture2D<float4> canvas` u0,
  `[numthreads(16,16,1)]`), FxCompile entry, `RcToy::BrushMode` + `RcToy::PaintConstants` (static_assert = 8 root
  constants, same field order as the cbuffer so HLSL's 16-byte register packing matches), brush UI in the panel.
- **What** (developer): one thread per pixel; pixel center `pixel + 0.5` vs `brushCenter`, scalar `length`, early
  return outside the radius; `switch (brushMode)` picks the whole value: light `(colour·intensity, 1)`, wall
  `(0,0,0,1)`, eraser `(0,0,0,0)`.
- **Bumps on the way** (3 rounds):
  1. Compile error `operands for short-circuiting logical binary operator must be scalar`: `pixel >= 1024 || pixel < 0`
     on `uint2` gives `bool2`s, and HLSL 2021 (DXC default) only allows `||`/`&&` on scalars (use `or()`/`any()`). The
     bounds check wasn't needed at all: `uint < 0` is always false, the dispatch covers exactly 1024², and
     out-of-bounds UAV writes are dropped.
  2. `float2 d = length(...)`: `length` returns one float, silently splatted into both components → the radius test
     became a `bool2` too. Missing `+ 0.5` (pixel center).
  3. One `float4(brushColor * brushIntensity, mode == 2 ? 0 : 1)` for all modes: walls glowed, the eraser left light
     in rgb (invisible in the panel because ImGui hides alpha 0). A per-mode scalar (`brushType` 1 / 0.5 / 0) can't
     fix it – modes differ in rgb AND alpha – so pick the whole float4 per case.
- **Checked**: `dxc -T cs_6_0` compiles; OceanRenderer Debug x64 builds. Cleanup (Claude): removed the unused
  `intensity` variable and the `TODO(human)`, added explanatory comments.
- **Next (round 2 skeleton, Claude)**: `m_paintPSO` (OceanCompute with root params [0] UAV table u0, [1] 8 constants
  b0) created in `Initialize`; `AddPasses` imports the canvas each frame and adds "RC-0 Paint" (`Write(canvas)`) while
  `m_painting`. `OceanCompute::SetPipelineState` made public so a pass can bind the PSO and set its own bindings.
  Developer: the pass's Execute body (constants, bind, dispatch).

## 2026-11-24 – RC-0 step 1c round 2: the paint pass – painting works
- **What** (developer): the "RC-0 Paint" Execute body – `m_paintPSO->SetPipelineState`, a stack `PaintConstants`
  filled from the brush members (`mode` cast to `uint32_t`), `SetCompute32BitConstants(PaintRoot::Brush, 8, &c)`,
  `SetUnorderedAccessView(PaintRoot::CanvasUAV, 0, canvas, 0)`, `Dispatch(CanvasSize / 16, CanvasSize / 16, 1)`.
- **Question (developer)**: "how do I pass the constant buffer data?" → Patroni board "Your own pass: constants,
  bind, dispatch (RC-0 Paint)" under Branch ③ render-graph: record-vs-execute sequence diagram, root slot ↔ register
  table, the constants' path (stack struct → copied into the command list → root slot → cbuffer b0), C++ vs HLSL
  cbuffer layout cards (+ a wrong field order: 48 B with padding), dispatch = group counts.
- **Review**: correct first time. Pointer form works (`sizeof / sizeof(float)` = 8); the template overload
  `SetCompute32BitConstants(slot, constants)` would count for you.
- **Checked**: developer ran it – lights, walls and eraser paint in the panel. Debug x64 builds; cleanup (Claude):
  `TODO(human)` + `(void)` lines removed, comments added, `static_cast<uint32_t>(_countof(...))` for the C4267 warning
  from the skeleton's `OceanCompute` construction.
- **Next**: step 2 – brute-force reference, accumulated over frames.
- **Committed**: `de62cb9` (steps 1a–1c + docs).
- **Decision (developer)**: the render graph wasn't written by them, so they set up at least ONE render-graph pass
  from scratch themselves (step 2a); after that Claude skeletons passes as before.

## 2026-11-24 – RC-0 step 2a: the "RC-0 Reference" pass, set up by the developer
- **Skeleton (Claude)**: `m_reference` (1024² `R32G32B32A32_FLOAT` + UAV – float32 because a running average moves
  by 1/n per frame and fp16 stalls after ~1000 frames), `m_referencePSO` (slots `ReferenceRoot`: [0] SRV table t0,
  [1] UAV table u0, [2] 2 root constants b0), `rc_reference.hlsl` stub (canvas rgb with alpha 1 → empty space black),
  `NextReferenceConstants()` (frameIndex, raysPerFrame; restart on paint / slider), panel Canvas/Reference toggle,
  rays slider, frame counter.
- **What** (developer, no pass shell given): `Import(m_reference)`, `AddPass("RC-0 Reference")
  .Read(canvas, NonPixelShaderRead).Write(reference)`, Execute: PSO, constants, SRV with the matching
  `NON_PIXEL_SHADER_RESOURCE` state, UAV, `Dispatch(64, 64, 1)`.
- **Bumps on the way** (2 rounds):
  1. `referenceTexture` (an `RGTexture` handle) used in the lambda without capturing it, and without
     `context.GetTexture` → compile error.
  2. `SetUnorderedAccessView(slot, 0, tex, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)`: the 4th parameter is the MIP;
     the state (value 8) silently converted to mip 8. Old-style enums convert to integers without a warning.
  3. `NextReferenceConstants()` moved INTO the lambda and called twice (counter +2 per frame). Fixed by calling it once
     while recording and capturing the result by value: the lambda only reads what it captured, never changes state.
- **Checked**: developer ran it – Reference view shows black empty space, painting works in both views. Debug x64
  builds; cleanup (Claude): `TODO(human)` removed, comments added.
- **Next (2b skeleton, Claude)**: `rc_reference.hlsl` gets `Random01` (PCG hash, per pixel + frame) and `March`
  (1-px steps, first solid texel's rgb, 0 when leaving the canvas); developer writes `main`: inside-solid case,
  stratified directions with a random rotation, average, running-average blend.

## 2026-11-24 – RC-0 step 2b: the brute-force reference converges
- **What** (developer): `rc_reference.hlsl` `main` – solid pixel → its own rgb; else `raysPerFrame` rays at
  `angle = 2π (u + k/K)` (u = `Random01(pixel, frameIndex)`), each `March`ed from the pixel centre, averaged; old value
  = 0 on frame 0, else `reference[pixel].rgb`; blend `old + (x − old) / (n + 1)`, alpha 1.
- **Rotation**: the developer's `2π (u + k/K)` turns the even set by up to a full circle; the planned `2π (k + u)/K`
  turns it by up to one gap (2π/K). The set repeats every gap, so both give the same distribution – fine as written.
- **Bumps on the way** (3 rounds):
  1. Blend pieces in the wrong places: `x + (n0 − old)/(n+1)` instead of `old + (x − old)/(n+1)` → would never
     smooth out. `n0` only set when `frameIndex == 0` (uninitialised otherwise – HLSL compiles that silently) and
     `float3 = float4` truncation warning.
  2. `oldCol` set correctly in `if`/`else`, but the blend still read `reference[pixel]` (float4) → right numbers by
     accident (frame-0 zero write + same-thread UAV read-after-write), 2 truncation warnings.
  3. Blend uses `oldCol` – DXC clean.
- **Checked**: developer ran it – Reference view converges (grain fades, resets on paint / slider). DXC `cs_6_0`
  clean. Cleanup (Claude): `TODO(human)` removed, the now-dead frame-0 zero write removed (the blend already gives
  `color` when old = 0), comments on the start-over and the blend formula.
- **Committed**: `2c6811c` (steps 2a + 2b).
- **Next**: step 3 – gather one cascade (`rc_cascade.hlsl`, raw c0…c5, no merge yet). Read first: m4xc "Cascades"
  (Hierarchy, Memory, Gathering), GM Shaders RC part 1 "Cascade Hierarchies", "How many Cascades?", "Raymarching from
  Probes".

## 2026-11-24 – RC-0 step 3 skeleton (Claude)
- Split into two rounds: (1) `DecodeTexel` – texel → probe centre, direction, interval; (2) `MarchInterval` + the real
  output (radiance, β).
- **Skeleton**: `m_cascades[2]` (1024² fp16 + UAV; [1] waits for step 4's ping-pong), `m_cascadePSO` (slots
  `CascadeRoot`: SRV t0 canvas, UAV u0, 1 constant `cascadeIndex`), pass "RC-0 Cascade" (only while the Cascade view is
  on, computes the cascade picked by the panel slider c0…c5), `rc_cascade.hlsl` with `ProbeRay` + `DecodeTexel` stub
  and a round-1 checkpoint output (rg = direction, b = probe checkerboard from `origin`).
- **Viewer (Claude, tooling)**: `rc_view.hlsl` + pass "RC-0 View" → `m_viewTexture`, alpha forced to 1 (ImGui
  alpha-blends and β = 0 = "hit" would draw the lit texels transparent); "Show beta" shows β as grey.
  `CreateSrvUavConstantsPSO` helper for the shared root-signature shape. OceanRenderer Debug x64 builds.

## 2026-11-24 – RC-0 step 3 round 1: DecodeTexel
- **What** (developer): `DecodeTexel` – `probeSide = 1 << (k+1)`, N = side², `probeIndex = texel / side`,
  `localTexel = texel & (side − 1)`, origin `(probeIndex + 0.5) · 2 · 2^k`, n = `x + y · side`,
  angle `2π (n + 0.5) / N`, interval `T0 (4^k − 1)/3 … T0 (4^(k+1) − 1)/3` via `pow`.
- **Bumps on the way** (3 rounds):
  1. Step 1 of the TODO ("block side and N") was unclear → reworded as "the size of one probe's square".
  2. `localTexel = texel + (side−1) & ~(side−1)` – the round-up-to-a-multiple alignment trick (and `+` binds before
     `&`) → block start, not the offset. `4 ^ k` = XOR in HLSL (no power operator), and `4 ^ k - 1` = `4 ^ (k−1)`.
     Interval math explained on the Patroni board "Interval math (step 3)" (geometric sum, gap = t · 2π/N ≈ 4 px).
  3. `tStart = T0 * pow(4, k) - 1 / 3.0` – brackets missing (c0 started at 1.67). Fixed.
- **Checked**: DXC `cs_6_0` clean; developer ran it – colour wheel in every probe block, blue checkerboard over probes.
  Cleanup (Claude): `TODO(human)` + instruction comments removed, per-block explanations added.

## 2026-11-24 – RC-0 step 3 round 2 skeleton (Claude)
- Checkpoint output removed; `main` writes `MarchInterval(origin, direction, tStart, tEnd)` (stub returns a miss).
  `MarchInterval` = `TODO(human)`: 1-px steps over the half-open `[tStart, tEnd)`, hit = (emission, β 0), miss or
  outside the canvas = (0, 0, 0, β 1). DXC clean.

## 2026-11-24 – RC-0 step 3 round 2: MarchInterval – step 3 done
- **What** (developer): `for (float t = tStart; t < tEnd; t++)`, position = origin + direction · t, `any()` bounds test
  on the position → break (miss), solid (`a >= 0.5`) → `(rgb, 0)`; after the loop `(0, 0, 0, 1)`.
- **Bumps on the way** (2 rounds):
  1. Bounds test on `t` (`t >= 1024`) instead of the position; unconditional `return` on the first step (every texel =
     the canvas colour at its interval start); hit returned β = 1 instead of 0.
  2. All three fixed – DXC clean.
- **Checked**: developer ran it – c0 thin outlines around lights, c2 a lit ring 10–42 px out, Show beta dark bands at
  each cascade's wall distances. Hovering gave an ImGui "conflicting ID" warning: the panel's radio button and slider
  were both labelled "Cascade" (Claude's panel code) → slider renamed "Shown cascade". Cleanup (Claude): `TODO(human)` +
  instruction comments removed, explanations kept.
- **Next**: step 4 – merge top-down (c5 → c0, ping-pong A/B). Read first: m4xc "Merging" (Intervals, Cones, Spatially,
  Algorithm); GM Shaders 1 "Merging Cascades" + "Ray Visibility Term and when to Merge"; GM Shaders 2 "Merging with
  Cascade N+1".

## 2026-11-24 – RC-0 step 4 skeleton (Claude)
- Six "RC-0 Cascade c<k>" passes, c5 first, ping-pong via `CascadeSlot(k) = (5 − k) & 1` (c_k writes one texture as
  UAV u0, reads c_(k+1) from the other as SRV t1). Root constants `cascadeIndex` + `mergeWithUpper` (0 for c5 or when
  the panel's "Merge" checkbox is off). `ProbeRay` gained `.probe` + `.n`; `MergeWithUpper` = `TODO(human)`.
  Step 4 board: node "Step 4 · Merging cascades" on the RC-0 board (explorer ①②③ + "over" demo ④).

## 2026-11-24 – RC-0 step 4 round 1a: one upper probe, one child
- **What** (developer): `MergeWithUpper` – first child `m = 4n`, `upperSide = 1 << (k+2)`, local = `(m % side,
  m / side)`, upper probe = `probe >> 1`, `upperTexel = upperProbe · upperSide + local`, then
  `(L_near + β_near · L_far, β_near · β_far)`.
- **Bumps on the way**: the original round 1 (4 children at once) was too big → split into 1a (one child) and 1b
  (average of 4). Two extra board sections: "Stuck? Round 1a in 3 tiny pieces" and "C · step by step" (name glossary
  with units + a clickable upperCascade picture + forward/backward DecodeTexel). Review: `ray.probe / probeSide`
  (probe index ÷ texel size, right only at k = 0); float maths on indices (`* 4.0`, `* 0.5`) → `* 4u`, `>> 1`;
  `upperTexelX/Y` → `localX/Y`.
- **Checked**: DXC `cs_6_0` clean; developer ran it – light reaches far, blocky (expected with one upper probe).
  Cleanup (Claude): round-1a instructions replaced by explanations; round 1b `TODO(human)` added (average the 4 children).

## 2026-11-24 – RC-0 step 4 round 1b: the whole cone (4 children averaged)
- **What** (developer): `upperProbeId` + `upperSide` hoisted out of a loop over i = 0…3, child `4n + i`, same
  local/upperTexel maths, sum of the 4 float4s, `far = sum / 4` (L and β averaged together), merge line unchanged.
- **Checked**: DXC `cs_6_0` clean; first-time pass. Developer ran it – smoother than 1a, still 2×2-blocky, Show beta
  shows grey steps at shadow edges (partly blocked cones = penumbrae). Cleanup (Claude): comments that had drifted when
  the lines moved into the loop put back next to their lines.

## 2026-11-24 – RC-0 step 4 round 2a skeleton (Claude)
- The 1b loop moved unchanged into `ReadUpperCone(upperProbeId, n, upperSide)`; `MergeWithUpper` still reads one upper
  probe through it. Round 2a `TODO(human)`: gridPos / base (int2) / weight in the upper probe grid, checkpoint = weight as
  colour on c3. Step 4 board: section "Round 2 · bilinear" (1D number line explorer, worked numbers, 2D weights, edge
  clamp, checkpoint + traps). DXC clean.

## 2026-11-24 – RC-0 step 4 round 2a: position in the upper probe grid
- **What** (developer): in `MergeWithUpper`, `gridPos = ray.origin / upperSide − 0.5`, `base = int2(floor(gridPos))`,
  `weight = gridPos − base`; debug return `float4(weight, 0, 1)`.
- **Bumps on the way** (3 reviews): first written inside `ReadUpperCone`, which only gets `ray.probe >> 1` (the bit
  that decides the weight is already gone), with a centre built from the upper probe id × the c0 spacing 2. Then in
  the right place but `(ray.origin + 0.5) * 2` (index → px applied to a value already in px → one solid colour) and
  `floor(uint2(gridPos))` (cast before floor truncates; −0.25 can't be a uint). Third version correct.
- **Checked**: code review passed. First run showed the normal merged c3 (debug return already removed – the cascade
  view shows raw direction texels, so it looks striped); with the return put back, the developer saw the 2×2
  four-colour pattern of solid probe blocks ✓. Cleanup (Claude): instructions → explanations; round 2b `TODO(human)`
  added (4 clamped upper probes × bilinear weights, `upperCount = 1024 / upperSide` and `far = 0` prepared, round-1
  single read removed). DXC `cs_6_0` clean.

## 2026-11-24 – RC-0 step 4 round 2b: bilinear over the 4 nearest upper probes
- **What** (developer): `offsets[4]` (0,0) (1,0) (0,1) (1,1) + `weights[4]` = products of the per-axis (1 − w) / w; loop:
  `clamp(base + offset, 0, upperCount − 1)` on int2, then `far += ReadUpperCone(uint2(id), n, upperSide) * weight`.
  Considered `far /= 4` (left commented out) – not needed, the 4 weights already sum to 1.
- **Checked**: DXC `cs_6_0` clean; first-time pass. Cleanup (Claude): `TODO(human)` + the commented-out division
  removed, explanations added (weights sum to 1, clamp the probe not the weight).
- **Run**: merged c0 much closer to Reference, but still blocks and lines, changing with the render/panel size.
  Explained: the Cascade view shows raw direction texels (each c0 probe = a 2×2 block of 4 DIFFERENT directions),
  drawn into a panel smaller than 1024 px (downscaled → moiré that moves with the size); Reference is per-pixel light.
  Like-for-like comparison needs step 5 (fluence = average of a probe's directions). Remaining RC artefacts (rings,
  leaks) are step 6. Step 4 done.
- **Committed**: steps 3 + 4 together (gather + merge, plumbing, rc_view viewer) on `path-tracing`, pushed 2026-11-24.
