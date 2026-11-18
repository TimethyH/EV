# OptiX AI denoiser, CUDA, and CUDA <-> Direct3D 12 interop (plus alternatives), as of November 2026

Versions checked during this research (Nov 2026): **OptiX SDK 9.1.0** (Dec 2025, latest tagged release; no 9.2 found), **CUDA Toolkit 13.5** (current docs and cuda-samples), **Open Image Denoise 2.5.1** (Aug 2026; OIDN 3 with temporal denoising announced), **NRD v4.18.0**, **Streamline 2.14.16** (DLSS RR guide), **AMD FSR SDK 2.4 / FSR Ray Regeneration 1.2.1** (Oct 2026), **ZLUDA v6** (Jun 2026) and 7-preview builds, **HIP RT 3.2.0** pre-release (Oct 2026).

Note on access: raytracing-docs.nvidia.com (the OptiX Programming Guide HTML and PDF) failed TLS certificate checks from the fetch tool, so OptiX facts below come from the official headers (github.com/NVIDIA/optix-dev), the optix-sdk GitHub releases, the NVIDIA download page, NVIDIA forum posts by NVIDIA staff, and Falcor and nvpro sample code.

---

## 1. OptiX AI denoiser: models, guide layers, API sequence, quality

### Takeaway
The OptiX denoiser is a standalone, driver-hosted neural denoiser. You can call it from plain CUDA with only an `OptixDeviceContext`. No OptiX ray-tracing programs, pipelines or acceleration structures are needed. For new code, use `OPTIX_DENOISER_MODEL_KIND_AOV` for single frames, `TEMPORAL_AOV` for sequences, and `UPSCALE2X` / `TEMPORAL_UPSCALE2X` to denoise and upscale in one pass. Feed linear HDR float/half buffers plus albedo and normal guides (world-space normals are recommended on R535+ drivers). Temporal modes also need flow vectors, the previous denoised output, and the two internal guide layers. OptiX 9.1 needs an R590+ driver and a Turing or newer GPU.

### Cited Findings

