# Practical DX12 / DXR setup for path tracer milestones M0-M3 (state as of 2026-11-18)

Context: student C++20 DX12 engine (static library linked into app exes), VS 2026, currently no Agility SDK, FxCompile SM 6.0, runtime DXC for hot reload, debug layer breaks on warnings, CI on WARP (no GPU). GPUs: AMD RX 9070 XT (RDNA 4, main) and NVIDIA RTX 3070 (Ampere). Decided: Agility SDK retail, DXR 1.1 inline RayQuery in compute first, 513x513 compute-displaced ocean grid with per-frame BLAS refit, staged descriptor table, SHaRC later.

This builds on `industry_realtime_path_tracing.md` and `ocean_path_tracing.md`. It does not repeat their verified points: the Agility timeline up to 1.722, SER/OMM vendor table, NVIDIA flag recipes, refit cost estimates and CLAS/PTLAS.

Labels:
- **[Verified]** fetched this session.
- **[Training knowledge]** background knowledge, not re-fetched. Check it before relying on it.
- **[Inference]** my own reasoning from cited facts.

Research method: web search and fetch on 2026-11-18, plus a read-only Glob/Grep of the local DirectX-Graphics-Samples copy.

---

## Q1. Agility SDK: current version, integration without NuGet, verification, pitfalls

### Takeaway
The latest **retail** Agility SDK is **1.620.2** (D3D12SDKVersion **620**, released 2026-11-09). **1.621 (SM 6.10 retail) has not shipped.** Microsoft targets "early December 2026". SM 6.9 / DXR 1.2 has been retail since 1.619.

