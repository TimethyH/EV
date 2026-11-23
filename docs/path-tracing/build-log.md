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

- **Bumps on the way**: the OPTIONS5 struct used for all three queries (runtime only checks the size → silent
  `E_INVALIDARG`); the shader-model input started at 0 (`= {}`) and then at the 5.1 default, and the loop lowered
  `m_renderCaps` instead of the struct field the runtime reads; printing SM with `/ 10`, `% 10` ("10.5") instead of
  the hex digits `>> 4`, `& 0xF`.
- **Cleanup (Claude, after review)**: removed the `TODO(human)` + step comments, added a comment explaining the
  shader-model loop, added the missing trailing `\n` to the log line.
