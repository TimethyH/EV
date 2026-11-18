# EV engine: architecture analysis for adding a path tracer

Repo: `T:\Projects\DX12\EV`, branch `render-graph` (HEAD `e738ba0`, clean, in sync with `origin/render-graph`).
Nothing in the repo was modified. All paths below are relative to `T:\Projects\DX12\EV` unless absolute.
Line numbers refer to the `render-graph` branch.

Short version:
- The engine started from Jeremiah van Oosten's "LearningDirectX12" tutorial (3dgep): `Application`, `CommandQueue`,
  `CommandList`, `ResourceStateTracker`, `DynamicDescriptorHeap`, `DescriptorAllocator`, `UploadBuffer`, `Scene`/`Mesh`/`Material`,
  `EffectPSO`, `SwapChain` and the `GUI` all keep his license headers. Later work (editor, hot reload, render graph, crash
  reporting) was added on top in a different, heavily commented style.
- There is **nothing DXR-related yet**: no raytracing feature query, no `ID3D12GraphicsCommandList4`, no state objects, no
  acceleration structures, no ray queries, no shared heaps or fences. The vendored `d3dx12` headers include
  `d3dx12_state_object.h` (`CD3DX12_STATE_OBJECT_DESC`) and `d3dx12_check_feature_support.h` (`CD3DX12FeatureSupport`), which help.
- The device is created as `ID3D12Device13` (enough for DXR, but it fails on older Windows; the `Dads-Birthday` branch already
  switched to `ID3D12Device2` for that reason, which is *not* enough for DXR). Command lists are `ID3D12GraphicsCommandList2`
  (DXR needs `...List4`).
- Descriptors are **not bindless**: every command list copies staged CPU descriptors into its own small (1024 entries)
  shader-visible heap and may switch heaps mid-list. This is the biggest structural obstacle for hit-shader material access.
- The render graph is a good place to hang a path tracer: passes are lambdas, resources (textures and buffers) are declared
  with accesses, barriers are derived. It needs new access kinds (acceleration structure build/read), a ray tracing pass
  label, and a way to split the frame across submissions for CUDA.
- The ocean surface exists only as a **flat 513x513-vertex grid** that the vertex shader displaces. To trace it, a compute pass
  must write displaced positions into a buffer and a BLAS must be refit from it every frame. The ocean is animated every frame,
  so progressive accumulation never converges unless ocean time can be frozen.

---

## 1. Project and build layout

### Solution and projects
- Solution: `EV-Engine/EV-Engine.slnx` (new XML solution format, VS 2026). Lines 6-12: three projects, both apps depend on the engine.
  - `EV-Engine/EV-Engine/EV-Engine.vcxproj`: **static library** (`ConfigurationType StaticLibrary`, lines 167, 174, 181, 188),
    output to `EV-Engine/EV-Engine/lib/debug` or `lib/release` (`OutDir`, lines 213, 218, 224, 230).
  - `EV-Engine/OceanRenderer/OceanRenderer.vcxproj`: application, links `EV-Engine.lib` (line 96/116/134/154).
  - `EV-Engine/ShaderArt/ShaderArt.vcxproj`: application (full screen pixel shader landscape, `ShaderArt/source/landscape.cpp`,
    `landscape_pso.cpp`). No editor (only a "Shader Errors" window), not on the render graph yet (`docs/render-graph.md:139`).
  - On `Dads-Birthday` a fourth project `EV-Engine/DadsBirthday/DadsBirthday.vcxproj` exists (a game built on OceanRenderer).
- No CMake for the engine itself (CMake is only used in CI to build assimp).
- Toolset `v145` (VS 2026), `WindowsTargetPlatformVersion 10.0` (= newest installed Windows SDK), Unicode, x64 and Win32 configs
  (Win32 is unused in practice).
- **C++20** (`<LanguageStandard>stdcpp20</LanguageStandard>`, EV-Engine.vcxproj 238/259/278/300; OceanRenderer.vcxproj 91/111/129/149).
  `ThrowIfFailed` uses `std::source_location` (C++20). Warning level 3, `SDLCheck`, `ConformanceMode`.
- Include paths (EV-Engine.vcxproj:211-227): `EV-Engine/header`, `thirdparty/DirectXTex`, `thirdparty/imgui`, `thirdparty/imguizmo`,
  `thirdparty/assimp/include`. `d3dx12*.h` live directly in `EV-Engine/EV-Engine/thirdparty/`.
- Engine link deps (EV-Engine.vcxproj:244): `d3d12.lib; dxgi.lib; d3dcompiler.lib; dxguid.lib; Shlwapi.lib; assimp-vc145-mtd.lib;
  DirectXTexd.lib; zlibstaticd.lib` (release: non-d variants). `dbghelp.lib` via `#pragma comment` (application.cpp:23).
  Prebuilt libs in `EV-Engine/EV-Engine/lib/` (DirectXTex, zlib committed; assimp `.lib` is gitignored, built locally or in CI).

### Shaders
- Compiled at build time by MSBuild's `FxCompile` task with **Shader Model 6.0** (project default `<FxCompile><ShaderModel>6.0`,
  EV-Engine.vcxproj:247-249 etc.; per-file `ShaderType`/`ShaderModel` in OceanRenderer.vcxproj:178-299). SM 6.x means the
  task runs **DXC** (from the Windows SDK), producing DXIL `.cso` files next to the executable
  (`EV-Engine/x64/Debug/*.cso`, default `$(OutDir)%(Filename).cso`). Entry point is the default `main`.
- OceanRenderer shaders (`EV-Engine/OceanRenderer/shaders/`): `vertex.hlsl`/`pixel.hlsl` (objects, PBR), `ocean_vertex.hlsl`/
  `ocean_pixel.hlsl`, `skybox_VS/PS`, `HDR_to_SDR_VS/PS` (tonemap), compute: `animate_waves`, `fft`, `permute`, `ibl_convolution`,
  `ibl_specular`, `brdf_lut`, `pano_to_cubemap_CS`.
- Engine-internal shaders are **embedded byte arrays**: `EV-Engine/EV-Engine/shaders/GenerateMips_CS.h`, `imGUI_VS.h`, `imGUI_PS.h`.
  The editor's `ModelPreview` and `TexturePreview` compile embedded HLSL at runtime with **FXC** (`D3DCompile`, `vs_5_1`/`ps_5_1`;
  `source/UI/model_preview.cpp:136,220`, `texture_preview.cpp:181,220,252`).
- Coupling to know about: the engine's `PanoToCubemapPSO` loads `pano_to_cubemap_CS.cso` from the executable folder, i.e. a
  shader that the *app* project builds (`source/DX12/pano_cubemap_pso.cpp:57-60`).
- **Consequence for a core-engine path tracer**: a static lib's `FxCompile` output lands in `lib/debug`, not next to the exe, and
  the shader library only searches `<exe name>/shaders` and `shaders` folders next to/above the exe (`shader_library.cpp:24-50`),
  which does not include `EV-Engine/EV-Engine/shaders`. Options: (a) each app project lists the engine's `.hlsl` files,
  (b) compile engine shaders at runtime from source with the existing `ShaderCompiler` (DXC) and register the engine shader
  folder with `ShaderLibrary::AddSourceDirectory`, (c) embed bytecode headers (no hot reload).

### DXC / Agility SDK
- **No Agility SDK**: no `D3D12SDKVersion`/`D3D12SDKPath` exports anywhere (grep for `D3D12SDKVersion`, `Agility`: no hits outside
  third-party). The OS `d3d12.dll` is used. Shader Model 6.6+ features (e.g. `ResourceDescriptorHeap[]` dynamic resources) and
  newer DXR features depend on the OS version without it.
- DXC is **not linked**: `ShaderCompiler` (`header/DX12/shader_compiler.h`, `source/DX12/shader_compiler.cpp`) loads
  `dxcompiler.dll`/`dxil.dll` on first use from the exe folder, the newest Windows SDK (`Windows Kits\10\bin\<ver>\x64`, via registry),
  or the DLL search path; uses `IDxcCompiler3`. Arguments: `-E main -T <profile> -I <dir>` plus `-Zi -Qembed_debug -Od` in Debug
  (shader_compiler.cpp:181-189). `dxcapi.h` comes from the Windows SDK.

### Third-party libraries (`EV-Engine/EV-Engine/thirdparty/`)
- `assimp` (model loading; CI builds v6.0.2), `DirectXTex` (DDS/HDR/TGA/WIC loading), `imgui` (1.93 docking), `imguizmo`,
  `d3dx12` split headers (recent: includes `d3dx12_state_object.h`, `d3dx12_check_feature_support.h`, enhanced barriers guarded by
  `D3D12_SDK_VERSION >= 608`), `entt` (present but **unused**), zlib (prebuilt). `scene-save-load` adds `nlohmann/json` 3.11.3.
