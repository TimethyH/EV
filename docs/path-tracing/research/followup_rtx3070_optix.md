# RTX 3070 follow-up: NVIDIA-specific facts for the DX12 path tracer (OptiX, CUDA, tiny-cuda-nn, NRC, DXR 1.2, VRAM)

Research date: 2026-11-18. Target: RTX 3070 (Ampere GA104, compute capability 8.6, 8 GB), Windows 11, used for CUDA/OptiX builds and comparisons next to the main RX 9070 XT PC. This builds on `optix_cuda_interop.md`, `neural_radiance_cache.md` and `industry_realtime_path_tracing.md`; items already verified there are referenced, not redone.

Access note: raytracing-docs.nvidia.com (OptiX Programming Guide) still fails TLS verification from the fetch tool ("unable to verify the first certificate"), so the Programming Guide text could not be read in this session either. Raw GitHub sources (optix-dev, optix-sdk, cuda-samples, tiny-cuda-nn, RTXGI) all fetched fine.

Driver floors that matter for the 3070, collected in one place (each cited below): OptiX 9.1 needs R590+; DXR 1.2 / SM 6.9 retail needs 595+; CUDA 13.5 new features need R620+; Nsight Graphics 2026.4 needs R620+. **So the practical floor for the 3070 PC is an R620-branch driver.**

---

## 1. OptiX 9.1 denoiser on Ampere: support and timings

### Takeaway
Ampere is supported: OptiX 9.1 requires "Turing and newer" GPUs and an R590+ driver, and the RTX 3070 is Ampere. There are no published per-GPU tables. NVIDIA staff say "low single-digit millisecond" denoising at 1080p is common on current GPUs. The only RTX 3070-specific numbers are for tiny images (about 0.5 ms per call at 32x32/64x64, HDR model). No UPSCALE2X or TEMPORAL_AOV timings for RTX 30 at 1080p or 1440p were found, and no official VRAM figures either. Measure both yourself after warm-up.

