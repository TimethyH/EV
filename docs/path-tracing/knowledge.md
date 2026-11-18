# Path tracing in EV – tutor knowledge base

Written 2026-11-18 for branch `path-tracing` (HEAD `6cf3e9b` at the time of writing). Main reference for every Claude
session that tutors this project. Decisions and roadmap: `plan.md` (**source of truth**; if anything here disagrees
with `plan.md`, `plan.md` wins and this file must be fixed). Progress: `build-log.md`. Raw sources: `research/`
(index in `research/README.md`).

Labels used throughout:

| Label | Meaning |
|---|---|
| (no label + link) | Verified in the research notes against the linked source (2026-11-18) |
| **[T]** | Standard theory / training knowledge, not re-fetched. Reliable in general, but check exact constants before relying on them |
| **[I]** | Inference by the researcher or by this document, derived from cited facts |
| **[E]** | Estimate (rule-of-thumb arithmetic, extrapolation). Must be replaced by a measurement |
| **[U]** | Unverified: a claim or URL that must be checked before use (all collected in section 12) |

Paths: engine files are relative to `EV-Engine/EV-Engine/` (e.g. `source/core/application.cpp`), app files to
`EV-Engine/OceanRenderer/`. Line numbers are for `path-tracing` at `6cf3e9b`; they drift as the developer edits, so
re-grep before quoting them.

---

## 0. How to use this document

### 0.1 Rules for the tutor

1. **The developer writes all code** (learning project, "learning mode"). Claude does not write the implementation:
   no complete functions, classes, shaders or kernels. Allowed: explanations, formulas, API names and signatures,
   pointers to exact EV files/lines, small *reference* snippets of a few lines (always marked "reference", ideally
   pseudo-code or the minimal API call shape), review comments, debugging help, test design.
2. **Teach before pointing.** For each step: what the concept is, why it is needed here, which EV code it touches,
   what can go wrong, and how to verify it. Ask the developer to predict results before running a test.
3. **Review like a senior engineer**: correctness first (barriers, states, lifetimes, pdfs, units), then conventions
   (section 2.8), then performance. Point at the line and explain the failure mode; let the developer fix it.
4. **Debug by hypothesis**: ask for the symptom, a capture (PIX / RRA / Nsight / Render Graph panel / Texture
   Viewer), the debug-layer message or DRED report (`ev_crash.log`), then narrow down. Suggest a visual debug
   output or a CPU reference before touching code.
5. **Unknowns get researched, not guessed.** If something is not in this file (or is marked [U]), search the web,
   prefer primary sources (specs, headers, vendor docs, source code), and **add the verified fact here** with its
   URL and the date. Downgrade or fix facts that turn out wrong.
6. **Keep `build-log.md` updated** after every step: date, milestone, what was built and by whom, results and
   measurements (GPU, driver, resolution, timings), problems, next step. Newest entry at the bottom.
7. **Keep this file honest.** When a milestone produces measurements (BLAS refit time, denoiser time, VRAM), put
   them in the relevant section and replace the [E] values.
8. **Both PCs, always.** Everything traced or in HLSL must run on the RX 9070 XT (main PC) and the RTX 3070.
   Vendor-only pieces (OptiX, NRC SDK, tiny-cuda-nn, DLSS-RR, FSR Ray Regeneration) are optional add-ons behind
   capability checks. WARP CI must stay green with path tracing off.
9. **Debug layer stays silent.** EV breaks on warnings in Debug (`application.cpp:516`). A DXR warning is a bug.
10. **Git**: work stays on `path-tracing` (D18). Commit only when the developer asks; no Claude co-author line.
11. **Comments**: the developer's own convention for new code is a file header, a comment per function and per
    logical block, explaining *why* (and explaining graphics concepts in plain words). Review for that.

### 0.2 Index

| § | Topic | Use it for |
|---|---|---|
| 1 | Hardware & capability matrix | "Does X work on the 9070 XT / 3070 / WARP?", driver floors, VRAM |
| 2 | EV engine reference | Where to hook in, gaps, ocean specifics, scene files, conventions |
| 3 | Path tracing theory | Rendering equation, MC, NEE, MIS, RR, GGX/VNDF, Fresnel, env sampling, samplers |
| 4 | Spectral rendering | HWSS equations, pbrt constants, Jakob–Hanika lookup, CMF/D65 data, Peters, tests |
| 5 | Water optics & media | Absorption/scattering/IOR data, phase function, VolPath rules, caustics |
| 6 | DX12 / DXR practicalities | Agility SDK, DXC, RayQuery, AS flags/barriers, ocean BLAS, tools, TDR, WARP |
| 7 | GPU compute (HIP + CUDA) | Toolchains, DLL layout, interop recipe, portability header, WMMA |
| 8 | Denoising | IDenoiser, OIDN, OptiX, temporal rules, timings, NRD/SVGF |
| 9 | Radiance caches & NRC | SHaRC, NRC paper numbers, the math to implement, data layouts, references |
| 10 | Milestone tutor guide M0–M11 | Per milestone: concepts, files, steps, tests, pitfalls, reading |
| 11 | Learning resources | Per track, with URLs |
| 12 | Open questions | Everything a tutor must verify before relying on it |
| 13 | Source index | Which research file covers what |

### 0.3 The 18 decisions in one breath (from `plan.md`)

D1 engine-core `raytracing` module with geometry providers · D2 DXR 1.1 inline RayQuery in compute, API-agnostic
`.hlsli` · D3 Agility SDK retail ≥1.619 + newer DXC next to the exe · D4 staged fixed descriptor table (M1–M7), then
persistent bindless heap (M8a) · D5 render-graph `.External()` pass around a shared fence · D6 compute-displaced
513×513 ocean grid, `ALLOW_UPDATE` BLAS refit per frame + rebuild every N; frozen time → build once `PREFER_FAST_TRACE`
· D7 reference mode auto-freezes ocean time + animated point light; scene-version counter resets accumulation · D8
reference mode first, real-time later · D9 4 wavelengths in a `float4`, 360–830 nm, pbrt `SampleVisible`, terminate
secondaries on dispersion · D10 Jakob–Hanika runtime table first, swappable HLSL interface (Peters later), film
RGB/XYZ · D11 shared GGX+Lambert BSDF (VNDF) + spectral water material · D12 `IDenoiser`: OIDN first (HIP/CUDA/CPU),
OptiX AOV second, accumulate-only fallback · D13 runtime DXC for engine PT shaders (hot reload) · D14 one CUDA-style
source → `EV-Hip.dll` + `EV-Cuda.dll`, chosen by vendor ID + LUID · D15 SHaRC → own NRC (CPU → HIP → offline → online →
fast); NRC SDK / tiny-cuda-nn only as references · D16 capability-gated fallbacks · D17 HDR sky with importance
sampling + sun disk (NEE+MIS) + point lights · D18 branch from `scene-save-load`, docs in `docs/path-tracing/`.

---

## 1. Hardware & capability matrix

### 1.1 The three machines