**Versions and requirements**
- The latest OptiX SDK is 9.1.0 (December 2025), tagged "Latest" on GitHub, and requires an R590 driver or later. Its highlights include "Improved denoiser image quality". 9.0.0 (Feb 2025) requires R570+, 8.1.0 (Oct 2024) R555+, and 8.0.0 (Aug 2023) R535+. Temporal denoising arrived in 7.3, temporal "arbitrary values" in 7.4, AI upscaling in 7.5, temporal + 2x upscaling in 7.7, and multi-layer AOV in 7.2. — [NVIDIA/optix-sdk releases](https://github.com/NVIDIA/optix-sdk/releases)
- OptiX 9.1 was announced on Dec 10, 2025. The SDK (headers + samples) went up on GitHub (optix-sdk, tag v9.1.0) on Jan 13, 2026. Support for OptiX 6-based apps was removed from the driver as of R590. — [NVIDIA forum: OptiX 9.1 Release](https://forums.developer.nvidia.com/t/optix-9-1-release/354119)
- System requirements: GeForce, RTX/Quadro and Data Center products "with Turing and newer generation GPUs", an R590+ driver, a "recent CUDA Toolkit" and a C/C++ compiler. The download requires NVIDIA Developer Program membership. — [NVIDIA OptiX Downloads](https://developer.nvidia.com/designworks/optix/download)
- OptiX 9.0 added RTX 50 (Blackwell) support. — [NVIDIA blog: new SDKs for RTX 50](https://developer.nvidia.com/blog/new-ai-sdks-and-tools-released-for-nvidia-blackwell-geforce-rtx-50-series-gpus/)
- The OptiX runtime is not a DLL you ship. The headers give access to "the OptiX functions provided by the NVIDIA display driver". github.com/NVIDIA/optix-dev holds "the minimal set of necessary header files for building an application with OptiX support". The full SDK with samples is a separate download. — [NVIDIA/optix-dev](https://github.com/NVIDIA/optix-dev)
- Denoiser quality and speed improvements usually arrive with driver updates and apply without rebuilding the app. API changes come with the SDK. (NVIDIA staff "dhart", Jan 2026.) — [NVIDIA forum: OptiX denoiser vs NRD vs DLSS](https://forums.developer.nvidia.com/t/how-is-optix-denoiser-comparing-nrd-and-dlss-which-one-is-best-for-offline-and-semi-realtime-denoise/357080)

**Licensing and redistribution**
- OptiX is "free to use within any application, including commercial and educational applications". Downloading means accepting the "SOFTWARE DEVELOPER KITS, SAMPLES AND TOOLS LICENSE AGREEMENT". — [NVIDIA OptiX Downloads](https://developer.nvidia.com/designworks/optix/download)
- NVIDIA staff (forum, 7.3 era) said the license that ships with the SDK is the only one needed for commercial binary releases. The SDK itself must not be redistributed. Older DesignWorks EULAs asked for an online notification before commercial release. — [NVIDIA forum: OptiX 7.3 licensing](https://forums.developer.nvidia.com/t/optix-7-3-licensing-requirements/196368); [NVIDIA forum: OptiX use in commercial software](https://forums.developer.nvidia.com/t/optix-use-in-commercial-software/222703)
- A copy of the SDK license text (as shipped in Houdini): [SideFX: SDK, Samples and Tools License](https://www.sidefx.com/docs/houdini/licenses/optix.html)

**Model kinds** (exact enum values from `optix_types.h`) — [optix-dev optix_types.h](https://raw.githubusercontent.com/NVIDIA/optix-dev/main/include/optix_types.h)
- `OPTIX_DENOISER_MODEL_KIND_AOV = 0x2324`: single-image denoising. It takes AOV layers and uses `hdrAverageColor`.
- `OPTIX_DENOISER_MODEL_KIND_TEMPORAL_AOV = 0x2326`: temporally stable denoising of image sequences.
- `OPTIX_DENOISER_MODEL_KIND_UPSCALE2X = 0x2327`: single-image 2x upscaling, with AOV support.
- `OPTIX_DENOISER_MODEL_KIND_TEMPORAL_UPSCALE2X = 0x2328`: temporally stable 2x upscaling, with AOV support.
- `OPTIX_DENOISER_MODEL_KIND_LDR = 0x2322`: deprecated, mapped internally to AOV. `OPTIX_DENOISER_MODEL_KIND_HDR = 0x2323` (legacy). `OPTIX_DENOISER_MODEL_KIND_TEMPORAL = 0x2325`: deprecated, use TEMPORAL_AOV.
- NVIDIA staff (droettger, Aug 2023): LDR and HDR "are considered legacy and shouldn't be used in new applications anymore". Both use the same network. The other models "get continued training and architectural improvements". — [NVIDIA forum: Camera space vs World space](https://forums.developer.nvidia.com/t/optix-8-0-denoiser-camera-space-vs-world-space/262875)
- "Kernel prediction" is the AOV-family mode. In Falcor, the `kernelPredictionMode && useAOVs` path uses `hdrAverageColor` instead of `hdrIntensity`. — [Falcor OptixDenoiser.cpp](https://raw.githubusercontent.com/NVIDIAGameWorks/Falcor/master/Source/RenderPasses/OptixDenoiser/OptixDenoiser.cpp)
- `optixDenoiserCreateWithUserModel(context, userData, size, &denoiser)` also exists. The user model must export exactly one weight set. — [optix-dev optix_host.h](https://raw.githubusercontent.com/NVIDIA/optix-dev/main/include/optix_host.h)

**Options, guide layers and layers** — [optix_types.h](https://raw.githubusercontent.com/NVIDIA/optix-dev/main/include/optix_types.h)
- `OptixDenoiserOptions { guideAlbedo; guideNormal; denoiseAlpha; }`. `denoiseAlpha` takes `OPTIX_DENOISER_ALPHA_MODE_COPY` (0, copies alpha through) or `OPTIX_DENOISER_ALPHA_MODE_DENOISE` (1).
- `OptixDenoiserGuideLayer` fields:
  - `albedo`: RGB.
  - `normal`: the header comment says XY camera-space for LDR/HDR and XYZ world-space for all other models.
  - `flow`: XY per-pixel screen-space motion from the previous frame to the current frame.
  - `previousOutputInternalGuideLayer` / `outputInternalGuideLayer`: temporal-mode internal images.
  - `flowTrustworthiness`: single channel, 0..1. It is ignored if its data pointer is 0.
- `OptixDenoiserLayer { input; previousOutput; output; type; }`. `type` is an `OptixDenoiserAOVType` hint: `NONE`, `BEAUTY` (0x7000), `SPECULAR`, `REFLECTION`, `REFRACTION`, `DIFFUSE`.
- `OptixDenoiserParams` fields:
  - `hdrIntensity`: device pointer to 1 float (average log intensity). If null, it is computed automatically. It should be supplied when tiling.
  - `blendFactor`: 0 = fully denoised, 1 = input.
  - `hdrAverageColor`: device pointer to 3 floats (average log color, AOV model).
  - `temporalModeUsePreviousLayers`: 0 on the first frame, 1 once the previous layers are valid.
  - `flowMulX` / `flowMulY`: motion-vector scale, 0 = no scaling.
- `OptixDenoiserSizes` fields:
  - `stateSizeInBytes`
  - `withOverlapScratchSizeInBytes` and `withoutOverlapScratchSizeInBytes`
  - `overlapWindowSizeInPixels`
  - `computeAverageColorSizeInBytes` and `computeIntensitySizeInBytes` (both independent of resolution)
  - `internalGuideLayerPixelSizeInBytes`
- Pixel formats: `HALF1/2/3/4`, `FLOAT1/2/3/4`, `UCHAR3/4`, and `OPTIX_PIXEL_FORMAT_INTERNAL_GUIDE_LAYER = 0x2209`.

**Normal space: the sources disagree**
- NVIDIA staff (droettger): "it's recommended to use world space normal vectors as denoiser input on current drivers (R535 and newer)". Only LDR/HDR require camera space, and the other models "will still work with camera space normal vectors as well". — [NVIDIA forum](https://forums.developer.nvidia.com/t/optix-8-0-denoiser-camera-space-vs-world-space/262875)
- The `optixDenoiserInvoke` comment in `optix_host.h` still says temporal models need "3D camera-space" normals. This looks like a stale comment. — [optix_host.h](https://raw.githubusercontent.com/NVIDIA/optix-dev/main/include/optix_host.h)
- Falcor transforms world normals with `transpose(inverse(viewMatrix))` (that is, to camera space) before passing them in. — [Falcor OptixDenoiser.cpp](https://raw.githubusercontent.com/NVIDIAGameWorks/Falcor/master/Source/RenderPasses/OptixDenoiser/OptixDenoiser.cpp)

**Exact API signatures** — [optix_host.h](https://raw.githubusercontent.com/NVIDIA/optix-dev/main/include/optix_host.h)
- `optixDenoiserCreate(OptixDeviceContext, OptixDenoiserModelKind, const OptixDenoiserOptions*, OptixDenoiser*)`
- `optixDenoiserComputeMemoryResources(denoiser, outputWidth, outputHeight, OptixDenoiserSizes*)`
  - Use the with-overlap scratch size when tiling. The without-overlap size is always smaller and is enough for full-frame use.
  - The output width and height must be at least the dimensions passed to setup.
- `optixDenoiserSetup(denoiser, CUstream, inputWidth, inputHeight, CUdeviceptr state, stateSize, CUdeviceptr scratch, scratchSize)`
  - With tiling, the input size includes 2x overlap.
  - The setup sizes are maximums; later invokes may use smaller inputs.
- `optixDenoiserInvoke(denoiser, stream, const OptixDenoiserParams*, state, stateSize, const OptixDenoiserGuideLayer*, const OptixDenoiserLayer* layers, numLayers, inputOffsetX, inputOffsetY, scratch, scratchSize)`
  - `guideLayer` must be non-null.
  - The beauty layer must be first; AOV models accept further layers.
  - The offsets are 0 for the full image or for border tiles.
  - State must stay valid until the next setup; scratch is only used during the call.
  - In-place operation requires identical formats when blending.
  - Temporal models: a flow image is required. For the first frame, `previousOutput` = the noisy input and flow = 0. The internal guide layers must be allocated with format `INTERNAL_GUIDE_LAYER`, and the previous one must be valid or zeroed on the first frame.
- `optixDenoiserComputeIntensity(denoiser, stream, const OptixImage2D* input, CUdeviceptr outIntensity /*1 float*/, scratch, scratchSize)` and `optixDenoiserComputeAverageColor(..., outAverageColor /*3 floats*/, ...)`
  - With tiling, compute them over the whole image.
  - UCHAR input is not supported; use 3- or 4-channel half or float.
- `optixDenoiserDestroy(denoiser)`.
- `optix_host.h` states no special alignment rules for denoiser buffers (alignment rules there only cover AS, micromap and cooperative-vector buffers).

**Using it without ray-tracing programs: yes**
- Falcor's `OptixDenoiser` render pass uses only a device context and the denoiser calls. It converts textures to shared linear buffers with compute shaders (`ConvertTexToBuf.cs.slang`, `ConvertNormalsToBuf.cs.slang`, `ConvertMotionVectorInputs.cs.slang`), denoises, and writes back with `ConvertBufToTex.ps.slang`. — [Falcor OptixDenoiser directory](https://github.com/NVIDIAGameWorks/Falcor/tree/master/Source/RenderPasses/OptixDenoiser)
- nvpro-samples `vk_denoise` path-traces in Vulkan, then copies images into shared linear buffers for the OptiX denoiser. "OptiX needs linear images, and Vulkan uses tiled ones." — [nvpro-samples/vk_denoise](https://github.com/nvpro-samples/vk_denoise)

**How Falcor drives it** (a useful reference implementation) — [Falcor OptixDenoiser.cpp](https://raw.githubusercontent.com/NVIDIAGameWorks/Falcor/master/Source/RenderPasses/OptixDenoiser/OptixDenoiser.cpp); [Falcor README via directory](https://github.com/NVIDIAGameWorks/Falcor/tree/master/Source/RenderPasses/OptixDenoiser)
- Model choice: it auto-selects `TEMPORAL` if motion vectors are connected, otherwise `HDR`. A user can force LDR/HDR/AOV/Temporal.
- Buffers: staging buffers are `OPTIX_PIXEL_FORMAT_FLOAT4` (RGBA32F) and `FLOAT2` for motion. No half formats are used.
- Intensity: it calls `optixDenoiserComputeIntensity` or `ComputeAverageColor` each frame into a 1-float or 3-float buffer.
- First frame: it sets `previousOutput = input`; afterwards the output becomes the history.
- Tiling: not implemented. There is a TODO, and the tile is the full texture.
- Resize: the denoiser is destroyed and recreated, then ComputeMemoryResources and Setup run again.
- Sync: `waitForFalcor()` runs before the OptiX calls and `waitForCuda()` after (fence-based; see section 2). OptiX runs on the default stream.
- Debug notes from its README: an `optixInit()` failure means the driver needs an update. Missing DLLs can stop it loading on Windows. CUDA and OptiX versions must match across passes.

**Temporal behavior and known quality issues**
- Flow convention: OptiX expects motion vectors pointing **forward in time** (previous to current). DLSS expects current to previous. Scale and range are the same. (NVIDIA staff dhart, Jan 13, 2026.) — [NVIDIA forum](https://forums.developer.nvidia.com/t/how-is-optix-denoiser-comparing-nrd-and-dlss-which-one-is-best-for-offline-and-semi-realtime-denoise/357080)
- `flowTrustworthiness` (0..1 per pixel) exists, and the `optixDenoiser` SDK sample uses it. The staff reply dates it to 7.7 and also cites the 8.0.0 release notes.
  - Null flow is acceptable (and expected on the first frame), but NaN must never be passed.
  - For disocclusions, use camera or geometry motion rather than zero where you can.
  - "Some ghosting is possible when no better motion value exists", though the denoiser tries to detect and reduce it.
  - — [NVIDIA forum: motion confidence buffer](https://forums.developer.nvidia.com/t/request-for-motion-confidence-buffer-for-optix-temporal-denoise/274544)
- Blender developers testing OptiX 7.3 temporal mode reported that it gives up detail for temporal stability compared with the non-temporal mode. One tester said it "does not appear to use temporal re-projection" and described it as having "a temporal memory of 1 frame". Cycles motion vectors also needed conversion to OptiX's convention. (Community testing, not NVIDIA statements.) — [Blender devtalk: OptiX 7.3 temporal denoising](https://devtalk.blender.org/t/taking-a-look-at-optix-7-3-temporal-denoising-for-cycles/18656?page=2); [Blender commit: Add OptiX temporal denoising](https://developer.blender.org/rB8393ccd0763)
- Sample counts: "DLSS is extremely good at low per-pixel sample counts". OptiX works better at higher counts (around 16 spp). OptiX handles per-pixel jitter, while DLSS needs temporal camera jitter. Some apps use DLSS while the camera moves and OptiX once it stops and progressive accumulation refines the image. (NVIDIA staff dhart, Jan 2026.) — [NVIDIA forum](https://forums.developer.nvidia.com/t/how-is-optix-denoiser-comparing-nrd-and-dlss-which-one-is-best-for-offline-and-semi-realtime-denoise/357080)
- vk_denoise only starts denoising after a set number of iterations (10 in the example), so early frames render faster. The denoised frame also takes long enough that in-flight frames are shown before it finishes. — [nvpro-samples/vk_denoise](https://github.com/nvpro-samples/vk_denoise)

**Timings**
- No official 1080p or 1440p millisecond figures for current RTX cards were found. NVIDIA's denoiser page shows only relative charts (OptiX 6.5, 4K, i9-12900K). The only old per-GPU figures are from an OptiX 5.1 SIGGRAPH 2018 slide: about 125 ms Pascal, 17 ms Volta, 39 ms Turing, with no resolution given. — [NVIDIA OptiX denoiser page](https://developer.nvidia.com/optix-denoiser); [SIGGRAPH 2018 slides (Marcus)](https://www.nvidia.com/content/dam/en-zz/Solutions/events/siggraph2018/pdf/thursday/sig1850-guillermo-marcus-ai-based-ssim-prediction-in-the-optix-sdk.pdf)
- One forum report measured 1200-1500 ms for the first call (initialization overhead), so time it after warm-up. — [NVIDIA forum: denoising benchmarks GTX 1080](https://forums.developer.nvidia.com/t/denoising-benchmarks-with-geforce-gtx-1080/66674)

### Inferences

**Minimal integration sequence** (assembled from the header signatures and Falcor's flow; not copied from a single source):
1. Initialize OptiX and create a context:
   - Include `optix_function_table_definition.h` in one .cpp and `optix_stubs.h` where needed.
   - `cudaSetDevice(dev)` (matched by LUID), then `cudaFree(0)` to create the primary context.
   - `optixInit()`.
   - `optixDeviceContextCreate(cuCtx /*0 = current*/, &opts, &ctx)`.
2. Create the denoiser and size its memory:
   - Set `OptixDenoiserOptions o{ guideAlbedo=1, guideNormal=1, denoiseAlpha=COPY }`.
   - `optixDenoiserCreate(ctx, OPTIX_DENOISER_MODEL_KIND_AOV /*or TEMPORAL_AOV*/, &o, &dn)`.
   - `optixDenoiserComputeMemoryResources(dn, W, H, &sizes)`.
   - `cudaMalloc` the state (`stateSizeInBytes`) and scratch (`withoutOverlapScratchSizeInBytes`, plus `computeIntensitySizeInBytes` or `computeAverageColorSizeInBytes` if those calls reuse the same scratch).
3. `optixDenoiserSetup(dn, stream, W, H, state, stateSize, scratch, scratchSize)`. Run it again on every resize.
4. Each frame, after the cudaWait on the D3D12 fence (section 2):
   - `optixDenoiserComputeAverageColor` (AOV models) or `optixDenoiserComputeIntensity` (HDR) into a small device buffer.
   - Fill `OptixImage2D{data, width, height, rowStrideInBytes, pixelStrideInBytes, format}` for the color, albedo, normal and (temporal) flow buffers.
   - `optixDenoiserInvoke(...)`.
   - cudaSignal the fence for D3D12.
5. Temporal modes:
   - Allocate two internal guide buffers of `W*H*internalGuideLayerPixelSizeInBytes` and swap prev/out each frame.
   - Keep the previous output buffer.
   - Set `temporalModeUsePreviousLayers=0` on the first frame and after camera cuts or resets.
- For a student path tracer with *interactive accumulation* (static camera, spp climbing), `AOV` (non-temporal) on the accumulated buffer is the natural fit. Temporal only matters while the camera or scene moves.
- Upscaling models expect outputs at 2x the input size. Size them with `ComputeMemoryResources(outputW, outputH)` (inferred from the header wording that the sizes are output dimensions).
- Spectral rendering: no source found on OptiX quality with spectral input specifically. The denoiser is trained on RGB. Convert spectral radiance to linear RGB before denoising; feeding raw spectral bins as AOVs is not a documented use.

### Gaps
- The exact memory sizes (state and scratch MB) at 1080p and 1440p are only available at runtime via `OptixDenoiserSizes`. No published numbers were found.
- No 2025-2026 benchmarks of OptiX denoise time on RTX 30/40/50 at 1080p or 1440p were found. Benchmark it yourself after warm-up.
- The OptiX 9.1 Programming Guide denoiser chapter (raytracing-docs.nvidia.com) could not be fetched because of a TLS certificate failure. The official wording on world vs camera normals for the AOV and temporal models remains contradictory between the forum and the header comment.
- No current EULA text was confirmed on whether commercial-release notification is still required.

---

## 2. CUDA <-> D3D12 interop: memory, fences, ordering, samples and integrations

### Takeaway
Share **committed buffers** (or a shared heap) created with `D3D12_HEAP_FLAG_SHARED`, export them with `ID3D12Device::CreateSharedHandle`, and import them with `cudaImportExternalMemory`:
- committed resource: `cudaExternalMemoryHandleTypeD3D12Resource` plus the required `cudaExternalMemoryDedicated` flag;
- heap: `cudaExternalMemoryHandleTypeD3D12Heap`.

Then map with `cudaExternalMemoryGetMappedBuffer`. Synchronize with one shared `ID3D12Fence` (`D3D12_FENCE_FLAG_SHARED`) imported as `cudaExternalSemaphoreHandleTypeD3D12Fence`, using monotonically increasing values. Match the CUDA device to the D3D12 adapter by LUID. Linear buffers are easiest and are what OptiX wants anyway. Mipmapped-array import works for textures but needs a DXGI-to-CUDA format mapping.

### Cited Findings

**Runtime API rules** (CUDA Runtime API 13.5, section 6.19 External Resource Interoperability) — [CUDA Runtime API: External Resource Interop](https://docs.nvidia.com/cuda/cuda-runtime-api/group__CUDART__EXTRES__INTEROP.html)
- `cudaImportExternalMemory` supports `cudaExternalMemoryHandleTypeD3D12Heap` and `cudaExternalMemoryHandleTypeD3D12Resource`.
  - Exactly one of `handle.win32.handle` (a shared NT handle from `ID3D12Device::CreateSharedHandle`) or `handle.win32.name` must be set.
  - `size` must be set.
  - `cudaExternalMemoryDedicated` must be set for D3D12Resource.
- `cudaExternalMemoryGetMappedBuffer`:
  - offset and size must meet the external API's alignment, and flags must be 0;
  - overlapping mappings may or may not alias, so map the whole range once and use offsets;
  - free the pointer with `cudaFree`.
- `cudaExternalMemoryGetMappedMipmappedArray`:
  - takes `offset`, `formatDesc`, `extent`, `flags` and `numLevels`;
  - flags follow `cudaMalloc3DArray`, and `cudaArrayColorAttachment` must be set if the array is used as a color target in the graphics API;
  - free it with `cudaFreeMipmappedArray`.
- `cudaDestroyExternalMemory`: mapped buffers and arrays must be freed separately, and use of the handle after the call is undefined.
- `cudaDestroyExternalSemaphore`: outstanding signals and waits must complete first.
- `cudaImportExternalSemaphore` with `cudaExternalSemaphoreHandleTypeD3D12Fence` takes the NT handle or name of an `ID3D12Fence`.
  - `cudaSignalExternalSemaphoresAsync` sets the fence to `params.params.fence.value` after prior work in the stream.
  - `cudaWaitExternalSemaphoresAsync` blocks the stream until fence >= value.

**Programming Guide (CUDA 13.5, section 4.22 "CUDA Interoperability with APIs")** — [CUDA Programming Guide: graphics interop](https://docs.nvidia.com/cuda/cuda-programming-guide/04-special-topics/graphics-interop.html)
- LUID matching: compare `cudaDeviceProp::luid` with `ID3D12Device::GetAdapterLuid()`. The D3D12 device "must not be created on a linked node adapter" (`GetNodeCount()==1`).
- Heaps and committed resources must be created with `D3D12_HEAP_FLAG_SHARED`. For a committed resource, "the flag `cudaExternalMemoryDedicated` must be set". The heap example does not set it.
- NT handles: "it is the application's responsibility to close the NT handle when it is not required anymore". The examples call `CloseHandle` after the import. Under Vulkan on Linux, CUDA takes ownership of the fd, which differs from D3D12 handles.
- Mipmapped arrays: the guide gives helper functions.
  - `getCudaChannelFormatDescForDxgiFormat` maps DXGI formats such as `R32G32B32A32_FLOAT` to `cudaChannelFormatKindFloat` 32-bit channels, and asserts on unsupported formats.
  - `getCudaMipmappedArrayFlagsForD3D12Resource` sets `cudaArrayColorAttachment` when the resource has `D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET`, `cudaArraySurfaceLoadStore` for surface writes, and layered/cubemap flags from the SRV dimension.
- Fences must be created with `D3D12_FENCE_FLAG_SHARED`. A D3D12 wait for a CUDA signal must be issued after the CUDA signal, and the same holds in the other direction.
- The guide does not discuss `D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS` or keyed mutexes, and gives no full ordering example. It also does not say how to compute `size`.
- The older CUDA 12.x guide (section 3.2.16.3) has subsections on matching device LUIDs, importing memory objects, mapping buffers and mipmapped arrays, and importing, signaling and waiting on synchronization objects. — [CUDA 12.3 Programming Guide contents](https://docs.nvidia.com/cuda/archive/12.3.0/cuda-c-programming-guide/contents.html)

**Official sample: cuda-samples `simpleD3D12`** — [NVIDIA/cuda-samples simpleD3D12](https://github.com/NVIDIA/cuda-samples/tree/master/cpp/5_Domain_Specific/simpleD3D12)
- It currently lives at `cpp/5_Domain_Specific/simpleD3D12/`. Older trees used `Samples/5_Domain_Specific/simpleD3D12` or `Samples/2_Graphics/simpleD3D12`; it was first added in CUDA 10.1 Update 1.
- Files: `simpleD3D12.cpp/.h`, `DX12CudaSample.cpp/.h`, `sinewave_cuda.cu`, `shaders.hlsl`, `Main.cpp`, `Win32Application.cpp/.h`, `d3dx12.h`, `ShaderStructs.h`, `DXSampleHelper.h`, `CMakeLists.txt`.
- What it does: a CUDA kernel writes a sine wave into a DX12 vertex buffer, the two sides synchronize with DX12 fences, and D3D12 renders it.
- APIs used: `cudaImportExternalMemory`, `cudaExternalMemoryGetMappedBuffer`, `cudaImportExternalSemaphore`, `cudaSignal/WaitExternalSemaphoresAsync`, `cudaDestroyExternalMemory/Semaphore`, `cudaStreamCreate/Synchronize`, `cudaGetDeviceProperties`.
- The README lists "SM 5.0 through 9.0" and requires "A DirectX12 Capable NVIDIA GPU ... Windows10 or higher".
- The cuda-samples repo targets CUDA Toolkit 13.5 and builds with CMake 3.20+, generating `CUDA_Samples.sln` on Windows. The Vulkan equivalents are `simpleVulkan` and `simpleVulkanMMAP` under `cpp/5_Domain_Specific/`. — [NVIDIA/cuda-samples](https://github.com/NVIDIA/cuda-samples)
- A known pitfall: a forum thread reports an integer-division-by-zero exception inside `cudaExternalMemoryGetMappedMipmappedArray` when importing a D3D12 texture. — [NVIDIA forum: GetMappedMipmappedArray division by zero](https://forums.developer.nvidia.com/t/exception-in-cuda-cudaexternalmemorygetmappedmipmappedarray-integer-division-by-zero/84225)

**Falcor's interop utilities** (production-quality open source, D3D12 + Vulkan) — [Falcor CudaUtils.cpp](https://raw.githubusercontent.com/NVIDIAGameWorks/Falcor/master/Source/Falcor/Utils/CudaUtils.cpp)
- `getSharedDevicePtr()`:
  - sets `cudaExternalMemoryHandleTypeD3D12Resource` (or `OpaqueWin32` / `OpaqueFd` for Vulkan), `size = bytes` and `flags = cudaExternalMemoryDedicated`;
  - then calls `cudaImportExternalMemory`, then `cudaExternalMemoryGetMappedBuffer`.
  - It never stores or destroys the `cudaExternalMemory_t`, so this path leaks the handle object.
- Buffers must be created with `ResourceBindFlags::Shared`.
- `importExternalSemaphore(Fence*)` requires a shared fence and uses `cudaExternalSemaphoreHandleTypeD3D12Fence` (or `TimelineSemaphoreWin32` / `Fd` for Vulkan). The signal and wait helpers set `params.params.fence.value`.
- Device selection: `findDeviceByLUID` does a `memcmp` of the 8-byte `cudaDeviceProp::luid` against the adapter LUID. It falls back to a name match, then to the first device with SM >= 7.
- `importTextureToMipmappedArray()` sets the size from `getTextureSizeInBytes()`, builds the channel desc from the format bits, uses `numLevels = 1`, and sets the extent as width x height x 1. `mapTextureToSurface()` then wraps level 0 in a `cudaSurfaceObject_t`.
- The OptixDenoiser pass uses shared *typed buffers* rather than textures, with compute shaders converting between the two. — [Falcor OptixDenoiser.cpp](https://raw.githubusercontent.com/NVIDIAGameWorks/Falcor/master/Source/RenderPasses/OptixDenoiser/OptixDenoiser.cpp)

**Vulkan equivalent: nvpro-samples vk_denoise** — [nvpro-samples/vk_denoise](https://github.com/nvpro-samples/vk_denoise)
- It copies the rendered image into a shared linear buffer (only those buffers get the external-memory flag, through a dedicated allocator). The denoiser code is in `optix_denoiser/` (`DenoiserOptix`).
- Sync uses timeline semaphores:
  1. Vulkan signals when the image has been rendered and copied.
  2. A CUDA wait holds the denoiser until then.
  3. A CUDA signal releases the Vulkan wait.
  4. Vulkan copies the buffer back to an image, tonemaps and presents.
  This avoids a CPU stall. The sample was profiled with Nsight Systems.
- Guide inputs: RGB, RGB+albedo, or RGB+albedo+normal (`inputKind` in `DenoiserOptix::initOptiX()`). The original sample targeted OptiX 7.3+.

**Newer option: CUDA-in-Graphics (CIG)**
- A CUDA context can be created in CIG mode by passing `cigParams` (an `ID3D12CommandQueue*` as shared data) to `cuCtxCreate` through `CUctxCreateParams`. Check support with `CU_DEVICE_ATTRIBUTE_D3D12_CIG_SUPPORTED`. `execAffinityParams` and `cigParams` are mutually exclusive. — [CUDA Driver API: Context Management](https://docs.nvidia.com/cuda/cuda-driver-api/cuda_driver_api/group__CUDA__CTX.html); [CUctxCreateParams](https://docs.nvidia.com/cuda/cuda-driver-api/structCUctxCreateParams.html)
- NVIDIA's TensorRT-RTX docs describe "Simultaneous Compute and Graphics" with a D3D12 compute queue passed to CIG. NVIGI uses CIG for its CUDA plugins. — [TensorRT-RTX: compute and graphics](https://docs.nvidia.com/deeplearning/tensorrt-rtx/1.4/inference-library/compute-graphics.html); [NVIGI GPU Scheduling for AI](https://docs.nvidia.com/nvigi-sdk/1.6.0/docs/nvigi_core/docs/GpuSchedulingForAI.html)
- Nsight Systems 2026.4.1 added D3D12 CIG stream interception, and 2026.5.1 traces CUDA work submitted through D3D12 CIG streams on the timeline. — [Nsight Systems release notes](https://docs.nvidia.com/nsight-systems/ReleaseNotes/)

**OIDN interop works the same way**
- OIDN 2.x imports D3D12 memory with `oidnNewSharedBufferFromWin32Handle` (`OIDN_EXTERNAL_MEMORY_TYPE_FLAG_D3D12_RESOURCE` / `_D3D12_HEAP`), plus D3D11/12 fences via `oidnNewSharedSemaphoreFromWin32Handle` (2.5+). — [OIDN documentation](https://www.openimagedenoise.org/documentation.html); [OIDN releases](https://github.com/RenderKit/oidn/releases)

### Inferences

**Recommended per-frame ordering** (one shared fence and a monotonically increasing counter `v`; derived from the API rules above and the vk_denoise pattern):
1. D3D12 direct queue:
   - render or accumulate;
   - compute-copy the textures into shared linear buffers (UAV writes to the shared buffers, then a resource barrier);
   - `queue->Signal(fence, ++v)`.
2. CUDA stream:
   - `cudaWaitExternalSemaphoresAsync(fence, v)`;
   - ComputeIntensity or AverageColor, then `optixDenoiserInvoke`;
   - `cudaSignalExternalSemaphoresAsync(fence, ++v)`.
3. D3D12:
   - `queue->Wait(fence, v)`;
   - compute or pixel pass that copies the output buffer into a texture, then tonemap and present.

Notes on this ordering:
- The CPU must issue the CUDA signal before submitting the D3D12 `Wait` that depends on it. The wait call itself is non-blocking, so call order on the CPU thread suffices.
- Never reuse or decrease fence values.
- Falcor's `waitForFalcor()` / `waitForCuda()` are this same pattern.

**Allocation recipe:**
- Create each shared buffer with `CreateCommittedResource(heap DEFAULT, D3D12_HEAP_FLAG_SHARED, Buffer desc with D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)`.
- `CreateSharedHandle(res, nullptr, GENERIC_ALL, nullptr, &h)`.
- Import as `D3D12Resource` with the `Dedicated` flag and size = `GetResourceAllocationInfo(0,1,&desc).SizeInBytes`. (The common practice in NVIDIA samples; the CUDA guide just says "size". Verify against simpleD3D12.cpp.)
- `CloseHandle(h)` after the import.
- Map offset 0 and the full size.
- Keep the `cudaExternalMemory_t` and destroy it on resize or shutdown. This avoids Falcor's leak.

**Buffers vs textures:**
- Use row-major linear buffers with `rowStrideInBytes = W * pixelSize` (tightly packed is fine for OptiX) and FLOAT4 or HALF4 formats.
- D3D12 textures use opaque swizzled layouts, so they can only be shared as CUDA arrays (mipmapped arrays), not as raw pointers that OptiX's `OptixImage2D` can consume. That is why both Falcor and vk_denoise copy through buffers.

**Resize and teardown:**
- Wait for both sides to finish (CPU-wait on the fence).
- Free with `cudaFree`, then `cudaDestroyExternalMemory`, then release the D3D12 resource.
- Re-create, then run `optixDenoiserSetup` again.

**Multi-GPU PC with an iGPU:** always pick the DXGI adapter first, then the CUDA device by LUID. The CUDA default device 0 may not be the adapter that D3D12 uses.

### Gaps
- `simpleD3D12.cpp` raw source could not be fetched (the raw URL returned 404), so the exact fence-value sequence and size computation in the official sample were not verified line by line.
- No official statement was found on whether the OptiX denoiser can run in a CIG-mode context, or on any performance benefit.
- Unreal Engine and Blender DX12 + OptiX integration file names were not researched in depth. Blender Cycles uses OptiX via CUDA, not D3D12.
- The pitfalls around row pitch (D3D12 `D3D12_TEXTURE_DATA_PITCH_ALIGNMENT` = 256 for copies) are standard D3D12 knowledge and were not verified against a source in this session.

---

## 3. Alternatives: OIDN, NRD, DLSS RR, AMD FSR Ray Regeneration, SVGF, and what runs on AMD

### Takeaway
- **Cross-vendor offline/interactive denoising with OptiX-like quality:** Intel **OIDN 2.5**. It runs on CPU, Intel (SYCL), NVIDIA (CUDA), AMD (HIP, with RDNA4 support since 2.3.3) and Apple (Metal). It imports D3D12 buffers and fences, so the same DX12 engine can denoise on both of your PCs. It is not temporally stable (OIDN 3 adds temporal).
- **Real-time, 1 spp, cross-API (HLSL compute):** NVIDIA **NRD**.
- **Vendor-locked ML real-time denoisers:** **DLSS Ray Reconstruction** (NVIDIA RTX only, via Streamline) and **AMD FSR Ray Regeneration** (RX 9000 / RDNA4 only, DX12 SM6.6).
- **Learn-by-doing option:** **SVGF / A-SVGF**, which you implement yourself in HLSL.

### Cited Findings

**Intel Open Image Denoise (OIDN)**
- Releases (newest first):
  - v2.5.1 (18 Aug 2026): security fixes and a CUDA/HIP shared-buffer import leak fix.
  - v2.5.0 (02 Jun 2026): a new external semaphore import API for CUDA (Windows/Linux) and HIP (Windows only; SYCL planned), plus `OIDN_EXTERNAL_MEMORY_TYPE_FLAG_DEDICATED`.
  - v2.4.0 (Jan 2026): builds with CUDA 13 and ROCm 7, and "Removed support for NVIDIA Volta GPUs".
  - v2.3.3: "Added NVIDIA Blackwell GPU support" and **"AMD RDNA4 GPU support"**.
  - v2.3.1: "HIP LUID support, enabling DX12 and Vulkan buffer import on Windows with recent AMD drivers".
  - v2.3.0: a new "fast" quality mode and higher-quality HDR prefiltering.
  - — [OIDN GitHub releases](https://github.com/RenderKit/oidn/releases)
- Backends: CPU, SYCL (Intel GPUs via Level Zero), CUDA, HIP and Metal.
- Filters: `RT` (generic path tracing) and `RTLightmap`.
- Quality modes: `OIDN_QUALITY_HIGH` (final frame), `BALANCED` (interactive) and `FAST` (preview).
- Inputs: color plus optional albedo and normal, an `hdr` flag, `cleanAux`, and prefiltering of the auxiliary images.
- The RT filter is "not temporally stable".
- D3D12 interop: `oidnNewSharedBufferFromWin32Handle` with `OIDN_EXTERNAL_MEMORY_TYPE_FLAG_D3D12_RESOURCE` / `D3D12_HEAP`, and semaphores for D3D11/12 fences and keyed mutexes, with `oidnSignalSemaphoresAsync` / `oidnWaitSemaphoresAsync`.
- CPU devices cannot import external memory.
- — [OIDN documentation](https://www.openimagedenoise.org/documentation.html)
- OIDN 3 will add temporal denoising (announced Jan 30, 2026; CG Channel headline). — [CG Channel: OIDN 3 temporal](https://www.cgchannel.com/2026/01/open-image-denoise-3-will-support-temporal-denoising/) (the article returned 403 to the fetch tool; only the headline and date were seen)

**NVIDIA NRD (Real-time Denoisers)** — [NVIDIA-RTX/NRD](https://github.com/NVIDIA-RTX/NRD)
- Version v4.18.0. It is "an API-agnostic, spatio-temporal library" used in 15+ AAA titles.
- Denoisers: REBLUR (diffuse/specular, AO/SO, SH/SG variants), RELAX (A-trous, designed for RTXDI) and SIGMA (shadows), plus REFERENCE.
- Required inputs:
  - `IN_MV`: motion, 2.5D recommended, without jitter;
  - `IN_NORMAL_ROUGHNESS`: world normal plus linear roughness;
  - `IN_VIEWZ`;
  - radiance split into diffuse and specular at the primary hit, with normalized hit distances.
- APIs: D3D12, Vulkan and D3D11. NRD itself makes no API calls: the app records the compute dispatches, or uses the NRI-based integration layer (`NRDIntegration.h/.hpp`, `NRD_NRI=ON`). The HLSL helper is `NRD.hlsli`. It builds with CMake 3.30+.
- Target use: "primarily ... 1 path/pixel path tracing". It accumulates over a limited number of frames, so inputs must converge within that window and fireflies should be avoided. It is not designed for volumetrics or transparency. SHARC or NRC radiance caching is recommended.
- Cost on an RTX 4080 at 1440p: REBLUR diffuse+specular 2.55 ms, RELAX 3.25 ms, SIGMA 0.40 ms.
- Sample: NRD-Sample, whose `simplex` branch is recommended for path tracing.
- The README does not state AMD support explicitly. It is plain HLSL compute, so it should be vendor-neutral, but treat that as an inference.

**DLSS Ray Reconstruction (NVIDIA only)**
- Streamline guide (version 2.14.16). Required inputs:
  - diffuse albedo `kBufferTypeAlbedo` (linear, not sRGB);
  - specular albedo `kBufferTypeSpecularAlbedo` (the guide includes an `EnvBRDFApprox2` function to generate it);
  - normals (`kBufferTypeNormals` or `NormalRoughness`; view or world space, F16 or F32);
  - roughness;
  - noisy color `kBufferTypeScalingInputColor`;
  - motion vectors (RG16F or RG32F);
  - depth (linear or hardware);
  - specular motion vectors, or specular hit distance plus the view and projection matrices;
  - output `kBufferTypeScalingOutputColor`.
- Optional inputs: transparency layer, SSS guide and DoF guide.
- Support is checked per adapter with `slIsFeatureSupported`, which rejects "older or non-NVDA GPU".
- — [Streamline ProgrammingGuideDLSS_RR.md](https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideDLSS_RR.md)
- Integrate DLSS Super Resolution first, then RR. Disable other denoisers (NRD) and set the mip bias. RR needs linear depth (`kBufferTypeLinearDepth`). Release builds need a production application ID and ship `nvngx_dlssd.dll`. — [NVIDIA blog: integrate DLSS 4 with Streamline](https://developer.nvidia.com/blog/how-to-integrate-nvidia-dlss-4-into-your-game-with-nvidia-streamline); [NVIDIA forum: DLSS RR applicationId](https://forums.developer.nvidia.com/t/requesting-dlss-ray-reconstruction-applicationid-guidance-for-unity-hdrp-integration/384927)
- DLSS is "built for real-time (games)". OptiX is built for film workflows. — [NVIDIA forum (dhart, Jan 2026)](https://forums.developer.nvidia.com/t/how-is-optix-denoiser-comparing-nrd-and-dlss-which-one-is-best-for-offline-and-semi-realtime-denoise/357080)

**AMD FSR Ray Regeneration ("FSR Redstone")**
- An ML real-time denoiser, standalone from upscaling. The latest version is 1.2.1 (Oct 2026), which adds standalone AO and SO denoising.
- Input signals: AO, direct diffuse, direct specular, dominant light visibility, indirect diffuse, indirect specular and SO.
- Requires an "AMD Radeon RX 9000 Series (RDNA 4)" GPU, Windows 11 and DX12 SM6.6. NVIDIA GPUs are not listed.
- It ships in FSR SDK v2.4; source is at GPUOpen-LibrariesAndSDKs/FidelityFX-SDK.
- — [GPUOpen: AMD FSR Ray Regeneration](https://gpuopen.com/amd-fsr-rayregeneration/)
- FSR SDK 2.2 (Mar 2026) brought Ray Regeneration 1.1, which broke the ABI from 1.0, and requires Agility SDK 1.4.9+. — [GPUOpen: FSR SDK 2.2](https://gpuopen.com/learn/amd-fsr-sdk-2-2-now-available/); [CG Channel: FSR SDK 2.2](https://www.cgchannel.com/2026/03/amd-releases-fsr-sdk-2-2-with-new-ai-denoiser-ray-regeneration/)
- The Redstone launch came with Adrenalin 25.12.1 (Dec 2025) and is RDNA4-exclusive (RX 9060 XT / 9070 / 9070 XT). — [PCPer: RDNA4 gets FSR Redstone](https://pcper.com/2025/12/amds-rdna-4-cards-get-fsr-redstone-bringing-machine-learning-to-upscaling/)
- The FidelityFX SDK also lists a non-ML "Denoiser" (shadows and reflections). — [GPUOpen FidelityFX Denoiser](https://gpuopen.com/fidelityfx-denoiser/) (details not fetched)

**What works on the AMD RX 9070 XT PC**
- OIDN HIP: yes (RDNA4 since 2.3.3). — [OIDN releases](https://github.com/RenderKit/oidn/releases)
- OIDN CPU: yes, everywhere.
- NRD (HLSL): yes, by inference.
- FSR Ray Regeneration: yes.
- SVGF: yes.
- OptiX, CUDA and DLSS RR: no.

### Inferences
- For a student DX12 path tracer with **interactive accumulation** (camera still, spp grows), use OIDN `RT` (BALANCED or HIGH) or OptiX `AOV` on the accumulated buffer, every N frames or when the camera stops. This gives the best quality per effort, and OIDN covers both PCs with one code path (CUDA device on NVIDIA, HIP device on AMD, CPU fallback).
- For **moving-camera, 1-4 spp real-time**, NRD (cross-vendor, HLSL) or your own SVGF/A-SVGF is the portable choice. DLSS RR and FSR Ray Regeneration are each locked to one vendor, so a dual-PC developer would need both plus a fallback.
- SVGF (Schied et al. 2017) and A-SVGF (Schied et al. 2018) need only HLSL compute plus G-buffer moments. No vendor lock-in, and a good learning exercise. No source was fetched in this session; see the gap below.

### Gaps
- No head-to-head quality or timing data (OIDN vs OptiX at 1080p/1440p on the same GPU) from 2026 was found.
- OIDN HIP on Windows needs a recent AMD driver, but the exact Adrenalin version needed for D3D12 import on RX 9070 XT is not documented.
- OIDN 3 release date and API: only the announcement headline was seen.
- SVGF / A-SVGF paper URLs were not fetched in this session. The papers are "Spatiotemporal Variance-Guided Filtering" (HPG 2017) and "Gradient Estimation for Real-Time Adaptive Temporal Filtering" (HPG 2018).
- The NRD license terms and explicit AMD validation were not shown on the README page.

---

## 4. Learning CUDA: resources, toolkit version, Visual Studio integration, compute capability, tools

### Takeaway
Use **CUDA Toolkit 13.5** (Nov 2026). It supports **Visual Studio 2026 (18.x), 2022 (17.x) and 2019**. Since CUDA 13.1 the driver is no longer bundled, so install it separately. Use the "CUDA 13.5 Runtime" VS template or the `CUDA 13.5.props` build customization for `.cu` files, or CMake. The RTX 50 series is compute capability 12.0, RTX 40 is 8.9, RTX 30 is 8.6 and RTX 20 is 7.5. Core learning path: the CUDA Programming Guide (13.5) plus *Programming Massively Parallel Processors*, 4th ed. (2022). Profile with Nsight Systems (CUDA + D3D12 timeline), Nsight Compute (kernels) and Nsight Graphics (D3D12 frames).

### Cited Findings
- CUDA 13.5 for Windows:
  - Visual Studio support: VS 2026 18.x (MSVC 195x; C++14 default, C++17, C++20), VS 2022 17.x (MSVC 193x) and VS 2019 16.x. VS 2017 was dropped in 12.9.
  - Windows support: Windows 11 22H2-SV2 to 25H2 and Windows 10 22H2.
  - The `visual_studio_integration_13.5` subpackage installs the CUDA wizard and the `CUDA 13.5.props` build customization in `...\MSBuild\Microsoft\VC\v170\BuildCustomizations`. New projects use NVIDIA > CUDA > "CUDA 13.5 Runtime". In an existing project, enable Build Dependencies > Build Customizations and mark `.cu` files as "CUDA C/C++". `msbuild /p:CudaToolkitDir=` overrides the toolkit.
  - "Starting with CUDA 13.1, the NVIDIA GPU driver is no longer bundled with the toolkit."
  - — [CUDA Installation Guide for Windows (13.5)](https://docs.nvidia.com/cuda/cuda-installation-guide-microsoft-windows/index.html)
- Toolkit-to-driver mapping: CUDA 13.4 is R615, 13.3 R610, 13.2 R595, 13.1 R590 and 13.0 R580. 13.4 ships Nsight Visual Studio Edition 2026.3. — [CUDA 13.4 release notes](https://docs.nvidia.com/cuda/archive/13.4.1/cuda-toolkit-release-notes/index.html); [CUDA 13.2 Update 1 release notes](https://docs.nvidia.com/cuda/archive/13.2.1/cuda-toolkit-release-notes/index.html)
- Compute capability: 12.0 for RTX 5090/5080/5070 Ti/5070/5060 Ti/5060/5050, 8.9 for RTX 40, 8.6 for RTX 30 and 7.5 for RTX 20. — [NVIDIA CUDA GPUs](https://developer.nvidia.com/cuda-gpus)
- cuda-samples (CUDA 13.5) builds with CMake 3.20+. On Windows this generates a VS solution. — [NVIDIA/cuda-samples](https://github.com/NVIDIA/cuda-samples)
- *Programming Massively Parallel Processors: A Hands-on Approach*, 4th ed. (Hwu, Kirk, El Hajj), Elsevier, Aug 2022, 580 pp., ISBN 978-0-323-91231-0. It adds chapters on stencil, reduction and sorting, a GPU-architecture chapter (Ampere examples), unified memory and cuDNN. — [Elsevier](https://shop.elsevier.com/books/programming-massively-parallel-processors/hwu/978-0-323-91231-0)
- The CUDA Programming Guide v13.5 has been restructured, for example "04-special-topics/graphics-interop". — [CUDA Programming Guide 13.5: interop](https://docs.nvidia.com/cuda/cuda-programming-guide/04-special-topics/graphics-interop.html)
- Nsight Systems traces D3D12 API calls, GPU workloads, markers and frame durations. 2026.4.1 and 2026.5.1 added D3D12 CIG CUDA stream tracing. — [Nsight Systems release notes](https://docs.nvidia.com/nsight-systems/ReleaseNotes/); [Nsight Systems D3D trace (2020.3 docs)](https://docs.nvidia.com/nsight-systems/2020.3/tracing/index.html)
- Nsight Graphics GPU Trace on Blackwell and newer collects start and end timing for compute workloads from D3D12, Vulkan and CUDA. — [Nsight Graphics GPU Trace UI](https://docs.nvidia.com/nsight-graphics/UserGuide/gpu-trace-ui.html)
- Nsight Compute is the kernel-level profiler and has 2025-2026 release notes. — [Nsight Compute release notes](https://docs.nvidia.com/nsight-compute/ReleaseNotes/topics/updates-2025-1-1.html)

### Inferences
- **Build setup for a DX12 engine:**
  - Add one `.cu` file (the conversion kernels) to the existing .vcxproj via Build Customizations.
  - Link `cudart_static.lib`, plus `cuda.lib` if you use driver-API calls such as `cuCtxCreate` with CIG.
  - Add `$(CUDA_PATH)\include` and the OptiX SDK `include` directory.
  - Compile for `sm_86;sm_89;sm_120` plus `compute_120` PTX for forward compatibility.
  - OptiX denoiser calls are host-only C calls, so no `.cu` is needed just for the denoiser.
- CUDA 13.x dropped offline compilation for pre-Turing GPUs. OptiX 9.1 also requires Turing+, which matches the RTX 20+ targets. This is not independently verified in this session beyond OIDN's "Removed Volta" note and OptiX's "Turing and newer".
- **Learning order:**
  1. The CUDA Programming Guide programming-model chapters.
  2. PMPP chapters 1-6 (threads, memory, tiling), then reduction and scan.
  3. Write image-processing kernels against your shared D3D12 buffers (tonemap, a bilateral filter, an SVGF a-trous pass in CUDA vs HLSL) and profile both with Nsight.

### Gaps
- NVIDIA DLI "Accelerated Computing" course list and 2026 pricing were not fetched.
- GPU Gems is free online at developer.nvidia.com, but its URL was not verified in this session.
- "CUDA by Practice": no source was found for a resource with this exact name.

---

## 5. Where CUDA fits in a DX12 path tracer; OptiX-traced vs DXR-traced; the AMD story

### Takeaway
In a DX12 engine that must also run on an AMD RX 9070 XT, keep tracing in **DXR/HLSL**. Use CUDA only as an **optional NVIDIA-only add-on** behind an interface: the OptiX denoiser (or OIDN-CUDA). On AMD, use OIDN-HIP or CPU, or an HLSL denoiser. Moving tracing into OptiX would make the whole renderer NVIDIA-only. ZLUDA does not run OptiX on Windows and is a hobby project again, and HIP RT is a separate AMD API. NRC is available as an HLSL/RTXGI SDK, so tiny-cuda-nn is not required.

### Cited Findings
- OptiX and CUDA run only on NVIDIA GPUs (OptiX: "Turing and newer generation GPUs"). — [NVIDIA OptiX Downloads](https://developer.nvidia.com/designworks/optix/download)
- ZLUDA:
  - ZLUDA v6 (Jun 29, 2026) added experimental 32-bit PhysX on Radeon and texture support enabling Blender. Commercial funding ended and development is back to "a weekend project". — [Phoronix: ZLUDA 6 released](https://phoronix.com/news/ZLUDA-6-Released); [Tom's Hardware: ZLUDA loses funding with v6](https://www.tomshardware.com/pc-components/gpu-drivers/cuda-emulator-for-amd-gpus-zluda-loses-funding-with-v6-release-embattled-project-goes-back-to-hobby-status-but-now-includes-32-bit-physx-support)
  - ZLUDA 7-preview.11 (Sep 2026) added some cuFFT APIs on top of hipFFT. — [Phoronix: ZLUDA cuFFT via hipFFT](https://www.phoronix.com/news/ZLUDA-cuFFT-APIs-hipFFT)
  - ZLUDA added CUDA 13.1 compatibility. — [Phoronix: ZLUDA CUDA 13.1](https://phoronix.com/news/zluda-cuda-13.1-compatibility)
  - ZLUDA's OptiX support was historically a "proof of concept", described as "buggy, unoptimized and incomplete", not built by default and Linux-only. Coverage of the open-sourced code said OptiX apps "just won't work". — [ZLUDA releases](https://github.com/vosen/ZLUDA/releases); [Phoronix: AMD-funded ZLUDA open-sourced](https://phoronix.com/review/radeon-cuda-zluda)
- HIP RT runs on AMD and NVIDIA GPUs. 3.0.9 added RDNA4 features (compressed BVH8, triangle packets). 3.1.0 is dated Jan 5, 2026, and 3.2.0 is a pre-release from Oct 2026 (CUDA 13 support, Medusa iGPU IDs). Blender Cycles uses HIP RT by default on RDNA2+ since Blender 5.1. — [GPUOpen HIP RT](https://gpuopen.com/hiprt/); [PCGH: HIP RT 3.2](https://www.pcgameshardware.de/CPU-CPU-154106/News/AMD-Zen-6-Medusa-HIP-RT-RDNA-4m-RDNA-3-5-1557741/); [noobfeed: HIP RT 3.2.0](https://www.noobfeed.com/hardware/amd-hip-rt-rdna-4m-rdna-medusa-apus)
- Neural Radiance Cache:
  - NRC ships in RTXGI SDK 2.0 (DX12 and Vulkan samples, "currently experimental"). It requires DXR 1.1+ and RTX 20xx or later. The standalone NVIDIA-RTX/NRC repo (Bin/Include/Lib) points to `Docs/NrcGuide.md` in RTXGI. — [NVIDIA RTXGI 2.0 getting started](https://developer.nvidia.com/rtxgi-getting-started); [NVIDIA-RTX/NRC](https://github.com/NVIDIA-RTX/NRC)
  - Microsoft is adding Cooperative Vectors to DirectX/HLSL for in-shader neural networks. — [NVIDIA blog: neural rendering at GDC 2025](https://developer.nvidia.com/blog/nvidia-rtx-advances-with-neural-rendering-and-digital-human-technologies-at-gdc-2025)
- NRD recommends SHARC or NRC radiance caching for better quality and disocclusion behavior. — [NVIDIA-RTX/NRD](https://github.com/NVIDIA-RTX/NRD)
- Mixing ecosystems has friction: using DLSS inside an OptiX application is "difficult". — [NVIDIA forum (dhart)](https://forums.developer.nvidia.com/t/how-is-optix-denoiser-comparing-nrd-and-dlss-which-one-is-best-for-offline-and-semi-realtime-denoise/357080)

### Inferences

**Trade-off A: trace in OptiX/CUDA and display via D3D12**
- Pros:
  - OptiX's mature path-tracing samples, callable programs and shader binding table;
  - CUDA C++ for the whole renderer (templates, pointers, printf, Nsight Compute);
  - the denoiser sits in the same API with no interop for tracing;
  - easy porting of research code such as tiny-cuda-nn.
- Cons:
  - NVIDIA-only, so the RX 9070 XT PC cannot run it;
  - two scene representations (D3D12 meshes and OptiX GAS/IAS), or OptiX owns the scene;
  - interop is needed every frame for the output image, and for any rasterized G-buffer if mixed;
  - DLSS RR, NRD and FSR need D3D12-side inputs;
  - it duplicates engine features.

**Trade-off B: trace in DXR, denoise in CUDA**
- Pros:
  - DXR runs on both PCs;
  - the only interop is a few full-screen buffers and one fence;
  - the denoiser is swappable: OptiX on NVIDIA, OIDN on either vendor, NRD or SVGF everywhere.
- Cons:
  - per-frame copies from textures to linear buffers, and cross-API sync latency (typically a fraction of a ms; not measured here);
  - a CUDA dependency for NVIDIA builds.
- For a student on two vendor PCs, **B is the clearly better fit**.

**Where CUDA earns its place:**
1. the OptiX denoiser;
2. OIDN through its CUDA backend (the same API also runs HIP on AMD);
3. offline or precompute tools, such as spectral upsampling tables (Jakob-Hanika style) and BRDF/LUT baking, where portability doesn't matter and results are saved to files that HLSL then loads;
4. learning experiments (tiny-cuda-nn NRC research).

**Where HLSL wins:** real-time post-processing (tonemap, bloom, TAA), SVGF/NRD, and NRC via the RTXGI SDK. All of these run on AMD and avoid the sync overhead.

**Recommended architecture:**
- An `IDenoiser` interface with implementations `OptixDenoiser` (NVIDIA only), `OidnDenoiser` (CUDA, HIP or CPU device picked at runtime by LUID), `HlslSvgf`/`Nrd` (all vendors) and an `Accumulate-only` fallback.
- Build CUDA and OptiX as an optional DLL loaded at runtime, so the AMD PC runs without CUDA installed.
- Detect the vendor through `DXGI_ADAPTER_DESC::VendorId` (0x10DE for NVIDIA, 0x1002 for AMD). This is standard DXGI knowledge, not verified in this session.

### Gaps
- No measured cost of D3D12 to CUDA fence round-trips or of texture-to-buffer copies at 1440p was found.
- Whether the NRC library uses CUDA internally (and so is NVIDIA-only at runtime) is not stated on its README. RTXGI lists RTX 20xx+ as a requirement.
- The current ZLUDA Windows status for OptiX in 2026 was not confirmed. No source says OptiX works; the historical sources say it does not on Windows.
- Spectral tables, and whether CUDA gives any advantage over HLSL compute for them: no source; this is an opinion and inference only.