- No CUDA, OptiX, NRD, NRC, FidelityFX or PIX runtime.

### CI (`.github/workflows/windows-build.yml`)
- Triggers: push to `imgui-editor`, `main`; PRs; manual (lines 3-7). Runner `windows-2025-vs2026` (line 14), `setup-msbuild` prerelease.
- Builds assimp v6.0.2 with CMake (cached, lines 33-56), then msbuild EV-Engine, OceanRenderer, ShaderArt Debug x64 (lines 58-65).
- Smoke test: runs OceanRenderer on **WARP** (`EV_USE_WARP=1`, line 73) with `sky4k.hdr` from the repo's `Assets` release and the
  glTF DamagedHelmet, screenshots the desktop after 60 s, prints `ev_crash.log` as an error, publishes `editor.png` to the
  `ci-screenshots` branch (lines 67-128). No GPU on the runner.
- Consequence: any DXR/CUDA code must degrade gracefully when the raytracing tier is unsupported (WARP's DXR support depends on
  the WARP version; the in-box one should be assumed unsupported unless queried) and the CUDA/OptiX parts must be optional at
  build time (CI has no CUDA toolkit).

### Branches (read-only survey)
Order documented on `origin/scene-save-load` (commit `2add933`, docs/editor-handoff.md "Branch order"): each branch started from
the previous one.

| Branch | Last commit | Contents relevant here |
|---|---|---|
| `main` | 2026-04-04 `4a25900` "base setup, renders uv to a screensize triangle" | Ocean with FFT cascades, IBL, ShaderArt. Base of the editor branches. |
| `imgui-editor` | 2026-10-18 | CI, docked ImGui editor, crash/DRED reports, WARP fallback, model preview |
| `shader-hot-reload` | 2026-10-19 | `Shader`, `ShaderLibrary`, `ShaderCompiler` (DXC), Shaders panel |
| `render-graph` (current) | 2026-10-23 | `RenderGraph`, `GpuTimer`, Render Graph panel, buffer viewer, OceanRenderer on the graph |
| `origin/scene-save-load` (remote only) | 2026-10-23 | +7: `.evscene` save/load (nlohmann/json), play mode (F5), `Mesh` remembers model file + mesh index (`aac3d33`) |
| `Dads-Birthday` | 2026-10-24 | builds on scene-save-load (+30). **Shadow map** (`4fb3e43`: `ShadowPSO`, `shadow_VS/PS.hlsl`, `EffectPSO::SetShadowMap`, "Shadows" graph pass), distance fog (`0c109cd`), IBL bake fix (`adc90c6`, also described as a TODO in docs/editor-handoff.md:69-115), **`ID3D12Device2` instead of `Device13`** (`5ce7993`, because Device13 fails with E_NOINTERFACE on older Windows), GPU hang fix in the specular IBL prefilter (`0cb371d`). Not a development branch; the handoff says to port only the two IBL shader edits. |
| `atmosphere-scatter` | 2026-10-18 | 1 commit on top of main: start of an atmosphere implementation ("old code check it later"). Not in any editor branch. |
| `LOD-clipmap` | 2026-03-11 | Despite the name: water "LOD" = cascades, UAV barrier fix; only a "removed sln file" commit is not in main. No clipmap geometry. |
| `FFT-compute`, `IBL`, `HDR-skybox`, `cube-render`, `foam-fix`, `barrier-fix`, `texture-format-resize` | Jan-Mar 2026 | Historic feature branches, all merged into `main` (`git branch --merged main`): FFT ocean, IBL, HDR skybox, foam, barrier fix. |
| `bicubic-foam`, `origin/landscape-shader` | Mar-Apr 2026 | **Not** merged into `main`: bicubic foam sampling; ShaderArt noise generation. |
| `origin/ci-screenshots`, `origin/claude/shader-hot-reload-26017u` | | CI screenshot storage / session branch |

The path tracer should probably branch from `render-graph` (or `scene-save-load` if scene files are wanted).

---

## 2. Device and adapter creation (`source/core/application.cpp`)

- `Application::Initialize` (lines 353-439):
  - Crash log rotation `ev_crash.log` -> `ev_crash.prev.log` (357), SIGABRT and unhandled-exception handlers (361-362).
  - **Debug layer** in `_DEBUG` only: `D3D12GetDebugInterface` + `EnableDebugLayer` (364-378). No GPU-based validation.
  - **DRED** auto-breadcrumbs and page faults forced on, in all builds (380-386).
  - **WARP**: `EV_USE_WARP=1` forces WARP; WARP is also the fallback when no hardware adapter is found (388-396).
  - Device-removed fence + thread-pool wait that writes a report as soon as the device is removed (401-407).
  - Three queues: direct, compute, copy (414-416). Tearing support check (418).
  - Feature check: **only** root signature version 1.1 (428-438). **No `D3D12_FEATURE_D3D12_OPTIONS5` (raytracing tier), no
    `OPTIONS7`/shader model/resource binding tier query.**
- `CreateAdapter(bool useWarp)` (441-482): `CreateDXGIFactory2` (debug flag in Debug), `EnumAdapters1`, picks the
  **non-software adapter with the most dedicated video memory** that can create a device at FL 11_0. Returns `IDXGIAdapter4`.
  No `EnumAdapterByGpuPreference`, no LUID kept (needed later to match the CUDA device: compare `DXGI_ADAPTER_DESC1::AdapterLuid`
  with `cudaDeviceProp::luid`).
- `CreateDevice` (501-562): `D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&ID3D12Device13))` (504).
  - **Interface: `ID3D12Device13`** (`application.h:66,195,210`). That is >= `ID3D12Device5`, so `CreateStateObject`,
    `GetRaytracingAccelerationStructurePrebuildInfo` are available, **but** creating Device13 fails on older Windows
    (see `Dads-Birthday` `5ce7993`, which downgraded to `ID3D12Device2`). Recommended: create a base device, then
    `As(&device5)` optionally and gate DXR on it plus `OPTIONS5.RaytracingTier >= 1_1`.
  - Feature level 11_0 is requested. DXR does not need a higher FL, only the raytracing tier.
  - Debug info queue (509-558): **breaks on CORRUPTION, ERROR and WARNING** (514-516), except device-removal messages; denies
    INFO and three IDs; registers `OnDebugLayerMessage` (`ID3D12InfoQueue1`) that writes every warning/error to `ev_crash.log`
    (844-851, 552-557). Any DXR debug-layer warning will stop the process in Debug.
- `GetDeviceRemovedReport` (1308-1370) prints DRED breadcrumbs; `GetBreadcrumbOpName` (1265-1290) does not know
  `DISPATCHRAYS`, `BUILDRAYTRACINGACCELERATIONSTRUCTURE`, `COPYRAYTRACINGACCELERATIONSTRUCTURE`, `SETPIPELINESTATE1`, etc.
  (they print as "Other"); worth extending.
- Frame loop: `Application::Run` pumps messages (660-691); `WndProc` `WM_PAINT` calls `ShaderLibrary::Update()` then
  `Window::OnUpdate` -> `Game::OnUpdate` (988-1000). The app's `OnUpdate` calls `OnRender` itself (ocean_scene.cpp:622).
- Error handling: `SafeWndProc` catches exceptions from the window procedure and calls `ReportFatalError` (message box + log,
  `__debugbreak` under a debugger, no message box when `CI` is set) (854-887); SEH filter prints a symbolized stack (929-947).