Integration takes four steps:
1. Unzip the `.nupkg`.
2. Put the Agility headers *before* the Windows SDK headers.
3. Copy `D3D12Core.dll` (plus `d3d12SDKLayers.dll` in debug) to `<exe dir>\D3D12\`.
4. Export `D3D12SDKVersion` / `D3D12SDKPath` **from the .exe**.

Because the engine is a static library, put the exports in each app's own `.cpp`, not in the engine library. If they must live in the library, force them in with `/INCLUDE`.

### Cited Findings
**Versions**
- Latest retail: **1.620.2 (SDK version 620, 11/9/2026)**, with 1.620.1 on 10/28/2026. The 1.619 line: 1.619.0 (2/26/2026), 1.619.5 (7/30/2026), 1.619.6 (9/14/2026). Latest preview: 1.722.1 (11/9/2026). **1.621 is not listed** on the downloads page. — [Agility SDK downloads](https://devblogs.microsoft.com/directx/directx12agility/)
- 1.620 retail adds, among other things:
  - Batched Async Commands (ID3D12GraphicsCommandList12)
  - Fence Barriers Tier 1
  - DX Dump Files and driver-reported messages
  - extended PIX markers
  - CheckFeatureSupport1 clamping tiers to the targeted SDK version
  - "RayQuery opt-in flags for Opacity Micromaps in pre-SM 6.9 shaders"
  — [Agility SDK downloads](https://devblogs.microsoft.com/directx/directx12agility/)
- 1.619 retail: SM 6.9, which includes SER and OMM (DXR 1.2), long vectors, 16-bit and 64-bit shader and wave ops as *required* features, and 16-bit float specials. — [Agility SDK downloads](https://devblogs.microsoft.com/directx/directx12agility/)
- SM 6.10 is still in preview. Microsoft is "targeting an early-December retail release" in 1.621. This comes from search summaries of the October 28 announcement and press. — [Visual Studio Magazine, 2026-10-28](https://visualstudiomagazine.com/articles/2026/10/28/directx-agility-sdk-adds-retail-gpu-diagnostics-previews-new-raytracing.aspx); [AMD GPUOpen Oct 2026](https://gpuopen.com/learn/amd-directx12-agility-sdk-october-2026/)

**Integration steps (non-NuGet)** — all from [Microsoft: Getting started with the Agility SDK](https://devblogs.microsoft.com/directx/gettingstarted-dx12agility/)
- Download the package. Rename the `.nupkg` to `.zip` and extract it (or use `Invoke-WebRequest` plus `Expand-Archive`).
- Keep linking `d3d12.lib` **from the Windows SDK**.
- Put the Agility SDK include directory **before** the Windows SDK includes. Otherwise D3D_FEATURE_LEVEL_12_2 and the newer shader-model defines are missing.
- Folder layout recommended by Microsoft:
  ```
  App.exe
  D3D12\D3D12Core.dll
  D3D12\D3D12SDKLayers.dll   (development only)
  ```
  Microsoft recommends a subfolder rather than the exe's own folder. Next to the exe, the app-local `D3D12SDKLayers.dll` can end up paired with the System32 `D3D12Core.dll`.
- The exports **must be in the main .exe**:
  ```cpp
  extern "C" { __declspec(dllexport) extern const UINT D3D12SDKVersion = 620; }
  extern "C" { __declspec(dllexport) extern const char* D3D12SDKPath = u8".\\D3D12\\"; }
  ```
  - The article's example uses `4`. Use the number from the downloads page, which is **620** for 1.620.x.
  - `D3D12SDKPath` is relative to the exe and **needs a trailing slash**.
  - The alternative is a `.def` file with `EXPORTS D3D12SDKVersion DATA PRIVATE` / `D3D12SDKPath DATA PRIVATE`.
- To verify, check the file version of the D3D12Core.dll you ship: its minor version equals D3D12SDKVersion. Then query `CheckFeatureSupport` for the new features.
- Common errors:
  - `D3D12_ERROR_INVALID_REDIST` (0x887E0003): either D3D12Core.dll was not found at D3D12SDKPath ("Missing D3D12GetInterface export"), or D3D12SDKVersion doesn't match the DLL.
  - Debug-layer mismatch: the System32 Core is loaded together with an app-local SDKLayers.
- When shipping, include D3D12Core.dll in the same folder structure and remove D3D12SDKLayers.dll.

**Not covered by the article**
- [Training knowledge] `ID3D12SDKConfiguration1::CreateDeviceFactory` (via `D3D12GetInterface(CLSID_D3D12SDKConfiguration, ...)`) lets an app or DLL pick an SDK version and path at runtime with no exe exports. This is the official route when exports are impossible. — [D3D12 Agility SDK docs (DeviceFactory)](https://devblogs.microsoft.com/directx/directx12agility/) (not re-verified)

### Inferences
**Static-library pitfall (important for this engine).** The linker pulls an object file out of a `.lib` only when something references one of its symbols. A `__declspec(dllexport)` variable defined only in the engine `.lib` is therefore usually **dropped silently**, and the app falls back to the System32 runtime with no error. Fixes, best first:
1. Put the two-line export in each app's `main.cpp`, for example through a header macro such as `EV_AGILITY_SDK_EXPORTS(620)` that the app expands once.
2. Keep the definition in the engine and add the linker option `/INCLUDE:D3D12SDKVersion /INCLUDE:D3D12SDKPath` to each app. x64 has no underscore decoration.
3. Use a `.def` file in each app project.

**Post-build copy.** Add a post-build event, or a `<Copy>` in a shared `.props`, that copies `$(AgilityDir)\build\native\bin\x64\D3D12Core.dll` and `d3d12SDKLayers.dll` to `$(OutDir)D3D12\`. That path layout is from [Training knowledge] of the NuGet package and should be verified after unzipping. Every app exe and the CI test exe need this copy.

**Runtime check.** Log which `D3D12Core.dll` actually loaded: call `GetModuleHandleW(L"D3D12Core.dll")` then `GetModuleFileNameW`, and assert the path contains `\D3D12\`. Also assert that `CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL)` with HighestShaderModel = 6_9 succeeds. This is cheap and catches the silent fallback described above.

**Debug layer.** "Breaks on warnings" stays as it is. Make sure the app-local SDKLayers matches the Core, which the subfolder layout guarantees. New retail features can raise new warnings, so expect to add a few message IDs to the info-queue deny list.

### Gaps
- No primary source fetched this session documents the static-lib dropping behaviour for these exports. It is standard MSVC linker behaviour [Training knowledge].
- The exact internal folder of the 1.620.2 nupkg (`build/native/bin/x64`) was not verified.

---

## Q2. Matching DXC, shader models, RayQuery HLSL

### Takeaway
Use a **DXC v1.9.x release (latest: v1.9.2609, September 2026)** for SM 6.9 retail. The 1.10.x builds are SM 6.10 *preview* only. Use the same `dxcompiler.dll` + `dxil.dll` for build-time and runtime (hot-reload) compiles, so that offline and hot-reloaded shaders are identical and signed. RayQuery requires **SM 6.5+**, for example `-T cs_6_6`. Its second template argument (RAYQUERY_FLAG_*) requires SM 6.9.

### Cited Findings
- DXC releases:
  - **v1.9.2609** (29 Sep 2026, marked "Latest")
  - v1.9.2607 (29 Jul 2026)
  - v1.9.2602.24 (3 Jun 2026, Feb 2026 patch 1)
  - **v1.9.2602** (26 Feb 2026, "SM 6.9 production support", requires the new Agility SDK and a supporting driver)
  - SM 6.10 preview builds: v1.10.2605.2 / .24 / .37 (the latest is 12 Aug 2026), which require Agility 1.720-preview
  - Release zips contain dxc.exe + dxcompiler.dll (compiler) and dxv.exe + dxil.dll (validator) on Windows. The page lists these names for the 2025 releases; the 2026 asset lists were not expanded.
  — [DXC releases](https://github.com/microsoft/DirectXShaderCompiler/releases)
- SM 6.9 SER preview shaders are not compatible with the SM 6.9 release, so don't mix preview and retail toolchains. — [DXC releases](https://github.com/microsoft/DirectXShaderCompiler/releases)
- The SM 6.9 retail post cites DXC 1.9.2602.16, while its downloads list says .17. — [Microsoft SM 6.9 retail](https://devblogs.microsoft.com/directx/shader-model-6-9-retail-and-more/)

**RayQuery facts** — all from the [DXR functional spec](https://microsoft.github.io/DirectX-Specs/d3d/Raytracing.html)
- "RayQuery is available starting at Shader Model 6.5." It needs raytracing Tier 1.1.
- Template: `RayQuery<RAY_FLAGS, RAYQUERY_FLAGS>`. The second parameter is optional and requires SM 6.9+.
- RAYQUERY_FLAG values: `RAYQUERY_FLAG_NONE = 0x00`, `RAYQUERY_FLAG_ALLOW_OPACITY_MICROMAPS = 0x01`, `RAYQUERY_FLAG_ALLOW_CLUSTERED_GEOMETRY = 0x02`.
- Signature: `void RayQuery::TraceRayInline(RaytracingAccelerationStructure AS, uint RayFlags, uint InstanceInclusionMask, RayDesc Ray);`. An instance is ignored when `!((InstanceInclusionMask & InstanceMask) & 0xff)`.
- RAY_FLAG list: NONE, FORCE_OPAQUE, FORCE_NON_OPAQUE, ACCEPT_FIRST_HIT_AND_END_SEARCH, SKIP_CLOSEST_HIT_SHADER, CULL_BACK_FACING_TRIANGLES, CULL_FRONT_FACING_TRIANGLES, CULL_OPAQUE, CULL_NON_OPAQUE, SKIP_TRIANGLES, SKIP_PROCEDURAL_PRIMITIVES, FORCE_OMM_2_STATE.
- Loop pattern from the spec's Example 2:
  ```hlsl
  RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> q;
  q.TraceRayInline(Scene, RAY_FLAG_NONE, 0xFF, ray);
  while (q.Proceed()) {
      switch (q.CandidateType()) {
      case CANDIDATE_NON_OPAQUE_TRIANGLE:
          if (MyAlphaTest(q.CandidateInstanceID(), q.CandidatePrimitiveIndex(),
                          q.CandidateTriangleBarycentrics()))
              q.CommitNonOpaqueTriangleHit();
          break;
      case CANDIDATE_PROCEDURAL_PRIMITIVE: /* ... */ break;
      }
  }
  switch (q.CommittedStatus()) {
      case COMMITTED_TRIANGLE_HIT: /* shade */ break;
      case COMMITTED_PROCEDURAL_PRIMITIVE_HIT: break;
      case COMMITTED_NOTHING: /* miss / sky */ break;
  }
  ```
  - CANDIDATE_TYPE values: CANDIDATE_NON_OPAQUE_TRIANGLE, CANDIDATE_PROCEDURAL_PRIMITIVE.
  - COMMITTED_STATUS values: COMMITTED_NOTHING, COMMITTED_TRIANGLE_HIT, COMMITTED_PROCEDURAL_PRIMITIVE_HIT.
  - Extra calls to `CommitNonOpaqueTriangleHit()` for the same candidate have no effect. `Abort()` makes the next Proceed() return false.
  - The `Candidate*` accessor names in the loop come from [Training knowledge] of the same spec; the loop structure itself is from the spec.
  - "Any number of live RayQuery objects" is allowed, "but at the cost of an unknown (and relatively high) amount of hardware register storage".
- AMD: "Keep only one active RayQuery object in scope in a shader at any time", and use explicit ray flags to skip procedural and non-opaque primitives when possible. — [GPUOpen RDNA performance guide](https://gpuopen.com/learn/rdna-performance-guide/)

### Inferences
- **FxCompile.** VS's built-in FxCompile uses the Windows SDK's DXC, which lags behind (my assumption, unverified). Replace FxCompile for SM 6.x shaders with a custom build step, or a small MSBuild target, that calls the **downloaded** `dxc.exe` v1.9.2609. Ship that release's `dxcompiler.dll` + `dxil.dll` next to the exe for runtime hot reload. The same binaries then compile both paths.
- **Flags.**
  - Use `-T cs_6_6` or higher. SM 6.6 also gives ResourceDescriptorHeap[], which helps when moving to bindless later, and RTXPT requires 6.6. Use `cs_6_9` only if SER/OMM or long vectors are needed.
  - Add `-HV 2021` (the default in recent DXC per [Training knowledge]).
  - Add `-enable-16bit-types`, which needs SM 6.2+ [Training knowledge]. 16-bit ops are a *required* SM 6.9 feature per the Agility page, so on SM 6.9 devices native fp16 is guaranteed.
  - Add `-Zi -Qembed_debug` or `-Fd` for PIX in debug builds [Training knowledge].
- **Ocean and opaque scene.** Declare `RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES>` (or mark geometry `D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE`). Then `Proceed()` returns false at once and there is no candidate loop. For shadow rays, add `RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH`. Template ray flags are compile-time, so the driver can specialise traversal.

### Gaps
- The file list of the 2026 DXC release zips was not expanded on GitHub.
- No source confirms which DXC version ships in the current Windows SDK, or which one VS 2026 FxCompile uses.

---

## Q3. Driver requirements and what each GPU reports

### Takeaway
SM 6.9 / DXR 1.2 retail needs **AMD Adrenalin 26.2.1 or newer** and **NVIDIA driver 595 or newer**. The RX 9070 XT supports SM 6.9 features. Microsoft's tables say "All RTX hardware" for SM 6.9 features, which by inference includes the RTX 3070. OMM is software-emulated below RTX 40. Neither GPU's exact `D3D12_RAYTRACING_TIER` value was found in a primary source. Query it at runtime and log it.

### Cited Findings
- Retail drivers: "AMD Software: Adrenalin Edition 26.2.1" and NVIDIA "Driver version 595 and newer". Feature table:
  - Long Vector and FP16 specials: AMD RX 9000, Intel Arc B, "All RTX hardware".
  - OMM: "Hardware-accelerated on RTX 4xxx+ GPUs, software-emulated on older".
  - SER: RX 9000 supports the API but does not reorder.
  — [Microsoft SM 6.9 retail](https://devblogs.microsoft.com/directx/shader-model-6-9-retail-and-more/)
- RTXPT requires driver 595.71+ with the Agility/DXR 1.2 path. — [RTXPT GitHub](https://github.com/NVIDIA-RTX/RTXPT) (from the earlier notes)
- AMD preview features (CLAS/PTLAS, SM 6.10 preview) need the "AMD Software: Agility SDK Developer Preview Edition 26.20.12.01" driver. — [AMD GPUOpen Oct 2026](https://gpuopen.com/learn/amd-directx12-agility-sdk-october-2026/)

### Inferences
- Both GPUs support DXR Tier 1.1 (inline RayQuery). The RTX 3070 has been DXR 1.1 since 2020 and the RX 9070 XT is DX12 Ultimate [Training knowledge]. With the 1.619+ runtime and new drivers, both should report highest shader model 6.9.
- [Training knowledge, unverified] Agility 1.619 added `D3D12_RAYTRACING_TIER_1_2` for DXR 1.2 (OMM + SER). Whether the RX 9070 XT reports 1.2 is unknown: OMM is not listed for AMD in Microsoft's table, which suggests it may report 1.1. Since inline RayQuery comes first, require only `>= D3D12_RAYTRACING_TIER_1_1`, then log the actual tier and the HighestShaderModel at start-up.
- Because CheckFeatureSupport1 now clamps tiers to the targeted SDK version (from 1.620), use D3D12SDKVersion 620 to see the newest tiers.

### Gaps
- No primary source gives the exact `RaytracingTier` value reported by an RX 9070 XT or an RTX 3070 on current drivers. Check GPUinfo-style databases or measure it.
- No current minimum Adrenalin version beyond 26.2.1 was found. No NVIDIA driver release notes naming Ampere SM 6.9 support were fetched.

---

## Q4. Ray tracing performance guidance per vendor; AS flags, alignment and barriers

### Takeaway
- **AMD:** inline RayQuery from compute on the compute queue is the preferred path; 8×4 thread groups; little groupshared memory, because traversal uses LDS; one live RayQuery; "smaller BVHs are always better"; rebuild the TLAS each frame.
- **NVIDIA:** inline RayQuery is fine while all shading is uniform; prefer DispatchRays once there are "multiple different shading models"; 8×8 or 16×8 thread groups.
- For the ocean, use `ALLOW_UPDATE | PREFER_FAST_TRACE` with in-place refits and a periodic rebuild, and rebuild the TLAS every frame.
- Alignment: AS destination 256 B, scratch 256 B, instance descs 16 B, shader tables 64 B.
- RDNA 4 adds BVH8 nodes and oriented bounding boxes (OBBs) in hardware. The developer does not have to do anything to use them.

### Cited Findings
**AMD (RDNA performance guide)** — all from [GPUOpen RDNA performance guide](https://gpuopen.com/learn/rdna-performance-guide/)
- "Trace as few rays as possible"; "1 ray per pixel can still generate high quality results with great denoiser".
- "Our traversal system runs on the RDNA WGP and uses LDS extensively. Avoid using too much groupshared memory in your ray tracing shaders." "Use an 8×4 threadgroup size to optimally use the RDNA WGP."
- "Smaller BVHs are always better"; "Use high-quality build flags wherever you can"; "Don't rebuild dynamic geometry every frame and only rebuild or refit the LODs you need". Rebuild the TLAS every frame, on the compute queue if possible.
- DXR 1.1: the best performance comes from calling TraceRay or RayQuery in compute shaders on a compute queue. Keep one RayQuery object in scope. For DXR 1.0, set max recursion depth to 1.
- The guide does **not** contain an explicit RayQuery-vs-DispatchRays comparison or any RDNA 4 notes.

**RDNA 4 hardware changes**
- The RDNA 4 BVH format adds OBBs and an 8-child node format, replacing 4 children. RRA 1.8 visualises them. — [GPUOpen RRA 1.8](https://gpuopen.com/learn/radeon_raytracing_analyzer_v1-8_support_for_rx9000_series_gpus_and_more/); [GPUOpen OBB article](https://gpuopen.com/?p=64215)
- Hot Chips 2025 (AMD): OBBs, BVH8 with 2x intersectors, and BVH compression. About 10% traversal gain from OBBs alone (geometry dependent) and about 2x traversal versus RDNA 3. — [Hot Chips 2025 AMD slides](https://hc2025.hotchips.org/assets/program/conference/day1/8_amd_pomianowski_final.pdf)
- Chips and Cheese notes a new BVH8 intersection instruction, and that OBBs still leave empty space that causes false hits. — [Chips and Cheese](https://old.chipsandcheese.com/?p=35892)

**NVIDIA (RTX best practices, updated, 25 July 2022, by Juha Sjöholm)** — all from [NVIDIA](https://developer.nvidia.com/blog/best-practices-for-using-nvidia-rtx-ray-tracing-updated/)
- Inline: "all shading happens inline in the shader that casts rays". "When multiple different shading models are required, using DispatchRays ... is a better choice."
- Compute thread groups for inline RT: "8×8 can be used"; "A practical choice for group size could then be 16×8 threads." Warp size is 32.
- TLAS: PREFER_FAST_TRACE and "perform only rebuilds". Static BLAS: PREFER_FAST_TRACE and compaction. Dynamic BLAS: profile PREFER_FAST_TRACE vs PREFER_FAST_BUILD vs neither.
- "BLAS updates are a good choice after limited deformations"; "large deformations after the previous rebuild can lead to non-optimal ray-trace performance". Rebuild periodically and spread rebuilds over frames.
- Barriers: "Do not place resource barriers between consecutive [BLAS build] calls"; "you can have a single global UAV barrier before TLAS build". "All BLAS build calls need unique scratch memory to allow execution without barriers."
- Mark geometry opaque. Use any-hit only where needed, for example alpha testing.

**DXR spec facts** — all from the [DXR functional spec](https://microsoft.github.io/DirectX-Specs/d3d/Raytracing.html)
- `D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT 256`: the "Destination address must be 256 byte aligned".
- Scratch: "The address must be aligned to 256 bytes" and must be in `D3D12_RESOURCE_STATE_UNORDERED_ACCESS`. Its contents are not preserved.
- `D3D12_RAYTRACING_INSTANCE_DESC_BYTE_ALIGNMENT 16`. Shader tables are 64 B aligned and records have a 32 B stride (not needed for inline RT).
- AS memory must be in `D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE`.
- Updates:
  - The source must have `ALLOW_UPDATE`, and the other flags must match.
  - Updates "can be performed in unlimited succession".
  - "If the addresses of the source and destination acceleration structures are identical, the update is performed in-place."
  - For a triangle BLAS, only vertex positions may change.
  - ALLOW_UPDATE "may result in increased memory consumption, build times and lower raytracing performance". An update "can negatively impact raytracing performance", especially after large position changes.

### Inferences
**Ocean BLAS recipe** (513×513 vertices = 512² quads ≈ 524k triangles)
- Build once with `ALLOW_UPDATE | PREFER_FAST_TRACE`. This is NVIDIA's "fastest trace against updateable AS" recipe from the earlier notes, and AMD's "high-quality build flags".
- Then each frame:
  1. Compute displacement → UAV barrier on the vertex buffer.
  2. `BuildRaytracingAccelerationStructure` with `PERFORM_UPDATE`, source = dest (in place).
  3. Global UAV barrier.
  4. TLAS full rebuild (PREFER_FAST_TRACE).
  5. UAV barrier on the TLAS.
  6. Path-trace dispatch.
- Do a full rebuild every N frames (start with about 60) or when significant wave height or choppiness changes. Compare trace time in RRA/PIX to tune N. Skip ALLOW_COMPACTION for the ocean, since it is a single object and is rebuilt.

**Barriers** [Training knowledge]
- With legacy barriers, use `D3D12_RESOURCE_BARRIER_TYPE_UAV` on the AS resource (or a null-resource global UAV barrier) between BLAS build → TLAS build → trace.
- With enhanced barriers, use a global barrier with SyncBefore `D3D12_BARRIER_SYNC_BUILD_RAYTRACING_ACCELERATION_STRUCTURE`, AccessBefore `D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_WRITE`, SyncAfter `..._BUILD_RAYTRACING_ACCELERATION_STRUCTURE | _COMPUTE_SHADING`, AccessAfter `..._READ`.
- These enum names were not re-verified this session. Check them in d3d12.h or the [Enhanced Barriers spec](https://microsoft.github.io/DirectX-Specs/d3d/D3D12EnhancedBarriers.html).

**Thread groups.** Use 8×4 on AMD and 8×8 or 16×8 on NVIDIA. One option is to make the path-trace group size a compile-time define, picked per vendor at hot-reload compile time.

**AS buffers.** Allocate AS buffers in a DEFAULT heap with `ALLOW_UNORDERED_ACCESS`, created in the RAYTRACING_ACCELERATION_STRUCTURE state [Training knowledge]. Size them from `GetRaytracingAccelerationStructurePrebuildInfo`: use ResultDataMaxSizeInBytes, ScratchDataSizeInBytes and UpdateScratchDataSizeInBytes, each rounded up to 256.

### Gaps
- No 2024–2026 vendor source was found that benchmarks inline RayQuery against DispatchRays on RDNA 4 or Ampere. The NVIDIA article is from 2022, and the AMD guide does not date or compare them.
- AMD's RDNA 4-specific RT best-practices page, if one exists, was not found. The RDNA performance guide fetched has no RDNA 4 section.
- The enhanced-barrier enum names for AS were not verified this session.

---

## Q5. WARP and DXR for CI

### Takeaway
Yes. The **Microsoft.Direct3D.WARP** NuGet package (latest **1.0.22**, 2026-10-27, targeting Agility 1.620) supports DXR 1.1 since 1.0.8, feature level 12_2 since 1.0.13, JIT-compiled AS traversal since 1.0.14, SM 6.9 / DXR 1.2 since 1.0.18, real SER reordering since 1.0.21, and dynamically indexed RayQuery arrays since 1.0.22. A small-resolution path-tracer smoke test with a golden-image or statistical comparison on WARP in CI is feasible.

### Cited Findings
- Release notes quoted on the NuGet page:
  - v1.0.8: "DirectX Raytracing (DXR) 1.1 support"
  - v1.0.11: "Shader model 6.8 support"
  - v1.0.13: "WARP now claims feature level 12_2"
  - v1.0.14: "Acceleration structure traversal is JITed" and "Fix a race condition in acceleration structure builds"
  - v1.0.18: SM 6.9, SER ("WARP does not re-order invocations") and OMM
  - v1.0.21: "SER now actually re-orders"
  - v1.0.22: "Support dynamically-index RayQuery arrays", targeting D3D12 Agility SDK 1.620
  - A prerelease 1.65535.22-preview exists.
  - The package contains D3D10Warp.dll "intended for testing and development".
  — [NuGet: Microsoft.Direct3D.WARP](https://www.nuget.org/packages/Microsoft.Direct3D.WARP)
- WARP supports the SM 6.10 preview features, including LinAlg, in the preview package. — [Microsoft 1.721 preview post](https://devblogs.microsoft.com/directx/announcing-agilitysdk-721-preview-and-more-shader-model-6-10-features/); [Microsoft LinAlg preview](https://devblogs.microsoft.com/directx/d3d12-linalg-preview/)
- General WARP documentation. — [Microsoft Learn: DirectX WARP](https://learn.microsoft.com/en-us/windows/win32/direct3darticles/directx-warp)

### Inferences
**CI setup**
1. Restore or unzip WARP 1.0.22 and Agility 1.620.2.
2. Copy `D3D10Warp.dll` next to the test exe. That location is [Training knowledge, unverified]; the NuGet page does not say where to put it.
3. Create the device with `IDXGIFactory4::EnumWarpAdapter`.
4. Assert that `RaytracingTier >= 1_1` and that `D3D10Warp.dll` loaded from the app folder (check with GetModuleFileName). The OS inbox WARP is older and may lack DXR.

**Smoke test**
- Use a 64×64 to 256×256 image and a tiny scene: a Cornell box plus a 33×33 ocean patch, BLAS refit included. Use a fixed RNG seed and accumulate 16–64 frames.
- Compare against a stored reference with a tolerance (mean and RMSE), not exact bytes, because WARP versions can change float rounding.
- WARP is a CPU rasteriser, so keep CI dispatches small. The 513² ocean refit works, just slowly.

### Gaps
- The NuGet page does not document where to place D3D10Warp.dll, or whether the Agility SDK is required with it. Check the [WARP Learn page](https://learn.microsoft.com/en-us/windows/win32/direct3darticles/directx-warp) or the package README.
- No published measurements were found for WARP DXR performance or accuracy.

---

## Q6. Debug and validation tools

### Takeaway
- **Both vendors:** use DRED with auto-breadcrumbs and page-fault reporting (op names below) and PIX, which supports all retail Agility features.
- **AMD:** Radeon Developer Panel → RRA (BVH viewer, which understands RDNA 4 BVH8/OBB) and RGP. Launch RDP *before* the app.
- **NVIDIA:** Nsight Graphics (AS viewer, GPU trace).
- GPU-based validation of ray-tracing shaders was not confirmed this session.

### Cited Findings
- DRED auto-breadcrumb ops relevant to DXR and compute: `D3D12_AUTO_BREADCRUMB_OP_DISPATCH` (6), `_RESOURCEBARRIER` (15), `_BUILDRAYTRACINGACCELERATIONSTRUCTURE` (31), `_EMITRAYTRACINGACCELERATIONSTRUCTUREPOSTBUILDINFO` (32), `_COPYRAYTRACINGACCELERATIONSTRUCTURE` (33), `_DISPATCHRAYS` (34), `_SETPIPELINESTATE1` (39), `_BARRIER` (45, enhanced barriers), `_BEGIN_COMMAND_LIST` (46), `_DISPATCHGRAPH` (47), `_SETPROGRAM` (48). — [Microsoft Learn: D3D12_AUTO_BREADCRUMB_OP](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_auto_breadcrumb_op)
- Inline RayQuery inside a compute shader shows up as a plain `DISPATCH` breadcrumb, not DISPATCHRAYS. This is inferred from the enum: there is no separate inline op.
- PIX: "PIX supports all retail and preview features released here" (SM 6.9 / DXR 1.2). See the PIX 2602.25 blog. — [Microsoft SM 6.9 retail](https://devblogs.microsoft.com/directx/shader-model-6-9-retail-and-more/)
- 1.620 retail adds DX Dump Files and driver-reported messages for GPU diagnostics. — [Agility SDK downloads](https://devblogs.microsoft.com/directx/directx12agility/)
- AMD tools:
  - RGP shows the shader table, profiles DXR workloads and shows RT shader disassembly. RRA visualises BVHs, BVH memory, and instance and triangle counts. — [GPUOpen RDNA performance guide](https://gpuopen.com/learn/rdna-performance-guide/)
  - The Radeon Developer Panel captures RRA scenes. The app "must NOT already be running", and RDP must be configured first. Choose the capture target, then "Capture scene". User markers need both Raytracing and Profiling enabled. — [RDP manual](https://gpuopen.com/manuals/rdp_manual/); [RRA quickstart](https://gpuopen.com/manuals/rra_manual/quickstart/); [RRA 1.7 user markers](https://gpuopen.com/learn/amd_radeon_raytracing_analyzer_1-7_adds_user_marker_support_for_ray_dispatches/)
  - RRA 1.8 added RX 9000 support. RRA 1.11.1 inspects CLAS/PTLAS. — [GPUOpen RRA 1.8](https://gpuopen.com/learn/radeon_raytracing_analyzer_v1-8_support_for_rx9000_series_gpus_and_more/); [AMD GPUOpen Oct 2026](https://gpuopen.com/learn/amd-directx12-agility-sdk-october-2026/)
  - Shader-source features in the Radeon tools need shader instrumentation and RX 9000+. Extended PIX markers need `ID3D12DeviceTools2::SetUserDefinedAnnotationMode(D3D12_USER_DEFINED_ANNOTATION_MODE_DRIVER_RETAIL)`. — [GPUOpen tool-suite update](https://gpuopen.com/learn/radeon-developer-tool-suite-shader-source-code/)
  - Radeon GPU Detective (RGD) is AMD's crash-analysis tool, a TDR companion to DRED. — [RGD 1.0 tutorial](https://gpuopen.com/learn/rgd_1_0_tutorial/)

### Inferences
- **Debug config:**
  - Debug layer with break-on-warning.
  - DRED: `ID3D12DeviceRemovedExtendedDataSettings1` with AutoBreadcrumbs, PageFault and BreadcrumbContext set to FORCED_ON [Training knowledge].
  - Insert PIX markers around "Displace", "BLAS refit", "TLAS build" and "PathTrace". These show up in RGP, RRA and Nsight, and as breadcrumb context.
- **GPU-based validation:** run GBV in a separate config (it is slow), and keep it off in normal dev runs. Historically, GBV did not fully cover DXR shader-table and AS validation [Training knowledge, unverified].
- **Nsight Graphics on the RTX 3070:** frame debugger → Acceleration Structure Viewer, plus GPU Trace for RT core utilisation [Training knowledge; not fetched].

### Gaps
- No 2026 page was fetched confirming GPU-based-validation coverage of inline RayQuery or AS, or the current Nsight Graphics version and features.
- The RDP manual's hardware list showed RX 7000/6000/5000 in the snippet. RX 9000 support is implied by RRA 1.8 but not confirmed on the RDP page.

---

## Q7. TDR and keeping per-dispatch work small

### Takeaway
The default TDR timeout is **2 s** (`TdrDelay`). For development only, raise `HKLM\System\CurrentControlSet\Control\GraphicsDrivers\TdrDelay` (REG_DWORD, seconds), for example to 10–60, and reboot. The real fix is a bounded per-dispatch budget: progressive accumulation at 1 spp per frame, bounded bounce counts, and optionally tiled dispatches.

### Cited Findings
- All under `HKEY_LOCAL_MACHINE\System\CurrentControlSet\Control\GraphicsDrivers`:
  - `TdrDelay`: "Number of seconds to delay. The default value is 2 seconds." It "is effectively the timeout threshold."
  - `TdrDdiDelay`: default 5 s.
  - `TdrLevel`: 0 = off, 3 = recover (default).
  - `TdrDebugMode`: 1 = ignore timeout.
  - `TdrLimitTime`: 60 s. `TdrLimitCount`: 5.
  - Microsoft: "End users shouldn't manipulate these registry keys. Applications shouldn't manipulate these registry keys outside of targeted testing or debugging".
  — [Microsoft Learn: TDR registry keys](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/tdr-registry-keys)

### Inferences
- Set TdrDelay to 10 on dev machines (the registry edit needs a reboot [Training knowledge]). Don't use TdrLevel 0, because a real hang then freezes the desktop.
- **Budget rules:**
  - 1 path per pixel per dispatch.
  - A max bounce cap, for example 8, plus Russian roulette.
  - A hard loop counter inside the `while(q.Proceed())` loop is unnecessary for opaque rays, but cap any user loops.
  - For "reference mode" with many spp, accumulate across frames rather than looping spp inside a single dispatch.
  - For very slow configurations (WARP, debug with GBV), split the screen into tiles, one dispatch per tile, with ExecuteCommandLists between them so that each submission stays short.

### Gaps
- No vendor-specific TDR guidance for ray tracing was fetched.

---

## Q8. SM 6.10 / DirectX linear algebra (`linalg`) status

### Takeaway
As of 2026-11-18, SM 6.10, including `linalg::Matrix`, is **preview only** (Agility 1.720/1.721/1.722-preview, DXC 1.10.2605.x). Retail is planned for Agility 1.621 in early December 2026, which has not shipped yet.
- **AMD:** RX 9000 supports it with an AMD Agility Developer Preview driver.
- **NVIDIA:** a developer driver is available only through developer relations. "All RTX hardware" is listed for LinAlg VectorAccumulate, but Ampere is not named.
- **WARP:** supports it.
- Linalg is a tiered capability (`D3D12_FEATURE_LINEAR_ALGEBRA_SUPPORT`, Tier 1+), so an NRC port must keep a fallback.

### Cited Findings
- The LinAlg preview needs Agility 1.720.1-preview, a preview DXC with SM 6.10, and `D3D12EnableExperimentalFeatures`. Query `D3D12_FEATURE_LINEAR_ALGEBRA_SUPPORT` for Tier 1 or higher.
  - AMD: "Linear Algebra Matrix is supported on AMD Radeon™ RX 9000 series", with driver 25.30.41.02 (Developer Preview).
  - NVIDIA: "Contact your developer relations representative for in-development driver access."
  - Intel: support planned on XMX.
  - WARP: supported in the latest preview package.
  - LinAlg "subsumes" the Cooperative Vectors APIs (MatrixScope::Thread was previously Cooperative Vectors).
  - The examples use F16 and F8_E4M3FN.
  - "We will continue to evolve the LinAlg Matrix APIs over the preview period."
  — [Microsoft: D3D12 LinAlg preview](https://devblogs.microsoft.com/directx/d3d12-linalg-preview/)
- The 1.721 preview post (May 28, 2026) adds LinAlg VectorAccumulate. Support: "AMD Radeon™ RX 9000 series hardware", Intel "Xe2 or newer (in a future driver)", NVIDIA "All RTX hardware". AMD preview driver "26.10.07.02". WARP "supports all these features". — [Microsoft 1.721 preview](https://devblogs.microsoft.com/directx/announcing-agilitysdk-721-preview-and-more-shader-model-6-10-features/)
- The SM 6.10 feature set includes linalg::Matrix, Group Wave Index, variable groupshared memory and the RT intrinsics TriangleObjectPositions and ClusterID. — [Agility SDK downloads](https://devblogs.microsoft.com/directx/directx12agility/)
- Press: SM 6.10 lifts the 32 KB groupshared cap via `MaxGroupSharedMemoryPerGroup`. NVIDIA preview support needs developer drivers. — search summaries of [TweakTown](https://www.tweaktown.com/news/111351/microsoft-previews-shader-model-6-10-with-a-matrix-math-api-making-neural-rendering-a-standard-directx-feature/index.html) and [Wccftech](https://wccftech.com/microsoft-shader-model-6-10-agilitysdk-720-preview-now-available-dx12-neural-rendering/)

### Inferences
- An HLSL NRC port should wait for 1.621 retail plus public NVIDIA drivers. On the RTX 3070, check the Tier at runtime: "All RTX hardware" suggests it will work, but performance on Ampere's tensor cores through linalg is unknown.
- Keep SHaRC (plain HLSL) as the baseline cache on both GPUs. That matches the earlier notes.

### Gaps
- No confirmed NVIDIA public driver version for SM 6.10 was found.
- No explicit statement on Ampere (RTX 30) linalg support or its tier was found.
- The 1.621 retail date and final feature list are unconfirmed; it is a plan only.

---

## Q9. Learning references with code; relevant local samples

### Takeaway
None of the local DirectX-Graphics-Samples D3D12Raytracing samples use inline RayQuery: a Grep for `RayQuery` / `TraceRayInline` across the whole local repo found **no matches**. They are all DispatchRays/state-object samples. They are still the best local reference for AS building, the SER and OMM APIs, and the AO denoiser. Inline-RayQuery code has to come from the DXR spec examples, NVIDIA/AMD samples, Capsaicin or RTXPT.

### Cited Findings
**Local sample projects** under `T:\Projects\DX12\Samples\DirectX-Graphics-Samples\Samples\Desktop\D3D12Raytracing\src\` (from a local Glob):
| Project | Relevance |
|---|---|
| `D3D12RaytracingHelloWorld` | Minimal device check, BLAS/TLAS build, DispatchRays. Start here for M0/M1 AS code. |
| `D3D12RaytracingSimpleLighting` | Index/vertex buffers through descriptor tables, normals, simple shading. |
| `D3D12RaytracingProceduralGeometry` | AABB geometry plus intersection shaders (the ocean heightfield-march alternative). |
| `D3D12RaytracingLibrarySubobjects` | State-object subobjects, for the later DXR pipeline. |
| `D3D12RaytracingMiniEngineSample` (ModelViewer_VS16) | RT in a larger engine (MiniEngine); reflections and shadows. |
| `D3D12RaytracingRealTimeDenoisedAmbientOcclusion` | RTAO plus a temporal and spatial denoiser, with AS management for many instances. Useful for M3 accumulation and denoising. |
| `D3D12RaytracingHelloShaderExecutionReordering`, `D3D12RaytracingSakuraForestSER` | DXR 1.2 SER (HitObject / MaybeReorderThread), for later. |
| `D3D12RaytracingOpacityMicromaps`, `D3D12OMMOfflineBaker` | DXR 1.2 OMM (alpha). |

- Also in the local repo: `Libraries\D3D12RaytracingFallback`, the deprecated compute fallback layer. It is historical only.
- Microsoft's spec is the canonical inline-RT code reference: TraceRayInline examples 1–3, including the alpha-test loop. — [DXR functional spec](https://microsoft.github.io/DirectX-Specs/d3d/Raytracing.html)

**Other references** [Training knowledge; URLs not fetched this session]
- Chris Wyman et al., "Introduction to DirectX Raytracing" (SIGGRAPH 2018 course; 2019 refresh), with DXR tutorials built on Falcor. — [cwyman.org/code/dxrTutors](http://cwyman.org/code/dxrTutors/dxr_tutors.md.html); [intro-to-dxr.cwyman.org](http://intro-to-dxr.cwyman.org/)
- Will Usher, "The RTX Shader Binding Table Three Ways", plus his DXR/Vulkan ChameleonRT path tracer (DX12 backend). — [willusher.io SBT post](https://www.willusher.io/graphics/2019/11/20/the-sbt-three-ways); [ChameleonRT](https://github.com/Twinklebear/ChameleonRT)
- NVIDIA DXR tutorial parts 1–2 (DispatchRays), plus nvpro-samples. — [NVIDIA DX12 RT tutorial](https://developer.nvidia.com/rtx/raytracing/dxr/dx12-raytracing-tutorial-part-1)
- "Ray Tracing in One Weekend" (Shirley): the CPU canon for material and sampling logic to port into HLSL. — [raytracing.github.io](https://raytracing.github.io/)
- Jacco Bikker: "How to build a BVH" series and the Breda Universities (BUas) graphics course material. Useful to understand BVH quality and why refits degrade. — [jacco.ompf2.com BVH series](https://jacco.ompf2.com/2022/04/13/how-to-build-a-bvh-part-1-basics/)
- AMD Capsaicin (D3D12, MIT): its reference path tracer and GI-1.x use inline ray queries through the `gfx` layer. Treat the RayQuery claim as unverified. — [Capsaicin](https://github.com/GPUOpen-LibrariesAndSDKs/Capsaicin)
- NVIDIA RTXPT (DX12 pure PT; SHaRC integration reference). — [RTXPT](https://github.com/NVIDIA-RTX/RTXPT); [SHARC](https://github.com/NVIDIA-RTX/SHARC)
- "Ray Tracing Gems" I and II, free PDFs. Chapters on refits, ray offsets ("A Fast and Robust Method for Avoiding Self-Intersection", RTG1 ch. 6) and sampling. — [realtimerendering.com/raytracinggems](https://www.realtimerendering.com/raytracinggems/)

### Inferences
- **Suggested reading order for M0–M3:**
  1. HelloWorld, for device and AS code. Port it to inline: replace DispatchRays with a compute shader using `RayQuery`.
  2. The spec's TraceRayInline examples.
  3. SimpleLighting, for vertex and index fetch from barycentrics in a descriptor table.
  4. RTAO denoiser, for accumulation and reprojection.
  5. Wyman's tutors and Ray Tracing in One Weekend, for path-tracing logic.
  6. RTXPT and SHARC later.
- Use RTG1 ch. 6 for the self-intersection offset. It matters a lot on a refit ocean surface with grazing rays.

### Gaps
- None of the Q9 external tutorial URLs were fetched this session, so their current availability is unverified.
- No current (2025–2026) "inline RayQuery path tracer in DX12" tutorial was found.
- Capsaicin's use of RayQuery is not verified.
