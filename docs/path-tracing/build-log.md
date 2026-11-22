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