**DXR search**: grep over `EV-Engine/` (excluding thirdparty) for `Raytracing|DXR|RayQuery|AccelerationStructure|BLAS|TLAS|OPTIONS5|
StateObject|DispatchRays|HEAP_FLAG_SHARED|CreateSharedHandle|D3D12SDKVersion` finds **nothing** except the word "PipelineStateObject"
(the engine's PSO wrapper class). The editor's `Editor::RayCastScenes` (editor.h:296) is a CPU AABB ray cast for picking.

---

## 3. Core abstractions and what DXR/CUDA would need

### Application (`header/core/application.h`, `source/core/application.cpp`)
Singleton (`Application::Create/Get/Destroy`). Owns adapter, device, the three `CommandQueue`s, a `DescriptorAllocator` per heap
type, the `ShaderLibrary`, the live-texture list for the editor. Factory methods (all wrap objects in `std::shared_ptr` via
private `Make*` subclasses at the top of application.cpp, lines 52-261):
`CreateTexture` (1231/1240), `CreateBuffer(D3D12_RESOURCE_DESC)` (1397; default heap, added for the render graph),
`CreateVertexBuffer`/`CreateIndexBuffer`, `CreateRootSignature(D3D12_ROOT_SIGNATURE_DESC1)` (158),
`CreatePipelineStateObject<Stream>` (header 150-157 -> `DoCreatePipelineStateObject` 596), `CreateShaderResourceView`/
`CreateUnorderedAccessView` (165-181), `CreateSwapchain` (489), `CreateGUI` (1402), `LoadShader` (586),
`AllocateDescriptors` (1195), `CreateDescriptorHeap` (731-743, always `FLAG_NONE` = CPU only).
Needed additions: `CreateStateObject`/`RaytracingPipeline` wrapper, `CreateAccelerationStructureBuffer`, shared-heap resource and
shared fence creation, a "capabilities" struct (raytracing tier, SM, binding tier, LUID).

### Window / Game / SwapChain
- `Window` (`header/core/window.h`, `source/DX12/window.cpp`) delivers events to the `Game` (`header/core/game.h`). `Window::OnResize`
  works again since the editor branch.
- `SwapChain` (`header/DX12/swapchain.h`): 3 back buffers (line 52), waitable object, `Present` executes a transition list,
  presents, signals, **waits for the next back buffer's fence** and then releases stale CPU descriptors (`swapchain.cpp:175-189`).
  So up to ~3 frames in flight. OceanRenderer creates it with `DXGI_FORMAT_R8G8B8A8_UNORM` (ocean_scene.cpp:326).

### CommandQueue (`header/DX12/command_queue.h`, `source/DX12/command_queue.cpp`)
- One `ID3D12CommandQueue` + one `ID3D12Fence` (`D3D12_FENCE_FLAG_NONE`, line 27) per queue type; a background thread resets
  finished command lists and returns them to a pool (189-214).
- `ExecuteCommandLists` (115-177): locks the global state tracker, closes each list and builds a "pending barrier" list from global
  states, executes both, signals. If a list generated mips, those compute lists run on the **compute queue after a GPU
  `Wait` on this queue** (169-174) — but nothing makes the direct queue wait for that compute work (callers CPU-wait instead).
- `Wait(const CommandQueue& other)` (179-182) waits on another EV queue's fence only. `GetCommandQueue()` exposes the raw queue.
- For CUDA: need a separately created **shared fence** (`D3D12_FENCE_FLAG_SHARED` + `ID3D12Device::CreateSharedHandle`, imported in
  CUDA as `cudaExternalSemaphoreHandleTypeD3D12Fence`), and `Signal(fence, value)`/`Wait(fence, value)` helpers on `CommandQueue`.

### CommandList (`header/DX12/command_list.h`, `source/DX12/command_list.cpp`)
- Wraps **`ID3D12GraphicsCommandList2`** (`command_list.h:93,511`; created at `command_list.cpp:47`). DXR needs
  `ID3D12GraphicsCommandList4` (`BuildRaytracingAccelerationStructure`, `SetPipelineState1`, `DispatchRays`); ray queries need
  nothing new on the CPU side. Simplest: create the list as `List4` (or newer) when the device supports it, or `As()` on demand.
- Each list owns: an `UploadBuffer` (2 MB pages, `upload_buffer.h:22`, allocations larger than a page throw, `upload_buffer.cpp:22-24`),
  a `ResourceStateTracker`, one `DynamicDescriptorHeap` per heap type, a `GenerateMipsPSO`/`PanoToCubemapPSO`, a tracked-object list
  that keeps D3D objects alive until the list is reset (`TrackResource(ComPtr<ID3D12Object>)`, 1623; works for state objects too).
- API (all name the root parameter index): `TransitionBarrier` (71-94), `UAVBarrier()`/`UAVBarrier(resource)` (96-123),
  `AliasingBarrier`, `FlushResourceBarriers`, `CopyResource`, `CopyVertexBuffer`/`CopyIndexBuffer` (via private `CopyBuffer`
  202-264: committed default-heap buffer in COMMON + per-call upload resource), `LoadTextureFromFile` (303-409, weak-ptr cache),
  `GenerateMips`, `PanoToCubemap`, `CopyTextureSubresource`, `SetGraphicsDynamicConstantBuffer` (upload heap CBV), `Set*32BitConstants`,
  `SetVertexBuffers`, `SetDynamic*Buffer`, `SetGraphicsDynamicStructuredBuffer`, `SetShaderResourceView` (texture/SRV/inline buffer),
  `SetUnorderedAccessView` (texture mip/UAV/inline buffer), `SetRenderTarget`, `Draw`/`DrawIndexed`/`Dispatch` (1518-1552; flush
  barriers then commit staged descriptors), `Get/RestoreResourceState` (editor), `CreateCube/Sphere/Plane`, `LoadSceneFromFile`.
- Caching gotchas for DXR:
  - `SetPipelineState` caches the last `ID3D12PipelineState*` (1261-1274). After `SetPipelineState1(stateObject)` the cache would be
    stale and a later `SetPipelineState(samePSO)` would be skipped. A `SetRaytracingPipeline`/`DispatchRays` wrapper must reset
    `m_pipelineState`.
  - One `m_rootSignature` cache is shared between graphics and compute (1276-1312). The DXR global root signature is set with
    `SetComputeRootSignature`, which is fine and even lets `DispatchRays` reuse `CommitStagedDescriptorsForDispatch` (the compute
    root setters) for the global root signature.
- There is no `ByteAddressBuffer`/`StructuredBuffer` class (the methods are commented out, 287-296). Buffers are `Buffer` (abstract
  base), `VertexBuffer`, `IndexBuffer`.

### ResourceStateTracker (`header/DX12/resource_state_tracker.h`, `source/DX12/resource_state_tracker.cpp`)
- Per-list known states + **static global state map** keyed by `ID3D12Resource*` (header 222-235). First use in a list becomes a
  pending barrier resolved at submit (cpp 90-95). Transitions of the same subresource in one batch are merged (24-47);
  never merged across UAV/aliasing barriers.
- Resources must be registered with `AddGlobalResourceState` at creation (`Resource` ctor does this with COMMON, `resource.cpp:109`).
- **Acceleration structures**: they must be created in `D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE` and are never
  transitioned; only UAV barriers order build -> build/trace. Keep AS buffers out of the transition path (do not call
  `TransitionBarrier` on them; register them with that state or not at all) and give the render graph an access kind that only
  emits UAV barriers. `CommandList::UAVBarrier(resource)` already exists.
- Scratch buffers: normal UAV buffers (COMMON -> UNORDERED_ACCESS through the tracker) — fine with `Application::CreateBuffer`.
- BLAS inputs (vertex/index buffers) must be in `NON_PIXEL_SHADER_RESOURCE` during the build; raster draws move them to
  `VERTEX_AND_CONSTANT_BUFFER` / `INDEX_BUFFER` every frame (command_list.cpp:1168, 1202). That works (the tracker transitions) but
  ping-pongs states; a combined read state could be used.
- CUDA-shared resources should be in `COMMON` when handed to CUDA; CUDA writes do not update the tracker.

### Descriptors (the biggest obstacle)
- CPU-only heaps: `DescriptorAllocator` per type, pages of **256** descriptors (`descriptor_allocator.h:20`, grows for larger requests),
  free-list allocation (`descriptor_allocator_page.*`). Stale descriptors are freed in `ReleaseStaleDescriptors` after
  `SwapChain::Present` (swapchain.cpp:189). `Application::m_frameCount` is **never incremented** (the `++` is commented out at
  application.cpp:993), so the frame number recorded with stale descriptors is always 0 — harmless today because only CPU descriptors
  are freed, but a persistent GPU-visible (bindless) heap would need real frame-based deferral.
- GPU-visible: `DynamicDescriptorHeap` (`header/DX12/dynamic_descriptor_heap.h`, 1024 descriptors per heap by default, line 23).
  `SetShaderResourceView`/`SetUnorderedAccessView` **stage** CPU handles into a per-root-parameter cache; `Draw`/`Dispatch` copy
  stale tables into the current shader-visible heap with `CopyDescriptors` and set root tables (cpp 148-204, 224-244). When a heap is
  full, a new one is requested and `SetDescriptorHeaps` is called mid-list (162-175). Heaps are pooled per command list and recycled
  on `Reset`.
  - `ParseRootSignature` asserts that the sum of all table sizes fits in one heap (cpp 69). Unbounded ranges (`UINT_MAX`) are
    summed literally in `RootSignature::SetRootSignatureDesc` (`root_signature.cpp:105-108`), so **unbounded/bindless tables
    are not supported**.
  - Only one CBV/SRV/UAV heap can be bound at a time, so a separate persistent bindless heap would conflict with the dynamic heaps.
- Consequences for path tracing:
  - **Global root signature only** (no local root signatures/descriptors in the shader table) fits the existing staging model:
    stage a fixed-size SRV table of all scene textures and buffers per frame and commit it for `DispatchRays` with the compute setters.
    Works up to ~1000 descriptors per list (bump `numDescriptorsPerHeap` if needed). Sponza has ~70 textures; helmet 5.
  - True bindless (SM 6.6 `ResourceDescriptorHeap[]` or a big unbounded table) needs a **single large persistent shader-visible heap**
    with a static region (scene textures/buffers) and a per-frame ring region that replaces the dynamic heaps (all PSOs would then use
    it). That is a core refactor; OS/Agility support for SM 6.6 must be checked.
  - Local root arguments in the shader binding table (GPU descriptor handles) are awkward because handles are only known after the
    per-list copy and the heap can change mid-list.

### Root signatures (`header/DX12/root_signature.h`, `source/DX12/root_signature.cpp`)
- Deep-copies the `D3D12_ROOT_SIGNATURE_DESC1`, counts descriptors per table and builds bit masks for the dynamic heaps, serializes
  with `D3DX12SerializeVersionedRootSignature` at the highest supported version (131-147).
- Every PSO builds its own root signature in its constructor with fixed register slots (e.g. `EffectPSO`: one table of 12 SRVs at
  t3, CBVs b0/b0 space1/b1, root SRVs t0/t2, static samplers s0/s1; `effect_pso.cpp:33-70`; `OceanPSO`: table of 15 SRVs at t3, CBVs
  b0-b3, three static samplers; `OceanRenderer/source/ocean_pso.cpp:29-77`). OceanRenderer's compute PSOs get their root parameters
  from `Ocean::LoadContent` (ocean_scene.cpp:396-433).
- A local root signature only needs the `D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE` flag and works with this class.
  Root signatures are not rebuilt on hot reload (docs/editor-handoff.md:64).

### PSO classes
- `PipelineStateObject` (`header/DX12/pipeline_state_object.h`): wraps `ID3D12PipelineState` created from a
  `D3D12_PIPELINE_STATE_STREAM_DESC` (`ID3D12Device2::CreatePipelineState`).
- `BasePSO` (`header/DX12/base_pso.h`): abstract effect with `SetMaterial`, `SetView/Projection/WorldMatrix`, `Apply(CommandList&)`,
  `LoadShaders(vs, ps)`, `CreatePipelineState()` (pure virtual), `UpdatePipelineState()` (hot reload hook, `base_pso.cpp:17-20`),
  dirty flags, aligned MVP. Used by `SceneVisitor`.
- Concrete: `EffectPSO` (lit PBR objects; lights in root SRVs, material CB, 12-slot texture table incl. IBL;
  `effect_pso.cpp:158-248`), `SkyboxPSO`, `SDRPSO` (tonemap), `OceanPSO` (app), `LandscapePSO` (ShaderArt), engine-internal
  `GenerateMipsPSO`, `PanoToCubemapPSO`; GUI/preview PSOs.
- Compute PSOs in OceanRenderer: `OceanCompute` (`OceanRenderer/include/compute_pso.h`; FFT, spectrum, permute) and
  `ConvolutionCompute` (IBL). Pattern: `Shader` + `PipelineShaders` member, root params passed in, `SetPipelineState` rebuilds after
  reload (`compute_pso.cpp:13-56`). There is no generic engine-level compute PSO class.
- **Ray tracing PSO** would be a new class wrapping `ID3D12StateObject` + `ID3D12StateObjectProperties` (shader identifiers) +
  shader table buffers, built with `CD3DX12_STATE_OBJECT_DESC`.

### Resources (`header/resources/*.h`)
- `Resource` (`resource.h`): `ComPtr<ID3D12Resource>`, optional clear value, name, format support. The descriptor constructor always
  does `CreateCommittedResource(DEFAULT heap, D3D12_HEAP_FLAG_NONE, desc, COMMON)` and registers COMMON with the tracker
  (`resource.cpp:94-112`). There is no way to pass heap type, heap flags (`D3D12_HEAP_FLAG_SHARED`) or an initial state; the
  `ComPtr` constructor (114-123) can wrap anything created elsewhere (the caller must then register the state).
- `Buffer` (abstract), `VertexBuffer` (view, count, stride), `IndexBuffer` (view, count, format). No SRV/UAV views on buffers;
  `ShaderResourceView` can be created for any resource with a desc (`Application::CreateShaderResourceView`). A TLAS SRV
  (`D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE`) needs `pResource == nullptr`; `ShaderResourceView` accepts a null
  resource when a desc is given (`shader_resource_view.cpp:16-23`), and `CommandList::SetShaderResourceView(srv)` then skips the
  transition (command_list.cpp:1320-1336) — so binding a TLAS through a descriptor table works with existing code.
  Alternatively a root SRV (inline GPU VA) via `SetShaderResourceView(rootIndex, buffer, state, offset)` would try to transition
  the buffer, which must not happen for an AS.
- `Texture` (`texture.h`): creates RTV/DSV/SRV/UAV-per-mip descriptors in `CreateViews` (`texture.cpp:242-360`), `Resize`
  recreates the resource (used for the viewport). Typeless depth handling for sampling depth.
- `UploadBuffer`: linear allocator over 2 MB upload pages, reset when the list is reset. Usable for per-frame TLAS instance
  descs (64 B each, 16 B alignment) and shader tables (64 B table alignment, 32 B record alignment) as long as each fits in 2 MB.

### Shader library and hot reload (`header/DX12/shader.h`, `shader_library.h`, `shader_compiler.h`)
- `Application::LoadShader(L"/x.cso")` -> `ShaderLibrary::Load` reads the `.cso` and finds `x.hlsl` (`shader_library.cpp:54-100`).
  Sources + includes are polled 4x/s (`g_checkInterval` 250 ms, line 15) from `WM_PAINT`.
- Recompile uses the profile read from the `.cso` (`ReadShaderTarget`, `shader.cpp:26-90`) and entry point **`"main"`**
  (`shader_library.cpp:18`). `ReadShaderTarget` knows vs/ps/gs/hs/ds/cs/ms/as (`shader.cpp:65-76`) but **not libraries
  (`lib_6_x`, program type 6)**, so a DXR library would load but `CanReload()` is false.
- `PipelineShaders::Update(callback)` (`shader.cpp:203-234`) calls any rebuild callback when a shader version changed, with the
  debug layer's break disabled and messages captured; failures keep the old pipeline. This is generic enough to rebuild a
  **state object + shader table** from a recompiled library, once `ReadShaderTarget` returns `lib_6_3+` and the compiler skips/accepts
  the entry point for libraries.
- For ray queries in compute (`cs_6_5`), hot reload works as is (profile is read from the `.cso`; build with ShaderModel 6.5).

### Other engine pieces
- `GpuTimer` (`header/DX12/gpu_timer.h`): timestamp queries per named scope, 4 frames in flight, used by the render graph.
- `RenderTarget` (`header/DX12/render_target.h`): 8 color + depth attachments.
- `Camera` (`header/core/camera.h`): LH (`XMMatrixLookAtLH`, `XMMatrixPerspectiveFovLH`, camera.cpp:34,193), has
  `GetInverseViewMatrix`/`GetInverseProjectionMatrix` (useful for ray generation). No "changed" counter.
- `light.h`: `PointLight`, `SpotLight`, `DirectionalLight` (64 B, HLSL-packed; directional has `directionWS`, `color`, `position`
  used to derive the direction).

### Summary: changes needed for DXR / ray queries / CUDA
| Need | Today | Change |
|---|---|---|
| Raytracing tier query | none | `CheckFeatureSupport(OPTIONS5)` (+ `SHADER_MODEL`, `OPTIONS` binding tier) in `Initialize`, store in a caps struct; toggle disabled when unsupported (WARP/CI) |
| Device5+ | Device13 (fails on old Windows) | create base device, `As<ID3D12Device5>` optional |
| CommandList4 | List2 | create/query List4; wrappers `BuildRaytracingAccelerationStructure`, `SetRaytracingPipeline` (reset PSO cache), `DispatchRays` (flush barriers, commit staged descriptors for dispatch) |
| State objects + shader tables | none | new `RaytracingPipeline` class (`CD3DX12_STATE_OBJECT_DESC`), shader identifiers, SBT in a default buffer (or upload page) |
| AS buffers | `Resource` always COMMON | new creation path with initial state `RAYTRACING_ACCELERATION_STRUCTURE` + UAV flag; never transitioned |
| UAV barriers on AS | `UAVBarrier(resource)` exists | render graph access that only emits UAV barriers |
| Ray queries in compute | compute pattern exists | only shader model 6.5 + TLAS SRV; fits `OceanCompute`-style PSO and a graph compute pass |
| Bindless materials | per-list 1024 dynamic heap | short term: fixed-size staged table under the global root signature; long term: one persistent shader-visible heap |
| Shared heaps | `HEAP_FLAG_NONE` only | `CreateCommittedResource(..., D3D12_HEAP_FLAG_SHARED, ...)` + `CreateSharedHandle`, wrap with the `ComPtr` ctor, register state |
| Shared fence | per-queue fence `FLAG_NONE` | separate fence with `D3D12_FENCE_FLAG_SHARED`; queue `Signal/Wait(fence, value)` |
| Hot reload of libraries | not supported | `ReadShaderTarget` for type 6, no `-E` (or configurable entry) for `lib_*`, rebuild state object via `PipelineShaders::Update` |
| DRED names | no DXR ops | extend `GetBreadcrumbOpName` |

---

## 4. Render graph (`header/DX12/render_graph.h`, `source/DX12/render_graph.cpp`, `docs/render-graph.md`)

### Model
- Rebuilt every frame, executed in declaration order (kajiya style; `docs/render-graph.md:118`). One `RenderGraph` per game
  (`Ocean::m_renderGraph`, ocean_scene.h:205), shown in the editor via `Editor::SetRenderGraph`.
- Resources: `rg.Create(name, RGTextureDesc::Tex2D(fmt, w, h, mips))` / `rg.Create(name, RGBufferDesc::Structured|Raw)` for transient
  resources; `rg.Import(name, texture)` / `rg.Import(name, buffer, stride)` for persistent ones (header 299-308). Typed 4-byte handles
  `RGTexture`, `RGBuffer` (99-116), valid for one frame.
- Transient textures are **2D only** (`CD3DX12_RESOURCE_DESC::Tex2D`, cpp 529) with flags derived from accesses (UAV/RT/DS, cpp 50-60);
  transient buffers use `Application::CreateBuffer` (cpp 522-524, COMMON). Pooled and reused across frames; released after 8 unused
  frames (cpp 22, 573-579). No aliasing, no temporal/history resources (history textures must be imported).
- Passes: `rg.AddPass(name)` returns `RGPassBuilder` with `.Read(t|b, RGAccess)`, `.Write(t|b, RGAccess)` (default UAV),
  `.RenderTarget(t, clear, slot)`, `.DepthStencil(t, clear, write)`, `.SideEffects()`, `.Execute(lambda(RGPassContext&))`
  (header 223-251). `RGPassContext` gives the `CommandList`, `GetTexture/GetBuffer(handle)`, bound render target and viewport (191-214).
- Accesses and states (`RGAccess`, header 76-91; state map cpp 27-48): ShaderRead (PS|NPS), PixelShaderRead, NonPixelShaderRead,
  UnorderedAccess, RenderTarget, DepthWrite, DepthRead (still DEPTH_WRITE), VertexBuffer/ConstantBuffer, IndexBuffer,
  IndirectArgument, CopySource, CopyDest. Declaration errors throw `std::logic_error` (cpp 172-183, 406-429).
- Execution (`Execute`, cpp 584-628; `ExecutePass`, 630-734): cull passes whose writes nobody needs (434-471), create transients,
  then per pass: `BeginEvent(name)`, transition every declared resource to its combined state, add a **UAV barrier when the previous
  use was also UAV** (653-660), flush, start GPU timer scope, bind/clear attachments and set viewport/scissor from the first
  attachment, run the lambda, end timer, optional capture copy, `EndEvent`.
- Debug: `RenderGraphDebugInfo` (passes with accesses, culled flag, `raster` flag, GPU ms; resources with lifetimes) drives the
  editor's Render Graph panel (Passes / Timeline / Resources / Buffer viewer, `source/editor/editor_render_graph.cpp`).
  `SetCapture(pass, resource)` copies a texture (or reads back up to 4 MB of a buffer) right after a pass.

### How OceanRenderer builds its frame
See section 6. `Ocean::RecordRenderGraph` (ocean_scene.cpp:668-826) declares 4 cascades x 6 compute passes, then Skybox, Ocean Surface,
Objects, Tonemap, and calls `rg.Execute(commandList)` on the **direct** command list; the same list then gets the GUI and is
executed once (ocean_scene.cpp:642-665).

### Limitations (from docs/render-graph.md and the code)
- Everything is recorded on the one command list given to `Execute` (direct queue); compute passes dispatch on it (doc line 96).
- No reordering, no async compute ("with one direct queue (the choice for now)", line 118); the declared accesses would allow it
  later (Possible next steps, lines 135-139: async compute, aliasing, temporal resources, ShaderArt on the graph).
- No memory aliasing (needs placed resources and aliasing barriers the engine does not have, line 122).
- No temporal resources (line 138) -> accumulation buffers, history, reservoirs must be imported.
- Transient textures are Tex2D only; no cube/3D transients.
- Culling keeps a pass only if it writes an imported resource, writes something read later, has `.SideEffects()`, or declares
  nothing (doc line 89). An AS build pass writing an imported TLAS is kept.
- `RenderGraphDebugInfo::Pass::raster` is a bool: the panel shows Raster/Compute only.

### Fitting a ray tracing pass
- **Inline ray queries (DXR 1.1) in a compute pass** fit with *no graph changes* beyond an AS access kind: declare the TLAS as
  read, G-buffer/accumulation as `Write` (UAV), dispatch. Hot reload works. Recommended first step.
- **DispatchRays pass**: same declaration shape; inside the lambda: set global root sig + state object, stage descriptors,
  `DispatchRays`. Add a pass "kind" (Raster/Compute/RayTracing/External) for the panel.
- **AS build pass**: new accesses, e.g. `RGAccess::AccelerationStructureWrite` (build/refit destination; no transition, UAV barrier
  after) and `RGAccess::AccelerationStructureRead` (trace; UAV barrier if the previous use was a build). Geometry inputs use
  `NonPixelShaderRead`; scratch is a transient UAV buffer (`Write`). AS buffers themselves must be imported (persistent and created
  in the AS state); importing them as `RGBuffer` lets the panel show them (capture/readback of an AS buffer is meaningless and should
  be blocked).
- **External CUDA pass (OptiX denoiser, NRC training if CUDA-based)**: needs a frame split, which the graph cannot do today because
  it only sees one command list. Options:
  1. A pass flag `.External()` that makes `Execute` close and submit the list recorded so far, signal a shared fence, run the CUDA
     callback (which waits/signals on the CPU-side CUDA stream with external semaphores), make the queue `Wait` on the fence, and
     continue recording on a new list. Requires `Execute` to get the `CommandQueue` (not just a list) and to transition the
     shared resources to COMMON before the split.
  2. Keep the graph single-list and run CUDA between two graphs (two `RenderGraph::Execute` calls in `OnRender`). Simpler, but the
     panel shows two frames' worth of graphs, and the per-frame handle invalidation (`Execute` resets the frame) has to be handled.
  The tracker's "pending barriers at submit" design copes with splitting since each list resolves its first-use states at submit.

---

## 5. Scene representation

- `Scene` (`header/DX12/scene.h`): root `SceneNode`, plus `m_materials`, `m_meshes` lists (private). Visitor pattern: `Scene::Accept`
  visits scene, then nodes, then meshes (`scene.cpp:453-460`). Created by `CommandList::LoadSceneFromFile` (command_list.cpp:1718)
  or procedural `CreateCube/Sphere/Plane` (single node, single mesh, white material; 1731-1756).
- `SceneNode` (`header/DX12/scene_node.h`): name, local transform + inverse (aligned), `GetWorldTransform()` (concatenated, computed on
  call), children/parent (public members), meshes, AABB. No dirty flag/version (TLAS rebuild every frame is the simple answer).
- `Mesh` (`header/resources/mesh.h`): `std::map<slot, VertexBuffer>`, one `IndexBuffer`, one `Material`, topology, AABB,
  `Draw(CommandList&)`. On `scene-save-load` it also remembers the source file + mesh index (`aac3d33`).
- **GPU storage**: one committed default-heap buffer per vertex buffer and per index buffer per mesh (`CopyBuffer`,
  command_list.cpp:202-264). No CPU copy is kept after upload. No SRVs exist for them. Created on whatever queue the loading list
  belongs to (OceanRenderer: copy queue; editor loads: copy queue + CPU wait, `editor_assets.cpp:98-121`).
- **Vertex format**: `VertexPositionNormalTangentBitangentTexture` (`header/resources/vertex_types.h:62-101`): 5 x `float3`
  (position, normal, tangent, bitangent, texCoord.xyz) = **60 bytes stride**, position at offset 0 -> BLAS `VertexFormat
  DXGI_FORMAT_R32G32B32_FLOAT`, `StrideInBytes 60`. In a hit shader read it with a `ByteAddressBuffer` or `StructuredBuffer`
  (stride 60) SRV.
- **Index format**: imported meshes use `std::vector<unsigned int>` (scene.cpp:376-393) and procedural meshes `uint32_t`
  (`IndexCollection`) -> **always `DXGI_FORMAT_R32_UINT`**. Non-triangle faces are dropped.
- Model loading (`Scene::LoadSceneFromFile`, scene.cpp:56-109): **assimp** with `aiProcessPreset_TargetRealtime_MaxQuality |
  aiProcess_OptimizeGraph | aiProcess_ConvertToLeftHanded | aiProcess_GenBoundingBoxes` (89-90), smoothing angle 80°, points/lines
  removed; caches a preprocessed `.assbin` next to the source. Node transforms transposed into `XMMATRIX` (412-417).
- **Material** (`header/resources/material.h`): `MaterialProperties` (44-113, `alignas(16)`): `diffuse`, `specular`, `emissive`,
  `ambient`, `reflectance` (float4 each), `metallic`, `roughness`, `opacity`, `specularPower`, `indexOfRefraction`, `bumpIntensity`,
  `has*Texture` flags for 9 slots. Texture slots (`TextureType`, 120-133): Ambient (used as AO), Emissive, Diffuse (albedo),
  Specular, SpecularPower, Normal, Bump, Opacity, MetallicRoughness (glTF: G = roughness, B = metallic), H0 (ocean leftover).
  Import (`Scene::ImportMaterial`, scene.cpp:161-324): colors, shininess, opacity, IOR, reflectivity, bump scale, metallic factor,
  roughness factor (default 1 for formats without it, 222-223), textures loaded with sRGB for emissive/diffuse/specular. Normal vs bump
  decided by bits per pixel (312-319).
  - Gotcha: the comment says 128 bytes but the struct is 5x16 + 6x4 + 9x4 = 140 B, padded to 144 by `alignas(16)`. Fine for a
    cbuffer, but a `StructuredBuffer<Material>` in HLSL would have stride 140 unless padded explicitly — define a dedicated GPU
    material struct for the path tracer.
  - No transmission/clearcoat/sheen fields; glTF extensions (e.g. the "DragonAttenuation" asset mentioned in comments) are not read.
- Textures: `CommandList::LoadTextureFromFile` (DDS/HDR/TGA/WIC via DirectXTex), mips generated on the compute queue, weak cache keyed by
  file + sRGB. All `Texture` objects with an SRV descriptor (`Texture::GetShaderResourceView`).
- Lights are not part of the scene: the app passes `std::vector<DirectionalLight/PointLight>` to PSOs.

What is available for BLAS/TLAS + a material table:
- Per `Mesh`: VB GPU VA + count + stride, IB GPU VA + count, material pointer -> one BLAS per mesh (cache by `Mesh*`/weak_ptr;
  one geometry per BLAS, opaque flag unless `IsTransparent`).
- Per frame: walk scenes with a visitor (like `SceneVisitor`, `source/DX12/scene_visitor.cpp:16-46`) collecting
  (`Mesh`, `GetWorldTransform()`) -> TLAS instances (`InstanceID` = index into a per-instance table holding VB/IB/material indices).
- Material table: build a GPU struct per `Material` + texture indices into the staged descriptor table.
- Scenes to include in OceanRenderer: `m_oceanPlane` (special, see 6), `m_helmet`, `m_editor->GetSpawnedScenes()`, and the point-light
  sphere `m_sphere` (one Scene drawn with mutated transforms, ocean_scene.cpp:807-811). `m_chessboard` and `m_boat` are loaded
  (364-366) but never drawn. Skybox cube is raster-only (miss shader samples the cubemap instead).

---

## 6. OceanRenderer app (`EV-Engine/OceanRenderer/`)

Main: `source/main.cpp:22-45` creates `EV::Ocean(L"EV Engine", 1920, 1080)` (vsync off). Class: `include/ocean_scene.h`,
`source/ocean_scene.cpp`.

### Constants and sizes
- `OCEAN_SUBRES 512` (FFT size and grid subdivisions), `OCEAN_PLANE_SIZE 1024.0f` m, `OCEAN_DEPTH 20.0f` (unused here)
  (ocean_scene.h:13-15). 4 cascades (`m_oceanCascadesNumber`, 292) with patch sizes **1500, 250, 17, 5 m** (ocean_scene.cpp:383-387).
- Cascade textures (GenerateH0, 1105-1214): H0 `R16G16B16A16_FLOAT` 512² (re, im, conj re, conj im), slope and displacement
  `R16G16B16A16_FLOAT` 512² UAV, foam (Jacobian) `R32_FLOAT` 512² UAV (1161-1171).
- IBL: skybox cubemap 1024²x6 full mips from the panorama format (`sky4k.hdr`), diffuse irradiance 32²x6 RGBA16F, specular prefiltered
  512²x6 with 5 mips RGBA16F, BRDF LUT 512² `R16G16_FLOAT` (ocean_scene.cpp:25-27, 151, 197-235).
- Scene colour: transient "HDR Color" `R16G16B16A16_FLOAT` and "Depth Render Target" `D32_FLOAT` at the editor viewport size
  (735-736); output `m_viewportTexture` `R8G8B8A8_UNORM` render target ("Scene Viewport (LDR)", 487-490), shown in the Scene panel.

### Startup (`LoadContent`, 320-505)
Swap chain RGBA8 (326) -> GUI -> `Editor` with panels "Ocean Parameters"/"Light Parameters" and menu (334-340) -> on the **copy**
queue: ocean plane `CreatePlane(1024, 1024, 512, 512)` (357), skybox cube (359), `sky4k.hdr` (361), helmet/chess/boat (364-366),
sphere, H0 for 4 cascades (389-392) -> root parameters for compute PSOs (396-433) -> PSOs (435-444) -> BRDF LUT on compute queue
(454, CPU wait) -> ocean render params (457-461) -> foam params (478-484) -> viewport texture (487-491) -> `SetEnvironment(panorama)`
(497: pano -> cubemap, diffuse + specular convolution on the compute queue, CPU wait, then swaps the maps into the PSOs, 190-250).

### Per frame
`OnUpdate` (507-624): FPS, `WaitForSwapChain`, `UpdateImageBasedLighting` (re-bake if IBL shaders were hot reloaded, 176-188), camera
(WASD/QE translate, pitch/yaw, x16 or x64 with shift, 536-542), 3 directional lights (dir = normalize(-position); light 0 defaults to
position (280, 5, 341), i.e. a **low sun**, light 1 from (0, 30, 0), 546-576), skybox matrices, ocean time, 1 animated point light
(599-617), then `OnRender`.

`OnRender` (626-666): apply full screen toggle, follow the viewport size (`ResizeViewport`: projection 45°, near 0.1, far 1000,
130-143), get a direct list, `RecordRenderGraph` (unless loading), clear back buffer, `OnGUI` (editor), execute, `Present`.

`RecordRenderGraph` (668-826), in order:
1. Per cascade i (imported H0/Slope/Displacement/Foam):
   - `Ocean Spectrum i`: reads H0, writes slope + displacement (`animate_waves.hlsl`, 32x32 groups of 16x16) (690-699).
   - `FFT Displacement Horizontal/Vertical i`, `FFT Slope Horizontal/Vertical i` (`fft.hlsl`, 512 groups, 701-714).
   - `Ocean Permute i`: writes slope, displacement, foam (`permute.hlsl`, foam decay/bias/add/threshold) (716-724).
2. Imports skybox cubemap, diffuse/specular irradiance, BRDF LUT (728-731).
3. `Skybox`: clears HDR colour (0.4, 0.6, 0.9) and depth (1.0); draws the inverted cube with `SkyboxPSO` (738-746).
4. `Ocean Surface`: reads all cascade textures, IBL, the plane's VB/IB (imported for inspection), draws the plane with `OceanPSO`
   (749-784).
5. `Objects`: helmet, spawned scenes, point-light spheres with `EffectPSO` (`vertex.hlsl`/`pixel.hlsl`) (786-812).
6. `Tonemap`: HDR colour -> `m_viewportTexture` with `SDRPSO` (`HDR_to_SDR_PS.hlsl`) (815-823).

### Ocean geometry
- CPU-generated flat grid in XZ, Y up, centred at the origin, **513 x 513 = 263,169 vertices, 524,288 triangles, 2 m spacing**,
  60-byte vertices (~15.8 MB VB, ~6.3 MB IB) (`CommandList::CreatePlane`, command_list.cpp:984-1054). Static after creation.
- **Displacement happens only in the vertex shader** (`shaders/ocean_vertex.hlsl:47-72`): `uv_c = Position.xz / patchSize_c` on the
  *object-space* position, sums `DisplacementTexture_c.SampleLevel(linearWrap, uv, 0).rgb` over 4 cascades, adds to the position
  (choppy XZ + height Y), outputs world/view positions and `WaveHeight = displacement.y`. The pixel shader recomputes uvs from the
  *world* position (`ocean_pixel.hlsl:148-151`) — identical while the plane's transform is identity, but the plane is in the editor
  hierarchy and can be moved with the gizmo, which would desynchronize them.
- No LOD/clipmap, no tessellation. Plane edges are 512 m from the origin; far plane 1000 m.
- Textures tile per cascade; the 1500 m cascade is larger than the plane, so instancing copies of one BLAS to extend the ocean would
  show seams unless the displacement is evaluated from world xz per copy.

### Ocean shading (`shaders/ocean_pixel.hlsl:145-269`)
- Normal from summed slopes: `normalize(-slope.x, 1, -slope.y)` (166). Foam = saturate(sum of 4 foam samples).
- Roughness `0.2 + foam * 0.8`, metallic 0, **F0 = 0.02** (water), Schlick Fresnel, GGX distribution, Smith-Schlick geometry with
  the *direct* k (90).
- Direct light: loop over the first 2 directional lights, diffuse `oceanColor / PI` + GGX specular (217-236).
- IBL: specular from the prefiltered cubemap at `roughness * 4` LOD + split-sum BRDF LUT; diffuse = irradiance x oceanColor (240-254).
- **Subsurface scattering approximation** (Garrett Gunnell's FFTWater): `k1 = peakScatter * max(waveHeight,0) * heightMod *
  pow(saturate(dot(L, -V)), 4) * pow(0.5 - 0.5*dot(L, N), 3)`, times `(1 - Fresnel) * scatterColor * sunColor` (258-261).
- Final: `sss + ambient * IBLIntensity + direct`, then `lerp(color, white, foam)` (264-267).
- No refraction, no absorption/depth, no reflections of objects (only the environment map), no shadows on this branch.
- Parameters (`OceanPSO::m_oceanRenderParams`, set at 457-461 and editable at 918-944): ocean colour (0, 0.15, 0.3), scatter colour
  (0, 0.5, 0.4), IBL 0.4, height modifier 2, peak scatter 0.6.

### Object shading (`shaders/pixel.hlsl:238-335`)
Metallic-roughness PBR (glTF channels), normal mapping via TBN, roughness clamped >= 0.04, F0 0.04/albedo, one point light
(inverse-square), one directional light, split-sum IBL scaled by AO and `Camera.IBLIntensity`, emissive. No shadows (shadow map only on
`Dads-Birthday`).

### Post / tonemap
`SDRPSO` + `HDR_to_SDR_PS.hlsl`: exposure `exp2(Exposure)`, method Linear/Reinhard/Reinhard²/"ACESFilmic" (actually the Hable
Uncharted 2 curve) — default **ACESFilmic, exposure 0, gamma 2.2** (`source/DX12/sdr_pso.cpp:22-33`) — written to an UNORM target with
manual gamma (no sRGB format). No bloom, TAA, auto exposure.

### Camera
Static `Ocean::m_camera` (ocean_scene.cpp:34; static so the PSOs can read its position). Start at (0, 5, -20) looking at (0, 5, 0)
(98-102). Controls: right mouse + WASD/QE when the viewport has focus (docs/editor-handoff.md:11), wheel = FOV.

### What ray tracing the ocean needs
1. A compute pass after `Ocean Permute 3` ("Ocean Displace Vertices") that reads the 4 displacement textures and the static plane
   VB (or regenerates grid positions from the vertex index) and writes **displaced positions** (optionally normals from slopes) into
   a persistent UAV buffer (`float3`, 12 B stride, or `float4`). Use the same uv convention as the VS (object-space xz).
2. A BLAS built once with `ALLOW_UPDATE` (+ `PREFER_FAST_BUILD` or `PREFER_FAST_TRACE`) over that buffer and the plane's static
   R32 index buffer, then **refit** (`PERFORM_UPDATE`) every frame (topology never changes; large displacements degrade refit quality —
   rebuild every N frames or when wind parameters change). 524k triangles: refit is cheap, a full build each frame is also feasible on
   recent GPUs.
3. Hit shader for the ocean: interpolate world position, sample slope/foam textures at world xz for the shading normal (as the PS does),
   then a water BSDF (dielectric Fresnel with IOR 1.33, reflection + refraction with absorption, plus the SSS/foam terms if desired).
   Needs the cascade textures + patch sizes in the global root signature.
4. Optionally use ray queries in a compute pass (DXR 1.1) for a hybrid first step: raster G-buffer stays, the compute pass traces
   reflection/shadow rays against the TLAS.
5. Accumulation only converges if ocean time is frozen: add a "pause ocean" toggle or treat the ocean as dynamic (per-frame
   denoising, temporal reprojection, NRC).

---

## 7. Editor integration (`header/editor/editor.h`, `source/editor/*.cpp`)

- Usage pattern (editor.h:3-17): `Editor(m_GUI)`, `AddScene`, per frame `BeginFrame()`, `SetSceneTexture(texture)`, app windows,
  `EndFrame(commandList, swapChainRenderTarget)`.
- App UI hooks: `AddPanel(name, drawCallback, DockSlot, open)` (editor.h:139) — OceanRenderer adds "Ocean Parameters" and
  "Light Parameters" (ocean_scene.cpp:339-340); `AddMenuCallback` (142) — OceanRenderer adds File and Options (V-Sync, Full screen)
  menus (850-870). A **"Path Tracing" panel** (enable, spp per frame, max bounces, accumulated sample count readout, reset button,
  freeze ocean time, denoiser on/off, debug views) fits `AddPanel`; a quick on/off could go into the Options menu or the viewport
  toolbar (`Editor::DrawViewportToolbar`, editor.cpp:808-832; it currently has Move/Rotate/Scale/Local-World buttons — a "Raster /
  Path traced" toggle there would need an editor API such as a render-mode callback since the toolbar is engine code).
- Slider idiom in the app: `Slider` lambda returns `IsItemDeactivatedAfterEdit()` (877-883); tooltips via `IsItemHovered`.
- Viewport texture: the app renders into `m_viewportTexture` (RGBA8 RT) through the graph; the editor draws it with
  `drawList->AddImage(m_gui->GetTextureID(m_sceneTexture), ...)` (editor.cpp:757-761). `GUI::GetTextureID(texture|srv)`
  (`source/UI/GUI.cpp:212-229`) registers a per-frame texture; the GUI binds each one with `SetShaderResourceView` per draw.
  The Scene panel size drives `m_viewportWidth/Height` (`ConsumeViewportResize`, editor.h:88-92; editor.cpp:725-733).
  A path tracer would write HDR radiance into an accumulation UAV and reuse the existing Tonemap pass into the viewport texture.
- **Accumulation reset triggers** available: camera matrices (compare `GetViewMatrix`/`GetProjectionMatrix` per frame; no change
  counter exists), viewport resize (`ConsumeViewportResize`), gizmo edits (`DrawGizmo` calls `SetLocalTransform`, editor.cpp:853-862),
  reparenting (995), drops/spawned scenes (`GetSpawnedScenes()` size), material edits in the Inspector, environment swaps
  (`SetEnvironment`), ocean/light parameter edits, shader hot reloads (`ShaderLibrary::GetReloadCount`). A simple "scene version"
  counter in the editor (incremented on any edit) would help.
- Debug tooling that already works for a path tracer: Render Graph panel (GPU time per pass, capture any texture/buffer after any
  pass), Texture Viewer (channels, range, texel readback), Shaders panel (reload errors).

---

## 8. Conventions

- **Naming**: namespace `EV`; classes and methods PascalCase (`RenderGraph::AddPass`, `CommandList::SetShaderResourceView`); members
  `m_camelCase`; file-local constants `g_camelCase` in an anonymous namespace (`render_graph.cpp:19-25`, `shader_library.cpp:12-22`);
  function statics `s_`; enums PascalCase, root parameter enums unscoped inside the class (`EffectPSO::RootParameters`); files
  `snake_case.h/.cpp`, headers in `EV-Engine/header/<area>/`, sources in `EV-Engine/source/<area>/` (areas: `core`, `DX12`,
  `resources`, `UI`, `editor`, `utility`). Apps use `include/` + `source/` + `shaders/`. Mixed tabs/4 spaces in older files.
- **Headers**: `#pragma once`. Tutorial-derived files keep the MIT license block + `@file/@date/@author/@brief`
  (e.g. `command_list.h:1-33`). New files use a doxygen `@file` / `@brief` block with a usage example and design notes
  (`render_graph.h:1-51`, `gpu_timer.h:1-18`, `shader.h:1-11`, `editor.h:1-17`). Comments explain *why* (e.g.
  `render_graph.cpp:36-39`, `command_list.cpp:1337`).
- **Error handling**: `ThrowIfFailed(hr)` throws `std::runtime_error` with HRESULT + file/line/function via `std::source_location`
  (`header/utility/helpers.h:9-18`). Logic errors in the render graph throw `std::logic_error` with the pass name. Exceptions in the
  window procedure become a fatal report; failed asserts/terminate and SEH crashes write stack traces to `ev_crash.log`; debug-layer
  warnings/errors are logged before the break; DRED report on device removal (see section 2). Hot reload never throws to the app
  (errors stored on the `Shader`).
- **Object naming**: `SetName` on D3D objects everywhere (queues, lists "Direct Command List N", textures, graph resources named after
  their handle) so DRED/PIX show them.
- **Docs**: `docs/editor-handoff.md` (running handoff notes: "Done", "Fixed: ..." sections with cause/fix, TODO, planned next) and
  `docs/render-graph.md` (usage, tables, "Why it is built this way" with sources). Plain, explanatory English; tables for APIs.
  A path tracer would fit as `docs/path-tracing.md` in the same style plus a handoff section.
- Commits: small, imperative, prefixed by area ("RenderGraph: ...", "Editor: ...", "OceanRenderer: ...", "Docs: ...").

---

## 9. Risks and gaps for adding path tracing

1. **No capability detection; Device13 portability.** Add raytracing tier/SM/binding-tier queries and gate the feature; switch to a
   base device + optional `Device5` query (Dads-Birthday already hit the Device13 failure). CI runs on WARP: the toggle must stay off
   (or the WARP version must be checked) and the app must not throw.
2. **Command list interface is List2**; PSO/root-signature caches in `CommandList` will go stale around `SetPipelineState1`.
3. **No bindless; per-list 1024-entry dynamic descriptor heaps with mid-list heap switches** (`dynamic_descriptor_heap.cpp:162-175`);
   unbounded ranges unsupported (`root_signature.cpp:105-108`, assert at `dynamic_descriptor_heap.cpp:69`). Start with a global root
   signature and a fixed-size staged texture table; plan a persistent shader-visible heap for later (also needed for NRC-scale
   resource counts and SM 6.6).
4. **Root signature rigidity**: every PSO hardcodes registers; root signatures are not rebuilt on hot reload; a path tracer shares
   nothing with `EffectPSO`/`OceanPSO` bindings, so lighting/material code (BRDF, Fresnel, SSS) must be factored into `.hlsli` includes
   to keep raster and path tracer consistent (hot reload already tracks includes).
5. **Hot reload does not know libraries** (`ReadShaderTarget` stage switch, entry point fixed to `main`). Ray queries in `cs_6_5`
   avoid this; DXR libraries need the small extension described in section 3. Engine-level shader files are not found by the
   source search and not built next to the exe (section 1).
6. **Resource creation is hardwired** to default heap, `HEAP_FLAG_NONE`, COMMON (`resource.cpp:103-109`): AS buffers (AS state),
   shared CUDA heaps (`HEAP_FLAG_SHARED`) and readback/upload buffers need new creation paths and correct tracker registration.
7. **Cross-queue sync**: the handoff note "the ocean runs compute (FFT) on the compute queue with no GPU-side sync to the direct
   queue" (`docs/editor-handoff.md:56`) is now **mostly obsolete** — the render graph moved the FFT onto the direct queue
   (`docs/render-graph.md:130`). Remaining hazards: parameter edits call `GenerateH0` on the **copy queue** and replace all cascade
   textures without any GPU wait before the direct queue uses them (`ocean_scene.cpp:961-971`, `GenerateH0` 1175-1213); mip generation
   runs on the compute queue after a GPU wait on the copy queue, but nothing makes the direct queue wait (callers CPU-wait); IBL bakes
   CPU-wait. BLAS builds must happen on direct/compute (not copy) after uploads have completed; CUDA adds a fourth timeline.
8. **Single command list per frame in the graph** (doc line 96): an external CUDA pass needs a submission split (section 4).
9. **Animated ocean vs. accumulation**: ocean time advances every frame; progressive path tracing never converges unless time is frozen
   (or a denoiser/temporal method is used). Same for the animated point light (`ocean_scene.cpp:603`).
10. **Ocean geometry is GPU-only and displaced in the VS**: needs a displacement-to-buffer compute pass + BLAS refit (section 6);
    the plane is finite (1024 m) and the VS/PS uv conventions differ (object vs world space).
11. **Scene change tracking**: no transform/material/scene version counters; TLAS rebuild every frame is fine for a handful of instances;
    BLAS must be built per mesh once (and released with the mesh — lifetime via weak_ptr cache, like the texture cache fix in the
    handoff notes).
12. **Material model gaps**: no transmission/IOR use, no clearcoat; `MaterialProperties` layout/size mismatch for structured buffers
    (140 vs 144 B); opacity/alpha-tested materials would need any-hit shaders or ray query candidate handling.
13. **Lights are app-side vectors**, not scene entities; sun = `DirectionalLights[0]` with direction from a "position"; no area lights,
    no emissive-mesh light list; environment = cubemap (needs importance sampling data — e.g. a luminance CDF of the panorama — for
    low-noise sky lighting).
14. **Debug layer breaks on warnings** (application.cpp:514-516): any DXR warning stops Debug runs; DRED op names lack DXR ops.
15. **TDR risk**: heavy `DispatchRays` per frame (high spp/bounces) can exceed the TDR timeout; `Dads-Birthday` already had to fix a
    GPU hang in the specular prefilter on slower GPUs (`0cb371d`). Keep spp per frame small and progressive.
16. **Descriptor lifetime**: `Application::m_frameCount` is never incremented, so frame-based deferred release does not really exist;
    matters once descriptors live in a persistent GPU-visible heap.
17. **CUDA/OptiX/NRC build and runtime dependencies**: CUDA toolkit + OptiX SDK (+ NRC SDK) are not in the repo or CI; make them an
    optional build configuration (`EV_WITH_CUDA`) and a runtime check (adapter LUID match, driver). OptiX's denoiser works on linear
    buffers, so plan compute copy passes texture -> shared buffer -> texture. NRC SDK specifics (D3D12 vs CUDA backend, SM/Agility
    requirements) need to be checked against its documentation.
18. **Upload page limit**: per-list upload allocations above 2 MB throw (`upload_buffer.cpp:22-24`) — large instance/SBT/material
    uploads need a dedicated buffer.

### Suggested incremental path (for the design discussion)
1. Caps query + `Device5`/`List4` plumbing + DRED names; "Path tracing" panel with the toggle disabled when unsupported.
2. AS module: BLAS per `Mesh` (cached), TLAS per frame from a scene visitor; render graph AS accesses; TLAS SRV.
3. Hybrid with ray queries in compute (shadows/reflections of objects on the ocean) to validate the AS and the ocean displacement
   buffer + refit.
4. Full path tracer (DXR pipeline or a ray-query megakernel), global root signature with a staged material/texture table,
   accumulation buffer (imported), reset logic, freeze-ocean option, reuse of Tonemap.
5. Denoiser: start with a D3D12 option (e.g. NRD or a simple SVGF in compute) or OptiX via shared buffers + shared fence + graph split.
6. NRC later, once the persistent descriptor heap and buffer infrastructure exist.