| | Main dev PC | Second PC | CI |
|---|---|---|---|
| GPU | AMD Radeon RX 9070 XT, RDNA 4, gfx1201 | NVIDIA RTX 3070, Ampere GA104, compute capability 8.6, **8 GB** | WARP (software, no GPU) |
| Role | Daily development, everything except CUDA/OptiX | CUDA/OptiX builds, NVIDIA references, comparisons | Build + smoke test |
| Wave/warp | wave32 for HIP (wave64 unsupported for HIP on gfx10+) ([HIP lang ext](https://rocm.docs.amd.com/projects/HIP/en/latest/how-to/hip_cpp_language_extensions.html)) | warp 32 | – |
| Shared memory | 64 KB per workgroup, 128 KB LDS per WGP (query `sharedMemPerBlock`, expected 65536 [U]) ([ROCm specs](https://rocm.docs.amd.com/en/latest/reference/gpu-arch-specs.html)) | 48 KB default, 99 KB per block opt-in, 100 KB per SM ([CUDA CC tables](https://docs.nvidia.com/cuda/cuda-programming-guide/05-appendices/compute-capabilities.html)) | – |
| Other | 64 CUs, VGPR file 768 KiB, L2 8 MiB, Infinity Cache 64 MiB, GFXIP 12.0; RT: BVH8 nodes + oriented bounding boxes in hardware, ~10 % traversal gain from OBBs (geometry dependent) ([Hot Chips 2025](https://hc2025.hotchips.org/assets/program/conference/day1/8_amd_pomianowski_final.pdf), [RRA 1.8](https://gpuopen.com/learn/radeon_raytracing_analyzer_v1-8_support_for_rx9000_series_gpus_and_more/)) | 184 3rd-gen Tensor Cores, 1536 threads/SM, 64K regs/SM, 255 regs/thread | – |

### 1.2 Capability matrix

| Capability | RX 9070 XT | RTX 3070 | WARP CI |
|---|---|---|---|
| DXR 1.1 inline RayQuery (`RaytracingTier ≥ 1_1`) | Yes [I] (DX12 Ultimate) | Yes [I] (since 2020) | Yes with **Microsoft.Direct3D.WARP NuGet ≥ 1.0.8**; inbox WARP: assume no ([NuGet WARP](https://www.nuget.org/packages/Microsoft.Direct3D.WARP)) |
| Exact `RaytracingTier` reported | Unknown – may be 1.1 not 1.2 because OMM is not listed for AMD [U] → **query and log** | Unknown [U] → query and log | 1.0.18+: SM 6.9/DXR 1.2 |
| Shader model with Agility 1.619+ | 6.9 with Adrenalin **26.2.1+** ([SM 6.9 retail](https://devblogs.microsoft.com/directx/shader-model-6-9-retail-and-more/)) | 6.9 with driver **595+**; "All RTX hardware" listed for SM 6.9 features | 6.9 (1.0.18+) |
| SER (DXR 1.2) | API accepted, **does not reorder** | API accepted, **reorder is a no-op on RTX 20/30** ([SER post](https://devblogs.microsoft.com/directx/ser/)); query `D3D12_FEATURE_DATA_D3D12_OPTIONS22::ShaderExecutionReorderingActuallyReorders` (should be FALSE) | 1.0.21+: actually reorders |
| OMM | Not listed by Microsoft | **Software-emulated** below RTX 40 (may be slower than any-hit alpha [U]) | 1.0.18+ |
| `dx::linalg` (SM 6.10) | Preview only (Agility 1.722-preview + AMD preview driver 26.20.12.01) | Preview only via NVIDIA DevRel drivers; "All RTX hardware" listed for VectorAccumulate, Ampere not named [U] | Preview package supports it |
| HIP | Legacy **HIP SDK 7.2.x**: Runtime + SDK + ROCgdb ✅ for gfx1201 ([sys req](https://rocm.docs.amd.com/projects/install-on-windows/en/latest/reference/system-requirements.html)); ROCm Core SDK 10.1.0 also supports gfx1201 | – | – |
| CUDA 13.5 | – | sm_86 fully supported; new 13.5 features need **R620+**, 13.x minor-version floor R580 ([CUDA 13.5 notes](https://docs.nvidia.com/cuda/cuda-toolkit-release-notes/index.html)) | – |
| OptiX 9.1 denoiser | – (ZLUDA: OptiX "won't work", hobby project ([Phoronix](https://phoronix.com/news/ZLUDA-6-Released))) | Yes: Turing+, **R590+** ([OptiX download](https://developer.nvidia.com/designworks/optix/download)) | – |
| OIDN 2.5.1 | HIP device (RDNA 4 since 2.3.3), Adrenalin **25.30.02.01+**; D3D12 buffer + fence import on Windows ([OIDN README](https://raw.githubusercontent.com/RenderKit/oidn/master/README.md)) | CUDA device, buffer + fence import | CPU device only (**CPU cannot import external memory** → needs a CPU copy path or stay accumulate-only) |
| NRD v4.18.0 | HLSL compute, AMD by inference [I] | Yes | Possibly (tiny resolutions) [U] |
| SHaRC 1.8.3 | Yes ("any DXR GPU"; needs native fp16, SM 6.2+, `-enable-16bit-types`) | Yes | Possibly [U] |
| NVIDIA NRC SDK v0.16.0 (RTXGI 2.9.0) | No | Yes (Turing+, Tensor Cores, "experimental", ships CUDA 13 runtime DLLs). Driver floor "617.42" is **not** in any current README/guide → unverified | No |
| tiny-cuda-nn | No | Yes; FullyFusedMLP width 64 needs ~20 KB smem → fits sm_86 [I] | No |
| DLSS-RR (Streamline 2.14.16) | No | Yes | No |
| FSR Ray Regeneration 1.2.1 / FSR Radiance Caching 0.9.0 | Yes (RDNA 4 only, SM 6.6, Win 11) | No | No |
| Debugger for GPU compute | ROCgdb (HIP SDK 7.2, gfx120x only, single AMD GPU → disable iGPU) | Nsight VSE / cuda-gdb [T] | – |

Consequence (plan.md): everything traced and everything HLSL runs on both PCs; GPU compute is one source built twice;
vendor extras are never required.

### 1.3 Driver floors (collected)

| Machine | Requirement | Floor |
|---|---|---|
| RTX 3070 | OptiX 9.1 | R590+ |
| | SM 6.9 / DXR 1.2 retail | 595+ (RTXPT wants 595.71+) |
| | CUDA 13.5 new features, Nsight Graphics 2026.4 | **R620+** → install an R620-branch driver; it satisfies everything ([followup_rtx3070_optix](research/followup_rtx3070_optix.md)) |
| RX 9070 XT | SM 6.9 retail | Adrenalin 26.2.1+ |
| | OIDN HIP | Adrenalin 25.30.02.01+ |
| | ROCm Core SDK 10.1 | Adrenalin 26.10.41.05 named |
| | ROCgdb (HIP SDK 7.2) | wants the **driver bundled with the SDK** ("Radeon Software PRO 25.30") – conflicts with newest Adrenalin; decide per debugging session |
| | Preview features (CLAS/PTLAS, SM 6.10) | "Agility SDK Developer Preview Edition 26.20.12.01" – experiment branches only |

### 1.4 VRAM budget on the 8 GB RTX 3070 (1080p)

| Item | Size | Source |
|---|---|---|
| NRC SDK | ~192 MB (191.793 MB; QueryRadianceParams 118.6 MB) | [NRC guide](https://raw.githubusercontent.com/NVIDIA-RTX/RTXGI/main/Docs/NrcGuide.md) |
| SHaRC 2^22 entries | 160 MiB (256 MiB with SH directional) | [SHaRC guide](https://raw.githubusercontent.com/NVIDIA-RTX/RTXGI/main/Docs/SharcGuide.md) |
| OptiX denoiser state + scratch | ~0.1–0.3 GB [E] (anecdotes; log `OptixDenoiserSizes`) | |
| Interop staging (≈6 buffers) | 1920×1080×16 B = 33 MB per FLOAT4 buffer; half with HALF4 | arithmetic |
| G-buffer, accumulation (RGBA32F = 33 MB), reservoirs | ~0.2–0.5 GB [E] | |
| CUDA context, desktop/DWM | a few hundred MB each [U] | |
| Ocean BLAS (524k tris) | ~31–42 MB [E] (60–80 B/tri) | [Gruen HPG 2020](https://highperformancegraphics.org/slides20/monday_gruen.pdf) |

≈1.5–2 GB before scene geometry/textures → ~5–6 GB left [E]. Rules: compact static BLASes (~50 %), HALF4 interop
buffers, never run NRC and SHaRC together, track the budget with `IDXGIAdapter3::QueryVideoMemoryInfo` [T].
At 1440p screen-sized items grow 1.78×; SHaRC does not.

---

## 2. EV engine reference for this work

### 2.1 Architecture summary (what matters for path tracing)

EV started as Jeremiah van Oosten's LearningDirectX12 tutorial (3dgep); the editor, hot reload, render graph, crash
reporting, scene files and play mode were added on top. **No DXR code exists** (grep for
`Raytracing|DXR|RayQuery|AccelerationStructure|BLAS|TLAS|OPTIONS5|StateObject|DispatchRays|HEAP_FLAG_SHARED|
CreateSharedHandle|D3D12SDKVersion` finds nothing). Helpful pieces that exist: vendored `d3dx12` split headers incl.
`d3dx12_state_object.h` (`CD3DX12_STATE_OBJECT_DESC`) and `d3dx12_check_feature_support.h` (`CD3DX12FeatureSupport`),
a render graph that derives barriers, `GpuTimer` + Render Graph panel (time and capture any pass), Texture Viewer,
hot-reloadable `cs_*` shaders via DXC.

**Application** (`header/core/application.h`, `source/core/application.cpp`) – singleton owning adapter, device,
three `CommandQueue`s (direct/compute/copy, 414-416), descriptor allocators, `ShaderLibrary`.
- `Initialize` 353-439: crash log rotation 357, handlers 361-362, debug layer (`_DEBUG` only, no GBV) 364-378, DRED
  auto-breadcrumbs + page faults forced on in all builds 380-386, WARP via `EV_USE_WARP=1` or as fallback 388-396,
  device-removed fence + thread-pool wait 401-407, tearing check 418, **only feature check: root signature 1.1**
  428-438 (end of function 439).
- `CreateAdapter(bool useWarp)` 441-482: `EnumAdapters1`, picks the non-software adapter with **most dedicated VRAM**
  that can create FL 11_0; returns `IDXGIAdapter4`; **LUID not stored**.
- `CreateDevice` 501-562: `D3D12CreateDevice(adapter, FL_11_0, ID3D12Device13)` at **504** (type in
  `application.h:66,195,210`). Device13 fails on older Windows (`Dads-Birthday` downgraded to Device2 in `5ce7993`,
  which is *not* enough for DXR). Info queue 509-558: break on CORRUPTION/ERROR/**WARNING** (514-516), callback
  `OnDebugLayerMessage` logs to `ev_crash.log` (844-851, 552-557).
- Factories (wrapped `std::shared_ptr` via `Make*` subclasses 52-261): `CreateTexture` 1231/1240, `CreateBuffer`
  1397 (default heap, for the render graph), `CreateGUI` 1402, `LoadShader` 586, `DoCreatePipelineStateObject` 596,
  `AllocateDescriptors` 1195, `CreateDescriptorHeap` 731-743 (CPU-only heaps).
- `GetBreadcrumbOpName` 1265-1290 (no DXR ops), `GetDeviceRemovedReport` 1308-1370.
- Frame counter: `m_frameCount` declared 47, reset 420, getter 752, **`++Application::m_frameCount` commented out at
  993** (inside `WndProc` `WM_PAINT` 988-1000, which calls `ShaderLibrary::Update()` then `OnUpdate`).

**CommandQueue** (`header/DX12/command_queue.h`, `source/DX12/command_queue.cpp`): one queue + one fence
(`D3D12_FENCE_FLAG_NONE`, line 27); `ExecuteCommandLists` 115-177 resolves pending barriers from the global tracker,
executes, signals; mip-generation lists run on the compute queue after a GPU wait (169-174) but **nothing makes the
direct queue wait for them** (callers CPU-wait); `Wait(const CommandQueue&)` 179-182 only for EV queues; recycle
thread 189-214; `GetCommandQueue()` exposes the raw queue.

**CommandList** (`header/DX12/command_list.h`, `source/DX12/command_list.cpp`): wraps **`ID3D12GraphicsCommandList2`**
(`command_list.h:93,511`, created `command_list.cpp:47`). Owns an `UploadBuffer` (2 MB pages, `upload_buffer.h:22`;
larger allocations **throw**, `upload_buffer.cpp:22-24`), a `ResourceStateTracker`, one `DynamicDescriptorHeap` per
type, tracked-object list (`TrackResource` 1623 keeps D3D objects alive until reset). Relevant methods:
`TransitionBarrier` 71-94, `UAVBarrier()`/`UAVBarrier(resource)` 96-123, `CopyBuffer` 202-264 (committed default
buffer in COMMON + per-call upload resource), `LoadTextureFromFile` 303-409, `CreatePlane` 984-1054,
`SetPipelineState` **caches the last PSO** 1261-1274, root signature cache shared graphics/compute 1276-1312,
`SetShaderResourceView(srv)` skips the transition for a null-resource SRV 1320-1336, `Draw`/`Dispatch` 1518-1552
(`Dispatch` 1542: flush barriers, commit staged descriptors), VB/IB transitions per draw 1168/1202,
`LoadSceneFromFile` 1718, `CreateCube/Sphere/Plane` 1731-1756. No structured/byte-address buffer class (commented out
287-296).

**ResourceStateTracker** (`header/DX12/resource_state_tracker.h` 222-235 global map): per-list known states +
static global map keyed by `ID3D12Resource*`; first use becomes a pending barrier resolved at submit (cpp 90-95);
same-subresource transitions merged (24-47). Resources must be registered at creation (`Resource` ctor registers
COMMON, `resource.cpp:109`).

**Resources** (`header/resources/*.h`): `Resource` ctor always does `CreateCommittedResource(DEFAULT,
HEAP_FLAG_NONE, desc, COMMON)` (`resource.cpp:94-112`); the `ComPtr` ctor (114-123) wraps external objects (caller
registers the state). `Buffer` (abstract), `VertexBuffer`, `IndexBuffer`; no SRV/UAV on buffers yet.
`ShaderResourceView` accepts a null resource when a desc is given (`shader_resource_view.cpp:16-23`) → **a TLAS SRV
through a descriptor table works with existing code**. A root SRV path would try to transition the AS → don't.
`Texture` views in `CreateViews` (`texture.cpp:242-360`), `Resize` recreates.

**Descriptors** (biggest structural obstacle): CPU heaps in pages of 256 (`descriptor_allocator.h:20`), stale ones
freed after `Present` (`swapchain.cpp:189`); GPU-visible `DynamicDescriptorHeap` with **1024 descriptors per heap**
(`dynamic_descriptor_heap.h:23`), staged per root parameter and copied at draw/dispatch (cpp 148-204, 224-244);
full heap → new heap + `SetDescriptorHeaps` mid-list (162-175); assert that all tables fit one heap (cpp 69);
unbounded ranges are summed literally (`root_signature.cpp:105-108`) → **no bindless**. Only one CBV/SRV/UAV heap can
be bound at a time.

**Root signatures / PSOs**: `RootSignature` deep-copies `D3D12_ROOT_SIGNATURE_DESC1`, serializes at the highest
version (131-147). Every PSO hardcodes its registers (`EffectPSO` `effect_pso.cpp:33-70`: table of 12 SRVs at t3,
CBVs b0/b0 space1/b1, root SRVs t0/t2, static samplers s0/s1; `OceanPSO` `OceanRenderer/source/ocean_pso.cpp:29-77`:
15 SRVs at t3, CBVs b0-b3, 3 samplers). Ocean compute PSOs (`OceanCompute`, `compute_pso.cpp:13-56`) get root params
from `Ocean::LoadContent` 405-442; they rebuild after hot reload. **Root signatures are not rebuilt on hot reload**
(`docs/editor-handoff.md:76`). No generic engine compute PSO class.

**Shaders / hot reload**: `ShaderLibrary` (`shader_library.cpp`): poll interval 250 ms (`g_checkInterval`, 15),
entry point fixed to `"main"` (`g_entryPoint`, 18), source search only `<exe name>/shaders` and `shaders` next to /
above the exe (24-50) → **engine `.hlsl` are not found**; `Load` 54-100 reads the `.cso` and finds the `.hlsl`.
`ReadShaderTarget` (`shader.cpp:26-90`, stages 65-76) knows vs/ps/gs/hs/ds/cs/ms/as, **not `lib_6_x`**.
`PipelineShaders::Update` (`shader.cpp:203-234`) rebuilds on version change with the debug-layer break disabled.
`ShaderCompiler` loads `dxcompiler.dll` at runtime (exe folder first, then newest Windows SDK, then search path),
args `-E main -T <profile> -I <dir>`, Debug adds `-Zi -Qembed_debug -Od` (`shader_compiler.cpp:181-189`).
Build-time: MSBuild `FxCompile` **SM 6.0** (`EV-Engine.vcxproj:249-251`; OceanRenderer per-file 178-299), `.cso`
next to the exe. Engine-internal shaders are embedded byte arrays (`shaders/GenerateMips_CS.h`, `imGUI_*.h`);
`PanoToCubemapPSO` loads the *app's* `pano_to_cubemap_CS.cso` (`pano_cubemap_pso.cpp:57-60`).

**Render graph** (`header/DX12/render_graph.h`, `source/DX12/render_graph.cpp`, `docs/render-graph.md`): rebuilt
every frame, executed in declaration order on **one direct command list** (doc :96, :118). Resources:
`rg.Create(name, RGTextureDesc::Tex2D(...))` / `RGBufferDesc::Structured|Raw`, `rg.Import(name, texture)` /
`Import(name, buffer, stride)` (header 299-308, `Create`/`Import` 301/306); handles `RGTexture`/`RGBuffer` (99-116)
valid for one frame. Transients are **Tex2D only** (cpp 529), flags from accesses (50-60), pooled, released after 8
unused frames (`g_` 22, 573-579). **No temporal resources** → accumulation/history must be imported. Passes:
`AddPass(name)` → `RGPassBuilder` (223-251) `.Read/.Write(handle, RGAccess)`, `.RenderTarget`, `.DepthStencil`,
`.SideEffects()`, `.Execute(lambda(RGPassContext&))`; `RGPassContext` (191-214) gives the `CommandList` and
`GetTexture/GetBuffer`. `RGAccess` (76-91) → state map (cpp 27-48): ShaderRead, PixelShaderRead,
NonPixelShaderRead, UnorderedAccess, RenderTarget, DepthWrite, DepthRead, VertexBuffer/ConstantBuffer, IndexBuffer,
IndirectArgument, CopySource, CopyDest. `Execute` 584-628, `ExecutePass` 630-734 (BeginEvent, transitions, UAV
barrier when previous use was UAV 653-660, GPU timer, lambda, optional capture, EndEvent). Culling 434-471: a pass is
kept if it writes an imported resource, writes something read later, has `.SideEffects()`, or declares nothing (doc
:89). `RenderGraphDebugInfo` 254 (`raster` bool 266) drives the editor panel (`editor_render_graph.cpp`).

**Other**: `SwapChain` 3 back buffers (`swapchain.h:52`), waits for the next buffer's fence then releases stale CPU
descriptors (`swapchain.cpp:175-189`) → ~3 frames in flight. `GpuTimer` (`gpu_timer.h`) 4 frames in flight.
`Camera` (`header/core/camera.h`) is **left-handed** (`XMMatrixLookAtLH`, `XMMatrixPerspectiveFovLH`,
`camera.cpp:34,193`), `GetInverseViewMatrix` (h:36), `SetProjection(fov, aspect, zNear, zFar)` (38),
`GetInverseProjectionMatrix` (40), `GetFov` (44); no change counter. Lights: `header/DX12/light.h` – `PointLight` 38,
`SpotLight` 65, `DirectionalLight` 102 (64 B, HLSL-packed; directional has `directionWS`, `directionVS`, `color`,
`position` used to derive the direction).

### 2.2 Gap table (today → what path tracing needs)

| Area | Today (file:line) | Needed | Milestone |
|---|---|---|---|
| Device | `ID3D12Device13` at FL 11_0 (`application.cpp:504`) | Base device + optional `As<ID3D12Device5>`; gate DXR on it + `OPTIONS5.RaytracingTier ≥ 1_1` | M0 |
| Feature checks | Root sig 1.1 only (428-438) | `RenderCaps`: RT tier, highest SM, resource binding tier, `OPTIONS22` SER-reorders flag, adapter LUID + VendorId, Agility version loaded | M0 |
| Command list | `List2` (`command_list.h:93,511`) | `ID3D12GraphicsCommandList4` (create as List4 or `As()`); wrappers for `BuildRaytracingAccelerationStructure`, `EmitRaytracingAccelerationStructurePostbuildInfo`, `CopyRaytracingAccelerationStructure`; later `SetPipelineState1`/`DispatchRays` **must reset the PSO cache** (1261-1274) | M0/M1, M11 |
| Descriptors | Per-list 1024-entry dynamic heaps, mid-list switches, no unbounded tables | M1–M7: fixed-size staged table under a global root signature (TLAS via null-resource SRV); M8a: one persistent shader-visible heap (static + per-frame ring) + SM 6.6 `ResourceDescriptorHeap[]` | M3, M8a |
| Resources | Always DEFAULT, `HEAP_FLAG_NONE`, COMMON (`resource.cpp:94-112`) | AS buffers created in `RAYTRACING_ACCELERATION_STRUCTURE` + UAV flag, **never transitioned**; shared committed buffers (`HEAP_FLAG_SHARED`) for HIP/CUDA; readback buffers (compaction sizes); dedicated upload path for >2 MB | M1, M6 |
| Render graph | One list per frame, Tex2D transients, no temporal resources, `raster` bool | `RGAccess::AccelerationStructureWrite/Read` (UAV barriers only, no transitions); pass kind enum (Raster/Compute/RayTracing/External) for the panel; `.External()` pass (submit, signal, callback, GPU wait, new list; `Execute` needs the `CommandQueue`); imported accumulation/history; block capture of AS buffers | M1, M6 |
| Shaders / hot reload | `FxCompile` SM 6.0 per app; engine `.hlsl` not found (`shader_library.cpp:24-50`); no `lib_*` | Runtime DXC for engine PT shaders: register the engine shader folder (`ShaderLibrary::AddSourceDirectory`, D13), SM ≥ 6.6 profile; ship DXC next to the exe; `lib_6_x` support only for M11 | M0/M2 |
| Agility SDK | None (OS `d3d12.dll`) | 1.620.2 exports in each app `main.cpp` + `D3D12\D3D12Core.dll` | M0 |
| Debug layer | Breaks on warnings (516) | Keep; add message IDs to the deny list only with a reason | always |
| DRED | `GetBreadcrumbOpName` 1265-1290 lacks DXR ops | Add ops 31–34, 39, 45–48 (section 6.7) | M0 |
| Frame counter | `++m_frameCount` commented (993) | Re-enable (deferred release for the persistent heap, sampler frame index) | M0 |
| Material | `MaterialProperties` (`header/resources/material.h:44-113`, `alignas(16)`) = 140 B padded to 144; no transmission/clearcoat | Dedicated GPU material struct (explicit padding, 16-B multiple) + texture indices | M3 |
| Lights | App-side vectors; sun = `DirectionalLights[0]` direction from "position" | Module-owned PT light list (sun disk, point lights), env-map importance data | M3 |
| CI | `.github/workflows/windows-build.yml`: WARP smoke test, **no trigger on push to `path-tracing`** (only `imgui-editor`/`main`, PRs, manual; lines 3-7) | PT off on WARP; new DLL projects optional (CI builds only EV-Engine, OceanRenderer, ShaderArt, lines 58-65); optional WARP-DXR test with NuGet WARP | M0, later |

### 2.3 Scene representation (what goes into the BVH)

- `Scene` (`header/DX12/scene.h`; `m_sceneFile` 124, `m_loadID` 125): root `SceneNode`, private material/mesh lists.
  `Scene::Accept` `scene.cpp:467-474` → `SceneNode::Accept` (`scene_node.cpp:201-216`: node, meshes, children) →
  `Mesh::Accept` (`mesh.cpp:107-110`). Visitor interface `header/DX12/visitor.h:39-47`. Template to copy:
  `SceneVisitor` (`source/DX12/scene_visitor.cpp:16-46`, keeps `GetWorldTransform()` at 32 for the following
  `Visit(Mesh&)`).
- `SceneNode` (`scene_node.h`): local transform + inverse, `GetWorldTransform()` computed on call, no dirty flag.
- `Mesh` (`header/resources/mesh.h`): `std::map<slot, VertexBuffer>`, one `IndexBuffer`, one `Material`, AABB;
  `Mesh::Source { std::wstring file; uint64_t loadID; uint32_t index; }` (56-61, `SetSource/GetSource` 100-101, member
  123), filled by `Scene::ImportMesh` (`scene.cpp:408-412`). Empty `file` for code-made meshes (`CreatePlane` = the
  ocean, cube, sphere). `loadID` changes on every load → use `(file, index)` for anything saved; key BLAS caches by
  `Mesh*` + `weak_ptr`. One mesh on several nodes = instancing (scene files do this, `editor_scene_file.cpp:520-544`).
- **Vertex format** `VertexPositionNormalTangentBitangentTexture` (`header/resources/vertex_types.h:62-101`):
  5×`float3` = **60-byte stride**, position at offset 0 → BLAS `VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT`,
  `StrideInBytes = 60`. In HLSL read with `ByteAddressBuffer` (`Load3(i*60 + offset)`) – a `StructuredBuffer` of a 60-B
  struct also works but is easy to get wrong.
- **Indices always `R32_UINT`** (imported `std::vector<unsigned int>`, `scene.cpp:384-401`; procedural `uint32_t`).
- Import: assimp `aiProcessPreset_TargetRealtime_MaxQuality | OptimizeGraph | ConvertToLeftHanded |
  GenBoundingBoxes` (`scene.cpp:95-96`), `.assbin` cache, node transforms transposed into `XMMATRIX` (425-431).
- GPU storage: one committed default-heap buffer per VB/IB (`CopyBuffer`), **no CPU copy kept**, no SRVs.
- **Material**: `MaterialProperties` fields `diffuse, specular, emissive, ambient, reflectance` (float4),
  `metallic, roughness, opacity, specularPower, indexOfRefraction, bumpIntensity`, 9 `has*Texture` flags. Slots
  (`TextureType` 120-133): Ambient (=AO), Emissive, Diffuse (albedo), Specular, SpecularPower, Normal, Bump, Opacity,
  MetallicRoughness (glTF: **G = roughness, B = metallic**), H0 (ocean leftover). `ImportMaterial` 169-332 (roughness
  default 1 for formats without it, 230-231; normal vs bump by bits per pixel, 320-327). Emissive/diffuse/specular
  textures loaded as sRGB. The header comment says 128 B but the struct is 140 B → padded 144: a
  `StructuredBuffer<Material>` would have stride 140 unless padded → **define a dedicated GPU struct**.
- Lights are not scene entities (app vectors passed to PSOs).
- Instances to include in OceanRenderer: `m_oceanPlane` (special, provider), `m_helmet`, `m_editor->GetSpawnedScenes()`
  (`editor_assets.cpp:175-186`), the point-light sphere `m_sphere` (**one Scene drawn once per light with a mutated
  transform**, `ocean_scene.cpp:816-820` → one instance per light). `m_chessboard`/`m_boat` are loaded (373-375) but
  never drawn. The skybox cube is raster-only (miss = sample the environment).
- Scenes built from a scene file are `make_shared<Scene>() + SetRootNode` (`editor_scene_file.cpp:560-562`) with an
  **empty private `m_meshes`** → always gather geometry by walking nodes.

### 2.4 OceanRenderer: frame and ocean specifics

**Constants** (`OceanRenderer/include/ocean_scene.h:13-15`): `OCEAN_SUBRES 512`, `OCEAN_PLANE_SIZE 1024.0f` m,
`OCEAN_DEPTH 20.0f` (**unused** – candidate seafloor depth). 4 cascades (`m_oceanCascadesNumber`, h:291), patch sizes
**1500, 250, 17, 5 m** (`ocean_scene.cpp:392-396`). `m_renderGraph` h:204.

**Grid**: `CommandList::CreatePlane(1024, 1024, 512, 512)` (`ocean_scene.cpp:366`; implementation
`command_list.cpp:984-1054`): flat XZ grid, Y up, centred at origin, **513×513 = 263,169 vertices, 524,288
triangles, 2 m spacing**, 60-byte vertices (~15.8 MB VB, ~6.3 MB IB). Static. Edges 512 m from origin; far plane 1000 m.
Check the vertex order in `CreatePlane` before reconstructing (i, j) from a vertex index.

**Displacement only in the vertex shader** (`shaders/ocean_vertex.hlsl:47-72`): `uv_c = Position.xz / patchSize_c`
on the **object-space** position (49-52), sums `DisplacementTexture_c.SampleLevel(linearWrap, uv, 0).rgb` (54-57),
adds to the position (choppy XZ + height Y), outputs `WaveHeight = displacement.y`. The pixel shader recomputes uvs
from the **world** position (`ocean_pixel.hlsl:148-151`). Identical only while the plane's transform is identity –
the gizmo can move it → **uv object-vs-world mismatch** (fix in M2: pick object space everywhere).

**Cascade textures** (`GenerateH0` 1111-1220, formats 1167-1177): H0 `R16G16B16A16_FLOAT` 512² (re, im, conj re,
conj im); slope and displacement `R16G16B16A16_FLOAT` 512² UAV; foam (Jacobian) `R32_FLOAT` 512² UAV.

**Ocean shading** (`ocean_pixel.hlsl:145-269`): normal from summed slopes `normalize(-slope.x, 1, -slope.y)` (166);
foam = saturate(sum of 4 foam samples); roughness `0.2 + foam*0.8`, metallic 0, **F0 = 0.02**, Schlick Fresnel, GGX
D, Smith-Schlick G with the *direct* k (90); direct light loops over the **first 2 directional lights** (217-236);
IBL specular prefiltered at `roughness*4` LOD + split-sum LUT, diffuse irradiance × oceanColor (240-254);
**SSS fake** (Garrett Gunnell FFTWater): `k1 = peakScatter * max(waveHeight,0) * heightMod * pow(saturate(dot(L,-V)),4)
* pow(0.5 - 0.5*dot(L,N), 3)` × `(1 - Fresnel) * scatterColor * sunColor` (258-261); final `sss + ambient*IBLIntensity
+ direct`, then `lerp(color, white, foam)` (264-267). No refraction, absorption, object reflections or shadows.
Render params (set 466-470, editable 924-950): ocean colour (0, 0.15, 0.3), scatter colour (0, 0.5, 0.4), IBL 0.4,
height modifier 2, peak scatter 0.6.

**Object shading** (`shaders/pixel.hlsl:238-335`): metallic-roughness PBR, TBN normal mapping, roughness clamped
≥ 0.04, F0 = lerp(0.04, albedo, metallic), **one point light** (inverse-square) + **one directional light**,
split-sum IBL × AO × `Camera.IBLIntensity`, emissive. No shadows. (Check here whether α = roughness² before matching
the PT BSDF.)

**Tonemap**: `SDRPSO` + `HDR_to_SDR_PS.hlsl`: exposure `exp2(Exposure)`, Linear/Reinhard/Reinhard²/"ACESFilmic"
(actually **Hable Uncharted 2**); default ACESFilmic, exposure 0, gamma 2.2 (`source/DX12/sdr_pso.cpp:22-33`),
manual gamma into an UNORM target.

**Lights** (`OnUpdate` 516-633): 3 directional lights, direction = `normalize(-position)` (576-581); light 0 defaults
to position (280, 5, 341) → **sun about 0.65° above the horizon** [I]; light 1 at (0, 30, 0) (569); colours mostly
default (colour assignment commented at 584). 1 point light, **animated**: `positionWS = (sinf(e.totalTime)*5, 1, 0)`
(612), colour (0, 0.4, 0.8).

**IBL**: `sky4k.hdr` (370) → skybox cubemap 1024²×6 full mips; diffuse irradiance 32²×6 RGBA16F; specular 512²×6, 5
mips, RGBA16F; BRDF LUT 512² `R16G16_FLOAT` (25-27, 151, 197-235). `SetEnvironment(panorama)` (190-250): pano →
cubemap → convolutions on the compute queue, **CPU wait**, swap. `UpdateImageBasedLighting` re-bakes after IBL shader
hot reload (176-188). Known open issue: IBL bake streaks/blocks – `docs/editor-handoff.md` "TODO: sky lighting
streaks and blocks" (90-136); fixing it first gives a cleaner raster reference.

**Frame** (`RecordRenderGraph` 677-835, single direct list):
1. Per cascade i (imported H0/Slope/Displacement/Foam): `Ocean Spectrum i` (`animate_waves.hlsl`, 32×32 groups of
   16×16, 699-708) → `FFT Displacement/Slope Horizontal/Vertical i` (`fft.hlsl`, 710-723) → `Ocean Permute i`
   (`permute.hlsl`, writes slope, displacement, foam with decay/bias/add/threshold, 725-733). Loop ends 734.
2. IBL imports 737-740; transients `HDR Color` `R16G16B16A16_FLOAT` + `Depth Render Target` `D32_FLOAT` at the editor
   viewport size (744-745).
3. `Skybox` (clear (0.4, 0.6, 0.9), depth 1.0; 747-755) → `Ocean Surface` (758-793; `m_oceanPlane` 791-792) →
   `Objects` (795-821; helmet 804-807, spawned scenes 810-813, light spheres 816-820) → `Tonemap` into
   `m_viewportTexture` `R8G8B8A8_UNORM` (824-832) → `rg.Execute(commandList)` (834).

`OnRender` 635-675: `ConsumeViewportResize` (646-649) → `RecordRenderGraph` → clear → `OnGUI` (editor) → execute →
`Present`. Editor changes made in frame N are visible to `RecordRenderGraph` in frame N+1 → **compare a scene-version
snapshot at the start of `RecordRenderGraph`** (one frame late, harmless).

**Ocean pitfalls to fix for the path tracer** (all verified on this branch):

| # | Problem | Where | Fix (developer implements) | When |
|---|---|---|---|---|
| 1 | Displacement exists only in the VS | `ocean_vertex.hlsl:47-72` | "Ocean Displace Vertices" compute pass after `Ocean Permute 3` writing a persistent position buffer (same object-space uv convention) | M2 |
| 2 | uv object (VS) vs world (PS) | `ocean_vertex.hlsl:49-52` vs `ocean_pixel.hlsl:148-151` | Use object-space xz everywhere (pass it from VS to PS); the hit shader does the same | M2 |
| 3 | `GenerateH0` after parameter edits runs on the **copy queue** and replaces the cascade textures with no GPU wait before the direct queue reads them | `paramsChanged` 967-977, replacement 1181-1219 | Make the direct queue GPU-wait the copy queue fence (`CommandQueue::Wait(other)` 179-182) or CPU-flush; keep old textures alive until in-flight frames finish | M2 (before the BLAS depends on them) |
| 4 | Ocean RNG seeded from `std::random_device` → waves differ per run | `GaussianRandom` 1222-1228 (`static std::random_device rd;` 1223) | Fixed seed (stored in scene/shot), "randomize" as an option | M2/M3 (reference shots) |
| 5 | Ocean time is a **function static** in `OnUpdate`, not pausable/settable/saved | `static double oceanTime` 520, `+= deltaTime` 525, copied to `m_oceanTime` 598 | Member with pause + set; reference mode auto-freezes (D7) | M2 |
| 6 | Point light animates with `e.totalTime` | 612 | Freeze in reference mode (D7) | M2/M3 |
| 7 | **FOV reset to 45° on every viewport resize** (also on play start/stop) | `ResizeViewport` 130-143, `SetProjection(45.0f, …)` at 136 | Use `m_camera.GetFov()` | M3 |
| 8 | Play mode not used by OceanRenderer; app panels + toolbar hidden while playing | `SetPlayModeCallback` (editor.h:204) never called | PT hotkey or app-owned ImGui window for play mode; reset accumulation on both edges | M3 |
| 9 | Camera rotation rebuilt from `m_pitch/m_yaw` each frame (550-551) | | When moving the camera from a shot, also run the camera-changed logic (340-346) | M3 |

Other OceanRenderer facts: static camera `Ocean::m_camera` (34), start (0, 5, −20) looking at (0, 5, 0) (98-102);
controls right mouse + WASD/QE, Shift ×4, wheel = FOV (1403-1418), R reset (1311-1317), V vsync, F5 play, Esc
(stops play; quits only when not playing, 1294-1300). Swap chain `R8G8B8A8_UNORM` (326). `main.cpp:22-45` creates
`EV::Ocean(L"EV Engine", 1920, 1080)` (vsync off).

### 2.5 Hook points for the `raytracing` module (D1)

Layout (engine convention): `EV-Engine/EV-Engine/header/raytracing/*.h`, `source/raytracing/*.cpp`, add
`ClInclude`/`ClCompile` to `EV-Engine.vcxproj` (next to the editor entries 53-56) and `.vcxproj.filters`. `header/`
is on the include path → `#include "raytracing/..."`. The module may use nlohmann/json directly (it compiles inside the
engine project); the app projects have no nlohmann include path. The module must not depend on the editor; the panel
calls into the module.

| Hook | Where | Notes |
|---|---|---|
| Caps query | `Application::Initialize` after 428-438 (before 439); adapter in `CreateAdapter` 441-482 (`GetDesc1` → `AdapterLuid`, `VendorId`: 0x1002 AMD, 0x10DE NVIDIA [T]) | Store `RenderCaps` on `Application` (members near `application.h:210-220`) + getter. Must not throw on WARP |
| Device | `CreateDevice` 501-562 | Base device + `As<ID3D12Device5>` |
| DRED | `GetBreadcrumbOpName` 1265-1290 | Add DXR op names |
| Panel | `Editor::AddPanel(name, draw, DockSlot, open)` (`editor.h:142`, drawn in `DrawEditorPanels` `editor.cpp:434-445`); register in `Ocean::LoadContent` next to 348-349 | e.g. `AddPanel("Path Tracing", …, DockSlot::Right)`. Hidden in play mode |
| Viewport mode toggle | `DrawViewportToolbar` `editor.cpp:882-906` (engine code, no app hook) | New editor API (e.g. next to `SetRenderGraph`, `editor.h:148`), drawn after Local/World (900-904) with `SameLine()`; hover test uses the last item's rect (`toolbarHovered`, 856) |
| Scene-version counter | `Editor` (`uint64_t m_sceneVersion` + `GetSceneVersion()`) | Sources in the table below; camera via matrix comparison |
| Graph passes | `RecordRenderGraph` 677-835 | After 734: "Ocean Displace Vertices", "Build BLAS/TLAS"; after 744-745: path-trace pass; in PT mode skip 747-821 and feed the PT HDR output to `Tonemap` (824-832) |
| Instance gathering | visitor over `m_oceanPlane`, `m_helmet`, `GetSpawnedScenes()`, one instance per point light | Rebuild instance list each frame (spawned scenes can be replaced wholesale by `ApplySceneFile`/`NewScene`) |
| Scene-file data | Needs an editor hook, e.g. `AddSceneData(key, save→string, load(string))` written to `file["application"][key]`; `SceneFileContents` (`editor.h:275-294`) gets a `map<string,string>`; `load` runs on the main thread in `ApplySceneFile` | `SaveScene` writes a fresh object (269-409) → **unknown keys are dropped on save** |
| Camera/play callbacks | `SetCameraChangedCallback` (`editor.h:185`), `SetPlayModeCallback` (204) | **Single slots** (`m_cameraChangedCallback`/`m_playModeCallback`, 515/523): setting again replaces; app must forward to the module, or turn them into lists |
| Agility exports | `OceanRenderer/source/main.cpp` and `ShaderArt/source/main.cpp` after line 11 (include of `core/application.h`), before `ReportLiveObjects` at 13 | Shared macro header in `header/core/` (section 6.1) |

**Scene-version sources** (bump the counter):

| Change | Where |
|---|---|
| Gizmo move/rotate/scale | `DrawGizmo` `editor.cpp:927-937` (`SetLocalTransform` 936) |
| Inspector transform | `DrawInspector` `editor.cpp:1194` |
| Material edit / texture cleared | `DrawMaterial` `editor.cpp:1257` / 1309 |
| Reparent | `ApplyPendingReparent` `editor.cpp:1069` |
| Delete node / remove scene | `DeleteSelectedNode` `editor_assets.cpp:1158-1190`, `RemoveScene` `editor.cpp:668-671` |
| Add scene (app) | `AddScene` `editor.cpp:660-666` |
| Dragged model placed | `editor_assets.cpp:926, 969, 975` (`PlaceDraggedModel` 887, `UpdateModelDrag` 930) |
| Texture dropped on material | `UpdateTextureDrops` `editor_assets.cpp:1078` |
| Environment changed | `UpdateEnvironmentLoad` `editor_assets.cpp:1045` → `Ocean::SetEnvironment` (190) |
| New scene / scene file applied | `NewScene` `editor_scene_file.cpp:259-267`; `ApplySceneFile` 616-681 |
| Play start/stop | `Play` `editor_play.cpp:28-64`; `StopPlaying` 66-106 |
| Viewport resize | `m_viewportResized = true` `editor.cpp:806`, `editor_play.cpp:149`; consumed `ocean_scene.cpp:646` |
| Shader hot reload | `ShaderLibrary` reload count (`m_shaderReloadCount`, `editor.h:499`) |
| App side | camera fly/rotate (545-551), FOV wheel (1403-1418), R reset (1311-1317), ocean params (967-977), foam (953-965), render params (924-950), lights (`DrawLightParameters` 981+), ocean time (598), point light (612) |

### 2.6 Scene files and play mode

- Format (`source/editor/editor_scene_file.cpp`, 788 lines; layout comment 31-49; `"format": "EV Scene"`,
  `"version": 1` 53-54; API `editor.h:161-185`): `camera` (position, rotation quaternion, fov – **not** aspect,
  near/far, pitch/yaw; 275-286), `environment` (only after an *editor* environment change, `m_environmentPath`
  `editor_assets.cpp:1046`), `models` (292-325), `materials` (all `MaterialProperties` + texture paths, `WriteMaterial`
  130-165), `objects` (spawned node trees, `writeNode` 299-355), `applicationScenes` (only node transforms of
  "Ocean"/"Damaged Helmet", matched by name, 372-376; applied 627-649 only if the node count is unchanged).
- **Not saved**: ocean time, spectrum/JONSWAP parameters, foam params, ocean render params, light params, tonemap
  settings, vsync, ocean RNG seed, any path-tracer setting.
- Loading: `LoadScene` 411-424 → `ReadSceneFile` 429-596 (async thread; newer `version` only warns 448-451) →
  `UpdateSceneLoad` 598-614 → `ApplySceneFile` 616-681 on the main thread (from `Editor::BeginFrame`, `editor.cpp:343`):
  removes all spawned scenes and pushes new ones (619-625), sets transforms, camera + camera callback (651-660), maybe
  async environment load (677-680). Models load via `EditorInternal::LoadModel` (copy queue, **CPU-waits for upload and
  mips**, `editor_assets.cpp:129-146`, `ExecuteAndWait` 41-51) → building BLASes on the direct queue next frame is safe.
- Command line: `--scene <file>`, `--play <file>` (no editor; Esc quits) (`editor.cpp:251-265`).
- Proposed PT data (needs the hook): `"pathTracer": { enabled/mode, sppPerFrame, maxBounces, targetSpp, rrStartBounce,
  clamp, denoiser, debugView, freezeOceanTime, seed, shots: [ { name, position, rotation, fov, width, height, oceanTime,
  oceanSeed, spp, maxBounces, seed, exposure, tonemap } ] }`. Rendering a shot at a fixed resolution needs its own size
  path in `RecordRenderGraph` (transients use the viewport size, 744-745).
- Play mode (`editor_play.cpp`, 218 lines; API `editor.h:187-204`): F5 / Play button (201-218, drawn `editor.cpp:540`),
  Esc stops. Viewport = window client size (141-150) → resize next frame (also resets FOV, pitfall 7). While playing
  `BeginFrame` returns early (no menu/status/dockspace/shortcuts) and `EndFrame` (`editor.cpp:395-412`) draws
  `DrawPlayView`: **no editor panels, no `AddPanel` panels, no Render Graph/Texture Viewer, no gizmo/toolbar**. Input
  still works (152-163). Snapshot/restore of node transforms + camera (28-64, 80-99); ocean time, lights, environment
  are **not** restored. Play mode is the natural full-window reference view.

### 2.7 Build and CI facts

| Item | Value |
|---|---|
| Solution | `EV-Engine/EV-Engine.slnx` (EV-Engine lib, OceanRenderer, ShaderArt) |
| Engine | `EV-Engine.vcxproj`, `StaticLibrary` (169/175/182/188), out `lib/debug` / `lib/release` (215/220/226/232), include path 213-229, link deps 246/267/286/307 (`d3d12; dxgi; d3dcompiler; dxguid; Shlwapi; assimp-vc145-mt(d); DirectXTex(d); zlibstatic(d)`), `#pragma comment` dbghelp (`application.cpp:23`), Comdlg32/Ole32 (`editor_scene_file.cpp:22-23`) |
| Apps | default OutDir `EV-Engine/x64/<Config>/`; debugger working dir `$(SolutionDir)` = `EV-Engine/` (local `.vcxproj.user`, not in git) |
| Toolset | `v145` (VS 2026), `WindowsTargetPlatformVersion 10.0`, C++20, W3, SDLCheck, ConformanceMode |
| CI | `windows-2025-vs2026` (14), assimp v6.0.2 via CMake (33-56), msbuild Debug x64 of the three projects (58-65), WARP smoke test 67-128 (`EV_USE_WARP` 73, runs from `EV-Engine/` line 70, screenshot after 60 s, publishes to `ci-screenshots`) |

### 2.8 Conventions

- Namespace `EV`; classes/methods PascalCase; members `m_camelCase`; file-local constants `g_camelCase` in an anonymous
  namespace (`render_graph.cpp:19-25`); function statics `s_`; root-parameter enums unscoped inside the class
  (`EffectPSO::RootParameters`); files `snake_case.h/.cpp` in `header/<area>/` + `source/<area>/`.
- `#pragma once`; new files get a doxygen `@file`/`@brief` block with a usage example and design notes
  (`render_graph.h:1-51`, `gpu_timer.h:1-18`, `editor.h:1-20`); comments explain *why*.
- Errors: `ThrowIfFailed(hr)` → `std::runtime_error` with HRESULT + `std::source_location` (`header/utility/helpers.h:9-18`);
  render-graph declaration errors throw `std::logic_error` with the pass name; hot reload never throws to the app.
  **Feature probes (caps, DLL loading, LUID match) must not throw** – they set "unsupported".
- `SetName` on every D3D object (DRED/PIX readability) – do it for BLAS/TLAS/scratch/shared buffers too.
- Docs: plain explanatory English, tables for APIs, "Why it is built this way" with sources (`docs/render-graph.md`);
  running handoff notes in `docs/editor-handoff.md`.
- Commits: small, imperative, area prefix ("RenderGraph: …", "Raytracing: …", "OceanRenderer: …", "Docs: …").

---

## 3. Path tracing theory primer

Primary textbook: **pbr-book 4th edition** (free, source matches pbrt-v4)
([pbr-book 4ed](https://pbr-book.org/4ed/Radiometry,_Spectra,_and_Color/Representing_Spectral_Distributions)).
Formulas below are standard **[T]** unless a link is given; cross-check against pbrt-v4 when implementing.

### 3.1 Rendering equation and Monte Carlo

- Rendering equation: `L_o(x,ω_o) = L_e(x,ω_o) + ∫_Ω f(x,ω_i,ω_o) L_i(x,ω_i) |cos θ_i| dω_i`, with
  `L_i(x,ω_i) = L_o(hit(x,ω_i), −ω_i)` → recursive.
- MC estimator with one sample: `⟨L⟩ = L_e + f·L_i·|cos θ_i| / p(ω_i)`. Unbiased if `p > 0` wherever the integrand ≠ 0.
- Path form: keep a throughput `β` (starts at 1, or at the camera weight); at each vertex
  `L += β · (emission or NEE contribution)`, then sample a direction and `β *= f·|cos θ| / p`.
- Averaging N independent samples: variance ∝ 1/N, RMSE ∝ 1/√N → **4× spp halves the noise**. This is the M3
  convergence test (MSE vs spp on a log-log plot has slope −1).
- Camera rays (EV camera is **left-handed**, D3D NDC): pixel centre `(px+0.5, py+0.5)` → NDC
  `x = 2(px+0.5)/W − 1`, `y = 1 − 2(py+0.5)/H` (D3D y flip) → `invProj · (x, y, 1, 1)` (any depth; divide by w) →
  direction in view space (+Z forward in LH) → `invView` to world. Use `Camera::GetInverseProjectionMatrix` /
  `GetInverseViewMatrix`; check row-vector vs column-vector convention against EV's HLSL (`mul` order) [U].
  Sub-pixel jitter = per-sample random offset in [0,1)² instead of 0.5 (anti-aliasing for free).
- Self-intersection: offset the ray origin along the **geometric** normal (side of the outgoing direction) with the
  integer-ULP method of RTG1 ch. 6 "A Fast and Robust Method for Avoiding Self-Intersection"
  ([Ray Tracing Gems](https://www.realtimerendering.com/raytracinggems/)) or a scaled epsilon + `TMin`. Grazing rays on
  the refit ocean make this matter.
- Shading vs geometric normal: normal maps can make `dot(N_s, ω)` and `dot(N_g, ω)` disagree → light leaks/black
  pixels; reject or flip consistently (pbrt uses N_g for side tests, N_s for BSDF frames) [T].

### 3.2 Importance sampling, Lambert, cosine sampling

- Choose `p` ≈ integrand shape. Variance → 0 when `p ∝ f·L·cos`.
- **Lambert**: `f = ρ/π`. Cosine-weighted hemisphere sampling (Malley: uniform disk sample lifted to the hemisphere):
  `p(ω) = cos θ/π` → weight `f·cos/p = ρ`. Reference: `r = √u1, φ = 2π u2, ω = (r cos φ, r sin φ, √(1−u1))` in the
  local frame of N.
- Orthonormal basis from N: branchless Duff et al. 2017 "Building an Orthonormal Basis, Revisited" (JCGT) [T][U URL].

### 3.3 Next-event estimation (NEE)

At every non-specular vertex, sample a light, trace a shadow ray, add
`β · f(ω_l)·|cos θ| · L_e / p_light(ω_l)` (with MIS weight, 3.4). Rules:
- Area/solid-angle conversion: `p_ω = p_A · d² / |cos θ_light|`.
- **Point light** (delta): `L_i = I / d²` (match EV's raster attenuation if comparing, `pixel.hlsl` uses
  attenuation terms – check them), no MIS (BSDF sampling can never hit it).
- **Directional light** (delta): `E` along a fixed direction, no MIS. EV's directional lights are delta lights in the
  raster path.
- **Sun disk (D17)**: angular radius ≈ 0.2666° ≈ 4.65e-3 rad [T] (the notes describe the sun as "0.5°-wide").
  `cos θ_max = cos(radius)`; solid angle `Ω = 2π(1 − cos θ_max)` ≈ 6.8e-5 sr. Uniform cone sampling:
  `cos θ = 1 − u1(1 − cos θ_max)`, `φ = 2π u2`, `p_ω = 1/Ω`. If the sun is specified by irradiance E (like a
  directional light), its radiance is `L = E/Ω`. Because the disk is not a delta, **BSDF-sampled rays can hit it →
  MIS needed**. For EV: sun = `DirectionalLights[0]` direction + a module parameter for the radius.
- Light selection: with few lights, pick uniformly or by power; pdf multiplies `p_light`. Env map is "one more light".
- Shadow ray: `RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_FORCE_OPAQUE` (template flags), `TMax` = distance −
  ε (or large for directional/sun/env).

### 3.4 Multiple importance sampling (MIS)

- Balance heuristic: `w_i = p_i / Σ_k p_k`; power heuristic (β = 2): `w_i = p_i² / Σ_k p_k²` (Veach 1997 thesis
  [U URL]).
- NEE + BSDF, one sample each:
  - NEE sample: contribution × `w = p_light² / (p_light² + p_bsdf²)` with `p_bsdf` = BSDF pdf of that direction.
  - BSDF sample that hits an emitter (sun disk, env): contribution × `w = p_bsdf² / (p_bsdf² + p_light²)` with
    `p_light` = the pdf NEE *would* have used for that direction (incl. light-selection probability).
  - Both pdfs **in solid angle**. Delta lights and specular (delta) BSDF lobes: weight 1, no MIS.
- Implementation trick: store the previous vertex's BSDF pdf (and whether it was specular) in the path state, apply
  the BSDF-side weight when the next ray misses (env/sun) or hits an emitter.
- Spectral version (HWSS): MIS over wavelengths is folded into `r_u`/`r_l` (section 4.1).

### 3.5 Russian roulette (RR)

Terminate with probability `1 − q`, divide survivors by `q`. Unbiased. Typical: start after 2–3 bounces,
`q = min(1, max_component(β·etaScale))` (`etaScale` undoes the 1/η² radiance scaling through refraction so RR does not
kill paths inside water). pbrt-v4 logic: `rrBeta = beta*etaScale/r_u.Average(); if (rrBeta.MaxComponentValue() < 1 &&
depth > 1) { q = max(0, 1 − rrBeta.MaxComponentValue()); if (u < q) break; beta /= 1 − q; }`
([pbrt-v4 integrators.cpp](https://raw.githubusercontent.com/mmp/pbrt-v4/master/src/pbrt/cpu/integrators.cpp)).
Keep a hard bounce cap too (TDR budget, section 6.8).

### 3.6 Microfacet GGX + VNDF sampling, metal-roughness

- **GGX / Trowbridge-Reitz** (isotropic, α = roughness² by the usual glTF/UE convention – confirm against
  `pixel.hlsl:238-335` so PT and raster match):
  `D(h) = α² / (π ((n·h)²(α²−1) + 1)²)`.
- Smith: `Λ(ω) = (−1 + √(1 + α² tan²θ)) / 2`, `G1 = 1/(1+Λ)` = `2(n·v)/((n·v) + √(α² + (1−α²)(n·v)²))`;
  height-correlated `G2 = 1/(1 + Λ(ω_o) + Λ(ω_i))`. EV's raster uses **Smith-Schlick with the direct k**
  (`ocean_pixel.hlsl:90`) → small expected differences in raster-vs-PT tests.
- Fresnel (conductor/F0 workflow): Schlick `F = F0 + (1−F0)(1 − (ω·h))⁵`, `F0 = lerp(0.04, baseColor, metallic)`.
- BRDF: `f_spec = D·G2·F / (4 |n·ω_i| |n·ω_o|)`; diffuse `(1−metallic)·baseColor/π` (EV raster weights it by
  `(1−F)`; whichever you choose, use the same in both renderers when comparing).
- **VNDF sampling** (Heitz 2018, "Sampling the GGX Distribution of Visible Normals", JCGT 7(4)
  [U URL https://jcgt.org/published/0007/04/01/]): sample `h` proportional to `G1(ω_o) max(0, ω_o·h) D(h) / (n·ω_o)`;
  reflect `ω_i = reflect(−ω_o, h)`; pdf of ω_i = `G1(ω_o) D(h) / (4 (n·ω_o))`; weight `f·cos/p = F · G2/G1(ω_o)`.
  Reference algorithm (local frame, n = +z): stretch `V_h = normalize(α V.x, α V.y, V.z)`; build T1, T2 ⟂ V_h; disk
  sample `r = √u1, φ = 2πu2, t1 = r cos φ, t2 = r sin φ`; `s = 0.5(1 + V_h.z)`; `t2 = (1−s)√(1−t1²) + s·t2`;
  `N_h = t1 T1 + t2 T2 + √max(0, 1 − t1² − t2²) V_h`; unstretch `h = normalize(α N_h.x, α N_h.y, max(0, N_h.z))`.
  Newer variants: Dupuy & Benyoub 2023 spherical caps (simpler code) [T][U], Eto & Tokuyoshi 2023 bounded VNDF (lower
  variance, SIGGRAPH Asia 2023 TC) [U].
- Lobe selection for metal-roughness: pick specular vs diffuse with probability `p_s` (e.g. luminance of the
  Fresnel-weighted specular albedo estimate vs diffuse albedo) → one-sample MIS: total pdf
  `p = p_s·p_spec + (1−p_s)·p_diff`, weight `f_total·cos/p` (evaluate both lobes) [T].
- Multiple scattering energy loss of single-scatter GGX (dark rough metals) is expected; white-furnace tests on rough
  conductors therefore lose energy (Kulla & Conty 2017 compensation, optional) [T].

### 3.7 Dielectric Fresnel, refraction, TIR (water)

- Exact unpolarized Fresnel with `η_i`, `η_t`, `cos θ_i`:
  `sin²θ_t = (η_i/η_t)² (1 − cos²θ_i)`; if `sin²θ_t ≥ 1` → **TIR, F = 1**; else
  `r_s = (η_i cos θ_i − η_t cos θ_t)/(η_i cos θ_i + η_t cos θ_t)`,
  `r_p = (η_t cos θ_i − η_i cos θ_t)/(η_t cos θ_i + η_i cos θ_t)`, `F = (r_s² + r_p²)/2` [T].
- Normal incidence `F0 = ((n−1)/(n+1))²`: 0.0204 at n = 1.333, 0.0212 at n = 1.3408 (EV raster uses 0.02).
- Critical angle (from inside water): `θ_c = asin(1/n)`: **48.6° at n = 1.333**, 48.5° for pure water (1.3343 at 550
  nm, 20 °C), 48.2° for seawater (1.3408) [I – arithmetic]. The plan's M5 test value "≈ 48.6°" assumes n = 1.333: use
  the n you actually render with.
- Smooth dielectric sampling: reflect with probability F, transmit with 1−F (single-sample estimator, as in
  pbrt/Mitsuba). Weight after the choice is 1 (×(η_i/η_t)² for radiance transport through refraction, tracked in
  `etaScale` for RR) [T].
- Snell direction: `ω_t = −(η_i/η_t) ω_i + ((η_i/η_t) cos θ_i − cos θ_t) n` (ω_i pointing away from the surface) [T].
- Rough dielectric (Walter et al. 2007 [canonical URL](https://www.cs.cornell.edu/~srm/publications/EGSR07-btdf.pdf),
  not fetched): generalized half vector for refraction `h_t = −normalize(η_i ω_i + η_o ω_o)`; BTDF
  `f_t = (|ω_i·h| |ω_o·h|)/(|ω_i·n| |ω_o·n|) · η_o² (1 − F) G D / (η_i (ω_i·h) + η_o (ω_o·h))²`; Jacobian
  `dω_h/dω_o = η_o² |ω_o·h| / (η_i (ω_i·h) + η_o (ω_o·h))²`. Sample h with VNDF, then choose reflect/refract by
  F(ω·h). Verify against pbrt-v4 `DielectricBxDF` before implementing [U].

### 3.8 Environment-map importance sampling (D17)

- EV's sky is an equirectangular HDR (`assets/sky4k.hdr`) converted to a cubemap. Sampling the **panorama** directly
  is simplest: build a luminance image `w(u,v) = lum(L(u,v)) · sin θ(v)` (sin θ compensates the pole stretch), marginal
  CDF over rows + conditional CDF per row (or an alias table / hierarchical warp of a mip pyramid) [T].
- Sample (u,v) → `θ = π v`, `φ = 2π u` → direction (match the convention of `pano_to_cubemap_CS.hlsl` exactly – a 90°
  or mirror mismatch makes NEE sample the wrong sky); pdf in solid angle `p_ω = p_uv(u,v) / (2π² sin θ)`.
- The **sun must not be counted twice**: either remove/clamp the sun pixels from the env map (and add the analytic sun
  disk), or skip the separate sun. EV's `sky4k.hdr` content (whether it contains a sun) must be checked [U].
- Raster comparison: EV scales IBL by `IBLIntensity` (ocean 0.4) / `Camera.IBLIntensity` (objects); use the same
  factor in the PT env lookup.
- Env sampling and BSDF sampling are combined with MIS (3.4).

### 3.9 Samplers and random numbers

- Each sample needs well-defined **dimensions**: wavelength (1D, once per camera ray, D9), pixel jitter (2D), and per
  bounce: lobe choice (1D), direction (2D), light selection (1D), light sample (2D), RR (1D) [T].
- Options (increasing quality): integer hash per (pixel, sample index, dimension) (e.g. PCG `pcg3d`/`pcg4d` from
  Jarzynski & Olano 2020, JCGT 9(3) [U URL https://jcgt.org/published/0009/03/02/]) → Sobol + hash-based Owen
  scrambling (Burley 2020, JCGT [U URL https://jcgt.org/published/0009/04/01/]), as RTXPT does
  ([RTXPT](https://github.com/NVIDIA-RTX/RTXPT)) → blue-noise screen-space samplers (Heitz et al. 2019 [U]). pbrt-v4 has
  stratified, Hammersley, Sobol/Owen (`lowdiscrepancy.h`)
  ([pbrt-v4 sampling.h](https://raw.githubusercontent.com/mmp/pbrt-v4/master/src/pbrt/util/sampling.h)).
- Reference pcg3d (Jarzynski & Olano) [T]: `v = v*1664525u + 1013904223u; v.x += v.y*v.z; v.y += v.z*v.x; v.z += v.x*v.y;
  v ^= v >> 16u; v.x += v.y*v.z; v.y += v.z*v.x; v.z += v.x*v.y;` then `float(v >> 8) * (1/16777216.0)`.
- Seed from `(pixel.xy, frameIndex/sampleIndex)` – **needs the frame counter** (M0) and a fixed scene seed for
  reproducible shots. Correlation bugs show as structured noise; test with a constant-colour scene.

### 3.10 Progressive accumulation

- Store **sum + count** (RGBA32F; A = sample count) or a running mean `m_n = m_{n−1} + (x_n − m_{n−1})/n`. The output
  pass divides and writes the HDR texture that `Tonemap` reads. fp32 sums stay fine for ~10⁵–10⁶ samples [T].
- Reset (scene-version change, camera matrix change, resize, play edges, shader reload) = write count 0 (or overwrite
  with the new sample when `frame == 0`). Imported texture (render graph has no temporal resources).
- Optional: Welford variance per pixel → convergence readout and adaptive "stop at target spp".

### 3.11 Fireflies

- Sources: low-probability paths (caustics through water, sun glints via BSDF sampling, rough-after-specular chains).
- Remedies: MIS (first), NEE on the sun disk, path regularization/roughening after a diffuse bounce (biased),
  luminance clamp of the **indirect** contribution per sample (biased; RTXPT has a firefly filter) – keep clamping
  **off or high in reference mode** and visible as a panel setting, on in real-time mode [T].
- Spectral fireflies in media come from missing spectral MIS (section 5.6).

---

## 4. Spectral rendering (D9, D10)

### 4.1 Hero Wavelength Spectral Sampling (HWSS) – the exact equations

Source: Wilkie, Nawaz, Droske, Weidlich, Hanika, "Hero Wavelength Spectral Sampling", CGF 33(4), EGSR 2014 – full
PDF read in the follow-up ([PDF](https://jo.dreggn.org/home/2014_herowavelength.pdf),
[mirror](https://cgg.mff.cuni.cz/wp-content/uploads/2021/05/WNDWH14HWSS.pdf),
[EG DL](https://diglib.eg.org/handle/10.1111/v33i4pp123-131)). Equation numbers are the paper's.

- Eq. 1 single-wavelength: `⟨I⟩ = (1/N) Σᵢ f(Xᵢ, λᵢ) / p(Xᵢ, λᵢ)`.
- Eq. 2 Evans & McCool: `⟨I⟩ = (1/N)(1/C) Σᵢ Σⱼ f(Xᵢ, λʲᵢ) / p(Xᵢ, λʲᵢ)`; Eq. 3: if path generation does not depend
  on λ, `p(Xᵢ, λʲᵢ) = p(λʲᵢ)·p(Xᵢ)`.
- **Eq. 5 rotation**: `rⱼ(λ_h) = (λ_h − λ_min + (j/C)·λ̄) mod λ̄ + λ_min`, j = 1..C, `λ̄ = λ_max − λ_min`. No extra random
  numbers.
- **Eq. 6 combined density**: `p(Xᵢ, λʲᵢ) = Σₖ p(λᵏᵢ) p(Xᵢ | λᵏᵢ) / C` (any of the C wavelengths could have been the
  hero). Most renderers wrongly use Eq. 7 `p̄ = p(λʲ)·p(Xᵢ | λʲ)` – only correct if the path pdf is the same for all λ.
- Eq. 8 balance heuristic over shifted techniques `w_s = p_s / Σₖ p_k`; Eq. 9 `⟨I⟩ = (1/N) Σᵢ Σₛ w_s f/p_s`.
- **Eq. 10**: `w_s = p(λ_hᵢ) p(Xᵢ | λ_hᵢ) / Σₖ p(λᵏᵢ) p(Xᵢ | λᵏᵢ)`.
- **Eq. 11 final estimator**: `⟨I⟩ = (1/N) Σᵢ Σⱼ f(Xᵢ, λʲᵢ) / Σₖ p(λᵏᵢ) p(Xᵢ | λᵏᵢ)`. Requires the same (Q)MC sequence
  for each technique so all produce the same path.
- **Eq. 12 with NEE/BDPT**: weight `p_t′(Xᵢ, λ_hᵢ) / Σ_{t∈T} Σₖ p(λᵏᵢ) p_t(Xᵢ | λᵏᵢ)` – sum over all path techniques ×
  all shifted wavelengths (Fig. 2: per-λ importance-sampled env map; hero-only MIS fails there).
- When weights matter: dispersive Dirac interfaces "degenerate to using only the hero wavelength"; for
  slowly-varying materials the weights are almost equal → plain average. They matter for media, SSS, dispersion,
  spectrally sampled lights.
- C = 4 is an engineering choice (SSE 4-wide), "proved to be sufficient in practice"; overhead 3–10 % wall clock
  (CPU). Pitfall (Sec. 4.1): divide by the **hero's** pdf (the one that generated the path), but keep all four pdfs
  for Eq. 10.
- Homogeneous media: "the transmittance can be perfectly importance sampled and the probability directly computed"
  (Sec. 4.4).

**How pbrt-v4 maps onto Eq. 11 [I – derived in the follow-up, have it double-checked]**: pbrt rotates `u`, not λ,
so each λⱼ has marginal density `p_vis(λⱼ)`; path-pdf ratios live in `r_u`; the final `/ r_u.Average()` supplies
`Σₖ p(X|λₖ)/(C·p(X|λ_h))`; `1/pdf(λⱼ)` is applied in `ToXYZ` which averages over j. With uniform λ this equals Eq. 11.

**HLSL path state (lane 0 = hero)** [I]: `float4 lambda, pdfLambda, beta, r_u, r_l` (+ `L` accumulated as XYZ or
spectral `float4`). Per vertex: `beta *= f/pdf_hero` and `r_u *= pdf_k/pdf_hero` per lane; contributions
`beta·Le / avg(r_u)`, or `/ avg(r_u + r_l)` with NEE MIS. Without dispersion, spectral media or per-λ light sampling,
all `pdf_k == pdf_hero` → `r_u ≡ 1`: implement the plumbing in M4 and **verify it is a no-op** before M5 needs it.

### 4.2 pbrt-v4 `SampledWavelengths` (exact constants)

- Range `Lambda_min = 360`, `Lambda_max = 830` nm, `NSpectrumSamples = 4`
  ([pbr-book 4.5](https://pbr-book.org/4ed/Radiometry,_Spectra,_and_Color/Representing_Spectral_Distributions)).
- `SampleUniform(u)`: `λ0 = lerp(u, 360, 830)`, `Δ = 470/4`, `λi = λ(i−1) + Δ` wrapped, `pdf = 1/470`.
- **`SampleVisible(u)`** (D9): for i in 0..3: `up = u + i/4; if (up > 1) up −= 1; λi = SampleVisibleWavelengths(up);
  pdf_i = VisibleWavelengthsPDF(λi)` ([spectrum.h](https://raw.githubusercontent.com/mmp/pbrt-v4/master/src/pbrt/util/spectrum.h)) with
  ([sampling.h](https://raw.githubusercontent.com/mmp/pbrt-v4/master/src/pbrt/util/sampling.h)):
  - `SampleVisibleWavelengths(u) = 538 − 138.888889 · atanh(0.85691062 − 1.82750197·u)`
  - `VisibleWavelengthsPDF(λ) = (λ < 360 || λ > 830) ? 0 : 0.0039398042 / cosh²(0.0072·(λ − 538))`
  - HLSL has no `atanh`: `atanh(x) = 0.5·log((1+x)/(1−x))`; `cosh` exists.
  - Mitsuba 3 uses the identical density (credits Radziszewski et al.) with `math::sample_shifted`; pdf constant
    0.003939804229326285, weight `253.82·cosh²(…)` ([Mitsuba spectrum.h](https://raw.githubusercontent.com/mitsuba-renderer/mitsuba3/master/include/mitsuba/core/spectrum.h)).
- **`TerminateSecondary()`**: `pdf[1..3] = 0; pdf[0] /= 4`; the hero (`lambda[0]`) continues; `SecondaryTerminated()`
  = all `pdf[i≥1] == 0`. Zero pdfs are handled with `SafeDiv` (0 where denominator is 0). Call it before sampling a
  dispersive refraction (IOR varies per lane).
- **`ToXYZ`**: `XYZ( SafeDiv(X̄·L, pdf).Average(), SafeDiv(Ȳ·L, pdf).Average(), SafeDiv(Z̄·L, pdf).Average() ) /
  CIE_Y_integral` ([spectrum.cpp](https://raw.githubusercontent.com/mmp/pbrt-v4/master/src/pbrt/util/spectrum.cpp)).
- `CIE_Y_integral = 106.856895` (pbrt). Summing the official CIE CSV at 1 nm gives 106.856917. **Mitsuba 3 uses
  106.7502594** (95-sample 5 nm table) → ~0.1 % luminance offset in Mitsuba comparisons unless rescaled.
- RR on spectral throughput uses a scalar (`MaxComponentValue()` / `Average()`; pbrt divides by `r_u.Average()`).
- Wavelengths need their **own sample dimension**, drawn once per camera ray (new hero per camera ray, D9).

### 4.3 Jakob–Hanika RGB → spectrum (rgb2spec), step by step

Paper: [RGL page](https://rgl.epfl.ch/publications/Jakob2019Spectral) ("as few as six floating-point instructions",
zero round-trip error over sRGB), code [mitsuba-renderer/rgb2spec](https://github.com/mitsuba-renderer/rgb2spec).

- Model: `S(λ) = s(c0·λ² + c1·λ + c2)`, `s(x) = 0.5 + x / (2√(1 + x²))` (`IsInf(x) → x>0 ? 1 : 0`)
  ([color.h](https://raw.githubusercontent.com/mmp/pbrt-v4/master/src/pbrt/util/color.h)).
- **Units: λ in nanometres** (closes the earlier gap). `rgb2spec_opt` optimizes in `t = (λ−360)/470 ∈ [0,1]` and
  rewrites before storing, with `c0 = 360`, `c1 = 1/470`: `out0 = A·c1²`, `out1 = B·c1 − 2·A·c0·c1²`,
  `out2 = C − B·c0·c1 + A·(c0·c1)²` ([rgb2spec_opt.cpp](https://raw.githubusercontent.com/mmp/pbrt-v4/master/src/pbrt/cmd/rgb2spec_opt.cpp)).
- **Table**: `float[3][64][64][64][3]` indexed `[maxc][z][y][x][coef]`, flat `idx = ((l·res + k)·res + j)·res + i`;
  `zNodes[k] = smoothstep(smoothstep(k/63))`, `smoothstep(x) = x²(3−2x)`. Size 3·64³·3 floats = **9,437,184 B (9.0
  MiB)** + 256 B zNodes. Optimizer: CIE 1931 2° CMFs at 5 nm, CIELAB residual, Gauss–Newton 15 iterations, warm-started
  z sweep.
- **Lookup** (`RGBToSpectrumTable::operator()`, [color.cpp](https://raw.githubusercontent.com/mmp/pbrt-v4/master/src/pbrt/util/color.cpp)):
  1. rgb must be in [0,1] (clamp).
  2. If r == g == b: return `(0, 0, (r − 0.5)/√(r(1−r)))` (mono black/white → c2 = ∓8192 in upstream rgb2spec, "we
     cannot use infinity as this would produce NaNs").
  3. `maxc = (r>g) ? ((r>b) ? 0 : 2) : ((g>b) ? 1 : 2)`.
  4. `z = rgb[maxc]`, `x = rgb[(maxc+1)%3]·(res−1)/z`, `y = rgb[(maxc+2)%3]·(res−1)/z` (guard `max(z, 1e-9)` like
     Mitsuba).
  5. `xi = min((int)x, res−2)`, `yi = min((int)y, res−2)`, `zi = FindInterval(res, zNodes[i] < z)` (binary search).
  6. `dx = x − xi`, `dy = y − yi`, `dz = (z − zNodes[zi]) / (zNodes[zi+1] − zNodes[zi])`.
  7. Trilinear interpolation of the 3 coefficients over the 8 corners.
- **GPU port advice**:
  - Exact & simplest: one `StructuredBuffer<float>` / `ByteAddressBuffer` in pbrt order + `zNodes[64]` in a constant
    buffer; port the lookup line by line (8 corners × `float3`); 6-step binary search for `zi`. Mitsuba 3's
    `SRGBModel::fetch_jit` is an existing GPU-style port (flat arrays, selects for maxc, binary search, manual
    trilinear) ([srgb.cpp](https://raw.githubusercontent.com/mitsuba-renderer/mitsuba3/master/src/render/srgb.cpp)).
  - Closed-form zi [I]: `S⁻¹(y) = 0.5 − sin(asin(1 − 2y)/3)` → `zi = clamp(floor(63·S⁻¹(S⁻¹(z))), 0, 62)`; keep pbrt's
    linear `dz`; if `z < zNodes[zi]` decrement (rounding guard).
  - Hardware trilinear alternative: three `Texture3D<float4>` 64³ (one per maxc), z coordinate computed manually
    (non-uniform nodes); filtering-weight precision (~8 bits) adds error amplified by the sigmoid [U] → prefer manual
    interpolation for reference A/B.
  - Per-texel precomputed coefficients (the later D10 step, a first GPU-compute kernel): one fetch + ~6 FLOPs per λ.
    **Never store nm-domain coefficients in fp16** (c0 ~1e-4 while c0·λ² ~10–100 → cancellation). Either fp32, or
    convert to the normalized domain [I]: `A = a2·470²`, `B = 470·(2·360·a2 + a1)`, `C = a2·360² + a1·360 + a0`, then
    evaluate `s(A t² + B t + C)` with `t = (λ−360)/470`. Filtering coefficients ≠ filtering RGB (accepted practice).
  - HLSL per vertex (reference): `float4 x = (c.x*lambda + c.y)*lambda + c.z; float4 s = 0.5 + 0.5*x*rsqrt(1 + x*x);`
    clamp |c2| ≤ 8192.
- **Where to get the table**: pbrt's CMake runs `rgb2spec_opt 64 rgbspectrum_srgb.cpp sRGB` at build time (also
  dci_p3, rec2020, aces; option `PBRT_USE_PREGENERATED_RGB_TO_SPECTRUM_TABLES` default OFF)
  ([CMakeLists](https://raw.githubusercontent.com/mmp/pbrt-v4/master/CMakeLists.txt)); `rgb2spec_opt` is a single
  .cpp with no dependencies beyond threads – run it offline and save a binary. Upstream binary format: `"SPEC"`,
  `uint32 res`, `float scale[res]`, `float data[res³·9]`; current master appends `uint32 nfine` + `float fwd[4·nfine+12]`
  and the new loader rejects files without it ([rgb2spec.c](https://raw.githubusercontent.com/mitsuba-renderer/rgb2spec/master/rgb2spec.c)).

### 4.4 The three spectrum kinds (pbrt-v4, [pbr-book 4.6](https://pbr-book.org/4ed/Radiometry,_Spectra,_and_Color/Color))

| Kind | Use in EV | Evaluation |
|---|---|---|
| `RGBAlbedoSpectrum` | base colour, diffuse/specular reflectance (rgb ∈ [0,1]) | `s(λ)` |
| `RGBUnboundedSpectrum` | emission colour, unbounded values | `scale = 2·max(r,g,b)`, fit `rgb/scale`, return `scale·s(λ)` |
| `RGBIlluminantSpectrum` | RGB lights, env map texels, sun colour | `scale·s(λ)·D65(λ)` (colour space white point) → "white" stays white |

D65 used here is normalized to Y = 1: pbrt scales named spectra by `CIE_Y_integral / ∫(spec·ȳ)`.

### 4.5 Data: CMFs, D65, XYZ → sRGB, Planck

- **CIE 1931 2° CMFs** (1 nm, 360–830, 471 rows, `λ,x̄,ȳ,z̄`, no header):
  `https://files.cie.co.at/Publications-datasets/CIE_xyz_1931_2deg.csv` (DOI 10.25039/CIE.DS.xvudnb9b,
  [landing](https://cie.co.at/datatable/cie-1931-colour-matching-functions-2-degree-observer)); CVRL copy
  `http://cvrl.ucl.ac.uk/database/data/cmfs/ciexyz31_1.csv`. pbrt embeds the same as `CIE_X/Y/Z/lambda[471]`.
- **D65** (1 nm, 300–830, normalized 100 at 560 nm): `https://files.cie.co.at/Publications-datasets/CIE_std_illum_D65.csv`
  (DOI 10.25039/CIE.DS.hjfjmt59, [landing](https://cie.co.at/datatable/cie-standard-illuminant-d65)).
- **Wyman, Sloan, Shirley 2013** analytic CMFs (JCGT 2(2), [PDF](https://jcgt.org/published/0002/02/01/paper.pdf),
  λ in nm). Multi-lobe 1931 fit, each term `α·exp(−½t²)`, `t = (λ−β)·(λ<β ? γ : δ)` (reference values):
  - x̄: (0.362, 442.0, 0.0624, 0.0374) + (1.056, 599.8, 0.0264, 0.0323) − (0.065, 501.1, 0.0490, 0.0382)
  - ȳ: (0.821, 568.8, 0.0213, 0.0247) + (0.286, 530.9, 0.0613, 0.0322)
  - z̄: (1.217, 437.0, 0.0845, 0.0278) + (0.681, 459.0, 0.0385, 0.0725)
  - errors (max / mean sq.): x̄ 2.0e-4 / 3.1e-5, ȳ 6.4e-5 / 7.1e-6, z̄ 4.9e-4 / 1.6e-5 – comparable to linear 10 nm
    interpolation; GPU cost 0.9 µs vs 13.9 µs (1 nm table) per 100k evaluations. Single-lobe Eq. 2 fits are cruder
    (RMS < 0.015). Use the fit in shaders, the 1 nm CSV for offline LUTs/tests; for bit-exact pbrt comparison use the table.
- **XYZ → linear sRGB (Rec.709, D65)**, pbrt `rgb2spec_opt.cpp`:
  `[[3.240479, −1.537150, −0.498535], [−0.969256, 1.875991, 0.041556], [0.055648, −0.204043, 1.057311]]`; inverse
  rows `(0.412453, 0.357580, 0.180423)`, `(0.212671, 0.715160, 0.072169)`, `(0.019334, 0.119193, 0.950227)`. Peters
  uses `[[3.2406255, −1.5372080, −0.4986286], [−0.9689307, 1.8757561, 0.0415175], [0.0557101, −0.2040211, 1.0569959]]`
  ([illuminant_spectra.py](https://raw.githubusercontent.com/MomentsInGraphics/path_tracer/spectral/tools/illuminant_spectra.py)).
  Check: D65 white XYZ (0.95047, 1, 1.08883) → R ≈ 1.00001 [I].
- **Planck** (pbrt `Blackbody(λ, T)`): `c = 299792458`, `h = 6.62606957e-34`, `kb = 1.3806488e-23`, `l = λ·1e-9`,
  `Le = 2hc² / (l⁵ (exp(hc/(l·kb·T)) − 1))`, 0 if T ≤ 0; `BlackbodySpectrum` normalizes the peak to 1 using Wien
  `λ_max = 2.8977721e-3/T` (m). Useful for a sun spectrum (~5778 K) or lamp colours [T for the sun value].
- Film: accumulate XYZ (or fold XYZ→Rec.709 into the CMFs, Peters style) → linear Rec.709 RGB → existing `Tonemap`.
  **The film never stores spectra.**

### 4.6 Peters' method (M5b A/B option)

Peters, Merzbach, Hanika, Dachsbacher, "Using Moments to Represent Bounded Signals for Spectral Rendering", ACM TOG
38(4), SIGGRAPH 2019 ([page](https://momentsingraphics.de/Siggraph2019.html), DOI 10.1145/3306346.3322964); blog
[part 1](https://momentsingraphics.de/SpectralRendering1Spectra.html), [part 2](https://momentsingraphics.de/SpectralRendering2Rendering.html);
code [path_tracer@spectral](https://github.com/MomentsInGraphics/path_tracer/tree/spectral).

- Reflectance as Fourier coefficients (trigonometric moments) of the spectrum over a phase `φ = warp(λ) ∈ [−π, 0]`
  (a 95-entry "XYZ warp" table over linspace(360, 830, 95), values in `illuminant_spectra.py`). Reconstruction: bounded
  maximum-entropy spectral estimate (smooth, in [0,1], matches the moments exactly).
- Assets: sRGB texture → "Fourier sRGB" via a 256³ LUT (`srgb_to_fourier_srgb.dat`, header `QQQQffffff`, indexed
  `lut[B,G,R]`), 3 numbers per texel, BC1-compressible.
- Runtime per hit: `fourier_srgb_to_fourier` (3×3 rows `(0.2276800310, 0.4748793271, 0.2993498525)`,
  `(0.2035160895, 0.0770505049, −0.2808208130)`, `(0.1563903497, −0.3230828819, 0.1668540863)`) →
  `prep_reflectance_real_lagrange_biased_3` (clamp m0 to [0.0001, 0.9999], trig→exp moments, Levinson with bias
  0.9999, correlations) → 3 Lagrange multipliers; per lane:
  `a = atan(2·(L1 cos φ + L2 cos 2φ + ½L0))·0.318309886 + 0.5`
  ([spectra.glsl](https://raw.githubusercontent.com/MomentsInGraphics/path_tracer/spectral/src/shaders/spectra.glsl)).
- Wavelength sampling: `p(λ) ∝ (|r̄|+|ḡ|+|b̄|)·i(λ)`, inverse CDF with `u_k = (u+k)/4`; a 1024-texel RGBA16F LUT per
  illuminant (8 KiB) stores `rgb(λ)/importance(λ)` + phase; accumulates linear sRGB directly. Multiple illuminants →
  merge into one power-weighted spectrum (more colour noise).
- GPU cost of spectral (RTX 5070 Ti, 1080p, 1 spp): Cornell box path length 2: 0.26 → 0.31 ms, length 8: 0.83 → 1.13
  ms; Bistro length 2: 2.71 → 2.89 ms, length 8: 14.3 → 14.6 ms → < 0.3 ms absolute. Colour noise with good importance
  sampling is "quite negligible" relative to other MC noise.

| | Jakob–Hanika | Peters (3-coeff Fourier sRGB) |
|---|---|---|
| Per texel | (c0,c1,c2) fp32 or normalized fp16 | 3 × 8 bit, BC1-able |
| Per hit | none (or table lookup) | ~100-flop prep (Levinson) |
| Per lane | ~6 flops at λ | cos/sin + atan at φ |

**Planned swappable HLSL interface (D10)** – reference shape from the follow-up [I]:
```hlsl
// reference only – the developer designs the real one
struct WavelengthSample { float4 lambda; float4 pdf; float4 phase; }; // phase only used by Peters
WavelengthSample SampleWavelengths(float u);          // pbrt SampleVisible OR Peters LUT (separate switch)
struct AlbedoCoeffs { float3 c; };                     // JH sigmoid coeffs OR Peters Lagrange multipliers
AlbedoCoeffs PrepareAlbedo(float3 rgb);                // JH table lookup / pre-baked; Peters prep
float4 EvalAlbedo(AlbedoCoeffs a, WavelengthSample w); // sigmoid(poly(lambda)) OR atan series at phase
```
Keep the wavelength sampler a **separate switch** from the upsampler (measure each in isolation). Peters' LUT already
folds CMF and 1/p into its weight → make the accumulator take a ready per-lane `float3 rgbWeight` so both fit. Convert
the same source sRGB texture to both representations.

### 4.7 What stays RGB

Denoiser inputs (OIDN/OptiX are RGB-trained), albedo guides (use the **RGB** albedo, not one wavelength's reflectance,
or the guide flickers with λ), SHaRC/NRC cache contents (RGB/XYZ at the cache boundary), tonemap, accumulation.
Spectral is converted at the camera-ray level; each frame's new hero integrates over wavelength through accumulation.

### 4.8 Sanity-test ladder (each step has an exact expected value)

1. **CMF integral**: MC average of `ȳ(λ)/pdf(λ)` with your sampler → 106.857 ± noise (tests sampler, pdf, rotation).
2. **Grey round-trip**: RGB (0.5,0.5,0.5) → c = (0,0,0), s ≡ 0.5; a Lambertian 0.5 under a white
   `RGBIlluminantSpectrum` (D65, Y = 1) renders sRGB 0.5. Repeat with (0.5, 0.5, 0.501) to exercise the table path.
3. **D65 → white**: D65 (Y=1) → XYZ → sRGB = (1,1,1) within 1e-4.
4. **White furnace**: albedo 1 (mono, c2 = +8192 → s = 1) in a uniform environment → pixel = env radiance at every
   bounce count; then a saturated albedo vs the analytic geometric series per lane.
5. **RGB ↔ spectral**: 24 Macbeth sRGB patch colours under D65, single-bounce direct light → input sRGB within ~1e-3.
   Multi-bounce differences are the real spectral effect, not a bug.
6. **Water slab**: absorption only, collimated beam through d m → XYZ = `∫ CMF·D65·e^{−σ_a(λ)d} dλ / ∫ ȳ·D65` computed
   offline at 1 nm (10 m: ≈ 94/58/11/1 % at 440/550/600/680 nm); then add scattering and compare against Mitsuba 3
   `scalar_spectral` (`homogeneous` medium with spectral `sigma_t`/`albedo`, `tabphase` from (1 + 0.835 μ²)).
7. **Mitsuba comparison** (scene as Mitsuba XML, EXR, HDR-FLIP `pip install flip-evaluator`, `flip --reference
   mitsuba.exr --test mine.exr`); expect ~0.1 % luminance offset (CIE_Y constants). Python calls
   `mi.set_variant('scalar_spectral'); mi.load_file; mi.render(scene, spp=N); mi.util.write_bitmap` are from memory [U].
8. **Dispersion**: prism/sphere with Quan & Fry n(λ) + `TerminateSecondary` → non-dispersive regions get no new colour
   noise; rainbow fringe converges.

Mitsuba facts: `<rgb>` always goes through rgb2spec (`srgb`); `<spectrum value="400:0.1, 700:0.2"/>` or
`filename=`; `uniform` spectrum 360–830; `homogeneous` medium params `albedo` (default 0.75), `sigma_t` "in inverse
scene units" (m⁻¹ if 1 unit = 1 m), `scale`, nested `phase`; phase plugins `isotropic, hg, sggx, tabphase, rayleigh,
blendphase` ([Mitsuba docs](https://mitsuba.readthedocs.io/en/stable/src/generated/plugins_media.html)); pip variants
include `scalar_spectral`, `llvm_ad_spectral`, `cuda_ad_spectral` ([variants](https://mitsuba.readthedocs.io/en/stable/src/key_topics/variants.html)).

---

## 5. Water optics and participating media (D11, M5)

Model: a smooth or GGX-rough **dielectric interface** (air ↔ water, n(λ)) above a **homogeneous absorbing, weakly
scattering medium**, plus a stochastic **foam** layer; optional seafloor plane (`OCEAN_DEPTH 20`, unused today).
Scene units are metres (2 m grid spacing) → σ in m⁻¹ directly.

### 5.1 Absorption σ_a(λ): Pope & Fry 1997 + extension

- Pope & Fry 1997, Appl. Opt. 36, 8710–8723; minimum 0.0044 ± 0.0006 m⁻¹ at 418 nm
  ([OMLC](https://omlc.org/spectra/water/abs/pope97.html)). Data file
  [pope97.dat](https://omlc.org/spectra/water/data/pope97.dat): **380–727.5 nm in 2.5 nm steps (140 values), units 1/cm
  → ×100 for m⁻¹** (resolves the earlier "700 vs 720 nm" conflict; the paper title still says 380–700 nm).

| nm | a_w (m⁻¹) | nm | a_w | nm | a_w |
|---|---|---|---|---|---|
| 380 | 0.01137 | 500 | 0.0204 | 620 | 0.2755 |
| 400 | 0.00663 | 520 | 0.0409 | 640 | 0.3108 |
| 417.5 | 0.00442 (min) | 540 | 0.0474 | 650 | 0.340 |
| 420 | 0.00454 | 550 | 0.0565 | 660 | 0.410 |
| 440 | 0.00635 | 560 | 0.0619 | 680 | 0.465 |
| 460 | 0.00979 | 580 | 0.0896 | 700 | 0.624 |
| 480 | 0.0127 | 600 | 0.2224 | 727.5 | 1.678 |

- Above 727.5 nm (pbrt range goes to 830): **Segelstein 1981**
  ([segelstein81.dat](https://omlc.org/spectra/water/data/segelstein81.dat), 1/cm): 729: 1.981, 740: 2.4773, 750:
  2.6125, 760: 2.6114, 780: 2.2706, 800: 1.9640, 815: 2.2335, 830: 3.0905 m⁻¹. Alternatives: Smith & Baker 1981
  ([smith81.dat](https://omlc.org/spectra/water/data/smith81.dat), 10 nm to 800: 700: 0.65, 720: 1.169, 740: 2.38, 760:
  2.55, 800: 2.07) and Hale & Querry 1973 ([hale73.dat](https://omlc.org/spectra/water/data/hale73.dat)). They differ
  (740 nm: 2.48 vs 2.38) → pick one consistently. Below 380 nm use Pope's 380 value (or Segelstein).
- Bake to a **471-entry 1 nm `Buffer<float>` over 360–830** with linear interpolation [I].
- Optional Case-1 chlorophyll/CDOM terms ("one artist knob" from blue ocean to green coast):
  [Ocean Optics Book Case 1](https://oceanopticsbook.info/view/optical-constituents-of-the-ocean/level-2/classic-iop-model-case-1-water/pdf).

### 5.2 Scattering σ_s(λ) and phase function (Morel)

- Pure seawater (Morel 1974, δ = 0.09, 20 °C, S = 35–39 ‰): `b_w(λ) = 0.00288·(λ/500)^−4.32 m⁻¹` → 400: 0.00755,
  500: 0.00288, 600: 0.00131, 700: 0.00067 ([DRDO review](https://publications.drdo.gov.in/ojs/index.php/dsj/article/download/6048/3176/15909),
  secondary). Natural seawater scatters ~30 % more: factor `1 + 0.3·S/37` on pure-water values (Buiteveld)
  ([Biogeosciences 2007](https://bg.copernicus.org/articles/4/1041/2007/bg-4-1041-2007.pdf)).
- Phase function `β(θ) ∝ 1 + ((1−δ)/(1+δ))·cos²θ` (Zhang, Hu, He 2009 `betasw_ZHH2009.m`
  ([code](https://www.oceanopticsbook.info/packages/iws_l2h/conversion/files/betasw_ZHH2009.m)); default δ = 0.039, which
  the Ocean Optics Book calls the best fit). Normalized, λ-independent:
  `p(μ) = (1 + f μ²) / (4π (1 + f/3))`, **f = 0.835 (δ = 0.09)** → `0.06225·(1 + 0.835 cos²θ)`
  ([Ocean Optics Book](https://www.oceanopticsbook.info/view/inherent-and-apparent-optical-properties/visualizing-vsfs)
  – its "cos 2ψ" is a typo for cos²ψ; ∫ = 0.99999), or f = 0.925 (δ = 0.039). g = 0, backscatter fraction 0.5.
- **Exact sampling** [I – derived]: CDF `F(μ) = [(μ+1) + f(μ³+1)/3] / [2(1+f/3)]`; `F = u` → `μ³ + pμ + q = 0` with
  `p = 3/f`, `q = −(3/f + 1)(2u − 1)`; single real root (p > 0):
  `μ = cbrt(−q/2 + √(q²/4 + p³/27)) + cbrt(−q/2 − √(q²/4 + p³/27))`; check f = 1: u = 1 → μ = 1, u = 0.5 → μ = 0.
  Alternative: uniform sphere sampling with weight `p/pdf ∈ [0.78, 1.43]` (nearly isotropic).
- Particles (optional): strongly forward HG or Fournier–Forand term [T].

### 5.3 Index of refraction n(λ, T, S): Quan & Fry 1995

Appl. Opt. 34, 3477–3480, DOI 10.1364/AO.34.003477; coefficients from
[rho::n_water source](https://raw.githubusercontent.com/AlexCast/rho/master/R/refractive_indexes.R) (λ nm, T °C, S ‰,
**10 coefficients n0..n9**, valid T 0–30 °C, S 0–40 ‰; extrapolates to ~220–1100 nm per Huibers 1997):

`n = n0 + (n1 + n2·T + n3·T²)·S + n4·T² + (n5 + n6·S + n7·T)/λ + n8/λ² + n9/λ³`

| n0 | n1 | n2 | n3 | n4 | n5 | n6 | n7 | n8 | n9 |
|---|---|---|---|---|---|---|---|---|---|
| 1.31405 | 1.779e-4 | −1.05e-6 | 1.6e-8 | −2.02e-6 | 15.868 | 0.01155 | −0.00423 | −4382 | 1.1455e6 |

- Result is **relative to air** (use it directly at an air–water interface). Vacuum: multiply by
  `n_air = 1 + (5792105/(238.0185 − σ²) + 167917/(57.362 − σ²))·1e-8`, σ = 1/λ[µm].
- **Sanity values** (20 °C, S = 35): 400 nm 1.349938, **550 nm 1.340789** (vacuum 1.341161), 700 nm 1.336480;
  pure water (S = 0) at 550 nm: 1.334338. Δn ≈ 0.0135 over 400–700 nm.
- Dispersion matters for spray, droplets, rainbows (~2° shift red↔violet) and caustic fringes, not on the bulk surface.
  Options: constant n at the hero λ for the bulk surface (no termination), dispersion only as an option →
  `TerminateSecondary` at every dispersive refraction (D11).

### 5.4 Surface BSDF, sub-pixel roughness, foam

- Interface: exact dielectric Fresnel (3.7) – TIR comes for free inside the water; smooth or Walter 2007 rough GGX
  BTDF ([PDF](https://www.cs.cornell.edu/~srm/publications/EGSR07-btdf.pdf)). Stochastic reflect/refract by F.
- **Bruneton, Neyret, Holzschuch 2010** ([HAL](https://hal.inria.fr/inria-00443630)): unresolved waves become BRDF
  roughness. Path-tracer version: the traced 2 m grid is coarse; the slope variance of wave components **below mesh
  resolution** (plus the normal map already sampled from the 17 m/5 m cascades) becomes GGX roughness, larger with
  distance (also reduces glint fireflies) [I]. Exact mapping slope variance → α (e.g. α² ≈ 2σ² for Beckmann-like
  distributions) must be checked in the paper [U].
- Hit shading for the ocean (D6): normals and foam are **not** in the BLAS; the hit code reconstructs the undisplaced
  grid xz from the primitive's vertex indices + barycentrics (or reads a parallel undisplaced-xz buffer), then samples
  slope/foam at `xz / patchSize` (object space, same as the VS) like `ocean_pixel.hlsl` does; transform the normal by
  the plane's world matrix.
- **Foam**: stochastic layer – with probability `foam` (from the foam map) treat the hit as Lambertian white (or
  slightly forward-scattering), else as the dielectric. Foam is where NEE is very effective. Whitecap coverage comes
  from the Jacobian of the choppy displacement (Tessendorf) – EV's permute pass already computes it.
- Raster comparison (calibration of the fake, 5.8) needs matching foam: raster uses `roughness 0.2 + foam*0.8` and
  `lerp(color, white, foam)`.

### 5.5 Medium bookkeeping (inside/outside)

The ocean is a height surface, not a closed mesh: treat "below the surface" as water down to the optional seafloor.
Track a `inWater` flag in the path state, flipped on each transmission through the ocean surface; decide the side with
the geometric normal (+Y up before displacement; use the triangle's face normal) and the ray direction [I]. Camera
start state: evaluate whether the camera is below the displaced surface (rare; default = air).

### 5.6 pbrt-v4 VolPath rules (spectral MIS in media)

Distances are sampled with the **hero's** majorant `sigma_maj[0]`; two rescaled path-probability vectors
`r_u = p_u/p_path`, `r_l = p_l/p_path` (pbr-book Eq. 14.26) are updated incrementally; at emitters divide by their
wavelength average (single-sample spectral MIS, balance heuristic). Without spectral MIS, variance is high; with it
"MSE is reduced by a factor of 149" (Fig. 14.7)
([pbr-book 14.2](https://pbr-book.org/4ed/Light_Transport_II_Volume_Rendering/Volume_Scattering_Integrators)).
Condensed logic of `VolPathIntegrator::Li`
([integrators.cpp ~953–1271](https://raw.githubusercontent.com/mmp/pbrt-v4/master/src/pbrt/cpu/integrators.cpp)):
```
state: L=0, beta=1, r_u=1, r_l=1
per tentative collision (sigma_maj, T_maj):
  emissive: pdf = sigma_maj[0]*T_maj[0]; L += beta*T_maj/pdf * sigma_a*Le / (r_u*sigma_maj*T_maj/pdf).Average()
  pAbsorb = sigma_a[0]/sigma_maj[0]; pScatter = sigma_s[0]/sigma_maj[0]; pNull = max(0, 1-pA-pS)
  ABSORB  -> terminate
  SCATTER -> pdf = T_maj[0]*sigma_s[0]; beta *= T_maj*sigma_s/pdf; r_u *= T_maj*sigma_s/pdf;
             L += SampleLd(...); sample phase: beta *= p/pdf; r_l = r_u/pdf
  NULL    -> sigma_n = max(0, sigma_maj - sigma_a - sigma_s); pdf = T_maj[0]*sigma_n[0];
             beta *= T_maj*sigma_n/pdf; r_u *= T_maj*sigma_n/pdf; r_l *= T_maj*sigma_maj/pdf
segment without event: beta, r_u, r_l *= T_maj/T_maj[0]
hit emitter/env: depth==0 or specular: L += beta*Le / r_u.Average()
                 else: r_l *= p_light; L += beta*Le / (r_u + r_l).Average()
surface BSDF sample: beta *= f*|cos|/pdf; r_l = r_u/pdf   (r_u unchanged: BSDF pdf is λ-independent)
RR on rrBeta = beta*etaScale/r_u.Average()
```
`SampleLd` (shadow rays): ratio tracking with its own `T_ray, r_l, r_u`; delta lights return
`beta*f*T_ray*L / r_l.Average()`, others `/ (r_l + r_u).Average()`; RR when `T_ray/(r_l+r_u).Average() < 0.05`
(q = 0.75). `HomogeneousMedium` majorant is exactly `sigma_a + sigma_s` (σ_n = 0), HG phase only
([media.h](https://raw.githubusercontent.com/mmp/pbrt-v4/master/src/pbrt/media.h)). Theory: Miller, Georgiev, Jarosz
2019 null-scattering path integral [U URL https://cs.dartmouth.edu/~wjarosz/publications/miller19null.html].

### 5.7 The simpler homogeneous-water scheme (recommended for M5) [I]

pbrt's algorithm specialised to σ_maj = σ_t, σ_n = 0, albedo weighting instead of absorb/scatter choice (`float4`
lanes, lane 0 = hero):
1. **Free flight**: `t = −ln(1−u)/σt.x`.
   - `t < tSurface` → scatter: `pdf = σt.x·exp(−σt.x·t)`; `beta *= σs·exp(−σt·t)/pdf`; `r_u *= σt·exp(−σt·t)/pdf`.
   - else pass through: `P = exp(−σt.x·tSurface)`; `beta, r_u, r_l *= exp(−σt·tSurface)/P`.
2. **No absorption termination**: weighting by σ_s/σ_t never kills paths; with a red hero (albedo ≈ 0.003/0.6) pbrt's
   absorb choice would kill nearly every path including the blue lanes. RR on `beta/avg(r_u)` terminates instead.
3. **Phase sample**: `beta *= p/pdf; r_l = r_u/pdf_phase` (λ-independent Morel shape).
4. **Shadow/NEE rays**: deterministic `T = exp(−σt·d)` per lane; `r_u`, `r_l` unchanged; exact and zero-variance
   (pbrt's generic ratio tracking with σ_n = 0 degenerates to a noisy track-length estimator).
5. **Pure absorption** (σ_s = 0, e.g. a cheap first version): `beta *= exp(−σa·d)` per lane, no distance sampling, no
   MIS.
6. **Why MIS matters here**: σ_t ranges ~0.012 m⁻¹ (blue) to ~0.6 m⁻¹ (red 700 nm). With a red hero, collisions
   cluster at ~1.6 m; blue lanes get huge weights `e^{−σ_b t}/(σ_r e^{−σ_r t})` at large t. Dividing by `avg(r_u)` caps
   this – **without it expect strong colour fireflies**.
7. Heterogeneous media (foam volumes, turbid patches): full null-scattering loop with a majorant grid (later).

**NEE across the interface**: a shadow ray from underwater to the sun cannot pass a refractive interface (pbrt treats
dielectrics as occluders for shadow rays) → underwater direct sun only through BSDF-sampled paths. Practical option:
approximate the refracted sun direction with the flat-interface Snell direction and accept missing caustics (what most
offline renderers do) [I].

### 5.8 Caustics and why spectral matters

- **Caustics** on the seafloor/submerged objects are L-S-D-S-E paths: unidirectional NEE cannot connect through the
  specular interface; BSDF paths must randomly hit the small sun → extremely noisy. Options by effort: (1) roughen the
  interface for NEE/shadow rays; (2) enlarge or regularize the sun; (3) a caustic photon / light-tracing pass from the
  sun into a caustic map (RTG II ch. 30 water method, or cascaded caustic maps)
  ([RTG II ch. 30](https://developer.download.nvidia.com/ray-tracing-gems/rtg2-chapter30-preprint.pdf),
  [Kougianos & Moustakas 2021](https://vvr.ece.upatras.gr/?p=3741)); (4) MNEE (Hanika et al. 2015) [U]; (5) BDPT/MLT
  (offline). M11 lists "photon pass / MNEE".
- **Why spectral**: absorption spans ~100× across the visible range inside an exponent. Over 10 m:
  `T = e^{−a·d}` ≈ **94 % (440 nm), 58 % (550), 11 % (600), 1 % (680)**. An RGB red channel averaging 600–700 nm mixes
  11 % with 1 %; exp(average) ≠ average(exp) → RGB Beer–Lambert gets the hue wrong with depth (derived, not from a
  water-specific paper). This is the M5 RGB-vs-spectral A/B.
- Sun glints are sub-pixel and high-energy → fireflies; roughen at distance (Bruneton) or clamp in real-time mode.

### 5.9 Calibrating the raster SSS fake

In a volumetric path tracer the wave-tip glow appears on its own (sun enters thin crests, single-scatters, exits with
a green-blue tint). Use frozen-time reference renders to tune `ocean_pixel.hlsl:258-261` (`peakScatter` 0.6,
`heightMod` 2, `scatterColor` (0, 0.5, 0.4), the `pow(…,4)`/`pow(…,3)` shaping) and grazing Fresnel, by image
differencing raster vs reference at the same camera, time and seed (D7 shots). Also compare far-field glitter
(Bruneton transition), foam albedo and sky IBL.

---

## 6. DX12 / DXR practicalities

### 6.1 Agility SDK 1.620.2 (D3)

- Latest retail **1.620.2 (D3D12SDKVersion 620, 2026-11-09)**; 1.620.1 2026-10-28; 1.619.0 2026-02-26 (SM 6.9 / DXR
  1.2 retail), 1.619.5, 1.619.6; preview 1.722.1 (2026-11-09; CLAS/PTLAS/indirect AS builds). **1.621 (SM 6.10
  retail) has not shipped**; Microsoft targets "early December 2026"
  ([downloads](https://devblogs.microsoft.com/directx/directx12agility/)). 1.620 adds batched async commands
  (`ID3D12GraphicsCommandList12`), fence barriers tier 1, DX Dump Files, extended PIX markers, **`CheckFeatureSupport1`
  clamping tiers to the targeted SDK version** (→ use 620 to see the newest tiers), RayQuery OMM opt-in flags for
  pre-SM 6.9 shaders.
- Integration without NuGet ([getting started](https://devblogs.microsoft.com/directx/gettingstarted-dx12agility/)):
  1. Rename `.nupkg` → `.zip`, extract (internal path for the DLLs believed to be `build/native/bin/x64` [U]).
  2. Agility `include` **before** the Windows SDK includes (else FL 12_2 / new SM defines are missing); keep linking
     `d3d12.lib` from the Windows SDK. Use the Agility `d3d12.h` consistently in engine and apps (the vendored d3dx12
     enhanced-barrier code is guarded by `D3D12_SDK_VERSION >= 608`).
  3. Copy `D3D12Core.dll` (+ `d3d12SDKLayers.dll` for development) to **`<exe dir>\D3D12\`** – for EV
     `EV-Engine/x64/<Config>/D3D12/` (post-build step or shared `.props`; every app and the CI exe). Subfolder, not
     the exe folder (otherwise an app-local SDKLayers can pair with the System32 Core).
  4. Export from **the .exe**:
     `extern "C" { __declspec(dllexport) extern const UINT D3D12SDKVersion = 620; }`
     `extern "C" { __declspec(dllexport) extern const char* D3D12SDKPath = u8".\\D3D12\\"; }` (relative to the exe,
     **trailing slash required**; the article's example uses 4 – use 620). Alternative: a `.def` file
     (`EXPORTS D3D12SDKVersion DATA PRIVATE` …).
- **Static-library pitfall (EV!)** [I, standard MSVC behaviour]: the engine is a static lib; the linker only pulls an
  object out of a `.lib` if something references it → exports defined in the engine are **silently dropped** and the
  app falls back to System32 with no error. Fixes, best first: (1) a header macro (e.g. `EV_AGILITY_SDK_EXPORTS(620)`
  in `header/core/`) expanded once in `OceanRenderer/source/main.cpp` and `ShaderArt/source/main.cpp` (after the
  include at line 11, before `ReportLiveObjects` at 13); (2) `/INCLUDE:D3D12SDKVersion /INCLUDE:D3D12SDKPath` per app
  (x64: no underscore decoration); (3) a `.def` per app. Runtime alternative without exports:
  `ID3D12SDKConfiguration1::CreateDeviceFactory` [T, not re-verified].
- **Verify at startup** [I]: `GetModuleHandleW(L"D3D12Core.dll")` + `GetModuleFileNameW` → path contains `\D3D12\`;
  `CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL)` with HighestShaderModel 6_9 succeeds; D3D12Core.dll file minor
  version = SDK version. Errors: `D3D12_ERROR_INVALID_REDIST` (0x887E0003) = DLL not found at the path ("Missing
  D3D12GetInterface export") or version mismatch; debug-layer mismatch = System32 Core + app-local SDKLayers.
- Expect a few new debug-layer warnings with new runtime features → add IDs to the info-queue deny list only with a
  written reason.

### 6.2 DXC

- Use **DXC v1.9.x** for SM 6.9 retail: latest **v1.9.2609** (2026-09-29); v1.9.2602 (2026-02-26, "SM 6.9 production
  support"); v1.10.x builds are **SM 6.10 preview only** (need Agility 1.720-preview). Release zips contain
  `dxc.exe` + `dxcompiler.dll` and `dxv.exe` + `dxil.dll`. Don't mix SM 6.9 preview and retail toolchains
  ([DXC releases](https://github.com/microsoft/DirectXShaderCompiler/releases)).
- Ship `dxcompiler.dll` + `dxil.dll` next to the exe: `ShaderCompiler` already loads from the exe folder first. Ideally
  the same binaries compile build-time shaders (replace `FxCompile` for SM 6.x with a custom build step calling the
  downloaded `dxc.exe`) so offline and hot-reloaded code are identical [I]. Which DXC the Windows SDK / VS 2026
  FxCompile uses is unknown [U].
- Flags: `-T cs_6_6` (or higher; 6.6 gives `ResourceDescriptorHeap[]`; `cs_6_9` only for SER/OMM/long vectors),
  `-HV 2021`, `-enable-16bit-types` (SM 6.2+, needed by SHaRC; 16-bit ops are required in SM 6.9), Debug
  `-Zi -Qembed_debug` (EV already adds `-Od`). Note `ShaderLibrary` recompiles with the **profile read from the
  `.cso`** and entry `main` – runtime-only engine shaders need a way to specify their profile (D13 design point).
- **RayQuery requires SM 6.5+ and raytracing Tier 1.1**; the optional second template argument
  (`RAYQUERY_FLAG_*`) requires SM 6.9 ([DXR spec](https://microsoft.github.io/DirectX-Specs/d3d/Raytracing.html)).

### 6.3 RayQuery API

- `RayQuery<RAY_FLAGS, RAYQUERY_FLAGS> q; q.TraceRayInline(RaytracingAccelerationStructure AS, uint RayFlags, uint
  InstanceInclusionMask, RayDesc ray);` – instance ignored when `!((InstanceInclusionMask & InstanceMask) & 0xff)`.
  `RayDesc { float3 Origin; float TMin; float3 Direction; float TMax; }` [T].
- RAY_FLAG: NONE, FORCE_OPAQUE, FORCE_NON_OPAQUE, ACCEPT_FIRST_HIT_AND_END_SEARCH, SKIP_CLOSEST_HIT_SHADER,
  CULL_BACK_FACING_TRIANGLES, CULL_FRONT_FACING_TRIANGLES, CULL_OPAQUE, CULL_NON_OPAQUE, SKIP_TRIANGLES,
  SKIP_PROCEDURAL_PRIMITIVES, FORCE_OMM_2_STATE. RAYQUERY_FLAG: NONE 0x00, ALLOW_OPACITY_MICROMAPS 0x01,
  ALLOW_CLUSTERED_GEOMETRY 0x02.
- Loop pattern (spec Example 2, reference):
  ```hlsl
  RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> q;
  q.TraceRayInline(Scene, RAY_FLAG_NONE, 0xFF, ray);
  while (q.Proceed()) {                       // only loops for non-opaque candidates
      if (q.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE &&
          AlphaTest(q.CandidateInstanceID(), q.CandidatePrimitiveIndex(), q.CandidateTriangleBarycentrics()))
          q.CommitNonOpaqueTriangleHit();
  }
  if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT) { /* shade */ } else { /* miss: sky */ }
  ```
  Candidate accessor names from training knowledge of the spec [T]. `Abort()` ends the loop; repeated commits are
  no-ops. Committed accessors [T]: `CommittedRayT()`, `CommittedTriangleBarycentrics()`, `CommittedInstanceIndex()`,
  `CommittedInstanceID()`, `CommittedGeometryIndex()`, `CommittedPrimitiveIndex()`, `CommittedObjectToWorld3x4()`,
  `CommittedTriangleFrontFace()`. Barycentrics are (b1, b2) → `b0 = 1 − b1 − b2`.
- Opaque scene (all of EV today): `RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES>` (or mark
  geometry `D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE`) → `Proceed()` returns false at once. Shadow rays add
  `RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH`. Template flags are compile-time → driver specializes traversal.
- Keep **one live RayQuery object** in scope at a time (AMD guidance; many live queries cost lots of registers).
- Alpha-tested materials (opacity textures) need the candidate loop + non-opaque geometry (D11 note).

### 6.4 Acceleration structures: build, refit, compaction, alignment, barriers

- Sizing: `GetRaytracingAccelerationStructurePrebuildInfo` → `ResultDataMaxSizeInBytes`, `ScratchDataSizeInBytes`,
  `UpdateScratchDataSizeInBytes`, each rounded up to 256; prebuild inputs must match the build inputs exactly.
- **Alignment**: destination 256 B (`D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT`), scratch 256 B and in
  `UNORDERED_ACCESS` (contents not preserved), instance descs 16 B (`..._INSTANCE_DESC_BYTE_ALIGNMENT`, 64 B each),
  shader tables 64 B / records 32 B (DXR pipeline only).
- AS buffers: DEFAULT heap, `ALLOW_UNORDERED_ACCESS`, **created in `D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE`
  and never transitioned** (keep them out of EV's tracker transition path; register with that state or not at all;
  `CommandList::UAVBarrier(resource)` exists). Scratch = normal UAV buffer (fine with `Application::CreateBuffer` +
  tracker). BLAS inputs (VB/IB) must be in `NON_PIXEL_SHADER_RESOURCE` during the build – raster draws move them to
  VB/IB states every frame (`command_list.cpp:1168, 1202`), which works but ping-pongs; a combined read state is an
  option [I].
- Triangle geometry desc for an EV mesh: `VertexBuffer.StartAddress = VB GPU VA`, `StrideInBytes = 60`,
  `VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT`, `IndexFormat = DXGI_FORMAT_R32_UINT`, `Transform3x4 = 0`,
  `Flags = OPAQUE` (unless transparent). Vertex formats also allowed: `R16G16B16A16_FLOAT`, `R16G16_FLOAT` [T].
- Instance desc [T]: `Transform[3][4]` (object→world, row-major 3×4, translation in the 4th column),
  `InstanceID:24` (→ index into a per-instance table: VB/IB/material indices), `InstanceMask:8`,
  `InstanceContributionToHitGroupIndex:24`, `Flags:8`, `AccelerationStructure` (BLAS GPU VA). DirectXMath row-vector
  `XMMATRIX` must be **transposed** into it (`XMStoreFloat3x4` is believed to store the transpose for exactly this use
  [U]) – a classic bug is mirrored or exploded instances.
- Update rules: source must have `ALLOW_UPDATE` and identical other flags; updates "can be performed in unlimited
  succession"; source == destination → in-place; triangle BLAS updates may change **only vertex positions**;
  `ALLOW_UPDATE` costs some memory, build time and trace speed; large deformations degrade trace performance.
- Flag recipes ([NVIDIA RTX best practices](https://developer.nvidia.com/blog/rtx-best-practices/),
  [updated 2022](https://developer.nvidia.com/blog/best-practices-for-using-nvidia-rtx-ray-tracing-updated/)):
  static BLAS `PREFER_FAST_TRACE | ALLOW_COMPACTION` + compaction; dynamic refit-every-frame `ALLOW_UPDATE |
  PREFER_FAST_TRACE` ("fastest trace against updateable AS"); rebuilt-every-frame `PREFER_FAST_BUILD`; TLAS
  `PREFER_FAST_TRACE`, **rebuild every frame** (don't refit); `MINIMIZE_MEMORY` usually costs speed. Throughput rule of
  thumb: builds O(100M) prims/s, updates O(1G) prims/s; **total AS budget "at most 2 ms"**; async compute "in many
  cases hides the cost almost completely" (EV's graph has no async compute yet → direct queue for now).
- Compaction [T]: build with `ALLOW_COMPACTION` → `EmitRaytracingAccelerationStructurePostbuildInfo(COMPACTED_SIZE)`
  into a buffer → read back (frames later) → allocate → `CopyRaytracingAccelerationStructure(..., COMPACT)`. Typical
  ~50 % (30–60 %). A compacted BLAS **can be refit but not rebuilt** (Indiana Jones; flags
  `PREFER_FAST_BUILD | ALLOW_UPDATE | ALLOW_COMPACTION` for swaying vegetation)
  ([NVIDIA Indiana Jones](https://developer.nvidia.com/blog/path-tracing-optimizations-in-indiana-jones-opacity-micromaps-and-compaction-of-dynamic-blass)).
  Skip compaction for the ocean (single object, rebuilt).
- **Barriers**: no barriers between consecutive BLAS builds (each needs **unique scratch**); one global UAV barrier
  before the TLAS build; UAV barrier on the TLAS before tracing. Legacy: `D3D12_RESOURCE_BARRIER_TYPE_UAV` on the AS or
  a null-resource global UAV barrier. Enhanced barriers [U names]: SyncBefore `BUILD_RAYTRACING_ACCELERATION_STRUCTURE`,
  AccessBefore `RAYTRACING_ACCELERATION_STRUCTURE_WRITE`, SyncAfter `BUILD_… | COMPUTE_SHADING`, AccessAfter `…_READ`
  ([enhanced barriers spec](https://microsoft.github.io/DirectX-Specs/d3d/D3D12EnhancedBarriers.html)).
- TLAS binding: SRV with `D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE`, `RaytracingAccelerationStructure.Location
  = TLAS GPU VA`, `pResource = nullptr` → works through EV's descriptor table path (2.1).
- Instance descs per frame: upload buffer (64 B each; EV's 2 MB page = 32k instances) or a default buffer + copy.
- Keep the sky out of the TLAS (miss = env lookup). Everything visible must be in the BVH (Indiana Jones had no hair
  in reflections because hair wasn't).

### 6.5 Ocean BLAS recipe (D6)

513×513 vertices = 512² quads ≈ 524k triangles. Estimates from the rule of thumb [E]: **full build ~5 ms, refit
~0.5 ms, BVH ~31–42 MB** (60–80 B/tri); 256² ≈ 1.3/0.13 ms; 1024² ≈ 21/2.1 ms. Measure on both GPUs.
1. Build once: `ALLOW_UPDATE | PREFER_FAST_TRACE`.
2. Each frame: "Ocean Displace Vertices" compute writes positions → UAV barrier on the position buffer (and transition
   to `NON_PIXEL_SHADER_RESOURCE`) → `BuildRaytracingAccelerationStructure` with `PERFORM_UPDATE`, source = dest
   (in place) → global UAV barrier → TLAS rebuild (`PREFER_FAST_TRACE`) → UAV barrier on TLAS → path-trace dispatch.
3. Full rebuild every **N frames (start ~60)** or when wave height/choppiness parameters change; tune N from logged
   refit vs rebuild trace times (RRA/PIX). Choppy XZ displacement inflates node bounds.
4. **Frozen time** (reference mode): build once with `PREFER_FAST_TRACE` (no update needed) for the best trace speed.
5. Position buffer format: `float3` (12 B stride) or `float4`/`R16G16B16A16_FLOAT` (16-bit loses precision at 512 m
   from the origin – keep fp32) [I]. Reuse the static R32 index buffer of the plane.
6. Alternatives (not chosen): AABB + intersection-shader heightfield march (slow, choppy ≠ heightfield); cluster
   templates (CLAS, DXR Part 2 preview, "comparable with traditional refit", 1.722-preview + preview drivers)
   ([DXR Part 2](https://microsoft.github.io/DirectX-Specs/d3d/Raytracing2.html)); instanced tiles of one periodic BLAS
   (EV's 1500 m cascade is larger than the 1024 m plane → seams).

### 6.6 Vendor performance guidance

- **AMD (RDNA)** ([RDNA performance guide](https://gpuopen.com/learn/rdna-performance-guide/)): trace as few rays as
  possible; traversal runs on the WGP and **uses LDS extensively → little groupshared memory** in RT shaders; **8×4
  thread groups**; one live RayQuery; skip procedural/non-opaque via flags; "smaller BVHs are always better", use
  high-quality build flags, don't rebuild dynamic geometry every frame; rebuild the TLAS every frame (compute queue if
  possible); best performance from TraceRay/RayQuery in compute on a compute queue. No RDNA 4-specific RT page found
  [U]. RDNA 4 BVH8/OBBs need no developer action.
- **NVIDIA**: inline RayQuery is fine while shading is uniform; prefer `DispatchRays` once there are "multiple
  different shading models"; compute groups **8×8 or 16×8**; mark geometry opaque; any-hit only where needed.
- → make the PT group size a compile-time define chosen per vendor at (hot-reload) compile time [I]. No 2024–2026
  RayQuery-vs-pipeline benchmark exists for RDNA 4 or Ampere → measure before M11.

### 6.7 Tools

| Tool | Use | Notes |
|---|---|---|
| D3D12 debug layer | Always in Debug; breaks on warnings | GPU-based validation in a separate config (slow); historic DXR coverage gaps [U] |
| DRED | Auto-breadcrumbs, page faults (EV forces on) | Consider `ID3D12DeviceRemovedExtendedDataSettings1` + BreadcrumbContext [T] |
| PIX | Captures, AS viewer, timings; "supports all retail and preview features" (PIX 2602.25+); AMD counters plugin built in (RX 9070 XT since May 2025) | ([PIX counters](https://devblogs.microsoft.com/pix/hardware-counters-in-gpu-captures/)); DX Dump Files with PIX 2610.28 |
| Radeon Raytracing Analyzer (RRA) 1.8+ | BVH viewer incl. RDNA 4 BVH8/OBB, memory, instance/triangle counts; 1.11.1 inspects CLAS/PTLAS | Capture through **Radeon Developer Panel, started before the app** (app must not be running); user markers need Raytracing + Profiling enabled ([RDP manual](https://gpuopen.com/manuals/rdp_manual/), [RRA quickstart](https://gpuopen.com/manuals/rra_manual/quickstart/)) |
| Radeon GPU Profiler (RGP) 2.7.1 | D3D12 + HIP profiling, RT shader ISA | Tool suite `RadeonDeveloperToolSuite-2026-10-02-1872.zip`; PIX markers in DX12 profiles since 2.7 |
| Radeon GPU Detective (RGD) | TDR/crash analysis on AMD | ([RGD tutorial](https://gpuopen.com/learn/rgd_1_0_tutorial/)) |
| Nsight Graphics 2026.4 | Frame debugger + AS viewer on the 3070 | R620+; **incompatible with the D3D debug runtime**; GPU Trace compute timing only on Blackwell+ |
| Nsight Systems 2026.5 | D3D12 + CUDA timeline (interop stalls) | traces CIG streams since 2026.4.1/2026.5.1 |
| Nsight Compute 2026.4 | CUDA kernel profiling | |
| Render Graph panel / Texture Viewer | Per-pass GPU ms, capture any texture/buffer after any pass | Block capture of AS buffers (meaningless) |

**DRED op names to add to `GetBreadcrumbOpName`** (`D3D12_AUTO_BREADCRUMB_OP`,
[Learn](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_auto_breadcrumb_op)): DISPATCH (6),
RESOURCEBARRIER (15), BUILDRAYTRACINGACCELERATIONSTRUCTURE (31), EMITRAYTRACINGACCELERATIONSTRUCTUREPOSTBUILDINFO (32),
COPYRAYTRACINGACCELERATIONSTRUCTURE (33), DISPATCHRAYS (34), SETPIPELINESTATE1 (39), BARRIER (45), BEGIN_COMMAND_LIST
(46), DISPATCHGRAPH (47), SETPROGRAM (48). Inline RayQuery shows up as plain DISPATCH. Put PIX markers around
"Displace", "BLAS refit", "TLAS build", "PathTrace" (the graph's `BeginEvent` already names passes).

### 6.8 TDR and per-dispatch budget

- `HKLM\System\CurrentControlSet\Control\GraphicsDrivers`: `TdrDelay` default **2 s** (the timeout), `TdrDdiDelay` 5 s,
  `TdrLevel` 0 off / 3 recover (default), `TdrDebugMode` 1 = ignore, `TdrLimitTime` 60 s, `TdrLimitCount` 5
  ([TDR keys](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/tdr-registry-keys)). Dev machines:
  `TdrDelay = 10`, reboot; never TdrLevel 0. Not for end users.
- Budget rules: 1 path per pixel per dispatch; bounce cap (e.g. 8) + RR; accumulate spp **across frames**, not in a
  loop inside one dispatch; tile the screen with separate submissions for WARP / GBV runs. `Dads-Birthday` already
  needed a GPU-hang fix in the specular prefilter (`0cb371d`).

### 6.9 WARP DXR for CI

- **Microsoft.Direct3D.WARP NuGet** latest **1.0.22** (2026-10-27, targets Agility 1.620): DXR 1.1 since 1.0.8, SM 6.8
  1.0.11, FL 12_2 1.0.13, JIT AS traversal + build race fix 1.0.14, SM 6.9/SER/OMM 1.0.18 (no reorder), real SER
  reordering 1.0.21, dynamically indexed RayQuery arrays 1.0.22; contains `D3D10Warp.dll` "intended for testing and
  development" ([NuGet](https://www.nuget.org/packages/Microsoft.Direct3D.WARP)). Preview package supports SM 6.10/LinAlg.
- CI plan [I]: restore WARP 1.0.22 + Agility 1.620.2; copy `D3D10Warp.dll` next to the test exe (location
  undocumented [U]); `IDXGIFactory4::EnumWarpAdapter`; assert `RaytracingTier ≥ 1_1` and that D3D10Warp.dll loaded from
  the app folder. Smoke test: 64²–256², Cornell box + 33×33 ocean patch with refit, fixed seed, 16–64 frames, compare
  mean/RMSE to a stored reference (not exact bytes). Default CI (inbox WARP) keeps PT off → panel "unsupported".
- Remember: CI does not run on push to `path-tracing` (only PRs / manual).

### 6.10 SM 6.10 / `dx::linalg` status (2026-11-18)

Preview only (Agility 1.720/1.721/1.722-preview, DXC 1.10.2605.x, `D3D12EnableExperimentalFeatures(D3D12ExperimentalShaderModels)`
before device creation, query `D3D12_FEATURE_LINEAR_ALGEBRA_SUPPORT ≥ D3D12_LINEAR_ALGEBRA_TIER_1`). Header
`<dx/linalg.h>`, `Matrix<ComponentType, M, N, MatrixUse::A/B/Accumulator, MatrixScope::Thread/Wave/ThreadGroup>`;
thread scope = the former Cooperative Vectors (deprecated "in favor of a future design")
([LinAlg preview](https://devblogs.microsoft.com/directx/d3d12-linalg-preview/)). 1.721-preview adds VectorAccumulate
(training gradients). SM 6.10 also brings Group Wave Index, variable groupshared (lifts the 32 KB cap), RT intrinsics
`TriangleObjectPositions`/`ClusterID`. AMD RX 9000 via preview drivers; NVIDIA via DevRel; WARP yes. Retail planned for
1.621 "early December 2026" – a plan, not a fact. Training support on RDNA 4 not verified. → Experiment branch only
(D3, D15 g).

---

## 7. GPU compute: HIP + CUDA from one source (D5, D14)

### 7.1 Toolchain choice on the AMD PC

| | Legacy **HIP SDK for Windows 7.2.x** | **ROCm Core SDK 10.1.0** ("TheRock", 2026-10-05) |
|---|---|---|
| Install | GUI/CLI `.exe` (admin), components: Core, Libraries, Runtime Compiler, Ray Tracing, **VS plugin (VS 2017/2019/2022)**, **ROCgdb**; bundles "Radeon Software PRO 25.30" ([install](https://rocm.docs.amd.com/projects/install-on-windows/en/docs-7.2/install/install.html)) | Tarball (`gfx120X-all`/`multiarch`) into `C:\TheRock\build` or pip; set `HIP_PATH`, `HIP_PLATFORM=amd`, `HIP_DEVICE_LIB_PATH`, `LLVM_PATH`, PATH; **remove the HIP SDK first**; disable WDAG + Smart App Control ([install](https://rocm.docs.amd.com/en/latest/install/rocm.html)) |
| Versions | HIP 7.2 (7.2.4 newest referenced), release notes 2026-07-31 | HIP 7.16.0, LLVM 24, rocWMMA 2.2.1, Windows 11 25H2, Adrenalin 26.10.41.05 |
| gfx1201 | Runtime, SDK, debugger ✅ | Supported |
| Debugger | ROCgdb on Windows (gfx120x only, single AMD GPU, DWARF host info) | ROCgdb + rocprof listed **Linux only** |
| Status | Docs labelled "legacy" | The forward path |

**Pick: legacy HIP SDK 7.2.x** for this project [I]: GUI install, VS plugin, the only Windows GPU debugger, gfx1201
fully supported. Pin it in the build docs (`HIP_PATH`), revisit 10.1 later. Verify with `hipInfo` / `hipconfig`.
Caveats: HIP-VS documents only VS 2017–2022 (EV uses VS 2026 / v145) [U]; CMake's HIP language is "unsupported" on
Windows in the legacy SDK, needs ROCm `clang++` for every compiler, Ninja (no VS generator), CMake ≥ 3.29 for DLLs, and
its arch detection silently falls back to gfx906 (`rocm_agent_enumerator` is not shipped; use `amdgpu-arch`); clang
predefines `_WIN32` but not `WIN32` ([amd/skills fault classes](https://github.com/amd/skills/blob/HEAD/skills/moat-cuda-to-rocm/references/fault-classes.md)).
Runtime: `amdhip64_7.dll` ships **inside the AMD driver** – never redistribute it; ship only hipRTC
(`hiprtc0702.dll`-style versioned names) or HIP libraries you use; no static linking of HIP SDK components
([deployment](https://rocm.docs.amd.com/projects/install-on-windows/en/docs-7.2/conceptual/deployment-guidelines.html)).

### 7.2 Build approach

- **HIP (`EV-Hip.dll`)** – Blender Cycles pattern ([Blender hip CMake](https://github.com/blender/blender/blob/main/intern/cycles/kernel/device/hip/CMakeLists.txt)):
  compile only the kernel TU offline: `hipcc --offload-arch=gfx1201 --genco -O3 -std=c++17 kernel.hip -o kernel_gfx1201.fatbin`
  (custom build step), load at runtime with `hipModuleLoadData` → `hipModuleGetFunction` → `hipModuleLaunchKernel`. The
  host part of the DLL compiles with **MSVC** (HIP runtime API headers or a dynamic loader like hipew/Orochi) → no
  clang-for-everything problem. Debug build: `-gdwarf -Wl,-debug:dwarf -O0` for ROCgdb
  ([debugger](https://rocm.docs.amd.com/projects/install-on-windows/en/latest/how-to/debugger-windows.html)). Alternative:
  whole DLL with hipcc/clang++ (`<<<>>>` launches) – simpler code, clang toolchain for the DLL; keep a C ABI either way.
- **CUDA (`EV-Cuda.dll`)**: a VS project with the **CUDA 13.5** build customization (`CUDA 13.5.props`; VS 2026 18.x,
  2022, 2019 supported; driver no longer bundled since 13.1)
  ([install guide](https://docs.nvidia.com/cuda/cuda-installation-guide-microsoft-windows/index.html)); mark `.cu`
  as "CUDA C/C++"; flags `-gencode arch=compute_86,code=sm_86 -gencode arch=compute_86,code=compute_86` (SASS + PTX for
  JIT on newer GPUs; add 89/120 to target RTX 40/50); CUDA 13 dropped pre-sm_75. Link `cudart_static.lib` (+ `cuda.lib`
  only for driver-API calls). OptiX needs headers only ([optix-dev](https://github.com/NVIDIA/optix-dev)): include
  `optix_function_table_definition.h` in exactly one .cpp, `optix_stubs.h` where used.
- **One source**: shared device code in e.g. `ev_gpu/kernels/*.h`; per backend one TU (`kernel.hip`, `kernel.cu`) that
  includes a compat header + the shared kernels (Blender's `kernel/device/{hip,cuda}/compat.h` + shared
  `kernel/device/gpu/kernel.h` ([Blender hip/kernel.cpp](https://github.com/blender/blender/blob/main/intern/cycles/kernel/device/hip/kernel.cpp))).
  Host interop code is identical apart from the `hip`/`cuda` prefix (macro header).
- **CI**: the workflow builds only EV-Engine, OceanRenderer, ShaderArt → the DLL projects must not be dependencies of
  the apps; the engine loads them at runtime and works without them.

### 7.3 DLL layout and selection (D14, D16)

- Plain C ABI (`extern "C"` entry returning a function table + an ABI version integer the engine checks); engine
  header `header/raytracing/gpu_compute.h` (name is the developer's choice) [I].
- At startup, after `CreateAdapter`: `DXGI_ADAPTER_DESC1::VendorId` 0x1002 → `LoadLibraryW(L"EV-Hip.dll")`, 0x10DE →
  `EV-Cuda.dll`; the DLL picks its device by **LUID**: CUDA `cudaDeviceProp::luid` (8-byte memcmp against
  `AdapterLuid`, then `cudaSetDevice`, `nodeMask = luidDeviceNodeMask`); HIP `hipDeviceProp_t::luid` /
  `luidDeviceNodeMask` (Windows, HIP ≥ 6) – **never match by UUID** (HIP's UUID is the board serial). D3D12 device must
  not be on a linked-node adapter (`GetNodeCount() == 1`). Missing DLL, missing driver or no LUID match → feature off,
  accumulate-only, log the reason, no throw.
- iGPU PCs: adapter 0 may not be the dGPU; CUDA device 0 may not be the D3D12 adapter → LUID matching is mandatory.

### 7.4 Portability header: the differences that matter

| Topic | HIP (gfx1201) | CUDA (sm_86) |
|---|---|---|
| Platform macros | `__HIPCC__`, `__HIP__`, `__HIP_DEVICE_COMPILE__` (replaces `__CUDA_ARCH__`), `__HIP_PLATFORM_AMD__` | `__CUDACC__`, `__CUDA_ARCH__` |
| Warp size | 32 on gfx10+ (64 unsupported for HIP); `warpSize` not a host compile-time constant (folded early since ROCm 7.0) | 32 |
| Lane masks, `__ballot`, `__activemask` | **64-bit** even in wave32; `_sync` shuffles take a 64-bit mask (on by default in ROCm 7.0; `HIP_DISABLE_WARP_SYNC_BUILTINS` also removes `__syncwarp` on 7.2) | 32-bit, `__ballot_sync(0xffffffff, p)` |
| Shared memory | ≤ 64 KB per workgroup | 48 KB default, 99 KB opt-in via `cudaFuncSetAttribute(..., MaxDynamicSharedMemorySize)` |
| `__launch_bounds__` | 2nd arg means MIN_WARPS_PER_EXECUTION_UNIT (≠ CUDA); `--maxregcount` unsupported | 2nd arg = min blocks/SM |
| Matrix cores | **no `nvcuda::wmma`**; `__builtin_amdgcn_wmma_*_gfx12` or rocWMMA | `nvcuda::wmma` / `mma.sync` |
| Dynamic parallelism | not supported | supported |
| fp16 | `<hip/hip_fp16.h>` `__half`, `__half2`; bf16 `__hip_bfloat16`; OCP FP8 native on RDNA 4 | `<cuda_fp16.h>` `__half` (same names) |
| Runtime compile | hipRTC (versioned DLL names) | NVRTC |

Rules [I]: write for warp 32 and assert `warpSize == 32` at init; never hard-code `0xffffffff` (use an
`ev_lanemask_t` typedef: `uint64_t` on HIP, `uint32_t` on CUDA); ≤ 48 KB shared memory per block; one-argument
`__launch_bounds__`; avoid cooperative-groups grid sync in v1. Sketch of the compat header (reference):
`#if defined(__HIPCC__) … #define gpuMalloc hipMalloc … typedef uint64_t ev_lanemask_t; #define EV_BALLOT(p) __ballot(p)
#elif defined(__CUDACC__) … typedef uint32_t ev_lanemask_t; #define EV_BALLOT(p) __ballot_sync(0xffffffffu, p) #endif`.
HIP-on-NVIDIA (hipnv headers `#define hip* cuda*`, compiled by nvcc) is undocumented on Windows → experiment only.

### 7.5 D3D12 ↔ HIP/CUDA interop recipe (identical apart from the prefix)

Works on Windows for both vendors: AMD's own Orochi `Test/SimpleD3D12` (port of cuda-samples simpleD3D12, AMD +
NVIDIA, touched Oct 2026) ([Orochi simpleD3D12.cpp](https://github.com/GPUOpen-LibrariesAndSDKs/Orochi/blob/main/Test/SimpleD3D12/simpleD3D12.cpp)),
OIDN 2.5's HIP device ([hip_external_buffer.cpp](https://github.com/RenderKit/oidn/blob/master/devices/hip/hip_external_buffer.cpp)),
AMD rocDecode; CUDA: [simpleD3D12.cpp](https://raw.githubusercontent.com/NVIDIA/cuda-samples/master/cpp/5_Domain_Specific/simpleD3D12/simpleD3D12.cpp),
[CUDA runtime interop API](https://docs.nvidia.com/cuda/cuda-runtime-api/group__CUDART__EXTRES__INTEROP.html),
[CUDA guide graphics interop](https://docs.nvidia.com/cuda/cuda-programming-guide/04-special-topics/graphics-interop.html).

**Buffers**
1. `CreateCommittedResource(DEFAULT heap, D3D12_HEAP_FLAG_SHARED, Buffer(size, ALLOW_UNORDERED_ACCESS), COMMON)` (EV's
   `Resource` cannot do this today → new creation path; wrap with the `ComPtr` ctor and register the state).
2. `device->CreateSharedHandle(res, nullptr /*or security attrs*/, GENERIC_ALL, nullptr, &h)`.
3. `size = device->GetResourceAllocationInfo(nodeMask, 1, &desc).SizeInBytes` (allocation size, not buffer width).
4. Import: type `…D3D12Resource` (CUDA `cudaExternalMemoryHandleTypeD3D12Resource`; HIP enum value 5), `handle.win32.handle = h`,
   `flags = …Dedicated` (**required** for CUDA committed resources; HIP accepts 0 or Dedicated – OIDN/Orochi set it),
   `size` ≠ 0 → `cudaImportExternalMemory` / `hipImportExternalMemory`.
5. `CloseHandle(h)` after the import (Orochi does; rocDecode keeps it until teardown – both work; the app owns NT handles).
6. Map the **whole range once** (`…ExternalMemoryGetMappedBuffer`, offset 0, flags 0; overlapping mappings may not alias);
   use offsets inside.
7. Keep the `…ExternalMemory_t` – Falcor's `getSharedDevicePtr()` leaks it; don't copy that.

**Fence**
1. `CreateFence(0, D3D12_FENCE_FLAG_SHARED, …)` + `CreateSharedHandle`.
2. Import as `…D3D12Fence` (CUDA `cudaExternalSemaphoreHandleTypeD3D12Fence`; HIP value 4), flags 0. On AMD use **only**
   this type: HIP's front end rejects semaphore types > 4 (D3D11 fence, keyed mutex, timeline) with
   `hipErrorInvalidValue` ([hip_memory.cpp](https://github.com/ROCm/rocm-systems/blob/HEAD/projects/clr/hipamd/src/hip_memory.cpp));
   HIP external semaphores are Windows-only.
3. `…WaitExternalSemaphoresAsync` / `…SignalExternalSemaphoresAsync` with `params.fence.value`, flags 0, on **one
   explicit stream**. Values strictly increasing, never reused.

**Textures**: D3D12 textures are tiled/swizzled → not shareable as linear memory (OptiX `OptixImage2D` and OIDN want
linear). Write **HLSL pack/unpack compute passes** texture ↔ shared linear buffer (Falcor `ConvertTexToBuf`,
`ConvertBufToTex`; nvpro `vk_denoise`; AMD rocDecode copies tiled textures to linear buffers too). CUDA mipmapped-array
import exists but needs DXGI→CUDA format mapping and has a reported division-by-zero bug.

**Teardown/resize**: CPU-wait both sides (fence) → `cudaFree`/`hipFree(mapped ptr)` → `…DestroyExternalMemory` → release
the D3D12 resource; `…DestroyExternalSemaphore` only after outstanding signals/waits complete. Test 100× resize.

### 7.6 Per-frame ordering (the `.External()` pass, D5)

One shared fence, one monotonically increasing counter `v`:
1. D3D12 direct queue: render/accumulate → pack passes write the shared buffers → transition shared resources to
   `COMMON` (CUDA/HIP writes are invisible to EV's tracker) → close + `ExecuteCommandLists` → `queue->Signal(fence, ++v)`.
2. HIP/CUDA stream: `Wait(fence, v)` → kernels / OIDN / OptiX → `Signal(fence, ++v)`.
3. D3D12: `queue->Wait(fence, v)` (**GPU-side wait** – cuda-samples simpleD3D12 never calls `ID3D12CommandQueue::Wait`,
   it only CPU-waits; don't copy that) → new command list → unpack → tonemap → present.
- CPU call order: issue the HIP/CUDA signal before submitting the D3D12 Wait that depends on it (and the D3D12 Signal
  before the HIP/CUDA wait). The calls are non-blocking, so call order on one thread suffices.
- The simpleD3D12 fence-value pattern looks like it signals the same value it waited on – use the plain counter above.
- EV changes: `CommandQueue::Signal(fence, value)` / `Wait(fence, value)` helpers; `RenderGraph::Execute` must take the
  `CommandQueue` (not only a list) so a pass flagged `.External()` can close/submit, signal, call the callback, wait and
  continue on a new list; the tracker's "pending barriers at submit" design copes with splits. `GpuTimer` cannot time
  HIP/CUDA → `hipEvent`/`cudaEvent` timing shown in the panel. Option later: one-frame-late async denoise (vk_denoise
  style) for reference mode; CUDA-in-Graphics (CIG) experiment (`CU_DEVICE_ATTRIBUTE_D3D12_CIG_SUPPORTED`; OptiX in CIG
  undocumented).
- **Windows batching pitfall**: the Windows KMD batches/defers submissions, so a missing stream ordering (e.g. tiny
  upload → consumer) fails deterministically on Windows (gfx1201 affected); `hipFree` is synchronizing, `hipFreeAsync`
  is not → always use one explicit `hipStream_t` (not the null stream) and order only through the imported fence.
- Fence round-trip cost on either vendor: not measured anywhere → measure (CPU timestamps / D3D12 timestamp queries).

### 7.7 Orochi (alternative, reference)

[Orochi](https://github.com/GPUOpen-LibrariesAndSDKs/Orochi) (MIT, AMD GPUOpen, commits to 2026-10-29): one `oro*` API
that dynamically loads `amdhip64_*.dll` or `nvcuda.dll`, usually with runtime compilation via hipRTC/NVRTC (`orortc`);
wraps external memory/semaphores; samples `SimpleD3D12`, `WMMA`, `RadixSort`, `VulkanComputeSimple`. HIP DLL name order
it tries: `amdhip64_8.dll`, `_7`, `_6`, `amdhip64.dll`; hipRTC `hiprtc0801` … `hiprtc0702` … `hiprtc0503`.
**Not chosen** (two DLLs teach where vendors differ, offline compile errors at build time), but use it as reference
code. HIP RT is AMD's separate ray-tracing library – irrelevant to the DXR tracer.

### 7.8 Debugging and profiling compute

- AMD: device `printf` + CPU-reference comparison first; ROCgdb for hard bugs (`rocgdb app.exe`; disable the Ryzen
  iGPU because "more than one AMD GPU" is unsupported; needs the SDK-bundled driver; HIPCC defaults to PDB host debug
  info, which ROCgdb can't read); a 7.1.1 note mentions an intermittent TDR 0x116 while debugging. RGP 2.7.1 profiles
  HIP and D3D12 (whether both show on one timeline is undocumented → profile separately). PIX for the D3D12 side.
- NVIDIA: Nsight Systems (D3D12 + CUDA timeline, CPU gaps), Nsight Compute (kernels), Nsight VSE debugger.
- Exercise: run every PMPP exercise through both DLLs and compare occupancy/LDS bank conflicts (Nsight Compute vs RGP).

### 7.9 Matrix cores: RDNA 4 WMMA vs Ampere `nvcuda::wmma`

- RDNA 4: `__builtin_amdgcn_wmma_<CD>_16x16x16_<AB>_w32_gfx12`, e.g. `__builtin_amdgcn_wmma_f32_16x16x16_f16_w32_gfx12`
  (A/B fp16, C/D fp32); one wave32 per 16×16×16 tile; each lane holds 8 elements of A, B and C/D; **D's layout equals
  B's** (apart from type) → MLP layers chain without lane shuffles (RDNA 3 needed shuffles); downcast still needed
  ([GPUOpen RDNA 4 matrix cores](https://gpuopen.com/learn/using_matrix_core_amd_rdna4/),
  [WMMA guide part 1](https://gpuopen.com/learn/wmma-guide-amd-rdna-4-gpus-part-1/), parts 2–3). Builtin signatures
  changed in LLVM PR #175039 (Jan 2026, `_Float16` types) → expect churn. rocWMMA (header-only, `fragment`/`mma_sync`)
  supports gfx1200/1201, listed for Windows in ROCm 10.1. OIDN uses CK WMMA convolutions on gfx120x.
- Ampere: `nvcuda::wmma` (`<mma.h>`) fp16 m16n16k16 with fp32 accumulate (fragment shapes/types from training knowledge
  [U]); loads from shared memory want 128-bit alignment.
- **`ev_mma16(acc, a, b)` abstraction** [I]: 16×16 fp16 A/B tiles, 16×16 fp32 accumulator, one warp/wave32; HIP →
  gfx12 builtin on 8-element `_Float16`/`float` ext_vectors; CUDA → `wmma::fragment` + `mma_sync`. Fragment↔lane layouts
  differ (CUDA's is opaque) → **load/store live inside the abstraction**; kernel code never indexes fragment elements.
  A 64-wide layer for a 16-row batch tile = 16 WMMA ops (K = 64 in 4 steps). First write a plain fp32 FMA reference MLP
  (same code on both DLLs), then the WMMA path behind the same interface, and compare outputs.
- Existing small-MLP code: AMD MiniDXNN (HLSL coop-vec inference; training "next release";
  [GPUOpen](https://gpuopen.com/learn/minidxnn-mlp-library-for-dx12/)); community tiny-rdna4-nn (gfx1201, Linux,
  inference-only width-64 rocWMMA MLP). No official AMD fused MLP *training* kernel exists.

---

## 8. Denoising (D12, M7)

### 8.1 `IDenoiser` design

- Engine-side interface (in the `raytracing` module) with backends: `AccumulateOnly` (everywhere, CI), `Oidn`
  (implemented inside `EV-Hip.dll` / `EV-Cuda.dll` so it shares their stream and fence; OIDN CPU device as last resort
  needs CPU copies because CPU devices cannot import external memory), `Optix` (inside `EV-Cuda.dll`), later
  `Nrd`/`Svgf` in HLSL [I].
- Suggested shape (reference): `Supports(caps)`, `Resize(w, h)` (recreate filters / `optixDenoiserSetup`),
  `Denoise(inputs{color, albedo, normal, flow?, previousOutput?}, output)`, `Name()`, timings. Inputs come from HLSL pack
  passes into shared linear buffers (7.5).
- Guides: **albedo = RGB** (first non-specular hit's base colour, demodulation-friendly, not per wavelength), **normal =
  world-space** primary-hit normal (unit length), both noise-free-ish (accumulate them too). Denoise **every N frames or
  when the camera stops**; vk_denoise starts only after 10 iterations; OptiX "works better at higher counts (around 16
  spp)". Benchmark after warm-up (first OptiX call ~1200–1500 ms).
- Verification (M7): FLIP/MSE vs a ~16k-spp ground truth at 1/4/16/64 spp; timings after warm-up; OIDN vs OptiX on the
  3070; AMD runs OIDN-HIP; WARP accumulate-only.

### 8.2 OIDN 2.5.1 (first backend)

- 2.5.1 (2026-08-18; fixes an imported-memory leak on failed import), 2.5.0 (2026-06-02: external **semaphore** import,
  CUDA Windows/Linux + **HIP Windows only**; `OIDN_EXTERNAL_MEMORY_TYPE_FLAG_DEDICATED` "must be combined with the
  handle type flag"), 2.4.0 (CUDA 13 / ROCm 7, Volta removed), 2.3.3 (RDNA 4), 2.3.1 (HIP LUID, DX12/Vulkan import on
  Windows) ([releases](https://github.com/RenderKit/oidn/releases)). OIDN 3 (temporal) not released (CG Channel said
  H2 2026) → the RT filter is **not temporally stable**.
- API ([docs](https://www.openimagedenoise.org/documentation.html)):
  - Devices: `oidnNewHIPDevice(const int* deviceIDs, const hipStream_t* streams, int numPairs)` (only one pair; NULL
    stream = default), `oidnNewCUDADevice(…)` analogous [T], `oidnNewDeviceByLUID(luid)` (warning: drivers may report
    mismatching LUIDs), `oidnIsHIPDeviceSupported` (2.3.0).
  - Memory: `oidnNewSharedBufferFromWin32Handle(device, OIDN_EXTERNAL_MEMORY_TYPE_FLAG_D3D12_RESOURCE |
    OIDN_EXTERNAL_MEMORY_TYPE_FLAG_DEDICATED, handle, NULL, byteSize)` ("always use dedicated allocations if possible"),
    or wrap an already-mapped device pointer with `oidnNewSharedBuffer(device, ptr, size)`.
  - Semaphores: `oidnNewSharedSemaphoreFromWin32Handle`, `oidnSignalSemaphoresAsync`, `oidnWaitSemaphoresAsync`,
    `oidnRetainSemaphore/ReleaseSemaphore`; query `externalSemaphoreTypes`. On AMD use only `…D3D12_FENCE` (HIP range
    check, 7.5).
  - Filter "RT" (and "RTLightmap"): images `color`, `albedo`, `normal`, `output`; `hdr`, `cleanAux`; quality
    `OIDN_QUALITY_HIGH` (final), `BALANCED` (interactive), `FAST` (preview). Filter creation "can be very expensive" →
    create once per resolution. Exact setter names (`oidnSetFilterImage`, `oidnCommitFilter`,
    `oidnExecuteFilterAsync`, `oidnGetDeviceError`) and the RGBA-buffer-as-FLOAT3-with-16-B-pixel-stride trick from
    training knowledge [T][U].
- **Recommended EV setup** [I]: create your own `hipStream_t` (or `cudaStream_t`) in the DLL → `oidnNewHIPDevice(&dev,
  &stream, 1)` → OIDN and your own kernels are ordered on one stream; import the D3D12 fence yourself
  (`hipImportExternalSemaphore`) and wrap the whole batch with your own Wait/Signal; pass mapped pointers via
  `oidnNewSharedBuffer` → no dependence on OIDN's semaphore path.
- Build-time: OIDN HIP from source needs HIP SDK ≥ 6.4.2, CMake 3.21+, Ninja (no VS generator), `OIDN_DEVICE_HIP=ON`;
  the official `oidn-2.5.1.x64.windows.zip` may or may not contain the HIP device DLL [U – check the ZIP].
- No published OIDN timings for the RX 9070 XT or RTX 3070 (one anecdote: a CUDA OIDN port ~14 ms vs OptiX ~3 ms at
  1080p on an unnamed GPU [U]).

### 8.3 OptiX 9.1 denoiser (second backend, RTX 3070 only)

- OptiX 9.1.0 (Dec 2025; GitHub 2026-01-13), R590+, Turing+; driver-hosted (no DLL to ship), headers from
  [optix-dev](https://github.com/NVIDIA/optix-dev); free incl. commercial, SDK not redistributable. Driver updates
  improve quality without rebuilding.
- Models (`optix_types.h`): **`AOV` 0x2324** (single image, uses `hdrAverageColor`), **`TEMPORAL_AOV` 0x2326**,
  `UPSCALE2X` 0x2327, `TEMPORAL_UPSCALE2X` 0x2328; legacy `LDR` 0x2322 (→ AOV), `HDR` 0x2323, `TEMPORAL` 0x2325
  (deprecated) ([optix_types.h](https://raw.githubusercontent.com/NVIDIA/optix-dev/main/include/optix_types.h)).
- Sequence ([optix_host.h](https://raw.githubusercontent.com/NVIDIA/optix-dev/main/include/optix_host.h)):
  1. `cudaSetDevice` (LUID-matched), `cudaFree(0)` (primary context), `optixInit()` (failure → driver too old),
     `optixDeviceContextCreate(0 /*current*/, &opts, &ctx)`.
  2. `OptixDenoiserOptions { guideAlbedo = 1, guideNormal = 1, denoiseAlpha = OPTIX_DENOISER_ALPHA_MODE_COPY }` →
     `optixDenoiserCreate(ctx, OPTIX_DENOISER_MODEL_KIND_AOV, &o, &dn)`.
  3. `optixDenoiserComputeMemoryResources(dn, W, H, &sizes)` → allocate `stateSizeInBytes` and
     `withoutOverlapScratchSizeInBytes` (+ `computeAverageColorSizeInBytes`); log `OptixDenoiserSizes` (the only VRAM
     figure).
  4. `optixDenoiserSetup(dn, stream, W, H, state, stateSize, scratch, scratchSize)` – again on every resize (Falcor
     destroys and recreates).
  5. Per frame after the fence wait: `optixDenoiserComputeAverageColor(dn, stream, &inputImage, avgColor /*3 floats*/,
     scratch, size)` → fill `OptixImage2D{data, width, height, rowStrideInBytes, pixelStrideInBytes, format}` for color,
     albedo, normal (+flow) → `optixDenoiserInvoke(dn, stream, &params, state, stateSize, &guideLayer, &layer, 1, 0, 0,
     scratch, scratchSize)` (guide layer non-null; beauty layer first) → signal the fence.
  6. `optixDenoiserDestroy`.
- **Normal space (resolved by the follow-up)**: `OptixDenoiserGuideLayer::normal` is **(X,Y,Z) world space for all
  models except legacy LDR/HDR** (`optix_types.h`); NVIDIA staff: world space recommended on R535+, camera space still
  accepted; the `optixDenoiserInvoke` comment saying "camera space" for temporal models is stale; the SDK sample does
  no transform; Falcor's camera-space conversion predates R535 → **pass world-space unit normals**
  ([forum](https://forums.developer.nvidia.com/t/optix-8-0-denoiser-camera-space-vs-world-space/262875)). The normal
  guide only takes effect together with albedo.
- Formats: `HALF1–4`, `FLOAT1–4`, `UCHAR3/4`, `INTERNAL_GUIDE_LAYER` 0x2209. Use **HALF4 staging** (denoiser uses half
  internally; halves VRAM; one test showed no speed gain). Row stride `W·pixelSize` tightly packed is fine.
- `OptixDenoiserParams`: `hdrIntensity` (HDR), `hdrAverageColor` (AOV), `blendFactor` (0 = fully denoised),
  `temporalModeUsePreviousLayers` (0 on first frame/after cuts), `flowMulX/Y`.
- **Temporal rules** (TEMPORAL_AOV): flow = per-pixel screen-space motion **previous → current (forward in time)** –
  opposite of DLSS; **never NaN** (null flow OK, e.g. first frame); first frame `previousOutput = noisy input`, flow 0;
  two internal guide layers `W·H·internalGuideLayerPixelSizeInBytes` swapped each frame; `flowTrustworthiness`
  optional; Blender testers saw detail loss and "a temporal memory of 1 frame" (community). Only relevant once the
  camera moves (M8b); reference mode uses AOV on the accumulated buffer.
- **Expected timings on the 3070 [E]**: AOV 1080p ~2–6 ms, 1440p ~4–10 ms (extrapolated from NVIDIA's "low
  single-digit ms at 1080p on current GPUs"; 3070-specific data only for 32²/64² images ≈ 0.5 ms per call). UPSCALE2X
  costs more (4× output). Fits "every N frames / camera stop", not a per-frame budget.
- VRAM: no official figures (anecdotes: +250 MB, a 33 MB scratch request) → log sizes.

### 8.4 Real-time denoisers later (M8b)

- **NRD v4.18.0** ([NRD](https://github.com/NVIDIA-RTX/NRD)): REBLUR (diffuse/specular), RELAX (RTXDI-tuned), SIGMA
  (shadows), REFERENCE; HLSL compute recorded by the app (or NRI integration layer); inputs `IN_MV` (2.5D recommended,
  no jitter), `IN_NORMAL_ROUGHNESS` (world normal + linear roughness), `IN_VIEWZ`, radiance split diffuse/specular with
  normalized hit distances; 1 path/pixel design; accumulates over a limited window → avoid fireflies; not for
  volumetrics/transparency; recommends SHaRC/NRC. RTX 4080 1440p: REBLUR 2.55 ms, RELAX 3.25 ms, SIGMA 0.40 ms. CMake
  3.30+. AMD support by inference.
- **Own SVGF / A-SVGF** (Schied et al. HPG 2017 / 2018 [U URLs]): temporal accumulation with moments + variance-guided
  à-trous wavelet; vendor-neutral learning option.
- **DLSS-RR** (Streamline 2.14.16; NVIDIA only; needs albedo, specular albedo, normals, roughness, motion, depth,
  specular motion/hit distance; production app ID) and **FSR Ray Regeneration 1.2.1** (RDNA 4 only; SM 6.6, Win 11;
  RX 9070 XT 4 signals: 0.99 ms at 960×540, **3.74 ms at 1080p**, 7.15 ms at 1440p) – optional vendor extras.
- Ocean-specific: motion vectors of the surface ≠ motion of the reflected image; sun glints = fireflies; disocclusion
  behind crests; low-roughness water at 1 spp is the denoisers' hardest case → slightly higher minimum roughness far
  away; design the G-buffer around NRD's inputs (they overlap with RR and Ray Regeneration).

---

## 9. Radiance caches and NRC (D15, M9, M10)

### 9.1 SHaRC (M9, both PCs)

([SHaRC guide](https://raw.githubusercontent.com/NVIDIA-RTX/RTXGI/main/Docs/SharcGuide.md),
[repo](https://github.com/NVIDIA-RTX/SHARC); SHaRC 1.8.3 in RTXGI 2.8.0+/RTXPT)
- World-space hash grid of voxels in plain HLSL/GLSL. Passes: **Update** (sparse path tracing, ~1 random pixel per 5×5
  block ≈ 4 %, writes radiance into voxels), **Resolve** (merge with previous frames, evict stale), **Query** (render
  paths read the cache at a hit and terminate early). Indiana Jones: `SharcUpdate` up to 4 bounces, `TraceMain` 2
  bounces reusing the cache.
- HLSL API: `SharcInit`, `SharcUpdateHit`, `SharcUpdateMiss`, `SharcSetThroughput`, `SharcResolveEntry`,
  `SharcGetCachedRadiance`, `GetVoxelSize`, optional SH directional encoding. Parameters: `staleFrameNumMax`,
  `SHARC_PROPAGATION_DEPTH` (default 2), `SHARC_ENABLE_RESPONSIVE_LIGHTING`; LOD by camera distance
  (`logarithmBase`, `levelBias`), `sceneScale` sets voxel size.
- Requirements: **native fp16, SM 6.2+, DXC `-enable-16bit-types`**; lock buffer when
  `HASH_GRID_ENABLE_64_BIT_ATOMICS = 0`. Memory **40 B/voxel** (8 hash + 16 accumulation + 16 resolved) → 2^22 entries
  ≈ **160 MiB** (64 B / 256 MiB with SH); 10–20 % occupancy with a static camera.
- Spectral: convert the path's spectral contribution to RGB/XYZ **at the cache boundary** (`SharcUpdateHit` takes RGB);
  at query, multiply the cached RGB by the path throughput (convert throughput to RGB, or upsample the cached RGB as an
  illuminant-like spectrum) [I]. Bias vs reference is the M9 test.
- Teaches the same update/query/resolve shape as NRC, and the hash-grid ideas reused by Instant-NGP encodings.

### 9.2 The NRC paper (Müller, Rousselle, Novák, Keller, SIGGRAPH 2021) – all numbers

([arXiv HTML v2](https://arxiv.org/html/2106.12372v2), [abstract](https://arxiv.org/abs/2106.12372))
- **Rendering paths** terminate into the cache once the area spread is large: with x0 camera, x1 primary vertex,
  `a(x1…xn) = (Σ_{i=2..n} √(‖x_{i−1} − x_i‖² / (p(ω_i | x_{i−1}, ω) · |cos θ_i|)))²`,
  `a0 = ‖x0 − x1‖² / (4π cos θ1)`; terminate when `a > c·a0`, **c = 0.01** (trades variance for bias/speed).
- **Training paths**: one path per tile (single random offset; tile size adapted per frame to hit the record target)
  is extended by a few vertices (**< 3 %** of paths; most suffixes have one vertex), suffix ends by the same rule; a
  fraction **u = 1/16** of suffixes ends only by Russian roulette (limits self-training bias).
- **Self-training**: query the cache at the training path's terminal vertex, propagate back; every vertex becomes a
  target; each iteration adds a bounce (infinite-bounce over frames). Self-training ~11 % overhead.
- **Schedule**: **4 Adam steps per frame**, each on a disjoint random batch of **16,384 records** (65,536 = 2^16 per
  frame). Weights smoothed with an **EMA, α = 0.99**; EMA weights serve queries. Converges in **~8 frames (~70 ms)**.
  The learning rate is not in the text read (tiny-cuda-nn default 1e-3; ~1e-2 is only a tuning start [U]).
- **Network**: 7 FC layers = input → **5 hidden × 64 neurons, ReLU, no biases** → RGB output; fp16; fully fused CUDA
  kernel, 128 batch elements per block; training ~4–5× slower than inference per element. **Output × (diffuse albedo
  + specular reflectance)** – the net learns radiance / (albedo + specular reflectance).
- **Inputs (62 → padded to 64 with constant 1 = implicit bias)**: position frequency encoding, 12 sine terms at 2^d,
  d = 0..11, per axis (3×12 = 36); scattered direction (spherical, one-blob 4 bins per coord = 8); surface normal (8);
  roughness one-blob of `1 − e^{−r}` (4); diffuse (3) + specular (3) reflectance as-is → 36+8+8+4+3+3 = 62. Gaussian
  and sine kernels replaced by a quartic kernel and a triangle wave (saves ~0.25 ms).
- **Loss**: relative L2 with a stop-gradient denominator `sg(ŷ)² + ε`, **ε = 0.01**, normalized per channel by squared
  luminance. Adam beat SGD/Novograd; Shampoo converged slightly faster but cost 0.3 ms.
- **Timings** (1080p, RTX 3090, D3D12 + Falcor, PT+ReSTIR+NRC): trace & shade avg 9.16 ms, **query 1.59 ms, training
  1.11 ms** (per scene: Attic 1.66/1.12, Bistro 1.38/1.11, Classroom 1.70/1.11, Living Room 1.85/1.11, Pink Room
  1.54/1.12, Zero Day 1.41/1.09); **13.6× speedup at equal MRSE vs PT+ReSTIR**; slightly slower than DDGI but less
  biased and handles glossy.
- **Failure cases**: contact shadows and caustics (absent from inputs), axis-aligned stripes from the frequency
  encoding, low-frequency flicker, little benefit for glass/branching specular chains → **matters for the ocean**.
- Later NRC uses the Instant-NGP hash grid (sharper detail; "predicts outgoing radiance at the first non-specular
  vertex"). NRC SDK v0.16 internals differ from the 2021 paper (not surveyed).

### 9.3 Own NRC: staged plan (plan.md M10 a–g)

| Step | Build | Purpose / check |
|---|---|---|
| a | **CPU MLP** (C++): fit an image (2D → RGB) with hand-written forward, backprop, Adam, EMA, relative L2 | Gradient check (finite differences), loss curve, fitted image |
| b | Same in **HIP and CUDA** from one source (naive kernels, fp32) | Bit-near-identical results HIP vs CUDA vs CPU |
| c | **Offline fit** of radiance records dumped from the tracer (position, normal, direction, albedo, roughness, radiance) | ML bugs isolated from rendering bugs; compare with tiny-cuda-nn/PyTorch on the 3070 |
| d | **Online NRC** in EV via `.External()`: query records + self-training records written from HLSL, inference + training per frame, resolve pass | `DirectCacheView`-style debug view; converges in ~8 frames |
| e | **Speed**: fp16, fused layers (weights in LDS/shared), WMMA (RDNA 4) / Tensor Cores (3070) via `ev_mma16` | query+train ms vs the paper's 1.59 + 1.11 ms (different GPU) |
| f | **Compare** with NVIDIA NRC SDK + tiny-cuda-nn on the 3070 | equal-time error vs SHaRC and plain PT |
| g | Optional `dx::linalg` port (SM 6.10, experiment branch) | runs in HLSL on both PCs, no interop |

Prerequisites (plan.md): persistent bindless heap (M8a), `.External()` (M6), per-frame training records from HLSL.

### 9.4 The math the developer implements

- **Forward** (no biases; the constant-1 input acts as bias): `z_1 = W_1 x`, `a_l = ReLU(z_l)`, `z_{l+1} = W_{l+1} a_l`,
  output `ŷ = W_L a_{L−1}` (linear; NRC multiplies by reflectance outside, optionally an exp/softplus is used in other
  setups [T]). Shapes for NRC: W_1 64×64, 4 × (64×64), W_out 64×3 (pad to 16 rows for tensor tiles).
  Parameters ≈ 5·4096 + 64·3 ≈ 20.7k → fp16 ~41 KB; fp32 master + Adam m, v + EMA ≈ 330 KB – tiny [I].
- **Backward** [T]: `δ_L = ∂L/∂ŷ`; `∂L/∂W_l = δ_l a_{l−1}ᵀ` (summed over the batch); `δ_{l−1} = (W_lᵀ δ_l) ⊙ ReLU'(z_{l−1})`
  (ReLU' = 1 if z > 0 else 0). Batch gradient = mean over records.
- **Relative L2** [T + paper]: `L = (1/N) Σ (ŷ − y)² / (sg(ŷ)² + ε)` per channel, or with the NRC luminance variant the
  denominator `sg(lum(ŷ))² + ε`; gradient `∂L/∂ŷ = 2(ŷ − y) / (N (sg(·)² + ε))` – the denominator is a **constant** for
  backprop (stop-gradient). In a spectral pipeline use Y (from XYZ) as the luminance.
- **Adam** (Kingma & Ba, [arXiv 1412.6980](https://arxiv.org/abs/1412.6980) [U-not fetched]): `m = β1 m + (1−β1) g`;
  `v = β2 v + (1−β2) g²`; `m̂ = m/(1−β1^t)`; `v̂ = v/(1−β2^t)`; `w −= lr · m̂/(√v̂ + ε)`; defaults β1 0.9, β2 0.999,
  ε 1e-8 (NRC's values unknown).
- **EMA weights**: `w_ema = α w_ema + (1−α) w`, α = 0.99; queries use `w_ema` (optionally bias-correct
  `w_ema/(1−α^t)` early on [T]).
- **Mixed precision** [T] (Micikevicius et al. [arXiv 1710.03740](https://arxiv.org/abs/1710.03740) [U]): fp16
  weights/activations for matmuls, fp32 master weights + optimizer state, loss scaling if gradients underflow.
- **Encodings**: frequency `sin(2^d π x)` (normalize x to the scene AABB first); one-blob: k bins over [0,1], each bin
  gets a kernel value of the distance to the bin centre (Gaussian in Müller 2019 Neural Importance Sampling, quartic in
  NRC) [T]; triangle wave instead of sine (cheap).
- **Hash grid** (Instant NGP, Müller, Evans, Schied, Keller 2022, [arXiv 2201.05989](https://arxiv.org/abs/2201.05989)):
  L levels, F features per level, table size T per level, resolutions `N_l = ⌊N_min · b^l⌋` with
  `b = exp((ln N_max − ln N_min)/(L−1))`; per level: the 8 voxel corners around x, hash
  `h(c) = (c_x·1 ⊕ c_y·2654435761 ⊕ c_z·805459861) mod T` (dense indexing where `(N_l+1)³ ≤ T`), trilinear weights →
  concatenate L·F features (+ other encodings) as MLP input. Values L = 16, F = 2, T = 2^14–2^24, primes from
  memory [U – verify in the paper]; tiny-cuda-nn example config: n_levels 16, n_features_per_level 2,
  log2_hashmap_size 19, base_resolution 16, per_level_scale 2.0.
  **Backward**: `∂L/∂feature(corner) += w_corner · ∂L/∂enc` → scatter with atomics (collisions accumulate – that is
  intended); Adam on table entries too (sparse updates; Instant NGP's special optimizer details [U]).
- **Self-training record flow** [I]: (1) the update pass writes, per training vertex, its encoded/raw inputs, the
  radiance collected **after** that vertex so far, and the throughput to the suffix end; (2) the query pass writes query
  records at rendering-path terminals and at training-path terminals; (3) inference on all queries; (4) a "propagate"
  kernel builds targets `y_i = L_partial_i + T_i · cache(x_end)` (raw `L_partial_i` for the u = 1/16 RR-only suffixes);
  (5) shuffle into 4 batches of 16,384 and train; (6) resolve: `pixel += throughput · cache(query)`; apply the
  reflectance factorization on both sides.
- **Data layouts and sizes** (NRC SDK reference numbers, 1080p, training 274×154): QueryRadianceParams **56 B ×
  2.12M = 118.6 MB** (≈ 14 floats of raw inputs: position 3, direction 2, normal 2, roughness 1, diffuse 3, specular 3),
  QueryRadiance 25.4 MB (RGB fp32), TrainingRadianceParams 18.9 MB, QueryPathInfo 16.6 MB, TrainingPathVertices
  16.2 MB (48 B each); total ~192 MB. Store **raw** inputs and encode inside the kernel (encoded 64 × fp16 = 128 B per
  record would double the memory) [I].

### 9.5 References on the RTX 3070

- **NVIDIA NRC SDK** (RTXGI 2.9.0 / NRC v0.16.0, experimental;
  [NRC guide](https://raw.githubusercontent.com/NVIDIA-RTX/RTXGI/main/Docs/NrcGuide.md)): ships `NRC_D3D12.dll`,
  `NRC_Vulkan.dll`, CUDA 13 runtime DLLs, `Nrc.hlsli`, `NrcD3d12.h`. CPU: `nrc::d3d12::Initialize(GlobalSettings)`,
  `Context::Create(ID3D12Device5*, ctx)`, `Configure(ContextSettings{frameDimensions, trainingDimensions, …}, Buffers*)`
  (may hitch), per frame `BeginFrame(cmdList4, FrameSettings)` → `PopulateShaderConstants` → (update + query passes in
  your shaders) → `QueryAndTrain(cmdList4, float* trainingLoss)` → `Resolve(cmdList4, output)` → `EndFrame(queue)`
  (**same queue**); helpers `computeIdealTrainingDimensions`, `VerifySignature`. Shader:
  `NrcIsUpdateMode`, `NrcCreateContext`, `NrcSetSampleIndex`, `NrcCreatePathState`, `NrcUpdateOnHit` (→
  `TerminateImmediately` / `TerminateAfterDirectLighting`), `NrcUpdateOnMiss`, `NrcSetBrdfPdf`,
  `NrcWriteFinalPathInfo`; custom resolve via `NrcGetPathInfoIndex`, `NrcUnpackQueryPathInfo`, `NrcUnpackRadiance`.
  Debug: `ResolveMode::DirectCacheView` (recommended), training-bounce heat maps, `selfTrainingAttenuation` to disable
  self-training; Nsight Graphics can't see the network. The 617.42 driver floor is unverified – use R620+.
- **tiny-cuda-nn** ([repo](https://github.com/NVlabs/tiny-cuda-nn), BSD-3): JSON config (`loss`, `optimizer`,
  `encoding`, `network`), `tcnn::create_from_config(n_in, n_out, config)`, `trainer->training_step(inputs, targets,
  &loss)`, `network->inference(in, out)`, batch sizes multiples of `tcnn::BATCH_SIZE_GRANULARITY`; NRC building blocks
  Composite, TriangleWave, OneBlob, HashGrid encodings + `RelativeL2Luminance` loss (no full NRC config ships). For cc
  8.6: FullyFusedMLP width 64 needs forward `2·(16+16·8)·(64+8)` = 20,736 B, backward 18,432 B shared memory (width 128:
  39,168 B) → well below 99 KB → **runs on sm_86** despite the README's "RTX 3090 / 2080 Ti or higher" warning
  (both are cc 8.6) [I]; CutlassMLP fallback for wide inputs ([fully_fused_mlp.cu](https://raw.githubusercontent.com/NVlabs/tiny-cuda-nn/master/src/fully_fused_mlp.cu)).
  Build: `TCNN_CUDA_ARCHITECTURES=86`, CUDA 13.5, VS 2022 (README lists 2019/2022 only; VS 2026 undocumented),
  `git submodule update --init --recursive` (CUTLASS); CMake 3.21+; JIT fusion `set_jit_fusion(supports_jit_fusion())`
  1.5–2.5×. Example "learn an image": ~1000 steps in ~1 s on an RTX 4090. A published RTX 3070 data point: width-64
  MLP ~3 ms eval / ~10 ms per training step at 1280×720 (Neural Parametric Mixtures, different task).
- Use both **only as references** (D15): ground-truth cache images (`DirectCacheView`) and timing baselines.

### 9.6 `dx::linalg` port option and spectral considerations

- HLSL training needs SM 6.10 (preview; VectorAccumulate for gradients in 1.721-preview); RTXNS (Slang, NVIDIA,
  Agility 1.721.3-preview, R615+) and the Slang autodiff MLP tutorial show forward/backward in shaders
  ([RTXNS SimpleTraining](https://github.com/NVIDIA-RTX/RTXNS/blob/main/docs/SimpleTraining.md)). Keep SHaRC as the
  fallback (tiered capability).
- Spectral: no published wavelength-conditioned radiance cache. **Cache RGB/XYZ first** (both SDKs and the paper are
  RGB; loss normalized by luminance → Y): training targets = per-path `Σ c(λ_i) L(λ_i)/p(λ_i)` projected to RGB/XYZ; at
  query multiply by the spectral throughput via RGB conversion (tristimulus approximation; fine for diffuse indirect,
  wrong for strong dispersion). Research options: λ as an extra encoded input with scalar output, or spectral-basis
  output coefficients. The reflectance factorization carries over (spectral albedo at the query λ).
- Follow-ups worth knowing: prefiltered-radiance NRC for specular (Klepikov & Frolov, arXiv 2610.11702), reliability-
  weighted EMA (WSCG 2026), GSCache for volumes (arXiv 2507.19718), Neural Parametric Mixtures path guiding.

---

## 10. Milestone tutor guide (M0–M11, from `plan.md`)

Format per milestone: **Goal** · **Teach first** · **Files** · **Suggested order (hints, the developer decides)** ·
**Verify** · **Pitfalls** · **Read**. Every milestone ends with a `build-log.md` entry with measurements.

### M0 – Capabilities & plumbing

- **Goal**: the engine knows what the GPU can do, loads the Agility runtime, has `List4`, DXR-aware DRED, a running
  frame counter and a "Path Tracing" panel that says "unsupported" on WARP.
- **Teach first**: what the Agility SDK is (app-local D3D12Core.dll) and why exports must live in the exe (static-lib
  linker rule); COM `QueryInterface`/`As<>` for optional interfaces; `CheckFeatureSupport` structs (`OPTIONS5`
  RaytracingTier, `SHADER_MODEL` with HighestShaderModel *input*, `OPTIONS` ResourceBindingTier, `OPTIONS22` SER
  reorders); adapter LUID / VendorId; why the debug layer must stay silent; what DRED breadcrumbs are.
- **Files**: `OceanRenderer/source/main.cpp`, `ShaderArt/source/main.cpp` (exports after line 11); new macro header in
  `header/core/`; app `.vcxproj` post-build copy to `$(OutDir)D3D12\`; `source/core/application.cpp` (`Initialize`
  353-439 → caps after 428-438; `CreateAdapter` 441-482 → keep LUID/VendorId; `CreateDevice` 501-562 → base device +
  `As<ID3D12Device5>`; `GetBreadcrumbOpName` 1265-1290; `++m_frameCount` at 993), `header/core/application.h`
  (Device13 at 66/195/210, caps member + getter near 210-220); `header/DX12/command_list.h` (93, 511) /
  `command_list.cpp:47`; `ocean_scene.cpp` (`AddPanel` next to 348-349); include order for Agility headers in
  `EV-Engine.vcxproj` (213-229) and the app projects.
- **Order**: (1) Agility exports + DLL copy, log the loaded D3D12Core path; (2) device creation change; (3) caps struct
  + log line (adapter name, VendorId, LUID, RT tier, highest SM, binding tier, SER-reorders, Agility version); (4) List4;
  (5) DRED names; (6) frame counter (check what else reads `GetFrameCount()` – `ReleaseStaleDescriptors` – before
  enabling); (7) panel with a disabled toggle when unsupported; (8) optionally DXC next to the exe.
- **Verify**: CI (manual run or PR, since push doesn't trigger) green on WARP with panel "unsupported"; both PCs log
  caps (expect RT tier ≥ 1.1, SM 6.9, binding tier 3 [I], SER-reorders FALSE on both); `GetModuleFileName` shows
  `...\x64\Debug\D3D12\D3D12Core.dll`; debug layer silent in a full session; a forced device removal (e.g. TDR test)
  prints known op names.
- **Pitfalls**: exports in the engine lib (silently dropped); missing trailing slash in `D3D12SDKPath`; version
  number ≠ DLL (`D3D12_ERROR_INVALID_REDIST`); SDKLayers next to the exe instead of `D3D12\`; mixing Windows-SDK and
  Agility `d3d12.h`; caps probing that throws on WARP; querying `SHADER_MODEL` without setting the input highest model;
  `CheckFeatureSupport1` clamping (SDK version 620 to see new tiers).
- **Read**: [Agility getting started](https://devblogs.microsoft.com/directx/gettingstarted-dx12agility/),
  [downloads](https://devblogs.microsoft.com/directx/directx12agility/), [SM 6.9 retail](https://devblogs.microsoft.com/directx/shader-model-6-9-retail-and-more/),
  local sample `T:\Projects\DX12\Samples\DirectX-Graphics-Samples\Samples\Desktop\D3D12Raytracing\src\D3D12RaytracingHelloWorld`
  (device check), section 6.1, 6.7.

### M1 – Acceleration structures

- **Goal**: BLAS per `Mesh` (cached), TLAS per frame from a visitor, AS build pass in the render graph with timing.
- **Teach first**: BVH concepts (Bikker's BVH series), BLAS vs TLAS, instance transforms, prebuild info, scratch,
  AS state rules (never transitioned, UAV barriers only), build vs update vs compaction, flag recipes (6.4).
- **Files**: new `header/raytracing/acceleration_structure.h` + source; `header/resources/resource.h` /
  `source/resources/resource.cpp` (new creation path: initial state + flags; or a dedicated AS buffer type);
  `application.*` (`CreateAccelerationStructureBuffer`-style factory); `render_graph.h` (`RGAccess` 76-91),
  `render_graph.cpp` (state map 27-48, `ExecutePass` 630-734 barrier logic, pass kind for the panel; block capture of
  AS buffers); `scene_visitor.cpp:16-46` as template; `mesh.h` (`Source`, VB/IB getters); `upload_buffer.*` (>2 MB).
- **Order**: (1) AS buffer creation + `SetName`; (2) one BLAS for the helmet (prebuild → allocate → build → UAV
  barrier), inspect in PIX/RRA; (3) BLAS cache keyed by `Mesh*` with `weak_ptr`, release when the mesh dies; (4)
  `RaytracingSceneVisitor` emitting `(Mesh*, world, material)` → instance descs (transpose!) + per-instance table;
  (5) TLAS rebuild per frame; (6) graph accesses `AccelerationStructureWrite/Read` (UAV barrier only), scratch as a
  transient UAV buffer; (7) timing in the Render Graph panel.
- **Verify**: RRA (AMD) / Nsight or PIX AS viewer (NVIDIA) show correct instance placement, triangle counts, no
  exploded/mirrored instances; spawned scenes added/removed via editor and scene files update the TLAS; build pass and
  GPU ms visible; no debug warnings; BLAS sizes logged (for the VRAM table).
- **Pitfalls**: transitioning an AS buffer (validation error); shared scratch between parallel BLAS builds without
  barriers; prebuild inputs ≠ build inputs; 256-B alignment; VB not in `NON_PIXEL_SHADER_RESOURCE` during the build;
  instance desc buffer > 2 MB upload page; `InstanceMask = 0` (instance never hit); forgetting the point-light sphere
  needs one instance per light; destroying a BLAS still referenced by an in-flight TLAS (defer release ~3 frames).
- **Read**: [DXR spec](https://microsoft.github.io/DirectX-Specs/d3d/Raytracing.html) (AS sections),
  [NVIDIA RTX best practices](https://developer.nvidia.com/blog/rtx-best-practices/),
  [Bikker BVH series](https://jacco.ompf2.com/2022/04/13/how-to-build-a-bvh-part-1-basics/) [U],
  HelloWorld sample, [RRA quickstart](https://gpuopen.com/manuals/rra_manual/quickstart/).

### M2 – RayQuery debug views + ocean BLAS

- **Goal**: first rays: a compute pass writing primary-hit depth / normal / instance ID / barycentrics; the ocean in
  the BVH via a displace pass + refit; freeze-time toggle; the `GenerateH0` sync and uv mismatch fixed.
- **Teach first**: inline RayQuery (6.3), camera ray generation (3.1), barycentric interpolation of vertex attributes
  (`ByteAddressBuffer` reads at stride 60), refit vs rebuild, cross-queue synchronization (why copy-queue work needs a
  GPU wait), ocean displacement math in `ocean_vertex.hlsl`.
- **Files**: `ocean_scene.cpp` (`RecordRenderGraph` 677-835: displace pass after 734, BLAS/TLAS pass, debug pass after
  744-745; `paramsChanged` 967-977 + `GenerateH0` 1111-1220 sync fix; ocean time static 520/525/598 → member + freeze;
  point light 612); new `OceanRenderer/shaders/ocean_displace_CS.hlsl` (or an engine shader through the provider);
  `ocean_vertex.hlsl:49-57` and `ocean_pixel.hlsl:148-151` (uv convention); engine `pt_debug.hlsl` (runtime DXC,
  engine shader folder registered with `ShaderLibrary`); geometry-provider interface in the `raytracing` module.
- **Order**: (1) runtime-DXC engine shader path + hot reload of a trivial compute shader; (2) debug pass for meshes
  only (depth/normal/instance); (3) displace pass writing positions (+ optionally undisplaced xz), check against raster
  in the Buffer viewer; (4) ocean BLAS build + refit + rebuild every N; (5) freeze toggle; (6) uv fix; (7) H0 sync fix;
  (8) log refit/build timings.
- **Verify**: traced depth − raster depth ≈ 0 in the Texture Viewer (use the same projection; compare linear depth);
  wave silhouettes match at frozen time; moving the plane with the gizmo keeps raster and traced waves aligned (uv fix
  works); refit/build timings on both GPUs vs the ~0.5/5 ms estimates; editing ocean parameters doesn't flicker or
  read half-written cascades.
- **Pitfalls**: displacing with world xz in the compute pass but object xz in the VS (or vice versa); forgetting the
  UAV barrier + state transition between displace and refit; refit with changed flags; NDC y flip; ray origin from
  `invView` translation vs camera position mismatch; TMin = 0 self-hits; reading the cascade textures before `Ocean
  Permute 3` finished (declare graph reads); fp16 positions losing precision; thread-group size 8×4 (AMD) vs 8×8/16×8
  (NVIDIA).
- **Read**: DXR spec TraceRayInline examples; [AMD RDNA guide](https://gpuopen.com/learn/rdna-performance-guide/);
  `D3D12RaytracingSimpleLighting` (VB/IB fetch from barycentrics); RTG1 ch. 6 (self-intersection); section 2.4, 6.5.

### M3 – RGB reference path tracer

- **Goal**: progressive, unbiased RGB path tracer over meshes + ocean (opaque GGX surface first), with NEE, MIS, env
  importance sampling and RR, tonemapped through EV's Tonemap.
- **Teach first**: section 3 in order 3.1 → 3.2 → 3.3 → 3.4 → 3.5 → 3.6 → 3.8 → 3.9 → 3.10; the white furnace idea;
  why 1/√N; staged descriptor tables (TLAS + VB/IB SRVs + material buffer + texture table under one global root
  signature).
- **Files**: new `header/raytracing/path_tracer.h` + source; engine shaders `pt/*.hlsli` (API-agnostic path loop,
  `bsdf.hlsli`, `lights.hlsli`, `sampling.hlsli`) + one `cs_6_6` entry; `header/resources/material.h` (GPU material
  struct alongside `MaterialProperties`); accumulation texture imported into the graph; scene-version counter in
  `editor.h/.cpp` (table 2.5); `ocean_scene.cpp` mode switch (skip 747-821, feed Tonemap 824-832), FOV fix (136),
  fixed ocean seed (1223); env-map CDF build (CPU or compute once per environment change, hook `SetEnvironment` 190).
- **Order**: (1) camera rays + miss = env lookup (should look like the skybox); (2) accumulation + reset logic; (3)
  Lambert-only with BSDF sampling (no NEE) → white furnace; (4) env importance sampling + MIS; (5) sun disk + point
  lights (NEE); (6) GGX VNDF + lobe selection, material textures through the staged table; (7) RR + bounce cap; (8)
  sampler upgrade (hash → Sobol/Owen); (9) panel: spp/frame, max bounces, sample count, reset, freeze, debug view.
- **Verify**: **white furnace** (albedo-1 Lambert sphere in a uniform env disappears at any bounce count); MSE vs spp
  slope −1 against a long reference; helmet under IBL vs raster (expect differences from shadows/interreflection and
  Smith-Schlick vs Smith-GGX, not from colour/brightness scale); simple scene vs Mitsuba 3 `scalar_rgb` / pbrt-v4 with
  FLIP; accumulation resets on every scene-version source (table 2.5) and on play-mode edges.
- **Pitfalls**: double-counting the sun (env + disk); pdf in area vs solid angle; MIS weight applied on one side only;
  env direction convention mismatch with `pano_to_cubemap_CS.hlsl`; missing sinθ in the env pdf; sRGB textures sampled
  without sRGB views (base colour must be linear); normal-map side flips; RR without dividing by q; correlated seeds
  (frame counter not incremented); fp16 accumulation; IBL intensity factor mismatch with raster; TDR on high spp → keep
  1 spp/dispatch; `MaterialProperties` stride 140.
- **Read**: pbr-book 4ed chapters on Monte Carlo, sampling, reflection models, light transport I (path tracing)
  ([pbr-book](https://pbr-book.org/4ed/)); Veach thesis ch. 9 (MIS) [U]; Heitz 2018 VNDF [U]; *Ray Tracing in One
  Weekend* series ([raytracing.github.io](https://raytracing.github.io/)) [U]; RTXPT source for structure.

### M4 – Spectral (HWSS)

- **Goal**: the same tracer with 4 wavelengths per path, Jakob–Hanika upsampling, D65 illuminant, CMF → XYZ → sRGB;
  RGB/spectral toggle.
- **Teach first**: section 4.1–4.5 (why HWSS, Eq. 5/6/11, pbrt constants, the three spectrum kinds, nm units, film
  stays RGB), the test ladder 4.8.
- **Files**: `pt/spectrum.hlsli` (`SampledWavelengths`, `SampleVisible`, `TerminateSecondary`, `ToXYZ`, CMF fit, XYZ →
  sRGB), `pt/rgb2spec.hlsli` behind the swappable interface (4.6), table loader in the module (offline-generated binary
  from `rgb2spec_opt`, StructuredBuffer + zNodes CB), `path_tracer.cpp` (permutation define), panel toggle.
- **Order**: (1) wavelength sampler + CMF integral test (4.8-1); (2) D65 + XYZ → sRGB, D65 → white test; (3) table
  upload + lookup with grey/near-grey tests; (4) spectral throughput in the path loop (albedo kind for materials,
  illuminant kind for env/sun/lights, unbounded for emission); (5) `r_u`/`r_l` plumbing as a verified no-op; (6) toggle
  + overhead measurement.
- **Verify**: grey/white round-trip neutral; colour chart ≈ RGB mode (single bounce ~1e-3); overhead in the Peters
  class (< 0.3 ms at 1080p 1 spp on a 5070 Ti – compare relative cost); compare with Mitsuba 3 `scalar_spectral`
  (expect ~0.1 % luminance offset from the CIE_Y constant).
- **Pitfalls**: λ normalized vs nm (coefficients are nm); fp16 coefficients; missing D65 on RGB lights (white light
  turns pinkish/greenish); dividing by the wrong pdf (must be each lane's pdf in `ToXYZ`, hero pdf for directions);
  using one wavelength's albedo for denoiser guides; forgetting `SafeDiv`; HLSL `atanh` missing; table index order
  `[maxc][z][y][x][coef]`; z = 0 (black) division.
- **Read**: [pbr-book 4.5/4.6](https://pbr-book.org/4ed/Radiometry,_Spectra,_and_Color/Representing_Spectral_Distributions),
  HWSS paper Sec. 3–4, [Jakob & Hanika 2019](https://rgl.epfl.ch/publications/Jakob2019Spectral),
  [Moments in Graphics part 2](https://momentsingraphics.de/SpectralRendering2Rendering.html), Wyman 2013.

### M5 – Spectral water

- **Goal**: a dedicated water material: dielectric with n(λ), rough BTDF, homogeneous absorbing + scattering medium
  with hero-wavelength distance sampling and spectral MIS, foam layer, optional dispersion and seafloor.
- **Teach first**: 3.7 (Fresnel/TIR/Snell, radiance scaling), section 5 (data, phase function, 5.6/5.7 MIS rules),
  why spectral matters (5.8), inside/outside tracking (5.5).
- **Files**: `pt/water.hlsli`, `pt/medium.hlsli`; baked 1 nm tables (σ_a Pope&Fry + Segelstein, σ_s Morel ×1.3, n(λ)
  Quan & Fry at chosen T/S) as buffers; ocean geometry provider (material ID, cascade textures + patch sizes); ocean
  panel (water parameters, T, S, chlorophyll later, dispersion toggle, seafloor depth = `OCEAN_DEPTH`).
- **Order**: (1) smooth dielectric surface with constant n (reflection/refraction, TIR) – check from below; (2)
  absorption-only medium (Beer–Lambert per lane); (3) slab test; (4) scattering with the albedo-weighted scheme + MIS;
  (5) foam layer; (6) rough BTDF; (7) n(λ) + dispersion toggle (`TerminateSecondary`); (8) seafloor plane + caustics
  observation; (9) raster SSS calibration session.
- **Verify**: 10 m slab ≈ **94/58/11/1 %** at 440/550/600/680 nm (compare against the offline 1 nm integral); TIR onset
  at `asin(1/n)` (48.6° for n = 1.333; 48.2° for seawater 1.3408); RGB vs spectral deep-water A/B shows the hue shift;
  Mitsuba `homogeneous` + `tabphase` comparison; no colour fireflies in deep water (spectral MIS works); dispersion off
  → no new colour noise.
- **Pitfalls**: wrong side of the interface (normal orientation on a height field); applying 1/η² twice or never;
  absorption sampling with a red hero killing all lanes; forgetting `r_l` updates; using air-relative n where vacuum is
  expected (or vice versa); m⁻¹ vs cm⁻¹ (×100); σ data below 380 / above 727.5 nm; underwater NEE through the interface
  (blocked); fireflies from glints.
- **Read**: [pbr-book 14.2](https://pbr-book.org/4ed/Light_Transport_II_Volume_Rendering/Volume_Scattering_Integrators),
  Walter 2007, Bruneton 2010, Ocean Optics Book ([water](https://www.oceanopticsbook.info/view/optical-constituents-of-the-ocean/water)),
  [OMLC water data](https://omlc.org/spectra/water/abs/), [RTG II ch. 30](https://developer.download.nvidia.com/ray-tracing-gems/rtg2-chapter30-preprint.pdf).

### M5b (optional, after M4) – Peters' Fourier method

- **Goal**: second RGB→spectrum implementation behind the same interface; A/B on a frozen shot.
- **Teach**: 4.6 (moments, bounded MESE, phase warp, Levinson); keep the wavelength sampler as a separate switch.
- **Verify**: colour difference (ΔE / FLIP) and noise at equal spp vs Jakob–Hanika; per-hit cost.
- **Pitfalls**: Peters' LUT weight already contains CMF/pdf (don't divide again); LUT index order `lut[B,G,R]`;
  8-bit sRGB input vs linear.
- **Read**: Peters et al. 2019, `spectra.glsl`, `illuminant_spectra.py`, `fourier_srgb_conversion.py`.

### M6 – GPU compute bring-up (HIP + CUDA)

- **Goal**: `EV-Hip.dll` (+ the same source as `EV-Cuda.dll`), proven D3D12 ↔ HIP/CUDA buffer + fence round trip,
  `.External()` render-graph pass, first kernels (image difference metric, Jakob–Hanika coefficient baker).
- **Teach first**: GPU programming model (PMPP ch. 1–6: grids/blocks/threads, memory hierarchy, tiling, reduction),
  HIP vs CUDA differences (7.4), external memory/semaphores (7.5), the frame split (7.6), Windows submission batching.
- **Files**: new projects `EV-Hip/` and `EV-Cuda/` (not app dependencies); `header/DX12/command_queue.h` +
  `command_queue.cpp` (`Signal/Wait(fence, value)`; current fence `FLAG_NONE` at 27); `render_graph.*` (`Execute` takes
  the queue; `.External()`); `resource.cpp` shared-heap path; `application.cpp` vendor/LUID → DLL loading.
- **Order**: (1) install HIP SDK 7.2, `hipInfo`; PMPP vector add / reduction through the compat header in a scratch
  project; (2) DLL skeleton + C ABI + LUID match; (3) **spike**: D3D12 compute writes a pattern → HIP adds 1 → D3D12
  checks bit-exact, with GPU-side waits; (4) `CommandQueue` helpers; (5) `.External()` pass; (6) resize/teardown loop;
  (7) same source on the 3070 as `EV-Cuda.dll`; (8) first useful kernels.
- **Verify**: bit-exact round trip; RGP / Nsight Systems timeline without CPU stalls; 100× resize/teardown without
  leaked handles (Task Manager handle count / debug-layer live objects); identical results HIP vs CUDA.
- **Pitfalls**: importing with the buffer width instead of the allocation size; missing Dedicated flag (CUDA);
  timeline/keyed-mutex semaphore types on AMD; null stream; CPU waits hiding a missing GPU wait; tracker not knowing
  external writes (transition to COMMON before the split); fence values reused; destroying external memory before the
  mapped pointer is freed; ROCgdb with the iGPU enabled; `0xffffffff` masks.
- **Read**: [Orochi simpleD3D12](https://github.com/GPUOpen-LibrariesAndSDKs/Orochi/blob/main/Test/SimpleD3D12/simpleD3D12.cpp),
  [cuda-samples simpleD3D12](https://github.com/NVIDIA/cuda-samples/tree/master/cpp/5_Domain_Specific/simpleD3D12),
  [HIP porting guide](https://rocm.docs.amd.com/projects/HIP/en/latest/how-to/hip_porting_guide.html),
  [HIP language extensions](https://rocm.docs.amd.com/projects/HIP/en/latest/how-to/hip_cpp_language_extensions.html),
  [amd/skills fault classes](https://github.com/amd/skills/blob/HEAD/skills/moat-cuda-to-rocm/references/fault-classes.md),
  PMPP 4th ed.

### M7 – Denoising

- **Goal**: `IDenoiser` with OIDN (HIP on AMD, CUDA on NVIDIA), OptiX AOV on NVIDIA, accumulate-only fallback; pack/
  unpack passes; denoise every N frames or on camera stop.
- **Teach first**: what learned denoisers need (HDR linear colour, albedo/normal guides, why RGB albedo), OIDN and
  OptiX APIs (8.2, 8.3), evaluation with FLIP.
- **Files**: `header/raytracing/denoiser.h`; pack/unpack HLSL passes (texture ↔ shared linear buffers,
  FLOAT4/HALF4); OIDN + OptiX code inside the DLLs; guide AOV outputs from the path tracer (first non-specular hit
  albedo, world normal, accumulated).
- **Verify**: FLIP/MSE vs ~16k-spp ground truth at 1/4/16/64 spp; timings after warm-up on both PCs; OIDN vs OptiX on
  the 3070; WARP runs accumulate-only.
- **Pitfalls**: sRGB/tonemapped input instead of linear HDR; guides from a single wavelength; camera-space normals
  (use world space); timing the first call; recreating filters every frame; NaN/Inf in the input (sanitize in the pack
  pass); row stride mismatch; OIDN CPU device with shared memory (impossible).
- **Read**: [OIDN docs](https://www.openimagedenoise.org/documentation.html),
  [optix_host.h](https://raw.githubusercontent.com/NVIDIA/optix-dev/main/include/optix_host.h),
  [Falcor OptixDenoiser](https://github.com/NVIDIAGameWorks/Falcor/tree/master/Source/RenderPasses/OptixDenoiser),
  [vk_denoise](https://github.com/nvpro-samples/vk_denoise), [FLIP](https://github.com/NVlabs/flip).

### M8a – Persistent bindless heap

- **Goal**: one shader-visible CBV/SRV/UAV heap (static region + per-frame ring), frame-based deferred release, SM 6.6
  `ResourceDescriptorHeap[]`, all raster passes unchanged.
- **Teach first**: descriptor heaps vs tables, why only one heap can be bound, ring allocation with fence-based
  retirement, SM 6.6 dynamic resources and the root-signature flags `CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED` [T],
  non-uniform indexing (`NonUniformResourceIndex`).
- **Files**: `dynamic_descriptor_heap.*` (1024/heap, 148-204, 224-244, 162-175), `descriptor_allocator*`,
  `root_signature.cpp` (105-108), every PSO class, `swapchain.cpp:189`, `application.cpp:993` frame counter.
- **Verify**: CI screenshot diff pixel-identical for all raster passes; descriptor-leak counter stable over a long
  session; PT material textures fetched bindlessly.
- **Pitfalls**: freeing descriptors still referenced by in-flight frames (~3 frames); mid-list heap switches left in
  old code paths; GUI/preview PSOs forgotten; missing `NonUniformResourceIndex` (works on one vendor, garbage on the other).
- **Read**: [DXR/D3D12 specs on SM 6.6 dynamic resources](https://microsoft.github.io/DirectX-Specs/) [U exact page],
  RTXPT (binding tier 3, SM 6.6).

### M8b – Real-time mode

- **Goal**: raster G-buffer primaries + motion vectors (incl. ocean), 1–2 bounces at 1 spp, temporal denoiser (NRD or
  own SVGF), firefly clamp, ocean time running.
- **Teach first**: hybrid rendering (industry recipe: G-buffer → direct (ReSTIR DI later) → short indirect → cache →
  denoiser), motion vectors for a displaced surface (previous-frame displacement), demodulation, temporal reprojection,
  NRD inputs (8.4).
- **Files**: new G-buffer / motion passes in `RecordRenderGraph`, `path_tracer.cpp` mode switch, ocean previous
  displacement (keep last frame's position buffer).
- **Verify**: frame budget on both GPUs; ghosting on moving crests; error vs frozen-time reference.
- **Pitfalls**: motion vectors of the surface ≠ reflected motion; jittered vs unjittered MVs; NaN flow; fireflies
  breaking temporal accumulation; low-roughness water at 1 spp.
- **Read**: [NRD](https://github.com/NVIDIA-RTX/NRD) (+ NRD-Sample `simplex` branch), SVGF / A-SVGF papers [U],
  [A Gentle Introduction to ReSTIR](https://intro-to-restir.cwyman.org/) [U-not fetched].

### M9 – SHaRC

- **Goal**: SHaRC update (~4 % of pixels) / resolve / query; spectral → RGB at the cache boundary.
- **Teach first**: world-space hashing, voxel LOD by distance, update/resolve/query, bias of caches (9.1).
- **Files**: `pt/sharc_*.hlsl` (SHaRC headers vendored under `thirdparty/`), DXC `-enable-16bit-types`, cache buffers
  (~160 MiB) imported in the graph, panel debug view.
- **Verify**: cache debug view (voxel colours stable, sensible LOD); bias vs reference; performance both PCs.
- **Pitfalls**: 64-bit atomics availability (lock buffer otherwise); `sceneScale` wrong for a 1024 m ocean (voxels
  too big/small); caching spectral values; stale entries after scene changes (tie resets to the scene version).
- **Read**: [SHaRC guide](https://raw.githubusercontent.com/NVIDIA-RTX/RTXGI/main/Docs/SharcGuide.md),
  [SHARC repo](https://github.com/NVIDIA-RTX/SHARC), RTXPT integration.

### M10 – Own NRC

- **Goal**: plan.md (a)–(g) (table 9.3).
- **Teach first**: 9.2 numbers, 9.4 math in order (forward → backward → loss → Adam → EMA → encodings → hash grid →
  mixed precision), then the record flow; Karpathy-style backprop by hand before any GPU code.
- **Files**: CPU MLP in a tool/test project first; then kernels in the shared GPU source (`nrc_*.h`), HLSL record
  writers in the path tracer, `.External()` pass for query+train, resolve pass.
- **Verify**: (a) gradient check vs finite differences (relative error < 1e-3 in fp32) and loss curve on an image; (b)
  HIP vs CUDA vs CPU outputs match; (c) offline fit loss vs tiny-cuda-nn on the same records; (d) cache converges in
  ~8 frames, `DirectCacheView`-style view looks plausible; (e) query+train ms; (f) equal-time error vs SHaRC and plain
  PT; NRC SDK as ground truth on the 3070.
- **Pitfalls**: forgetting the stop-gradient in relative L2; EMA weights used for training (should train raw, query
  EMA); input normalization (positions to [0,1] per scene AABB); fp16 overflow in radiance targets (factorize by
  reflectance, scale); atomics contention in hash-grid backward; training batch not shuffled; caustics/glass expected
  failures (paper).
- **Read**: [NRC paper](https://arxiv.org/html/2106.12372v2), [Instant NGP](https://arxiv.org/abs/2201.05989),
  [tiny-cuda-nn](https://github.com/NVlabs/tiny-cuda-nn) (`fully_fused_mlp.cu`), [NRC guide](https://raw.githubusercontent.com/NVIDIA-RTX/RTXGI/main/Docs/NrcGuide.md),
  [GPUOpen RDNA 4 matrix cores](https://gpuopen.com/learn/using_matrix_core_amd_rdna4/), Karpathy micrograd [U],
  Adam paper [U].

### M11 (optional) – DXR pipeline, SER, ReSTIR DI, spectral sky, cluster-template ocean, caustics

- **DXR pipeline**: new state-object + SBT class (`CD3DX12_STATE_OBJECT_DESC`), global root signature only (local root
  args are awkward with per-list dynamic heaps), `SetPipelineState1`/`DispatchRays` wrappers resetting the PSO cache
  (`command_list.cpp:1261-1274`), `ReadShaderTarget` (`shader.cpp:65-76`) + no `-E` for `lib_6_x` hot reload
  (`PipelineShaders::Update` can rebuild the state object). Reuse the API-agnostic `.hlsli`.
- **SER**: `dx::HitObject` + `dx::MaybeReorderThread(hitObject, coherenceHint, numHintBits)` from raygen (names from
  training knowledge [U]) – **no reorder on either PC** (RX 9070 XT accepts the API, RTX 30 no-op) → speed-up measurable
  only on RTX 40+ / Intel Arc B / WARP 1.0.21+; verify "no regression".
- **ReSTIR DI**: [Gentle Introduction to ReSTIR](https://intro-to-restir.cwyman.org/) [U], RTXDI ([repo](https://github.com/NVIDIA-RTX/RTXDI) [U]).
- **Spectral sky**: Hosek–Wilkie 2012 ([project](https://cgg.mff.cuni.cz/projects/SkylightModelling/) [U]) or Bruneton
  precomputed scattering ([repo](https://github.com/ebruneton/precomputed_atmospheric_scattering) [U]); note
  `atmosphere-scatter` branch has old atmosphere code.
- **Cluster-template ocean** (CLAS, preview): 16×16-quad tiles = 512 tris → two CLAS per tile (≤ 256 tris/verts per
  CLAS), "comparable with traditional refit" ([DXR Part 2](https://microsoft.github.io/DirectX-Specs/d3d/Raytracing2.html));
  Microsoft sample on RX 9070: 788 ms → 3.11 ms build, 7.5 GiB → 774 MiB (rebuild comparison, not refit).
- **Caustics**: photon pass / MNEE (5.8).

---

## 11. Learning resources (per track)

Unverified URLs are marked [U] (known canonical locations, not fetched by the research).

| Track | Resource | Why |
|---|---|---|
| Path tracing | [pbr-book 4ed](https://pbr-book.org/4ed/) + [pbrt-v4 source](https://github.com/mmp/pbrt-v4) | The textbook; code matches |
| | [Ray Tracing in One Weekend series](https://raytracing.github.io/) [U] | Beginner logic to port to HLSL |
| | Veach thesis, [graphics.stanford.edu/papers/veach_thesis](https://graphics.stanford.edu/papers/veach_thesis/) [U] | MIS, path integral |
| | Heitz 2018 VNDF [jcgt 0007/04/01](https://jcgt.org/published/0007/04/01/) [U]; Eto & Tokuyoshi 2023 bounded VNDF [U]; Burley 2020 Owen [jcgt 0009/04/01](https://jcgt.org/published/0009/04/01/) [U]; Jarzynski & Olano 2020 GPU hashes [jcgt 0009/03/02](https://jcgt.org/published/0009/03/02/) [U] | Sampling |
| | [Ray Tracing Gems I/II](https://www.realtimerendering.com/raytracinggems/) [U] | RTG1 ch. 6 self-intersection, RTG2 ch. 30 caustics, ch. 42 GPU spectral |
| | [NVIDIA RTXPT](https://github.com/NVIDIA-RTX/RTXPT), [Capsaicin](https://github.com/GPUOpen-LibrariesAndSDKs/Capsaicin), [kajiya](https://github.com/EmbarkStudios/kajiya) (archived) | Production/research DX12/Vulkan path tracers |
| | [Jacco Bikker BVH series](https://jacco.ompf2.com/2022/04/13/how-to-build-a-bvh-part-1-basics/) [U] | BVH quality, why refits degrade |
| DX12/DXR | [DXR spec](https://microsoft.github.io/DirectX-Specs/d3d/Raytracing.html), [DXR Part 2](https://microsoft.github.io/DirectX-Specs/d3d/Raytracing2.html), [Agility downloads](https://devblogs.microsoft.com/directx/directx12agility/), [getting started](https://devblogs.microsoft.com/directx/gettingstarted-dx12agility/) | API |
| | [NVIDIA RTX best practices](https://developer.nvidia.com/blog/rtx-best-practices/) + [updated](https://developer.nvidia.com/blog/best-practices-for-using-nvidia-rtx-ray-tracing-updated/), [AMD RDNA performance guide](https://gpuopen.com/learn/rdna-performance-guide/) | Vendor guidance |
| | Local `T:\Projects\DX12\Samples\DirectX-Graphics-Samples\Samples\Desktop\D3D12Raytracing\src\` (HelloWorld, SimpleLighting, ProceduralGeometry, LibrarySubobjects, MiniEngine, RealTimeDenoisedAmbientOcclusion, HelloShaderExecutionReordering, SakuraForestSER, OpacityMicromaps) – **none uses inline RayQuery** | AS code, SER/OMM APIs, RTAO denoiser |
| | Wyman DXR tutors [cwyman.org/code/dxrTutors](http://cwyman.org/code/dxrTutors/dxr_tutors.md.html) [U]; Will Usher SBT post [U] + [ChameleonRT](https://github.com/Twinklebear/ChameleonRT) [U]; [NVIDIA DXR tutorial](https://developer.nvidia.com/rtx/raytracing/dxr/dx12-raytracing-tutorial-part-1) [U] | DispatchRays background (M11) |
| | [Indiana Jones PT optimizations](https://developer.nvidia.com/blog/path-tracing-optimizations-in-indiana-jones-opacity-micromaps-and-compaction-of-dynamic-blass), [DOOM TDA Q&A](https://developer.nvidia.com/blog/how-id-software-used-neural-rendering-and-path-tracing-in-doom-the-dark-ages) | Industry practice ("start with general ray tracing before path tracing") |
| Spectral | [HWSS PDF](https://jo.dreggn.org/home/2014_herowavelength.pdf), [Jakob & Hanika](https://rgl.epfl.ch/publications/Jakob2019Spectral) + [rgb2spec](https://github.com/mitsuba-renderer/rgb2spec), [Wyman 2013](https://jcgt.org/published/0002/02/01/), [Moments in Graphics 1](https://momentsingraphics.de/SpectralRendering1Spectra.html)/[2](https://momentsingraphics.de/SpectralRendering2Rendering.html), [Peters 2019](https://momentsingraphics.de/Siggraph2019.html), [Mitsuba 3 variants](https://mitsuba.readthedocs.io/en/stable/src/key_topics/variants.html), [RTG II ch. 42](https://link.springer.com/content/pdf/10.1007/978-1-4842-7185-8_42.pdf), [spectral-restir](https://github.com/nathencoelhoo/spectral-restir) | |
| Ocean optics | [Tessendorf 2004](https://jtessen.people.clemson.edu/reports/papers_files/coursenotes2004.pdf), [Bruneton 2010](https://hal.inria.fr/inria-00443630) [U], [Walter 2007](https://www.cs.cornell.edu/~srm/publications/EGSR07-btdf.pdf) [U], [OMLC water](https://omlc.org/spectra/water/abs/), [Ocean Optics Book water](https://www.oceanopticsbook.info/view/optical-constituents-of-the-ocean/water), [rho n_water](https://rdrr.io/github/AlexCast/rho/man/n_water.html), [RTG II ch. 30](https://developer.download.nvidia.com/ray-tracing-gems/rtg2-chapter30-preprint.pdf), Horvath 2015 JONSWAP/TMA [U] | |
| GPU compute | [PMPP 4th ed.](https://shop.elsevier.com/books/programming-massively-parallel-processors/hwu/978-0-323-91231-0) (ch. 1–6, then reduction/scan), [CUDA guide interop](https://docs.nvidia.com/cuda/cuda-programming-guide/04-special-topics/graphics-interop.html), [CUDA runtime interop](https://docs.nvidia.com/cuda/cuda-runtime-api/group__CUDART__EXTRES__INTEROP.html) | Model + interop |
| | [HIP language extensions](https://rocm.docs.amd.com/projects/HIP/en/latest/how-to/hip_cpp_language_extensions.html), [porting guide](https://rocm.docs.amd.com/projects/HIP/en/latest/how-to/hip_porting_guide.html), [hardware implementation](https://rocm.docs.amd.com/projects/HIP/en/latest/understand/hardware_implementation.html), [HIP interop how-to](https://rocm.docs.amd.com/projects/HIP/en/latest/how-to/hip_runtime_api/external_interop.html), [rocm-examples](https://github.com/ROCm/rocm-examples), [HIPIFY tables](https://github.com/ROCm/HIPIFY/blob/amd-staging/docs/reference/tables/CUDA_Runtime_API_functions_supported_by_HIP.md) | HIP specifics |
| | [Orochi](https://github.com/GPUOpen-LibrariesAndSDKs/Orochi), [cuda-samples simpleD3D12](https://github.com/NVIDIA/cuda-samples/tree/master/cpp/5_Domain_Specific/simpleD3D12), [Falcor CudaUtils](https://raw.githubusercontent.com/NVIDIAGameWorks/Falcor/master/Source/Falcor/Utils/CudaUtils.cpp), [Blender HIP compat.h](https://github.com/blender/blender/blob/main/intern/cycles/kernel/device/hip/compat.h) | Code patterns |
| | [Nsight Systems](https://docs.nvidia.com/nsight-systems/ReleaseNotes/), [Nsight Graphics](https://docs.nvidia.com/nsight-graphics/ReleaseNotes/index.html), [RGP manual](https://gpuopen.com/manuals/rgp_manual/), [ROCgdb Windows](https://rocm.docs.amd.com/projects/install-on-windows/en/latest/how-to/debugger-windows.html) | Tools |
| Denoising | [OIDN](https://www.openimagedenoise.org/documentation.html), [optix-dev](https://github.com/NVIDIA/optix-dev), [optix-sdk optixDenoiser sample](https://github.com/NVIDIA/optix-sdk/tree/main/SDK/optixDenoiser), [Falcor OptixDenoiser](https://github.com/NVIDIAGameWorks/Falcor/tree/master/Source/RenderPasses/OptixDenoiser), [NRD](https://github.com/NVIDIA-RTX/NRD), [FLIP](https://github.com/NVlabs/flip) | |
| NRC / ML | 3Blue1Brown neural networks [U], [Karpathy Zero to Hero / micrograd](https://karpathy.ai/zero-to-hero.html) [U], [Adam](https://arxiv.org/abs/1412.6980) [U], [Fourier features (Tancik)](https://arxiv.org/abs/2006.10739) [U], [Neural Importance Sampling](https://arxiv.org/abs/1808.03856) [U], [mixed precision](https://arxiv.org/abs/1710.03740) [U] | Foundations before CUDA MLPs |
| | [NRC paper](https://arxiv.org/html/2106.12372v2), [Instant NGP](https://arxiv.org/abs/2201.05989), [tiny-cuda-nn](https://github.com/NVlabs/tiny-cuda-nn), [SHaRC guide](https://raw.githubusercontent.com/NVIDIA-RTX/RTXGI/main/Docs/SharcGuide.md), [NRC guide](https://raw.githubusercontent.com/NVIDIA-RTX/RTXGI/main/Docs/NrcGuide.md) | The algorithm + SDKs |
| | [GPUOpen RDNA 4 matrix cores](https://gpuopen.com/learn/using_matrix_core_amd_rdna4/), [WMMA guide 1](https://gpuopen.com/learn/wmma-guide-amd-rdna-4-gpus-part-1/)/[2](https://gpuopen.com/learn/wmma-guide-amd-rdna-4-gpus-part-2/)/[3](https://gpuopen.com/learn/wmma-guide-amd-rdna-4-gpus-part-3/), [rocWMMA](https://github.com/ROCm/rocWMMA), [MiniDXNN](https://gpuopen.com/learn/minidxnn-mlp-library-for-dx12/) | Matrix cores |
| | [D3D12 LinAlg preview](https://devblogs.microsoft.com/directx/d3d12-linalg-preview/), [RTXNS SimpleTraining](https://github.com/NVIDIA-RTX/RTXNS/blob/main/docs/SimpleTraining.md), [Slang autodiff tutorial 2](https://shader-slanggithubio-aidanfnv-wip.readthedocs.io/en/latest/auto-diff-tutorial-2.html) | In-shader training |

Suggested interleaving (from the report): pbr-book sampling/MIS/microfacets with M3, chapter 4 with M4; PMPP 1–6 +
simpleD3D12/Orochi before M6; Falcor OptixDenoiser + OIDN docs before M7; CPU MLP with hand-written backprop and Adam
before M10b; tiny-cuda-nn "learn an image" as the first NVIDIA ML reference run.

---

## 12. Open questions / unverified facts (verify before relying on them)

**Hardware / drivers**
1. Exact `RaytracingTier` reported by the RX 9070 XT and RTX 3070 on current drivers (1.1 vs 1.2) – log in M0.
2. Whether emulated OMM on RTX 30 is faster or slower than any-hit alpha testing.
3. `sharedMemPerBlock` on gfx1201 (expected 65536) – query at runtime.
4. NVIDIA public-driver support for `D3D12_LINEAR_ALGEBRA_TIER_1`; Ampere linalg support/tier; RDNA 4 training in linalg.
5. Whether SM 6.10 actually ships in Agility 1.621 in early December 2026.
6. NRC SDK driver floor "617.42" (no primary source; use R620+).
7. CUDA context VRAM overhead on WDDM; real BVH sizes of EV scenes; OptiX denoiser VRAM (log `OptixDenoiserSizes`).

**Toolchain / build**
8. HIP-VS plugin support for VS 2026 (only 2017–2022 documented); default HIP SDK 7.2 install path.
9. Which DXC ships with the installed Windows SDK / is used by VS 2026 FxCompile; asset lists of 2026 DXC zips.
10. Internal folder of the Agility 1.620.2 nupkg (`build/native/bin/x64`?).
11. Where `D3D10Warp.dll` must be placed for the NuGet WARP; whether Agility is required with it.
12. Whether the official `oidn-2.5.1.x64.windows.zip` contains the HIP device DLL.
13. tiny-cuda-nn with CUDA 13.x + VS 2026 (no reports either way); FullyFusedMLP on sm_86 confirmed only by arithmetic.
14. Whether consumer Adrenalin ships `hiprtc*.dll` (only matters with runtime compilation).
15. ROCm Core SDK 10.1 version numbering (HIP 7.16 inside "10.1"); 7.2.x point-release list.

**APIs**
16. RayQuery candidate/committed accessor names (from training knowledge of the spec) – check `dxc` intrinsics.
17. Enhanced-barrier enum names for AS build/read.
18. `XMStoreFloat3x4` transposing for DXR instance transforms.
19. GPU-based validation coverage of inline RayQuery and AS.
20. SER API names (`dx::HitObject`, `MaybeReorderThread`) and the RayQuery → HitObject path.
21. OIDN filter-setter names and the 16-byte-pixel-stride FLOAT3 trick; OIDN normal-space expectations.
22. HIP device-side behaviour of `amdhip64_7.dll` vs the open `clr` source (closed runtime); D3D12 ↔ HIP minimum driver.
23. OptiX in a CUDA-in-Graphics (CIG) context; CIG support on Ampere.
24. OptiX 9.1 Programming Guide wording on normals (site failed TLS) – header + staff statement used instead.

**Theory / data**
25. The pbrt ↔ HWSS Eq. 11 mapping derivation (inference; double-check before M5).
26. Slope-variance → GGX α mapping for Bruneton-style sub-pixel roughness.
27. Walter 2007 BTDF formula as written in 3.7 (training knowledge) – check vs pbrt-v4 `DielectricBxDF`.
28. NRC Adam learning rate / β values; Instant-NGP hash-grid parameters and primes; Instant-NGP optimizer details.
29. Whether `sky4k.hdr` contains a sun (double counting); direction convention of `pano_to_cubemap_CS.hlsl`.
30. Whether EV's raster uses α = roughness²; EV point-light attenuation terms (for raster-vs-PT tests).
31. Morel 1974 original table/exponent (secondary source); Segelstein vs Smith & Baker above 700 nm (pick one).
32. Macbeth spectral reflectance data source (BabelColor [U]) for a true spectral chart.
33. Mitsuba Python API calls and `.spd` file format (from memory).
34. Hardware trilinear filtering precision effect on Jakob–Hanika coefficients.

**Measurements nobody has published (do them)**
35. Ocean BLAS refit/build timings on RX 9070 XT and RTX 3070 (estimates 0.5/5 ms).
36. D3D12 ↔ HIP/CUDA fence round-trip cost; texture↔buffer pack cost at 1080p/1440p.
37. OIDN and OptiX denoise times at 1080p/1440p on both GPUs.
38. RayQuery vs DispatchRays on RDNA 4 / Ampere for this tracer.
39. Spectral overhead in EV (Peters' figures are on an RTX 5070 Ti).

---

## 13. Source index

All files in `research/` are dated 2026-11-18. Follow-ups supersede older notes where they conflict; `plan.md`
supersedes the report.

| File | Covers | Used in § |
|---|---|---|
| `00_report_2026-11-18.md` | Synthesis: industry survey, theory primer, options for D1–D18 (original recommendations: OptiX first, own NRC via tiny-cuda-nn, unknown RTX model – **superseded by plan.md**) | 0.3, 1, 10 |
| `industry_realtime_path_tracing.md` | Shipped PT titles, light transport (ReSTIR, SHaRC, NRC), denoisers (DLSS-RR, NRD, FSR RR), DXR 1.2/SER/OMM state, AS practice, materials, hybrid vs reference | 6.4, 8.4, 9.1 |
| `offline_and_spectral_rendering.md` | Production renderers' spectral status, HWSS basics, pbrt `SampledWavelengths`, Jakob–Hanika, three spectrum kinds, Peters timings, open-source renderers | 4 |
| `optix_cuda_interop.md` | OptiX 9.1 denoiser API/models, CUDA ↔ D3D12 interop, Falcor/vk_denoise patterns, OIDN/NRD/DLSS-RR/FSR alternatives, CUDA 13.5 learning | 7.5, 8 |
| `neural_radiance_cache.md` | NRC paper details, tiny-cuda-nn, NRC SDK + SHaRC, HLSL linalg/coop-vec/RTXNS/Slang, spectral NRC options, production use, ML learning path | 9 |
| `ocean_path_tracing.md` | Ocean in DXR (refit/rebuild, CLAS), water optics (Pope & Fry, Morel, Quan & Fry), spectral water argument, caustics, hybrid/denoising, references | 5, 6.5 |
| `ev_engine_architecture.md` | Full EV read on `render-graph` (device, queues, lists, tracker, descriptors, render graph, scene, ocean, editor, conventions, risks) – **line numbers for changed files superseded by the branch note** | 2 |
| `ev_engine_architecture_path_tracing_branch.md` | Corrections for `path-tracing` (line shifts), `Mesh::Source`, scene files, play mode, callbacks, hook points, Agility export locations, new pitfalls (FOV reset, RNG seed, ocean time) | 2 |
| `followup_amd_hip_compute.md` | HIP SDK 7.2 vs ROCm 10.1, build patterns, **HIP ↔ D3D12 interop works on Windows** (Orochi/OIDN/rocDecode), Orochi, Blender, portability header, RDNA 4 WMMA, OIDN 2.5 on AMD, ROCgdb/RGP, learning | 1, 7, 8.2 |
| `followup_rtx3070_optix.md` | OptiX on Ampere (timing estimates), **world-space normals**, tiny-cuda-nn on sm_86, NRC SDK on Ampere (617.42 unverified), SER/OMM on RTX 30, CUDA 13.5 flags, simpleD3D12 sequence (no GPU Wait), 8 GB VRAM budget | 1, 7.6, 8.3, 9.5 |
| `followup_spectral_details.md` | HWSS equations from the full paper, pbrt VolPath rules + simple water scheme, **rgb2spec λ in nm**, table format/lookup, CMF/D65 data, Wyman coefficients, Planck, water data (Pope & Fry to 727.5 nm, Segelstein, Morel phase + sampling, Quan & Fry 10 coefficients), Peters code, test ladder | 4, 5 |
| `followup_dx12_dxr_practical.md` | Agility 1.620.2 integration + static-lib pitfall, DXC versions, RayQuery API, driver floors, vendor RT guidance, AS alignment/barriers, ocean BLAS recipe, **WARP DXR since 1.0.8**, tools, DRED ops, TDR keys, SM 6.10 status, local samples | 1, 6 |