### Cited Findings
- OptiX system requirements: GeForce/RTX/Quadro/Data Center "with Turing and newer generation GPUs", R590+ driver (this covers Ampere/RTX 3070). — [NVIDIA OptiX Downloads](https://developer.nvidia.com/designworks/optix/download); 9.1.0 needs R590+ per [NVIDIA/optix-sdk releases](https://github.com/NVIDIA/optix-sdk/releases) (both verified in the earlier note `optix_cuda_interop.md`)
- droettger (NVIDIA), Jan 1 2024: "low single-digit millisecond results for a 1920x1080 noisy input image are common on current GPUs". Performance depends on "the underlying GPU and the display driver version". There is no per-model, per-GPU performance table. He lists the models as "HDR (outdated), AOV (recommended) or temporal AOV". — [NVIDIA forum: time to denoise a 1080p image](https://forums.developer.nvidia.com/t/how-much-time-does-it-takes-for-nvidia-ai-accelerated-denoiser-onnd-to-denoise-a-1080p-image/277342)
- In the same thread, the original poster (Dec 2023, OptiX 7.5, GPU not stated) measured "about 5 ms" at 1080p. Papers they had read claimed 50-100 ms. — [same thread](https://forums.developer.nvidia.com/t/how-much-time-does-it-takes-for-nvidia-ai-accelerated-denoiser-onnd-to-denoise-a-1080p-image/277342)
- RTX 3070-specific data (user80183, Sep 2023):
  - Setup: RTX 3070, OptiX 7.6, driver 535.86.05, CUDA 12.2, Ubuntu, HDR model with FLOAT3 input.
  - A single 32x32 or 64x64 image takes "approximately 0.5ms"; 100 parallel calls take "closer to 6ms".
  - The AOV model was "slightly slower than HDR but needed fewer samples".
  - HALF4 gave no significant speedup in that test.
  - The poster's Windows RTX 3080 machine was slower than the Linux RTX 3070 one; the poster suspected a CPU bottleneck.
  - — [NVIDIA forum: fast denoising of many low-res images](https://forums.developer.nvidia.com/t/fast-denoising-of-high-number-of-low-res-images/266255)
- droettger, same thread:
  - "the OptiX denoisers are using half internally", so HALF4 input may be faster.
  - "there is still some fixed overhead in the denoising invocation".
  - Invoke calls "should run fully asynchronously to the CPU".
  - Inputs smaller than 256x256 scale poorly because they don't saturate the GPU.
  - Use at most about 8-16 CUDA streams.
  - Gains come from drivers and models more than from SDK versions.
  - dhart (NVIDIA) recommends Nsight Systems to compare kernel times and CPU gaps.
  - — [same thread](https://forums.developer.nvidia.com/t/fast-denoising-of-high-number-of-low-res-images/266255)
- Max Liani (renderer developer) on X: the OptiX denoiser takes about 3 ms at 1080p vs 14 ms for his CUDA port of OIDN. The GPU is not named, and this was seen only as a search snippet (not fetched). — [X post](https://x.com/maxliani/status/1631474374749347840)
- The first call has a large one-off overhead (1200-1500 ms reported on a GTX 1080), so time only after warm-up. — [NVIDIA forum: denoising benchmarks GTX 1080](https://forums.developer.nvidia.com/t/denoising-benchmarks-with-geforce-gtx-1080/66674) (from the earlier note)
- Denoiser VRAM: no official numbers. Only anecdotes were found:
  - a Daz3D user saw roughly 250 MB more VRAM once the denoiser started, and advised keeping about 1 GB of headroom — [Daz3D forum](https://www.daz3d.com/forums/discussion/comment/4933181);
  - one OptiX error log asked for "at least 33179143 bytes" (about 33 MB) of scratch at an unstated resolution — [Arnold answers](https://answers.arnoldrenderer.com/questions/5800/optix-denoiser-error-when-rendering.html?sort=votes);
  - Blender Cycles allocates `[denoiser state][scratch]` as one buffer sized from `optixDenoiserComputeMemoryResources` and tiles very large images, because the denoiser has an upper pixel limit — [Blender commit rB1766549](https://developer.blender.org/rB17665494186816cebb9e8304199e40f9ee033990). These come from search summaries; the pages were not fetched individually.
- The SDK `optixDenoiser` sample picks the model as `upscale2x ? (temporal ? TEMPORAL_UPSCALE2X : UPSCALE2X) : (temporal ? TEMPORAL_AOV : AOV)`. It uses `withoutOverlapScratchSizeInBytes` when not tiling and `withOverlapScratchSizeInBytes` plus `overlapWindowSizeInPixels` when tiling, and calls `optixDenoiserSetup(tileW + 2*overlap, tileH + 2*overlap, ...)`. — [optix-sdk SDK/optixDenoiser/OptiXDenoiser.h](https://raw.githubusercontent.com/NVIDIA/optix-sdk/main/SDK/optixDenoiser/OptiXDenoiser.h)

### Inferences
- Budget about 2-6 ms for AOV at 1080p on a 3070. 1440p has 1.78x the pixels, so roughly 4-10 ms if cost scales linearly. These are extrapolations from the "low single-digit ms on current GPUs" statement (the 3070 is an older mid-range card), not measurements. That fits "denoise when the camera stops / every N frames", not a 60 fps per-frame budget on top of path tracing.
- UPSCALE2X outputs 4x the pixels, so expect it to cost more than AOV at the same input size. This is unverified.
- Use HALF4 staging buffers, since the denoiser uses half internally. That halves interop buffer VRAM compared with Falcor's FLOAT4. The one user test showed no speed gain, so the reason is memory, not speed.
- Log `OptixDenoiserSizes` (state + scratch + internal guide layers) at 1080p and 1440p on the first run. That is the only reliable VRAM figure.

### Gaps
- No published UPSCALE2X or TEMPORAL_AOV timings on any RTX 30 card at 1080p or 1440p, and no Blender/Cycles per-GPU denoise-time benchmark was found.
- No official denoiser VRAM figure at 1080p.

---

## 2. Normal space for the AOV / TEMPORAL_AOV / UPSCALE2X guide normals

### Takeaway
Definitive current answer, from the current OptiX header (`optix-dev` main): the normal guide is **(X, Y, Z) world space for all models except the legacy LDR/HDR**, which take (X, Y) camera space. NVIDIA staff (droettger) confirmed this in 2023: world space is recommended on R535+ drivers, and camera space is still accepted by the newer models. The `optixDenoiserInvoke` comment in `optix_host.h` still says temporal models need "3d vectors in camera space". Treat that as a stale comment that predates the R535 change. The SDK sample does no transformation, which is consistent with world space. **Recommendation: pass world-space normals.** No current source gives a camera-space sign convention. Older guidance (2018, OptiX 5) was to transform world normals "by the inverse camera orientation", and a search summary (not fetched) mentions "right-handed, looking down negative z".

### Cited Findings
- `optix_types.h` (optix-dev main), `OptixDenoiserGuideLayer::normal`:
  - "image with two or three components: X, Y, Z."
  - "(X, Y) camera space for OPTIX_DENOISER_MODEL_KIND_LDR, OPTIX_DENOISER_MODEL_KIND_HDR models."
  - "(X, Y, Z) world space, all other models."
  - The `flow` field is "pixel movement from previous to current frame for each pixel in screen space".
  - — [optix_types.h](https://raw.githubusercontent.com/NVIDIA/optix-dev/main/include/optix_types.h)
- `optix_host.h` (optix-dev main), `optixDenoiserInvoke` comment:
  - for temporal models, "the normal vector guide image must be given as 3d vectors in camera space";
  - also "the x and y channels are used and other channels are ignored";
  - the flow is "the flow from the previous to the current frame (a 2d vector in pixel space)";
  - "camera" appears only once in the file, and "world space" never.
  - — [optix_host.h](https://raw.githubusercontent.com/NVIDIA/optix-dev/main/include/optix_host.h)
- droettger (NVIDIA), Aug 11 2023:
  - "only the two old modes OPTIX_DENOISER_MODEL_KIND_LDR and OPTIX_DENOISER_MODEL_KIND_HDR require camera space normals";
  - "All other denoiser models will still work with camera space normal vectors as well";
  - "it's recommended to use world space normal vectors as denoiser input on current drivers (R535 and newer)";
  - "We're going to update the OptiX Programming Guide about that";
  - "The OptiX API reference documentation is a little sparse in the OptixDenoiserGuideLayer struct".
  - The OptiX 8.0 release notes, as quoted by the thread starter, say: "Camera space normals still work, but for better results world space normal should be used".
  - — [NVIDIA forum: OptiX 8.0 denoiser camera vs world space](https://forums.developer.nvidia.com/t/optix-8-0-denoiser-camera-space-vs-world-space/262875)
- The SDK `optixDenoiser` sample (optix-sdk main) assigns `guideLayer.normal = inData.normal` with no space transform. Its files are `OptiXDenoiser.h`, `optixDenoiser.cpp`, `OptiXDenoiserUtil.h`, `applyflow.h`, `README.TXT`, `motiondata/` and `data/`. The README shows a temporal run with `-n motiondata/soane-Normal-+++.exr` but does not say what space those normals are in. — [OptiXDenoiser.h](https://raw.githubusercontent.com/NVIDIA/optix-sdk/main/SDK/optixDenoiser/OptiXDenoiser.h); [optix-sdk SDK/optixDenoiser](https://github.com/NVIDIA/optix-sdk/tree/main/SDK/optixDenoiser)
- Falcor converts normals to camera space with `transpose(inverse(viewMatrix))`. Its code predates or ignores the R535 recommendation. — [Falcor OptixDenoiser.cpp](https://raw.githubusercontent.com/NVIDIAGameWorks/Falcor/master/Source/RenderPasses/OptixDenoiser/OptixDenoiser.cpp) (from the earlier note)
- Historical camera-space convention: in 2018 droettger edited a post to say the denoiser expects camera-space normals of the primary hit, and a reply says world normals must be "transformed by the inverse camera orientation". A search summary of older threads also cites the OptiX 7 docs as describing camera space as "right-handed, looking down negative z" (not fetched). — [NVIDIA forum: denoiser input buffer questions (2018)](https://forums.developer.nvidia.com/t/optix-denoiser-input-buffer-questions/60034); [NVIDIA forum: normal input makes no sense](https://forums.developer.nvidia.com/t/bugreport-optix-ai-denoiser-normal-input-makes-no-sense/115132)
- The normal guide only takes effect together with the albedo guide (forum statement relayed by a search summary). — [NVIDIA forum: denoiser input buffer questions](https://forums.developer.nvidia.com/t/optix-denoiser-input-buffer-questions/60034)

### Inferences
- Header `optix_types.h` (the struct definition, explicitly per-model) + staff statement + release-note wording outweigh the single invoke comment in `optix_host.h`. So for AOV, TEMPORAL_AOV, UPSCALE2X and TEMPORAL_UPSCALE2X, use **world-space XYZ** normals at the primary hit (FLOAT3/HALF3 or the xyz of HALF4).
- If you ever choose camera space (for example to match Falcor), it should be the view-space rotation only: right-handed, -Z forward, which matches the OptiX/OpenGL convention. Note that a D3D left-handed view space (+Z forward) would flip Z. This is unverified.
- Write unit-length normals. No source says normalization is required, but it is the safe choice.

### Gaps
- The OptiX 9.1 Programming Guide denoiser text could not be read (TLS failure again), so it is not confirmed whether NVIDIA ever applied the promised guide update.
- No 2024-2026 NVIDIA staff post specifically about normal space for TEMPORAL_AOV or UPSCALE2X was found.

---

## 3. tiny-cuda-nn on the RTX 3070 (sm_86)

### Takeaway
FullyFusedMLP at 64 neurons fits easily on CC 8.6. From the source formulas, the forward kernel needs about 20 KB of dynamic shared memory per block at WIDTH 64 (about 38 KB at WIDTH 128). CC 8.6 allows 99 KB per block (opt-in above 48 KB) and 100 KB per SM. The README's "RTX 3090 / 2080 Ti or higher" warning cannot be about a 3070-vs-3090 difference, because both are CC 8.6 with identical per-SM shared memory. CUDA 13 is handled in tiny-cuda-nn's CMake (minimum arch 75). No CUDA 13 or VS 2026 breakage reports were found, but the README still only lists VS 2019/2022.

### Cited Findings
- README warning: FullyFusedMLP "requires a very large amount of shared memory in its default configuration" and "will likely only work on an RTX 3090, an RTX 2080 Ti, or higher-end GPUs". Lower-end cards "must reduce the `n_neurons` parameter or use the `CutlassMLP`" (better compatibility, slower). Requirements:
  - Windows: VS 2019 or 2022, CUDA 11.5+;
  - Linux: CUDA 10.2+;
  - CMake 3.21+ and a C++17 compiler;
  - the benchmark figure was measured on an RTX 3090.
  - — [tiny-cuda-nn README](https://raw.githubusercontent.com/NVlabs/tiny-cuda-nn/master/README.md)
- Source (`src/fully_fused_mlp.cu`):
  - constants: `SKEW = WIDTH % 16 == 0 ? 8 : 0`, `N_ITERS = WIDTH >= 256 ? 2 : 8`;
  - forward: `shmem_size = sizeof(__half) * (16 + 16 * N_ITERS) * (WIDTH + SKEW)`, enlarged to at least `sizeof(__half) * (WIDTH + 16) * (in_width + INPUT_SKEW)` when the input is wide;
  - backward: `sizeof(__half) * ((16 * N_ITERS) * (WIDTH + SKEW))`;
  - shared memory is requested with `cudaFuncSetAttribute(..., cudaFuncAttributeMaxDynamicSharedMemorySize, shmem_size)`;
  - instantiated widths: 16, 32, 64, 128;
  - there is no compute-capability check in the file; it uses WMMA;
  - the error text is "FullyFusedMLP: insufficient shared memory available on the GPU ... Reduce `n_neurons` or use `CutlassMLP`";
  - only `__half` precision; at least 1 hidden layer.
  - — [fully_fused_mlp.cu](https://raw.githubusercontent.com/NVlabs/tiny-cuda-nn/master/src/fully_fused_mlp.cu)
- CC 8.6 limits (CUDA 13.5 Programming Guide):
  - 100 KB maximum shared memory per SM, and 99 KB per thread block with opt-in (above 48 KB needs opt-in);
  - carveout options 0/8/16/32/64/100 KB out of a 128 KB unified data cache;
  - 1536 resident threads (48 warps) per SM and 64K 32-bit registers per SM;
  - for comparison, CC 8.0 (A100) allows 164 KB per SM and 163 KB per block;
  - CC 8.6 is a column in the 13.5 tables, so it is still supported.
  - (Values matched by column position from the extracted table; worth checking against the rendered page.)
  - — [CUDA Programming Guide: compute capabilities](https://docs.nvidia.com/cuda/cuda-programming-guide/05-appendices/compute-capabilities.html)
- tiny-cuda-nn `CMakeLists.txt`:
  - architectures come from the env var `TCNN_CUDA_ARCHITECTURES`, else the CMake variable, else autodetection, else the fallback "75;86";
  - `if (CUDA_VERSION VERSION_GREATER_EQUAL 13.0)` sets the minimum arch to 75 (50 for CUDA 12.x);
  - archs below the minimum use `message(ERROR ...)`, which is not a real CMake mode, so it may not stop the build;
  - fully fused MLPs are built only when `MIN_GPU_ARCH GREATER 70`, otherwise "Falling back to CUTLASS MLPs";
  - MSVC flags: `/MP /Zc:preprocessor`, and nvcc gets `-Xcompiler=/bigobj -Xcompiler=/Zc:preprocessor`;
  - C++17;
  - CUTLASS is a git submodule at `dependencies/cutlass` (`git submodule update --init --recursive`);
  - the file has no `-allow-unsupported-compiler`.
  - — [tiny-cuda-nn CMakeLists.txt](https://raw.githubusercontent.com/NVlabs/tiny-cuda-nn/master/CMakeLists.txt)
- Known Windows build failures, all with older toolchains:
  - "No CUDA toolset found": fixed by copying the CUDA MSBuild customization files into the VS Build Tools directory — [issue #164](https://github.com/NVlabs/tiny-cuda-nn/issues/164);
  - nvcc rejecting a newer MSVC (`C1189 unsupported Microsoft Visual Studio version`; the workaround `-allow-unsupported-compiler` is risky) — [issue #169](https://github.com/NVlabs/tiny-cuda-nn/issues/169), [nerfstudio #3171](https://github.com/nerfstudio-project/nerfstudio/issues/3171);
  - "Unknown compute capability": set `TCNN_CUDA_ARCHITECTURES` — [issue #200](https://github.com/NVlabs/tiny-cuda-nn/issues/200).
  - (From search summaries.)
- Related CUDA 13 evidence: OIDN issue "Build fails with CUDA 13, which removed support for GPU architectures prior to sm_75" (Sep 2025), fixed in OIDN 2.4.0. — [RenderKit/oidn #274](https://github.com/RenderKit/oidn/issues/274)
- An RTX 3070 has been used with tiny-cuda-nn in published work: about 3 ms evaluation and about 10 ms per training step at 1280x720 with a width-64 MLP (cited in the earlier note). — see `neural_radiance_cache.md` line ~362

### Inferences
- Worked numbers for a 3070:
  - WIDTH 64 forward: 2 x 144 x 72 = 20,736 B (about 20 KiB). Backward: 2 x 128 x 72 = 18,432 B. Input staging at in_width 64 is about 11.5 KB (INPUT_SKEW assumed 8).
  - WIDTH 128 forward: 2 x 144 x 136 = 39,168 B.
  - All are far below the 99 KB per-block limit. **FullyFusedMLP at 64 (and even 128) should run on sm_86**; shared memory only limits occupancy (about 4 blocks per SM at 64).
  - The README warning dates from an early configuration and is conservative. This is an inference: no issue thread that names the 3070 was found.
- CutlassMLP is the fallback if a config with a very wide encoding output (large `in_width`) raises the shared-memory error.
- Build: set `TCNN_CUDA_ARCHITECTURES=86` explicitly; autodetect works too, but explicit avoids the "Unknown compute capability" class of errors. Use CUDA 13.5 with VS 2022 (17.x), which the README lists. VS 2026 should work with nvcc 13.5, since the CUDA 13.5 install guide lists VS 2026 18.x (see `optix_cuda_interop.md`), but tiny-cuda-nn has not documented it. Keep VS 2022 installed as the fallback host compiler.

### Gaps
- No GitHub issue was found that mentions the RTX 3070/3060/3080 or sm_86 with FullyFusedMLP failures or successes. The issue tracker could not be searched directly.
- No reported build of tiny-cuda-nn with CUDA 13.x + VS 2026 was found, for or against.

---

## 4. NVIDIA NRC SDK (RTXGI 2.9.0, NRC v0.16.x) on Ampere

### Takeaway
NRC needs "NV GPUs >= Turing" with Tensor Cores, so the RTX 3070 qualifies. Nothing in the RTXGI README, changelog or NRC guide mentions an Ampere caveat. **The "driver >= 617.42" floor is not in the current RTXGI README, changelog or NRC guide.** 617.42 exists as a GeForce Game Ready driver (Oct 6, 2026), but no source ties it to NRC. Treat it as unverified, most likely a search-snippet artefact. Use an R620+ driver anyway for CUDA 13.5 and Nsight.

### Cited Findings
- RTXGI README prerequisites:
  - "Any DXR GPU for SHaRC";
  - NRC: "NV GPUs ≥ Turing (arch 70)";
  - CMake 4.5.0, Git LFS, Vulkan SDK 1.4.363.1, VS 2022, Windows SDK >= 10.0.20348.0;
  - NRC needs Tensor Cores and is "currently experimental";
  - **no minimum driver version is stated on the page**.
  - — [NVIDIA-RTX/RTXGI](https://github.com/NVIDIA-RTX/RTXGI) (the raw `main/README.md` URL returned 404, so the rendered page was read)
- RTXGI changelog:
  - latest RTXGI 2.9.0 updates the Agility SDK to v1.619.6, with no driver or architecture notes;
  - the latest NRC entry is in 2.8.0: "Update to v0.16.0. to introduce Windows on Arm64 support";
  - older NRC versions: 0.14.1.0 (2.4.0), 0.13.4.0 (2.3.2), 0.13.3.0 (2.3.1), 0.13.2.0 (2.3.0), 0.12.1 (2.2.0);
  - 2.2.0 moved to CUDA Toolkit 12.5.1;
  - 2.1.0 contained an internal NRC fix "to run on NVIDIA 20xx series GPUs";
  - no Ampere mention.
  - — [RTXGI Changelog.md](https://raw.githubusercontent.com/NVIDIA-RTX/RTXGI/main/Changelog.md)
- NRC guide:
  - uses "Tensor Cores to train the neural network each frame";
  - ships `NRC_D3D12.dll`, `NRC_Vulkan.dll` and "CUDA 13 runtime dlls" for x64 and arm64;
  - "currently in experimental mode";
  - memory estimate: 191.793 MB total at 1920x1080 with a 274x154 training resolution;
  - no driver minimum and no Cooperative Vectors mention.
  - — [NrcGuide.md](https://raw.githubusercontent.com/NVIDIA-RTX/RTXGI/main/Docs/NrcGuide.md)
- The standalone NVIDIA-RTX/NRC repo (Bin/Include/Lib, License.md, Readme.md, 13 commits, no releases) lists no GPU or driver requirements and defers to the RTXGI changelog. — [NVIDIA-RTX/NRC](https://github.com/NVIDIA-RTX/NRC)
- GeForce 617.42 WHQL was released Oct 6, 2026, a game-ready driver (CoD Modern Warfare 4 and others; fixes an Assassin's Creed Shadows crash). A hotfix 617.70 followed. No mention of NRC/RTXGI was found in coverage. — [Guru3D 617.42](https://www.guru3d.com/download/nvidia-geforce-61742-whql-driver-download/); [mixed-news](https://mixed-news.com/en/?p=35772); [Guru3D hotfix 617.70](https://www.guru3d.com/publish/comments/geforce-hotfix-driver-61770-download/)

### Inferences
- Because NRC ships CUDA 13 runtime DLLs, the driver must support CUDA 13.x. The CUDA 13.x minor-version-compatibility floor is R580 (see section 6). That is the only driver floor derivable from sources. An R620 driver covers it.
- "arch 70" in the README is odd, since Turing is CC 7.5. It is probably shorthand and does not affect the 3070 (CC 8.6).
- The 3070 has 184 third-generation Tensor Cores; the per-frame NRC training cost will be higher than on RTX 40/50 cards. No NRC timing for RTX 30 was found except the changelog note "FPS saw a 2% improvement at 1080p on 3060" after a parameter tune (in `neural_radiance_cache.md`).

### Gaps
- No primary source for any NRC driver minimum. The original "617.42" snippet could not be reproduced.
- No Ampere-specific NRC timing data.

---

## 5. DXR 1.2 (SER, OMM) on the RTX 3070

### Takeaway
**SER:** RTX 20/30 accept the SER API, but "the reorder step is a no-op on these architectures". The retail table lists only RTX 40+ as "support API and do reordering". Apps can check `D3D12_FEATURE_DATA_D3D12_OPTIONS22::ShaderExecutionReorderingActuallyReorders`, which should read FALSE on a 3070. **OMM:** supported on "All RTX hardware"; hardware-accelerated on RTX 40+ and "software-emulated on older", so emulated on the 3070. **Driver:** "595 and newer" for SM 6.9 retail (Agility SDK 1.619, Feb 26 2026).

### Cited Findings
- SM 6.9 retail post (Feb 26 2026), appendix table, NVIDIA column:
  - OMM: "All RTX hardware. Hardware-accelerated on RTX 4xxx+ GPUs, software-emulated on older";
  - SER: "RTX 4xxx+ GPUs support API and do reordering";
  - "Driver version 595 and newer";
  - apps can now check whether a device actually reorders.
  - — [Microsoft DirectX blog: SM 6.9 retail](https://devblogs.microsoft.com/directx/shader-model-6-9-retail-and-more/)
- SER preview post (May 30 2025): "SER is already accelerated on all NVIDIA GeForce RTX™ 40 and 50 Series GPUs". "NVIDIA GeForce RTX™ 20 and 30 Series GPUs also support SER"; "the reorder step is a no-op on these architectures" with "no performance downside to enabling it". — [Microsoft DirectX blog: D3D12 SER](https://devblogs.microsoft.com/directx/ser/)
- The cap field is `ShaderExecutionReorderingActuallyReorders` in `D3D12_FEATURE_DATA_D3D12_OPTIONS22`, queried with `CheckFeatureSupport`. OMMs are added with DXR Tier 1.2 (`D3D12_RAYTRACING_TIER_1_2`). — [DirectX-Specs: Raytracing](https://microsoft.github.io/DirectX-Specs/d3d/Raytracing.html)
- Press summary: driver 595+, and NVIDIA was the only vendor with OMM driver support at launch. — [Wccftech](https://wccftech.com/microsoft-shader-model-6-9-dxr-1-2-dx12-enhancements-agility-sdk-1-619/) (search summary)

### Inferences
- On the 3070, SER cannot be used to measure SER speed-ups. Any SER gain has to be measured on an RTX 40/50 card. For the dual-PC student, SER is a no-op on both the 3070 and the RX 9070 XT (the AMD no-reorder status is in the earlier note). It remains worth coding (harmless, future-proof), but neither machine will show a benefit.
- Emulated OMM on Ampere means OMM results on the 3070 will not represent RTX 40+ gains. It may even be slower than a plain any-hit alpha test. That is not measured, so benchmark both paths.
- Report `ShaderExecutionReorderingActuallyReorders` and the raytracing tier in the app's startup log on both PCs.

### Gaps
- No source states whether emulated OMM on RTX 20/30 is faster or slower than any-hit alpha testing.
- No RTX 30-specific minimum driver beyond the general "595 and newer".

---

## 6. CUDA 13.5 on Ampere: arch flags, VS integration, PTX JIT, Nsight

### Takeaway
CUDA 13.5 still fully supports sm_86. CUDA 13.0 removed offline compilation for Maxwell, Pascal and Volta, so the minimum is sm_75. New 13.5 features need an R620+ driver; the 13.x minor-version floor is R580. VS 2026, 2022 and 2019 are supported (from the earlier note). Bundled tools: Nsight Compute 2026.4.0, Nsight Systems 2026.5.2, Nsight VSE 2026.4.0. Nsight Graphics 2026.4 needs R620+ and only announces Turing removal, not Ampere, so the 3070 is fully covered by the toolchain.

### Cited Findings
- CUDA 13.5 release notes:
  - "CUDA 13.5 new features and newly enabled platforms require an R620 or later driver";
  - minor-version compatibility for CUDA 13.x: ">= 580";
  - no Ampere deprecation notice;
  - cuFFT 13.0 "Removed support for Maxwell, Pascal, and Volta GPUs";
  - bundled Nsight Compute 2026.4.0.10, Nsight Systems 2026.5.2.323, Nsight Visual Studio Edition 2026.4.0.26265, and Visual Studio Integration 13.5.25;
  - no VS 2026 known issues listed.
  - — [CUDA Toolkit 13.5 release notes](https://docs.nvidia.com/cuda/cuda-toolkit-release-notes/index.html)
- The CUDA Features Archive says offline compilation and library support for Maxwell, Pascal and Volta were removed in the CUDA 13.0 major release. — [CUDA Features Archive](https://docs.nvidia.com/cuda/cuda-features-archive/index.html) (search summary); corroborated by "CUDA 13, which removed support for GPU architectures prior to sm_75" — [OIDN #274](https://github.com/RenderKit/oidn/issues/274) — and by the tiny-cuda-nn CMake minimum arch of 75 for CUDA >= 13 — [CMakeLists.txt](https://raw.githubusercontent.com/NVlabs/tiny-cuda-nn/master/CMakeLists.txt)
- CC 8.6 is still a column in the CUDA 13.5 Programming Guide's compute-capability tables. — [CUDA Programming Guide: compute capabilities](https://docs.nvidia.com/cuda/cuda-programming-guide/05-appendices/compute-capabilities.html)
- CUDA 13.5 on Windows supports VS 2026 18.x, 2022 17.x and 2019 16.x. The `CUDA 13.5.props` build customization ships, and the driver is no longer bundled since 13.1. — [CUDA Installation Guide for Windows](https://docs.nvidia.com/cuda/cuda-installation-guide-microsoft-windows/index.html) (verified in the earlier note)
- Nsight Graphics v2026.4 (updated Oct 27 2026):
  - minimum driver "Release 620 or newer" on Windows and Linux;
  - deprecation: "Support for Turing architectures will be removed in a future release", and nothing about Ampere;
  - "Nsight Graphics is incompatible with the debug runtime in all versions of Direct3D".
  - — [Nsight Graphics release notes](https://docs.nvidia.com/nsight-graphics/ReleaseNotes/index.html)
- Nsight Graphics GPU Trace collects compute timing for D3D12/Vulkan/CUDA "on Blackwell and newer". That feature is not available on the 3070. — [Nsight Graphics GPU Trace UI](https://docs.nvidia.com/nsight-graphics/UserGuide/gpu-trace-ui.html) (from the earlier note)

### Inferences
- Recommended nvcc flags when the build runs only on the 3070: `-gencode arch=compute_86,code=sm_86 -gencode arch=compute_86,code=compute_86`. That is native SASS plus embedded PTX for JIT on newer GPUs. In MSBuild: CUDA C/C++ > Device > Code Generation `compute_86,sm_86`; in CMake: `CMAKE_CUDA_ARCHITECTURES=86` (which emits SASS + PTX for 86 by default). To also run on RTX 40/50: `86;89;120` plus PTX. This follows standard nvcc semantics, not a 13.5-specific source.
- PTX JIT: CUDA 13 dropped only pre-Turing targets. PTX built for compute_86 JIT-compiles on any newer GPU, and PTX built for compute_75 JITs on the 3070. No source found suggests sm_86 PTX JIT changed. This is inferred from the removal scope (archs < 75 only).
- Install an R620-branch Game Ready or Studio driver on the 3070. It satisfies OptiX 9.1 (R590), SM 6.9 (595), CUDA 13.5 features (R620) and Nsight Graphics 2026.4 (R620) at once.
- Do not run Nsight Graphics with the D3D12 debug layer on.

### Gaps
- The exact 13.5 release-notes wording on supported architectures (for example a list that includes sm_86) was not quoted. Support is inferred from the Programming Guide tables and the drop list.
- No Nsight Compute or Systems statement on Ampere support for the 2026 versions was fetched, though no deprecation was seen either.

---

## 7. CUDA <-> D3D12 interop on Ampere/Windows: gotchas and the actual simpleD3D12 sequence

### Takeaway
The raw source now fetches from `cpp/5_Domain_Specific/simpleD3D12/simpleD3D12.cpp`. The earlier note's 404 gap is resolved. The sample confirms the recipe:
- a committed buffer in a DEFAULT heap with `D3D12_HEAP_FLAG_SHARED`;
- `CreateSharedHandle(..., GENERIC_ALL, name, &h)` with explicit security attributes;
- the import size from `GetResourceAllocationInfo(m_nodeMask, 1, &desc).SizeInBytes`, plus `cudaExternalMemoryDedicated`;
- a fence created with `D3D12_FENCE_FLAG_SHARED`, imported as `cudaExternalSemaphoreHandleTypeD3D12Fence`;
- the CUDA device selected by an 8-byte LUID memcmp, and `m_nodeMask = devProp.luidDeviceNodeMask`.

**The sample does not use `ID3D12CommandQueue::Wait`.** D3D12 is ordered after CUDA only via a CPU wait (`SetEventOnCompletion` + `WaitForSingleObjectEx`). For a denoiser in the frame, add a GPU-side `queue->Wait(fence, cudaSignalValue)`.

### Cited Findings (all from [simpleD3D12.cpp](https://raw.githubusercontent.com/NVIDIA/cuda-samples/master/cpp/5_Domain_Specific/simpleD3D12/simpleD3D12.cpp))
- Resource: `CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_SHARED, &CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize), D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, ...)`.
- Handle: `CreateSharedHandle(m_vertexBuffer.Get(), &windowsSecurityAttributes, GENERIC_ALL, name, &sharedHandle)`.
- Size: `GetResourceAllocationInfo(m_nodeMask, 1, &CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize))`, then `externalMemoryHandleDesc.size = actualSize` (= `SizeInBytes`), `.handle.win32.handle = sharedHandle`, `.flags = cudaExternalMemoryDedicated`, then `cudaImportExternalMemory`.
- Fence: `CreateFence(m_fenceValues[m_frameIndex], D3D12_FENCE_FLAG_SHARED, ...)`, then `CreateSharedHandle` on the fence, imported with type `cudaExternalSemaphoreHandleTypeD3D12Fence`. Afterwards `m_fenceValues[m_frameIndex]++`.
- `OnRender` order: `ExecuteCommandLists`, then `Present(1,0)`, then `m_commandQueue->Signal(m_fence, currentFenceValue)`, then `MoveToNextFrame()`.
- `MoveToNextFrame` order:
  1. `cudaWaitExternalSemaphoresAsync(value = currentFenceValue, m_streamToRun)`;
  2. `RunSineWaveKernel(...)`;
  3. `cudaSignalExternalSemaphoresAsync(value = m_fenceValues[m_frameIndex])`;
  4. `m_frameIndex = GetCurrentBackBufferIndex()`;
  5. if `GetCompletedValue() < m_fenceValues[m_frameIndex]`, a CPU wait via `SetEventOnCompletion` / `WaitForSingleObjectEx`;
  6. `m_fenceValues[m_frameIndex] = currentFenceValue + 2`.
- `WaitForGpu`: `Signal`, then `SetEventOnCompletion`, then `WaitForSingleObjectEx`, then `++`.
- There is no `m_commandQueue->Wait(...)` anywhere in the fetched code (as reported by the fetch tool).
- LUID matching: `hardwareAdapter->GetDesc1(&desc); m_dx12deviceluid = desc.AdapterLuid;`, then for each CUDA device it memcmps `LowPart` against `devProp.luid` and `HighPart` against `devProp.luid + sizeof(LowPart)`, and on a match calls `cudaSetDevice(devId); m_nodeMask = devProp.luidDeviceNodeMask;`.
- The README requires "A DirectX12 Capable NVIDIA GPU ... Windows10 or higher" and lists SM 5.0 through 9.0, which is stale given CUDA 13's sm_75 minimum. — [cuda-samples simpleD3D12](https://github.com/NVIDIA/cuda-samples/tree/master/cpp/5_Domain_Specific/simpleD3D12) (from the earlier note)
- CUDA guide rules (from the earlier note):
  - match the device by LUID, and the D3D12 device must not be on a linked-node adapter;
  - `D3D12_HEAP_FLAG_SHARED` on heaps and committed resources;
  - the dedicated flag for committed resources;
  - the app closes NT handles;
  - `D3D12_FENCE_FLAG_SHARED`;
  - waits must be issued after the corresponding signals.
  - — [CUDA Programming Guide: graphics interop](https://docs.nvidia.com/cuda/cuda-programming-guide/04-special-topics/graphics-interop.html)

### Inferences
- How the sample's fence values work is not fully clear from the excerpt. CUDA waits for `currentFenceValue` and then signals `m_fenceValues[m_frameIndex]` before `m_frameIndex` changes, which reads as the same value. The fetch may have skipped an increment line. Do not copy that pattern. Use one monotonic counter: D3D12 `Signal(++v)`, CUDA `Wait(v)`, work, CUDA `Signal(++v)`, D3D12 `Wait(v)`. This is the sequence in `optix_cuda_interop.md` section 2.
- Gotchas worth recording for the 3070 PC:
  - Pass the node mask from `luidDeviceNodeMask` into `GetResourceAllocationInfo`. It is 1 on a single GPU, but the sample does it for correctness.
  - Use the allocation size, not the buffer width, for `size`.
  - GeForce cards run WDDM only; TCC mode is unavailable on GeForce, so this is never an issue on the 3070. This is training knowledge, not verified in this session.
  - On a PC with an iGPU, D3D12 adapter 0 may not be the 3070, which is why the sample does LUID matching.
- The sample's interop is a buffer (a vertex buffer), which matches the "linear buffers for OptiX" plan; no texture import is needed.

### Gaps
- No 2024-2026 NVIDIA forum thread with Ampere-specific D3D12 interop gotchas was found. None were searched beyond the sample, because of the tool-call budget.
- It is unverified whether CIG (CUDA-in-Graphics) contexts are supported on Ampere (`CU_DEVICE_ATTRIBUTE_D3D12_CIG_SUPPORTED` would tell at runtime).

---

## 8. VRAM budget sanity for 8 GB

### Takeaway
The add-ons are modest: NRC about 192 MB at 1080p, SHaRC 160 MiB (256 MiB with directional encoding), OptiX denoiser tens to a few hundred MB, interop staging buffers about 32 MB per FLOAT4 1080p buffer. The real risk on 8 GB is geometry plus textures. NVIDIA had to compact dynamic BLASes in Indiana Jones (1027 MB down to 606 MB) to make full path tracing fit on **12 GB** cards. For a student scene (Sponza / Bistro class) 8 GB is fine. Running NRC and SHaRC together, plus the OptiX denoiser, plus a large Bistro-class scene with uncompacted BLASes, is where it gets tight.

### Cited Findings
- NRC: 191.793 MB total at 1920x1080 (training 274x154). The largest item is QueryRadianceParams, 118.6 MB. — [NrcGuide.md](https://raw.githubusercontent.com/NVIDIA-RTX/RTXGI/main/Docs/NrcGuide.md)
- SHaRC: "A solid baseline for most scenes can be the usage of 2^22 elements". "this will require 160 MiBs of video memory with the default layout" and "256 MiBs with directional radiance encoding". The default layout uses 40 B per voxel, 64 B with directional encoding. "SHaRC requires native fp16 shader type support" (SM 6.2+, DXC `-enable-16bit-types`). A lock buffer is used if `HASH_GRID_ENABLE_64_BIT_ATOMICS` is 0. — [SharcGuide.md](https://raw.githubusercontent.com/NVIDIA-RTX/RTXGI/main/Docs/SharcGuide.md)
- Indiana Jones (NVIDIA blog): BLAS VRAM went from 1027 MB to 606 MB with dynamic BLAS compaction, "which let full PT run on 12 GB GPUs"; the 26.9M-triangle AS was at 1080p internal. — [NVIDIA dev blog: OMMs and BLAS compaction](https://developer.nvidia.com/blog/path-tracing-optimizations-in-indiana-jones-opacity-micromaps-and-compaction-of-dynamic-blass) (from `industry_realtime_path_tracing.md`)
- Alan Wake 2 with RTX Mega Geometry: 300 MB less VRAM (search summary). — [NVIDIA dev blog](https://developer.nvidia.com/blog/nvidia-rtx-innovations-are-powering-the-next-era-of-game-development/) (from `industry_realtime_path_tracing.md`)
- OptiX denoiser memory anecdotes: about 250 MB increase (Daz3D), about 33 MB scratch (an error log at an unknown resolution), and Blender tiles very large images because of a pixel limit. — [Daz3D](https://www.daz3d.com/forums/discussion/comment/4933181); [Arnold answers](https://answers.arnoldrenderer.com/questions/5800/optix-denoiser-error-when-rendering.html?sort=votes); [Blender rB1766549](https://developer.blender.org/rB17665494186816cebb9e8304199e40f9ee033990)

### Inferences
- Rough 1080p budget on the 3070. All figures are estimates except the NRC and SHaRC numbers, which come from NVIDIA's guides.

  | Item | Estimate | Source |
  |---|---|---|
  | NRC | ~0.19 GB | NRC guide |
  | SHaRC | ~0.16 GB | SHaRC guide |
  | OptiX denoiser state + scratch | ~0.1-0.3 GB | anecdotes |
  | Interop staging (color/albedo/normal/flow/output/previous output, about 6 FLOAT4 or HALF4 buffers at 1080p) | 0.05-0.2 GB | arithmetic: 1920x1080x16 B = 33 MB per FLOAT4 buffer |
  | G-buffer + accumulation + ReSTIR reservoirs | ~0.2-0.5 GB | |
  | CUDA context | a few hundred MB | unverified |
  | Windows desktop / DWM | ~0.3-0.5 GB | unverified |

  That totals roughly 1.5-2 GB before geometry and textures, leaving about 5-6 GB for the scene.
- To use that remaining room well:
  - Compact BLASes: `ALLOW_COMPACTION` plus a compaction pass, which typically gives about 50%.
  - Prefer HALF4 for interop buffers.
  - Run only one of NRC or SHaRC at a time.
  - Use `IDXGIAdapter3::QueryVideoMemoryInfo` budget tracking so the app does not silently page over the 8 GB.
- At 1440p, the screen-sized items (NRC query buffers, G-buffer, interop, denoiser) grow by about 1.78x. SHaRC is resolution-independent.

### Gaps
- No published typical BVH sizes for standard student scenes (Sponza, Bistro, San Miguel) on DXR were found in this session; measure them with `GetRaytracingAccelerationStructurePrebuildInfo` and post-build compaction queries.
- No source for CUDA context VRAM overhead on Windows/WDDM.
- No source states that 8 GB is specifically "tight" for path tracing beyond the Indiana Jones 12 GB note.
