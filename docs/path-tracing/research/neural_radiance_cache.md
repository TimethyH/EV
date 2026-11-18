# Neural Radiance Cache (NRC) and Neural Rendering for Real-Time Path Tracing

Research notes as of 2026-11-18. Target: a C++ DirectX 12 spectral path tracer on Windows, one NVIDIA RTX PC (model unknown) and one AMD RX 9070 XT (RDNA 4) PC. All versions and dates are labelled. Claims not backed by a fetched source are in Inferences or Gaps.

---

## 1. The original paper: Müller, Rousselle, Novák, Keller, "Real-time Neural Radiance Caching for Path Tracing" (SIGGRAPH 2021)

### Takeaway
NRC traces a short path per pixel, stops it once its footprint (spread) is wide enough, and reads the rest of the light from a small online-trained MLP. A few longer training paths (under 3%) are "self-trained": the cache is queried at their ends, and that value is pushed back along the path as training targets. That yields multi-bounce transport over frames. The paper's network is tiny: 5 hidden layers of 64 neurons, ReLU, half precision, fully fused CUDA. Each frame it takes 4 Adam steps of 16,384 records on a relative-L2 loss. It adds about 2.6 ms at 1080p on an RTX 3090.

### Cited Findings
**Publication facts**
- Authors Müller, Rousselle, Novák, Keller. SIGGRAPH 2021, arXiv 2106.12372 (submitted 23 Jun 2021, v2 25 Jun 2021). No pretraining: "generalization via adaptation". Self-training gives low-noise targets, and infinite-bounce transport comes from iterating few-bounce training updates. Updates plus queries cost about 2.6 ms at full HD. — [arXiv abstract](https://arxiv.org/abs/2106.12372)

**Algorithm: rendering paths**
- Each pixel gets a short rendering path that terminates into the cache once the path's area spread is large enough. The spread uses a heuristic after Bekaert et al. — [arXiv HTML v2](https://arxiv.org/html/2106.12372v2)
- Termination heuristic (Sec. 3.4):
  - x0 is the camera vertex and x1 the primary vertex.
  - Subpath spread: a(x1…xn) = ( Σ_{i=2..n} sqrt( ‖x_{i−1} − x_i‖² / ( p(ω_i | x_{i−1}, ω) · |cos θ_i| ) ) )², where p is the BSDF sampling PDF.
  - Reference: a0 = ‖x0 − x1‖² / (4π cos θ1).
  - Terminate when a(x1…xn) > c · a0. "We found c=0.01 to yield satisfactory results." c trades variance (longer paths) for bias and speed (shorter paths). — [arXiv HTML v2](https://arxiv.org/html/2106.12372v2)

**Algorithm: training paths and self-training**
- Training paths: "we extend a fraction (typically under 3%) of the short rendering paths by a few vertices". Most suffixes have one vertex. A suffix ends under the same heuristic, a(xn…xm) > c·a0. — [arXiv HTML v2](https://arxiv.org/html/2106.12372v2)
- Tiling: "Using a single random offset, we promote one path per tile to be a long training path". Tile size is adjusted each frame to hit the target number of training records. — [arXiv HTML v2](https://arxiv.org/html/2106.12372v2)
- Self-training: the cache is evaluated at the terminal vertex of each training path, and that value is propagated back to earlier vertices, each of which becomes a training target. "each iteration increases the number of simulated light bounces." — [arXiv HTML v2](https://arxiv.org/html/2106.12372v2)
- Unbiased suffixes: a fraction u = 1/16 of training suffixes ends only by Russian roulette, which limits self-training bias. — [arXiv HTML v2](https://arxiv.org/html/2106.12372v2)

**Training schedule**
- 4 gradient steps per frame, each on a disjoint random subset of the frame's data: s = 4 batches × l = 16,384 records = 65,536 (2^16) records per frame. — [arXiv HTML v2](https://arxiv.org/html/2106.12372v2)
- Weights are smoothed with an exponential moving average, α = 0.99, and the EMA weights serve queries. — [arXiv HTML v2](https://arxiv.org/html/2106.12372v2)
- The cache converges in about 8 frames (about 70 ms). — [arXiv HTML v2](https://arxiv.org/html/2106.12372v2)

**Network**
- 7 fully connected layers: 5 hidden layers of 64 neurons with ReLU, plus an RGB output layer. No bias vectors. — [arXiv HTML v2](https://arxiv.org/html/2106.12372v2)
- Output factorization: the network output is multiplied by the sum of diffuse albedo and specular reflectance. This "helps recover colors while letting the cache focus on complementary details". So the network learns radiance divided by (albedo + specular reflectance). — [arXiv HTML v2](https://arxiv.org/html/2106.12372v2)

**Input encoding (Table 1)**
- Position: frequency encoding with 12 sine terms at frequencies 2^d, d = 0…11 (cosines omitted).
- Scattered direction and surface normal: one-blob encoding of spherical coordinates, 4 blobs each.
- Roughness: one-blob encoding of 1 − e^(−r), 4 dims.
- Diffuse and specular reflectance: fed in as-is.
- Total 62 dims, padded to 64 with the constant 1, which acts as an implicit bias.
- Gaussian and sine kernels are replaced by a quartic kernel and a triangle wave, saving about 0.25 ms/frame. — [arXiv HTML v2](https://arxiv.org/html/2106.12372v2)

**Loss and optimizer**
- Relative L2 loss: the denominator is a stop-gradient of the prediction squared, plus ε = 0.01. Per-channel normalization uses squared luminance. — [arXiv HTML v2](https://arxiv.org/html/2106.12372v2)
- Adam was chosen over SGD and Novograd ("converges in the fewest iterations while having an overhead of practically zero"). Shampoo converged in slightly fewer iterations but cost 0.3 ms/frame. — [arXiv HTML v2](https://arxiv.org/html/2106.12372v2)

**Implementation and timings**
- CUDA, half precision, Tensor Core matmuls, one fully fused kernel. Each thread block processes 128 batch elements for a 64-wide network. Training is about 4–5× slower than inference per element. 5–10× faster than XLA TensorFlow 2.5.0 at relevant batch sizes. — [arXiv HTML v2](https://arxiv.org/html/2106.12372v2)
- Test setup: 1920×1080, i9-9900K + RTX 3090, Direct3D 12 + Falcor. — [arXiv HTML v2](https://arxiv.org/html/2106.12372v2)
- Table 3, PT+ReSTIR+NRC, in ms (trace&shade / query / training):

  | Scene | Trace & shade | Query | Training |
  |---|---|---|---|
  | Attic | 10.88 | 1.66 | 1.12 |
  | Bistro | 11.96 | 1.38 | 1.11 |
  | Classroom | 12.28 | 1.70 | 1.11 |
  | Living Room | 5.82 | 1.85 | 1.11 |
  | Pink Room | 5.36 | 1.54 | 1.12 |
  | Zero Day | 8.67 | 1.41 | 1.09 |
  | **Average** | **9.16** | **1.59** | **1.11** |

  Self-training adds about 11% overhead. — [arXiv HTML v2](https://arxiv.org/html/2106.12372v2)

**Comparisons**
- vs PT+ReSTIR: average 13.6× speedup at equal MRSE. — [arXiv HTML v2](https://arxiv.org/html/2106.12372v2)
- vs DDGI (fps, DDGI / NRC): Zero Day 96.7 / 87.7, Living Room 134 / 111, Pink Room 141 / 124. DDGI's average split is 9.46 / 0.58 / 1.37 ms. DDGI "can expose visible bias (e.g. in the Pink Room)". So NRC is slightly slower than DDGI but less biased and handles glossy surfaces. — [arXiv HTML v2](https://arxiv.org/html/2106.12372v2)

**Failure cases**
- Sharp detail absent from the inputs, such as contact shadows and caustics, is learned slowly or not at all.
- The frequency encoding causes axis-aligned stripes, and some low-frequency flicker remains.
- Little benefit for glass and branching specular chains (Bistro MRSE 2.79 → 2.10).
- Indirect use adds hemispherical-sampling variance. — [arXiv HTML v2](https://arxiv.org/html/2106.12372v2)

**Hash-grid follow-up**
- Later NRC uses the multiresolution hash encoding (Instant NGP, 2022). The network "predicts outgoing radiance at the first non-specular vertex of each pixel's path", trained online from the real-time path tracer's rays. Compared with the triangle-wave encoding, the hash encoding captures sharper detail such as shadows. — [Instant NGP project page](https://nvlabs.github.io/instant-ngp/)

### Inferences
- A minimal first implementation in the student's engine needs:
  - a short-path termination rule (the spread heuristic with c = 0.01),
  - ~2^16 training vertices per frame from a sparse tile of longer paths,
  - a 64-wide, 5-hidden-layer MLP with Adam, relative L2 and EMA weights,
  - the output multiplied by (diffuse + specular reflectance).
- Self-training is the key trick. Without it, training targets need full-length paths, which are noisy and expensive.
- The Adam learning rate is not stated in the paper text read. tiny-cuda-nn's generic default is 1e-3 ([tiny-cuda-nn README](https://github.com/NVlabs/tiny-cuda-nn)). NRC-style online training commonly uses higher rates (around 1e-2), but treat that as a tuning starting point, not a paper fact.

### Gaps
- Adam learning rate, β1/β2/ε: not found in the paper text fetched (the tail of the HTML, about 10 KB, was not read).
- The exact hash-grid settings used in the post-2022 NRC (levels, table size) were not found in a fetched source. From my reading of the Instant NGP paper (not re-verified here): L = 16 levels, F = 2 features per level, T = 2^14–2^24 entries, spatial hash = XOR of coords × large primes (1, 2654435761, 805459861), trilinear interpolation. Verify in the [paper PDF](https://arxiv.org/abs/2201.05989).
- No head-to-head NRC vs SHaRC quality numbers from NVIDIA were found.

---

## 2. tiny-cuda-nn and Instant NGP (how to use from C++/CUDA, requirements, license)

### Takeaway
tiny-cuda-nn (NVlabs, BSD 3-Clause) is the CUDA/C++ library behind NRC research and Instant NGP. It provides FullyFusedMLP, the HashGrid / TriangleWave / OneBlob / Composite encodings, and a RelativeL2Luminance loss. It is configured with JSON and needs CUDA 11.5+ on Windows plus an NVIDIA GPU. It is the fastest way to prototype NRC in CUDA next to a DX12 renderer, but it never runs on AMD.

### Cited Findings
**Requirements**
- NVIDIA GPU, and Tensor Cores help. C++17. Visual Studio 2019/2022 on Windows. CUDA 11.5+ on Windows (10.2+ on Linux). CMake 3.21+. Clone with `--recursive`, then build:
  ```
  cmake . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
  cmake --build build --config RelWithDebInfo -j
  ```
  — [tiny-cuda-nn README](https://github.com/NVlabs/tiny-cuda-nn)
- FullyFusedMLP "requires a very large amount of shared memory in its default configuration" and "will likely only work on an RTX 3090, an RTX 2080 Ti, or higher-end GPUs". On lower-end cards, reduce `n_neurons` or use CutlassMLP (slower, more compatible). — [tiny-cuda-nn README](https://github.com/NVlabs/tiny-cuda-nn)

**C++ API**
- Config: `nlohmann::json` with `loss`, `optimizer` (e.g. Adam, lr 1e-3), `encoding` (e.g. HashGrid: n_levels 16, n_features_per_level 2, log2_hashmap_size 19, base_resolution 16, per_level_scale 2.0) and `network` (FullyFusedMLP, ReLU, n_neurons 64, n_hidden_layers 2).
- Calls: `tcnn::create_from_config(n_in, n_out, config)`, then `model.trainer->training_step(inputs, targets, &loss)` and `model.network->inference(in, out)`. Batch sizes must be multiples of `tcnn::BATCH_SIZE_GRANULARITY`. — [tiny-cuda-nn README](https://github.com/NVlabs/tiny-cuda-nn)

**NRC building blocks**
- The README names the "Composite" encoding (to build the NRC encoding), "TriangleWave" (a low-cost encoding used in NRC) and the "Relative L2 Luminance" loss (used in NRC). No full NRC config file ships. — [tiny-cuda-nn README](https://github.com/NVlabs/tiny-cuda-nn)

**Performance features**
- JIT fusion (v2.0+): `model->set_jit_fusion(tcnn::supports_jit_fusion())` gives 1.5–2.5× speedups.
- Possible slowdowns: hash grids over about 20M parameters, layers wider than 128, and RTX 3000-series or older GPUs. — [tiny-cuda-nn README](https://github.com/NVlabs/tiny-cuda-nn)
- Example speed: about 1000 training steps take a bit over 1 s on an RTX 4090 with the default image example. — [tiny-cuda-nn README](https://github.com/NVlabs/tiny-cuda-nn)

**PyTorch bindings and license**
- `pip install git+https://github.com/NVlabs/tiny-cuda-nn/#subdirectory=bindings/torch`. FP16 is the default on Volta and later. At small batches Python overhead is about 2× vs native. — [tiny-cuda-nn README](https://github.com/NVlabs/tiny-cuda-nn)
- License: BSD 3-Clause (LICENSE.txt). Third-party mirrors that list MIT are wrong. — [tiny-cuda-nn README](https://github.com/NVlabs/tiny-cuda-nn); conflicting mirror listing: [SourceForge mirror](https://sourceforge.net/projects/tiny-cuda-neural-netw.mirror/)

**Instant NGP**
- Müller, Evans, Schied, Keller. ACM TOG 41(4), Article 102 (SIGGRAPH 2022). "a multiresolution hash table of trainable feature vectors whose values are optimized through stochastic gradient descent". The multiresolution structure lets the network disambiguate hash collisions. — [arXiv 2201.05989](https://arxiv.org/abs/2201.05989), [project page](https://nvlabs.github.io/instant-ngp/)

### Inferences
- **Using tiny-cuda-nn from the DX12 engine:**
  1. Write NRC query and training records from HLSL into shared D3D12 buffers.
  2. Import them into CUDA via external memory (`cudaImportExternalMemory` with a D3D12 shared handle).
  3. Sync with an imported D3D12 fence (`cudaImportExternalSemaphore`).
  4. Run tcnn inference and training in CUDA.
  5. Write predictions back to a buffer that a resolve pass in HLSL reads.

  This is the same split the NVIDIA NRC SDK uses internally (it ships CUDA runtime DLLs; see section 3). It is a good way to learn CUDA. The CUDA interop function names are general CUDA knowledge, not taken from a fetched source here.
- The FullyFusedMLP shared-memory note suggests a low-end Turing/Ampere card may need width 32 or CutlassMLP. Any RTX 30/40/50 desktop card at width 64 should be fine.

### Gaps
- No official compute-capability table was found in the README fetched. The CMake variable `TCNN_CUDA_ARCHITECTURES` is how the target architecture is chosen (general knowledge, not verified this session).
- No latest release tag or date was found on the page (the Releases section is empty).

---

## 3. NVIDIA RTX Neural Radiance Cache SDK (RTXGI 2.x / RTX Kit) and SHaRC

### Takeaway
NVIDIA ships NRC as a binary library inside the RTXGI SDK (NVIDIA-RTX/RTXGI), currently RTXGI 2.9.0 with NRC v0.16.0 and SHaRC 1.8.3. It is still labelled "experimental".
- NRC has D3D12 and Vulkan front ends (`NRC_D3D12.dll`, `NRC_Vulkan.dll`). It ships CUDA 13 runtime DLLs and needs Tensor Cores, so it is NVIDIA-only, Turing or newer.
- SHaRC is a non-neural, hash-grid radiance cache in plain HLSL/GLSL (SM 6.2 + fp16). It runs on any DXR GPU, including the RX 9070 XT, and is the ideal stepping stone because NRC's integration pattern is nearly the same.

### Cited Findings
**Repository and versions**
- The RTXGI v2 repo provides "world-space radiance caches" for path tracers: NRC ("AI-based… currently experimental", requires Tensor Cores) and SHaRC ("runs on a wider range of hardware with no vendor-specific requirements").
- Prerequisites: any DXR GPU for SHaRC, NVIDIA Turing+ for NRC, CMake, Git LFS, Vulkan SDK 1.4.x, VS2022, Windows SDK 10.0.20348+. Sample app: `Samples/Pathtracer`. License: License.md (custom NVIDIA license; the type was not shown). — [RTXGI GitHub](https://github.com/NVIDIA-RTX/RTXGI)
- A search snippet of the README gives "NV GPUs ≥ Turing (arch 70)" and "driver ≥ 617.42" for NRC (not confirmed in the page fetch). — [RTXGI GitHub](https://github.com/NVIDIA-RTX/RTXGI)
- RTXGI 2.0 (NRC + SHaRC) was announced at GDC 2024 (secondary). — [Wccftech](https://wccftech.com/nvidia-rtxgi-2-0-available-next-frontier-ray-traced-visuals-neural-radiance-cache-spatial-hash-radiance-cache-dynamic-diffuse-global-illumination/). The RTX Kit page links the same GitHub download. — [RTX Kit](https://developer.nvidia.com/rtx-kit)

**Changelog (no dates in the file)** — [RTXGI Changelog](https://raw.githubusercontent.com/NVIDIA-RTX/RTXGI/main/Changelog.md)

| RTXGI | NRC | SHaRC | Notes |
|---|---|---|---|
| 2.9.0 | — | — | IndirectDiffuse sample, SER, NRD 4.18 |
| 2.8.0 | v0.16.0 | 1.8.3 | Windows on Arm64 |
| 2.4.0 | 0.14.1 | — | Fixes internal hash grid bug, custom CUDA DLL paths |
| 2.3.x | 0.13.x | — | Network config hot-reload, exposed training iteration count |
| 2.2.0 | 0.12.1 | — | CUDA Toolkit 12.5.1, `Nrc.hlsli` Slang compatibility, DLL signing |
| 2.1.0 | — | — | NRC runs on RTX 20xx |
| 2.0.0 | — | — | Initial release |

**NRC distribution**
- `/bin/<arch>`: `NRC_D3D12.dll`, `NRC_Vulkan.dll` and the CUDA 13 runtime DLLs.
- `/include`: `Nrc.hlsli`, `NrcD3d12.h`, `NrcVk.h`.
- `/lib`: `NRC_D3D12.lib`, `NRC_Vulkan.lib`.
- The resolve pass and internal kernels are bundled. The network is trained every frame on Tensor Cores and learns radiance (not irradiance), so it handles glossy surfaces. — [NRC Guide](https://raw.githubusercontent.com/NVIDIA-RTX/RTXGI/main/Docs/NrcGuide.md)

**CPU API (D3D12)**
- Setup: `nrc::d3d12::Initialize(GlobalSettings)` and `Context::Create(ID3D12Device5*, ctx)`.
- `Configure(ContextSettings{frameDimensions, trainingDimensions,…}, Buffers*)` creates the network and allocates memory. It can hitch if called in the render loop.
- Per frame: `BeginFrame(cmdList4, FrameSettings)` (clears counters), `PopulateShaderConstants(NrcConstants&)`, `QueryAndTrain(cmdList4, float* trainingLoss)`, `Resolve(cmdList4, outputBuffer)`, `EndFrame(ID3D12CommandQueue*)` (must be the same queue).
- Memory: SDK-managed (`enableGPUMemoryAllocation`) or app-managed (`GetBuffersAllocationInfo`).
- Helpers: `computeIdealTrainingDimensions(frameDims, avgTrainingVerticesPerPath)`, `VerifySignature()`. — [NRC Guide](https://raw.githubusercontent.com/NVIDIA-RTX/RTXGI/main/Docs/NrcGuide.md)

**Shader API (`Nrc.hlsli`)**
- `NrcIsUpdateMode()`, `NrcCreateContext(constants, buffers, pixelIndex)`, `NrcSetSampleIndex`, `NrcCreatePathState(seed)`.
- `NrcUpdateOnHit(...)` returns an `NrcProgressState` (`TerminateImmediately` / `TerminateAfterDirectLighting`).
- `NrcUpdateOnMiss`, `NrcSetBrdfPdf`, `NrcWriteFinalPathInfo(ctx, state, throughput, radiance)`.
- For a custom resolve: `NrcGetPathInfoIndex`, `NrcUnpackQueryPathInfo`, `NrcUnpackRadiance`.
- Buffers: `queryPathInfo`, `trainingPathInfo`, `trainingPathVertices`, `queryRadianceParams`, `countersData`. — [NRC Guide](https://raw.githubusercontent.com/NVIDIA-RTX/RTXGI/main/Docs/NrcGuide.md)

**Frame flow**
1. Update pass at reduced `trainingDimensions` (1 spp) writes training paths.
2. Query pass at full `frameDimensions` writes query records.
3. `QueryAndTrain` predicts radiance at query points, propagates it back along the training paths (self-training) and trains.
4. `Resolve` adds the predicted radiance × path throughput to the output.

A custom resolve is needed for split diffuse/specular signals, e.g. for denoisers. — [NRC Guide](https://raw.githubusercontent.com/NVIDIA-RTX/RTXGI/main/Docs/NrcGuide.md)

**Memory example**
- 1920×1080 output, training 274×154, 8 bounces, 1 spp: about 191.8 MB total. Largest items: QueryRadianceParams 118.6 MB (56 B × 2.12M), QueryRadiance 25.4 MB, TrainingRadianceParams 18.9 MB, QueryPathInfo 16.6 MB, TrainingPathVertices 16.2 MB (48 B each). — [NRC Guide](https://raw.githubusercontent.com/NVIDIA-RTX/RTXGI/main/Docs/NrcGuide.md)

**Debugging**
- `ResolveMode`s include `DirectCacheView` (recommended for checking an integration), training-bounce heat maps, primary/secondary training radiance and query-index hashes.
- `FrameSettings::selfTrainingAttenuation` disables self-training for debugging.
- Nsight Graphics does not show the network calls.
- Vulkan needs scalar block layout and uniform buffer standard layout enabled. Shaders use `-fvk-use-dx-layout`. — [NRC Guide](https://raw.githubusercontent.com/NVIDIA-RTX/RTXGI/main/Docs/NrcGuide.md)

**SHaRC**
- Passes: Update (sparse path tracing into a world-space hash grid of voxels), Resolve (merge with previous frames) and Query (render paths read the cache and terminate early). LOD depends on camera distance (`logarithmBase`, `levelBias`), and `sceneScale` sets voxel size. — [SHaRC Guide](https://raw.githubusercontent.com/NVIDIA-RTX/RTXGI/main/Docs/SharcGuide.md)
- HLSL API: `SharcInit`, `SharcUpdateHit`, `SharcUpdateMiss`, `SharcSetThroughput`, `SharcResolveEntry`, `SharcGetCachedRadiance`, `GetVoxelSize`, optional SH directional encoding.
- Parameters: `staleFrameNumMax`, `SHARC_PROPAGATION_DEPTH` (default 2), `SHARC_ENABLE_RESPONSIVE_LIGHTING`. — [SHaRC Guide](https://raw.githubusercontent.com/NVIDIA-RTX/RTXGI/main/Docs/SharcGuide.md)
- Requirements: native fp16. DXIL SM 6.2+ with DXC `-enable-16bit-types`. GLSL is supported. Memory is 40 B/voxel (8 hash + 16 accumulation + 16 resolved): about 160 MiB at 2^22 entries, or 256 MiB with SH. Trace about one random pixel per 5×5 block (about 4%) in the update pass. 10–20% occupancy with a static camera. — [SHaRC Guide](https://raw.githubusercontent.com/NVIDIA-RTX/RTXGI/main/Docs/SharcGuide.md)
- NVIDIA's RTXPT path tracer also ships SHaRC 1.8.3 as a submodule. — [RTXPT releases](https://github.com/NVIDIA-RTX/RTXPT/releases)

**AMD counterpart**
- FSR Radiance Caching v0.9.0, a "Redstone" technical preview released December 2025:
  - DirectX 12 only, Windows 10/11.
  - RX 9000 series (RDNA 4) and later, Adrenalin 25.12.1+.
  - An online ML model trains on-device in real time from path-tracer data, and the library auto-selects the fastest model for the device.
  - Network details, API and performance numbers are not disclosed on the page.
  - Ships in the FSR SDK, GitHub: GPUOpen-LibrariesAndSDKs/FidelityFX-SDK.

  — [AMD GPUOpen FSR Radiance Caching](https://gpuopen.com/amd-fsr-radiancecaching/)
- Demo in Warhammer 40,000: Darktide. AMD recommends pairing it with ReSTIR (secondary coverage). — [TechPowerUp FSR Redstone review](https://www.techpowerup.com/review/amd-fsr-4-redstone/), [Tweaktown](https://www.tweaktown.com/news/109283/amd-fsr-redstone-is-here-ai-powered-upscaling-and-frame-generation-for-rdna-4/index.html)

### Inferences
- NRC SDK execution model: CUDA DLLs in `/bin`, "custom CUDA DLL paths" and a "CUDA Toolkit 12.5.1" update in the changelog. So the D3D12 front end almost certainly shares buffers and fences with an internal CUDA backend (tiny-cuda-nn-like kernels) rather than using HLSL cooperative vectors. That is why it is NVIDIA-only and why Nsight Graphics cannot see the network.
- Recommended order for this student:
  1. SHaRC: works on both PCs and teaches the update/query/resolve structure and hash grids.
  2. The NVIDIA NRC SDK on the RTX PC, as a reference "ground truth" NRC.
  3. Their own NRC: CUDA + tiny-cuda-nn first, then HLSL LinAlg/cooperative vectors to run on AMD too.
- The AMD FSR Radiance Caching preview is the only vendor NRC-like library for the RX 9070 XT.
- Spectral engine and the SDKs: both SDKs take RGB radiance in `NrcWriteFinalPathInfo` / `SharcUpdateHit`. The spectral tracer must convert per-path wavelength samples to RGB/XYZ contributions before handing them to the cache (see section 5).

### Gaps
- No release dates in the RTXGI changelog. The 2.8/2.9 dates (likely 2025–2026) are unverified.
- No confirmation of whether NVIDIA plans a cooperative-vector/LinAlg (vendor-neutral) NRC backend.
- No official NRC performance numbers in the guide. The RTXGI license type (custom NVIDIA RTX SDK license?) was not read.
- AMD FSR Radiance Caching's integration API was not examined (docs at gpuopen.com `/manuals/fsr_sdk/techniques/radiance-cache/`).

---

## 4. In-shader neural networks: DirectX Cooperative Vectors (SM 6.9) → DirectX Linear Algebra (SM 6.10), Vulkan VK_NV_cooperative_vector, Slang / SlangPy / RTXNS

### Takeaway
As of November 2026 the HLSL path for running and training MLPs is moving from "Cooperative Vector" (SM 6.9 preview, announced at GDC 2025) to DirectX Linear Algebra (`dx::linalg::Matrix`, SM 6.10).
- SM 6.10 has been in preview since April 2026 (Agility 1.720.1-preview) and is expected to go retail in Agility SDK 1.621 in early December 2026.
- AMD supports LinAlg on RX 9000 (RDNA 4, so the RX 9070 XT) with a preview driver. NVIDIA support is via in-development drivers and DevRel. Intel is planned.
- On Vulkan, `VK_NV_cooperative_vector` (not ratified, NVIDIA, RTX 20xx+) includes training ops.
- NVIDIA's RTXNS SDK (Slang) uses either DirectX (Agility 1.721.3-preview, R615+) or Vulkan coop-vec for training and inference in shaders.

### Cited Findings
**Microsoft roadmap**
- Microsoft at GDC 2026: Cooperative Vector, introduced in SM 6.9, gave "hardware-accelerated vector-matrix operations directly from HLSL". Not all ML workloads fit it, so Microsoft introduced DirectX Linear Algebra (vector and matrix ML in one model; spec in microsoft/hlsl-specs proposal 0035), with "public preview in April". Cooperative-Vector-style workloads named include "neural radiance caching (NRC)". AMD, Intel, NVIDIA and Qualcomm committed to support. The DirectX Compute Graph Compiler (full models) was announced for private preview in summer 2026. — [Microsoft GDC 2026](https://developer.microsoft.com/en-us/games/articles/2026/03/gdc-2026-evolving-directx-for-ml-era-on-windows/)

**D3D12 LinAlg Matrix preview (blog, 2026-04-27)**
- Agility SDK 1.720.1-preview plus DXC 1.10.2605.2 (SM 6.10, `-T cs_6_10`). Header `<dx/linalg.h>`, namespace `dx::linalg`. `Matrix<ComponentType, M, N, MatrixUse::A/B/Accumulator, MatrixScope::Thread/Wave/ThreadGroup>`. "Thread-scope matrices are the former Cooperative Vectors API", and the feature "subsumes these APIs into a singular set of orthogonal APIs". Intel: it "replaces cooperative vectors". — [DirectX blog: D3D12 LinAlg Matrix Preview](https://devblogs.microsoft.com/directx/d3d12-linalg-preview/)
- Enable it: call `D3D12EnableExperimentalFeatures(D3D12ExperimentalShaderModels)` before device creation, then query `D3D12_FEATURE_LINEAR_ALGEBRA_SUPPORT` ≥ `D3D12_LINEAR_ALGEBRA_TIER_1`. — [DirectX blog](https://devblogs.microsoft.com/directx/d3d12-linalg-preview/)
- Vendor support: AMD RX 9000 with the "AgilitySDK Developer Preview Edition 25.30.41.02" driver. NVIDIA: contact DevRel for in-development drivers. Intel: planned on XMX. WARP 1.0.20-preview. — [DirectX blog](https://devblogs.microsoft.com/directx/d3d12-linalg-preview/)
- Thread-scope matmul sketch:
  ```hlsl
  using MatA = Matrix<ComponentType::F16, 16, 16, MatrixUse::A, MatrixScope::Thread>;
  MatA m = MatA::Load<MatrixLayout::RowMajor>(InBuff, 0, 32);
  vector<float16_t, 16> y = Multiply<float16_t>(m, x);
  ```
  — [DirectX blog](https://devblogs.microsoft.com/directx/d3d12-linalg-preview/)

**AMD, 2026-11-03**
- Agility 1.620 is retail (2026-10-28). 1.722-preview adds SM 6.10 features: Group Wave Index, Variable Group Shared Memory, `TriangleObjectPositions`/`ClusterID` and `linalg::Matrix`.
- "Microsoft has said that Shader Model 6.10 will move to retail in Agility SDK 1.621, coming in early December."
- `linalg::Matrix` is supported on RX 9000 and Radeon AI PRO R9700/R9600. RX 7000 is not listed.
- Drivers: Agility SDK Developer Preview Edition 26.20.12.01. — [AMD GPUOpen Oct 2026 Agility SDK](https://gpuopen.com/learn/amd-directx12-agility-sdk-october-2026/)

**SM 6.9 status**
- SM 6.9 shipped retail in Agility SDK 1.619.0 on 2026-02-26. — [Agility SDK downloads](https://devblogs.microsoft.com/directx/directx12agility/)
- Whether Cooperative Vector itself went retail in 1.619 is contested. A third-party blog claims it is included and also that it is "being deprecated" for DXLA. — [Sean Kim blog (unverified)](https://blog.imseankim.com/directx-ml-shader-model-6-9-cooperative-vectors-neural-rendering-gdc-2026/). Microsoft's own Cooperative Vector post describes it as preview. — [DirectX blog: Cooperative Vector](https://devblogs.microsoft.com/directx/cooperative-vector/)
- The GDC 2025 announcement of DXR 1.2 and neural rendering / Cooperative Vectors is here. — [DirectX blog GDC 2025](https://devblogs.microsoft.com/directx/announcing-directx-raytracing-1-2-pix-neural-rendering-and-more-at-gdc-2025/)

**Vulkan**
- `VK_NV_cooperative_vector` "accelerate[s] the evaluation of small neural networks, where each invocation is performing its own independent evaluation of the network". Rev. 3 (2024-05-23) "Add training functions". Status: "Not ratified", revision 4. SPIR-V: `SPV_NV_cooperative_vector`. — [Vulkan refpage](https://docs.vulkan.org/refpages/latest/refpages/source/VK_NV_cooperative_vector.html), [proposal](https://docs.vulkan.org/features/latest/features/proposals/VK_NV_cooperative_vector.html)
- Training ops: `OuterProductAccumulate` and `VectorAccumulate` compute weight and bias gradients, with a `TRAINING_OPTIMAL` matrix layout. — [gamedev.net coop-vec intro](https://gamedev.net/news/3317-cooperative-vectors-introduction/), [Vulkan bindings docs](https://hackage-content-origin.haskell.org/package/vulkan-3.27/candidate/docs/Vulkan-Extensions-VK_NV_cooperative_vector.html)

**RTXNS (NVIDIA RTX Neural Shading SDK)**
- Built on Slang. Examples go from simple inference to training a network to represent a shader or texture, plus SlangPy samples.
- Requirements: Slang 2026.10, CMake 3.24, VS2022.
  - DirectX: Preview Agility SDK 1.721.3-preview, DXC 1.10.2605.24, NVIDIA R615+.
  - Vulkan: SDK 1.3.296.0, R570+, `VK_NV_cooperative_vector`, minimum "NVIDIA RTX 20XX".
- No non-NVIDIA support is mentioned. — [RTXNS GitHub](https://github.com/NVIDIA-RTX/RTXNS)
- `docs/SimpleTraining.md` walks through the forward and backward pass with RTXNS linear-layer, activation and gradient helpers. — [RTXNS SimpleTraining](https://github.com/NVIDIA-RTX/RTXNS/blob/main/docs/SimpleTraining.md)

**Slang**
- Cooperative vector types are now natively differentiable. Neural samples are in the slangpy-samples repo. — [Khronos Slang in action slides](https://www.khronos.org/developers/linkto/slang-in-action-slides)
- A "neural.slang" toolkit is planned but still experimental. A unified Tensor type is landing. — [Vulkanised 2025 Slang talk](https://vulkan.org/user/pages/09.events/vulkanised-2025/T42-Shannon-Woods-NVIDIA.pdf)
- The Slang autodiff tutorial trains a tiny MLP. — [Slang autodiff tutorial 2](https://shader-slanggithubio-aidanfnv-wip.readthedocs.io/en/latest/auto-diff-tutorial-2.html)
- SIGGRAPH 2025 neural shading course code has basic MLP training plus a coop-vec variant ("only supported on Nvidia hardware on Windows/Linux"). — [neural-shading-s25 repo mirror](https://gh.r-mj.com/shader-slang/neural-shading-s25)

**OptiX**
- NVIDIA OptiX also exposes cooperative vectors for neural rendering. — [NVIDIA blog: Neural rendering in OptiX using cooperative vectors](https://developer.nvidia.com/blog/neural-rendering-in-nvidia-optix-using-cooperative-vectors)

**Third-party NRC on RTXNS**
- A Packt book repo, "Neural Rendering in Action", Chapter 7, has an NRC built on RTXNS with a training path that needs a cooperative-vector training device (not verified). — [Packt Chapter07 NRC README](https://github.com/PacktPublishing/Neural-Rendering-in-Action/blob/main/Chapter07/src/nrc/README.md)

### Inferences
- **Where CUDA and HLSL fit:**
  - CUDA (tiny-cuda-nn or hand-written) is the best place to learn and get maximum speed on NVIDIA. It needs D3D12–CUDA interop and is NVIDIA-only.
  - HLSL LinAlg (SM 6.10) is the vendor-neutral path. It is the only way one codebase trains NRC on both the RTX PC and the RX 9070 XT. It is preview until early December 2026 and drivers are still preview/DevRel for NVIDIA.
  - Slang is the most productive way to write the in-shader backward pass (autodiff) and can emit HLSL/DXIL for the DX12 engine.
- Plan to write against `dx::linalg` (thread scope ≈ old coop-vec) rather than the SM 6.9 Cooperative Vector intrinsics, which are being subsumed.
- Hardware matrix:

  | GPU | NRC SDK (CUDA) | tiny-cuda-nn | HLSL LinAlg / coop-vec | SHaRC |
  |---|---|---|---|---|
  | RTX 20 (Turing) | yes | yes; FullyFused may need width ≤ 64, high-end cards only | coop-vec Vulkan min RTX 20XX | yes |
  | RTX 30/40/50 | yes | best | DX12 via R615+ preview drivers (RTXNS) | yes |
  | RX 9070 XT (RDNA 4) | no | no | yes, preview driver | yes |

### Gaps
- No confirmation that NVIDIA public drivers expose `D3D12_LINEAR_ALGEBRA_TIER_1` as of November 2026. RTXNS's R615 requirement suggests DX12 coop-vec/LinAlg works on public NVIDIA drivers, but which API version it targets is unclear.
- Whether SM 6.10 actually shipped in 1.621 (expected early Dec 2026) cannot be known yet.
- No ratified KHR cooperative-vector extension was found.
- Training (backward pass, outer-product accumulate) support inside DX LinAlg for AMD RDNA 4 was not verified. The blog emphasizes inference examples.

---

## 5. NRC with spectral rendering; neural path guiding; follow-ups (2022–2026) on dynamics, specular, volumes

### Takeaway
No published work was found that conditions an NRC on wavelength. Both vendor SDKs and the original paper cache RGB, and the paper normalizes its loss by luminance. For a spectral tracer the practical choices are:
- (a) cache RGB/XYZ and project each hero-wavelength sample's contribution into it, or
- (b) feed wavelength(s) as an extra encoded input and output scalar spectral radiance (research territory).

Follow-ups target specular lighting (prefiltered radiance, Oct 2026), stability vs lag (reliability-weighted averaging, WSCG 2026), volumes (GSCache 2025) and neural path guiding (NPM, Dong et al. 2023).

### Cited Findings
**Spectral**
- The original NRC is RGB. Its loss normalizes each colour channel by squared luminance. — [arXiv HTML v2](https://arxiv.org/html/2106.12372v2)
- A search for spectral or wavelength-conditioned radiance caches found no matching publication. The closest results were NRC itself and NRC patents. — [USPTO patent 11610360](https://image-ppubs.uspto.gov/dirsearch-public/print/downloadPdf/11610360)

**Specular**
- "Neural Caching of Prefiltered Radiance for Specular Lighting", Klepikov & Frolov, arXiv 2610.11702 (2026-10-08, preprint):
  - an "NRC variant tailored to specular lighting",
  - inputs use a reflection-direction parameterization, and the target is roughness-dependent prefiltered incoming radiance,
  - combined with a precomputed BRDF integration map via split-sum,
  - "faster convergence" vs NRC baselines on Bunny and Specular Sponza.

  — [arXiv 2610.11702](https://arxiv.org/abs/2610.11702)

**Stability vs lag**
- "Reliability-Weighted Averaging for Real-Time Neural Incident Radiance Caching" (WSCG 2026): a stronger weight EMA improves stability but lags after scene changes, and the paper proposes a reliability-weighted blend. — [WSCG 2026 F07](http://wscg.zcu.cz/wscg2026/Papers/F07.pdf)
- Sample-based radiance caching (Pacific Graphics 2025) caches first-hit samples. It claims better temporal stability than hash caches, at a high cost. — [DTU PG 2025 paper](https://people.compute.dtu.dk/jerf/papers/stablesample.pdf)

**Volumes**
- GSCache (arXiv 2507.19718, 2025): a real-time radiance cache for volume path tracing using a multi-level 3D Gaussian hierarchy instead of a hash grid. — [arXiv 2507.19718](https://arxiv.org/html/2507.19718v2)

**Neural path guiding**
- Neural Parametric Mixtures for Path Guiding (Dong, Wang, Li, Peking University; ACM 2023 venue, likely SIGGRAPH 2023; arXiv 2504.04315):
  - a multiresolution grid embedding (L = 8, F = 4) plus a tiny-cuda-nn MLP (3 layers, width 64) outputs K = 8 vMF lobes,
  - targets incident radiance or the full product, trained online on a KL divergence,
  - about 3 ms evaluation and about 10 ms per training step at 1280×720 on an RTX 3070,
  - interactive, not real-time (real-time is future work),
  - cites Müller 2019 Neural Importance Sampling as having "prohibitive computational cost".

  — [arXiv 2504.04315](https://arxiv.org/html/2504.04315v1)
- Real-time screen-space path guiding with parametric mixtures (Derevyannykh, EG 2022 short): 8 parameters per pixel, up to 4× lower FLIP at 1 spp, under 1.5 ms on an RTX 2070 at 1080p. — [EG Digital Library](https://diglib.eg.org/handle/10.2312/egs20221024), [DeepAI listing](https://deepai.org/publication/real-time-path-guiding-based-on-parametric-mixture-models)
- Online NASG mixtures (Huang et al.) are reported as robust to parallax and anisotropy (secondary summary only). — [Emergent Mind summary](https://www.emergentmind.com/topics/neural-path-guiding)

### Inferences
- **Spectral design options:**
  1. **RGB/XYZ cache (recommended first).**
     - Each training vertex's radiance target is the spectral sample's contribution converted to linear RGB or XYZ. With hero-wavelength sampling of 4 wavelengths, sum c(λ_i)·L(λ_i)/p(λ_i), where c is the CMF/RGB basis.
     - At a query, multiply the cache's RGB by the path's spectral throughput, converted via the RGB→spectrum upsampling already used for albedo.
     - Error: throughput is a spectrum but the cache is RGB, so the product is a tristimulus approximation. Usually fine for diffuse indirect light, wrong for strong dispersion.
  2. **Wavelength-conditioned cache.**
     - Add λ, one-blob or frequency encoded, as an input. Output 1 scalar (or the 4 hero wavelengths at once, as 4 outputs and 4 inputs).
     - Training gets noisier per wavelength, and queries are per wavelength (4× inference cost unless batched as outputs).
     - Novel enough to be a student research contribution.
  3. **Spectral basis output.** Output coefficients of a low-dimensional spectral basis (e.g. 3–8 coefficients of a sigmoid/Fourier basis), then evaluate at any λ.
- The reflectance factorization in a spectral engine: multiply by the spectral (diffuse + specular) reflectance at the query wavelength(s), so the network learns a smoother factor. This should carry over well.
- The luminance-normalized relative L2 loss maps to a Y (from XYZ) normalization in a spectral pipeline.

### Gaps
- No spectral NRC paper was found. A deeper literature search (EGSR/HPG 2024–2026) may find work on spectral radiance caches.
- The full set of 2022–2026 NRC improvements (e.g. NVIDIA's internal changes between the 2021 paper and SDK v0.16, light-tracing or ReSTIR-integrated caches) was not exhaustively surveyed.
- Huang et al. NASG path guiding was not verified from the primary source.

---

## 6. Production uses: games and engines shipping NRC

### Takeaway
NRC ships mainly through RTX Remix:
- Half-Life 2 RTX demo, 2025-03-18: NVIDIA claims indirect lighting "up to 15% faster".
- The RTX Remix runtime, with Ultra/Medium presets and later memory and performance tuning.
- Quake III Arena RTX demo (GDC 2026).
- Community mods such as Left 4 Dead RTX.

No independent NRC-only benchmarks were found. AMD's equivalent (FSR Radiance Caching) was demoed in Warhammer 40,000: Darktide.

### Cited Findings
**Half-Life 2 RTX**
- NVIDIA, 2025-03-18: "Our first publicly released neural shader, Neural Radiance Cache, enhances the quality of fully ray-traced indirect lighting". It uses "a grid of AI neural networks across screen and world space", trains on live game data, and "runs up to 15% faster in Half-Life 2 RTX". RTX Remix 1.0 "introduces the first Neural Shader in games". Demo minimum: RTX 3060 Ti. — [NVIDIA GeForce news](https://www.nvidia.com/en-us/geforce/news/rtx-remix-half-life-2-rtx-demo-launching-march-18/)
- Hands-on: about 40–50 fps with DLSS Auto, 150+ fps with 4× Frame Generation (setup unclear). — [PC Gamer](https://www.pcgamer.com/hardware/i-just-played-half-life-2-rtx-a-fully-ray-traced-overhaul-of-the-original-and-its-meaty-headcrabs-have-me-hankering-for-more/)
- Live demo: NRC plus the DLSS transformer took MFG from about 235 to 250 fps. It doesn't isolate NRC. — [Tweaktown](https://www.tweaktown.com/news/102507/half-life-2-rtx-remix-with-nvidias-new-groundbreaking-neural-shaders-running-on-the-5090/index.html)

**RTX Remix release notes**
- 1.1: "Finetuned NRC training parameters to increase performance and lower memory usage". 4K DLSS-P, NRC Ultra: −166 MB. 1080p Balanced, NRC Medium: −92 MB. "FPS saw a 2% improvement at 1080p on 3060".
- 1.2: fixed rare NRC crashes.
- 1.5: "NRC Boost Emissives" (default on) "restores missing emissive lighting on indirect bounces when using Neural Radiance Caching". — [RTX Remix release notes](https://docs.omniverse.nvidia.com/kit/docs/rtx_remix/latest/docs/changelog/remix-releasenotes.html)
- A search snippet of the same notes says paths terminate "generally at 2-3 bounces, at which point an indirect radiance from a multitude of additional bounces is inferred by AI". This was not seen in the portion fetched. — [RTX Remix release notes](https://docs.omniverse.nvidia.com/kit/docs/rtx_remix/latest/docs/changelog/remix-releasenotes.html)

**Other titles**
- Quake III Arena RTX demo (around GDC 2026) includes NRC. — [NVIDIA GDC 2026 GeForce announcements](https://www.nvidia.com/en-us/geforce/news/gdc-2026-nvidia-geforce-rtx-announcements/)
- Left 4 Dead RTX (NoMercyStudios, Remix mod) supports NRC, DLSS RR and MFG. Ship status unconfirmed. — [GameGPU](https://en.gamegpu.com/news/igry/left-4-dead-rtx-poluchit-trassirovku-puti-i-sovremennye-tekhnologii-nvidia)
- A third-party blog says a later HL2 RTX update added SHaRC as an alternative to NRC (unverified). — [gamertagmythras blog](https://gamertagmythras.com/blog/half-life-2/half-life-2-rtx-update-4)

**AMD**
- FSR Radiance Caching demo in Warhammer 40,000: Darktide (Fatshark, Stingray). — [AMD GPUOpen](https://gpuopen.com/amd-fsr-radiancecaching/)
- Coverage notes adoption is slated for 2026, and one outlet argues it is "years away from real adoption". — [TechPowerUp](https://www.techpowerup.com/review/amd-fsr-4-redstone/)

### Inferences
- NRC's shipped benefit is modest in fps (2–15%). Its main value is fewer traced bounces plus lower noise for the denoiser and upscaler, which matches the paper's equal-error speedups vs PT+ReSTIR.

### Gaps
- No confirmation found that Portal with RTX received an NRC update. Big commercial titles (Cyberpunk 2077, Alan Wake 2, Indiana Jones, Doom: The Dark Ages, 007 First Light) were not confirmed to use NRC. NVIDIA's GDC 2026 path-traced game lists don't say which use NRC. — [NVIDIA GDC 2026 DLSS 4.5 news](https://www.nvidia.com/en-us/geforce/news/dlss-4-5-rtx-path-tracing-game-announcements-gdc-2026/)
- No independent NRC on/off benchmark found.

---

## 7. Recommended learning path for the ML parts

### Takeaway
Order: MLP + backprop by hand → Adam → encodings (frequency/one-blob → hash grid) → mixed precision and Tensor Cores → CUDA (fully fused MLPs via tiny-cuda-nn) → in-shader training (Slang autodiff / RTXNS / DX LinAlg). Each stage has a concrete primary resource. Doing a tiny "fit an image" or "fit radiance from offline data" exercise before online NRC isolates ML bugs from rendering bugs.

### Cited Findings
- Slang autodiff MLP tutorial: train a tiny MLP to fit polynomials. A good bridge from theory to shader code. — [Slang autodiff tutorial 2](https://shader-slanggithubio-aidanfnv-wip.readthedocs.io/en/latest/auto-diff-tutorial-2.html)
- RTXNS SimpleTraining doc: a forward/backward pass in shader code with linear-layer, activation and gradient helpers. — [RTXNS SimpleTraining](https://github.com/NVIDIA-RTX/RTXNS/blob/main/docs/SimpleTraining.md)
- SIGGRAPH 2025 neural shading course code: MLP training and a coop-vec variant. — [neural-shading-s25](https://gh.r-mj.com/shader-slang/neural-shading-s25)
- tiny-cuda-nn "learn an image" example (about 1000 steps in about 1 s on an RTX 4090). A first CUDA milestone. — [tiny-cuda-nn README](https://github.com/NVlabs/tiny-cuda-nn)
- NRC paper sections 3–5: the reference for the loss, encodings and training schedule. — [arXiv HTML v2](https://arxiv.org/html/2106.12372v2)
- Instant NGP, for hash encoding theory. — [arXiv 2201.05989](https://arxiv.org/abs/2201.05989)
- The NRC chapter of "Neural Rendering in Action" (Packt) has code on RTXNS. — [Packt repo](https://github.com/PacktPublishing/Neural-Rendering-in-Action/blob/main/Chapter07/src/nrc/README.md)

### Inferences
These widely known resources were not fetched this session; their URLs are the canonical locations.

**Suggested sequence**
1. **Intuition.** 3Blue1Brown "Neural networks" series (https://www.3blue1brown.com/topics/neural-networks).
2. **Backprop by hand.** Andrej Karpathy "Neural Networks: Zero to Hero" / micrograd (https://karpathy.ai/zero-to-hero.html, https://github.com/karpathy/micrograd). Write a 2-layer MLP with manual backprop in C++ on the CPU.
3. **Optimization.** Kingma & Ba, "Adam" (https://arxiv.org/abs/1412.6980). Implement Adam with bias correction plus a weight EMA (α = 0.99, as in NRC).
4. **Encodings.** Tancik et al., "Fourier Features Let Networks Learn High Frequency Functions" (https://arxiv.org/abs/2006.10739) for frequency encoding. Müller et al. 2019 "Neural Importance Sampling" (https://arxiv.org/abs/1808.03856) for one-blob encoding. Then Instant NGP's hash grid.
5. **Mixed precision and Tensor Cores.** Micikevicius et al., "Mixed Precision Training" (https://arxiv.org/abs/1710.03740), for loss scaling and fp16 weights with fp32 master copies. The NVIDIA CUDA C++ Programming Guide, WMMA / warp matrix section (https://docs.nvidia.com/cuda/cuda-c-programming-guide/).
6. **CUDA.** Kirk & Hwu, "Programming Massively Parallel Processors" (4th ed.). NVIDIA "An Even Easier Introduction to CUDA" (https://developer.nvidia.com/blog/even-easier-introduction-cuda/). Then read tiny-cuda-nn's `fully_fused_mlp.cu`.
7. **D3D12 ↔ CUDA interop.** NVIDIA CUDA Runtime API "External Resource Interoperability" (https://docs.nvidia.com/cuda/cuda-runtime-api/group__CUDART__EXTRES__INTEROP.html) and the NVIDIA cuda-samples `simpleD3D12` sample (https://github.com/NVIDIA/cuda-samples).
8. **In-shader.** Slang user guide, autodiff chapter (https://shader-slang.org/slang/user-guide/autodiff.html). RTXNS. DirectX LinAlg spec (microsoft/hlsl-specs proposal 0035).

**Milestone plan for the engine**
1. Offline: dump (position, normal, direction, albedo, roughness, radiance) records from the spectral tracer, then fit them with tiny-cuda-nn or PyTorch to check the encoding and loss.
2. SHaRC in the DX12 engine (both PCs).
3. NVIDIA NRC SDK in the engine (RTX PC) as a reference.
4. Own NRC: CUDA + tiny-cuda-nn via D3D12 interop.
5. Port inference, then training, to HLSL `dx::linalg` (SM 6.10) to run on the RX 9070 XT.
6. Spectral extensions (section 5 options).

### Gaps
- The resources in the Inferences list were not fetched or verified in this session (URLs are the known canonical locations). Check them before citing in a final report.
- No official NVIDIA "NRC from scratch" tutorial was found.
