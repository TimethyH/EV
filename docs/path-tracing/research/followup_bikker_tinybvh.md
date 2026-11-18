# Jacco Bikker, TinyBVH and what EV can take from them (follow-up research, 2026-11-18)

Scope: Jacco Bikker's TinyBVH library, his blog, his renderers and course material, and how they map onto the EV
path-tracing plan (`plan.md` D1–D18, M0–M11, M5c, O1/O2). Labels: [T] training knowledge, [I] inference,
[E] estimate, [U] unverified. Facts without a label come from the linked source, fetched 2026-11-18.
Local folders under `T:\Projects\Buas\Y1C` were only read, never changed.

## 1. TinyBVH: what it is, version, license, features, GPU kernels, demos, hardware-RT comparison

### Takeaway
TinyBVH is Bikker's MIT-licensed, dependency-free C++ BVH library. It is currently v1.9.2, merged to `main` on
2026-11-14, and under very active development. It has fast CPU builders (Bonsai, binned SAH, SBVH, H-PLOC,
reinsertion optimizer), refit, TLAS/BLAS instancing, and many layouts. Those include `BVH4_CPU` (fastest on CPU),
`BVH_GPU`, `BVH4_GPU`, `BVH8_CWBVH` (GPU) and the AMD RDNA3/RDNA4 hardware BVH formats. For EV the most important
find is `external/dx12rt/`, a **D3D12 benchmark** that traces Crytek Sponza with DXR `TraceRay`, inline RayQuery and
TinyBVH compute traversal (`BVH_GPU`, `BVH4_GPU`, CWBVH in HLSL), and prints MRays/s as a % of DXR. No published
DXR-vs-CWBVH numbers were found.

### Cited Findings
**Identity, version, license**
- The README says "Current version: 1.9.2". The only note for 1.9.2 is that "the `extreme` and `stochastic` flags are gone". There is no dated changelog and the GitHub Releases section is empty — [README](https://raw.githubusercontent.com/jbikker/tinybvh/main/README.md), [repo](https://github.com/jbikker/tinybvh)
- Recent `main` history: "Merge pull request #317 from jbikker/dev" and "Preparing for release to main." on **Nov 14, 2026**; "Update CITATION.cff" on Nov 17, 2026. In the days before that: a threaded `Compact`, faster Bonsai construction, a "Fix for the hang on bvh4gpu layouts" (Nov 12), a new `BVH_DOBB` layout (Nov 11), and an "Initial implementation of a C API" (Nov 14) — [commits](https://github.com/jbikker/tinybvh/commits/main)
- License is MIT ("This library is made available under the MIT license"). "This library has no dependencies." — [repo](https://github.com/jbikker/tinybvh)
- Packaging: "The library is `tiny_bvh.h` plus five headers next to it", namely `tiny_bvh_base.h`, `tiny_bvh_x86_float.h`, `tiny_bvh_x86_double.h`, `tiny_bvh_arm_float.h` and `tiny_bvh_arm_double.h`. "include only `tiny_bvh.h`, and define `TINYBVH_IMPLEMENTATION` in exactly one translation unit." So it is no longer strictly one single header — [README](https://raw.githubusercontent.com/jbikker/tinybvh/main/README.md)
- Language: "written in C++14 / 'Sane C++' … with C++17 for threading". It "builds with any C++17 compiler (C++14 without threading), including g++, clang, Visual Studio and Emscripten / WASM". A Visual Studio solution (`tiny_bvh_examples.sln`) and a CMake file are included. Threading uses a built-in `std::thread` pool, `TINYBVH_NO_BUILTIN_POOL` lets you bring your own, and `NO_THREADED_BUILDS` drops threading — [README](https://raw.githubusercontent.com/jbikker/tinybvh/main/README.md)
- SIMD: "x86-64 Windows/Linux: optimized SSE / AVX / AVX2 build and traversal kernels"; NEON on ARM. "Without AVX2 or NEON, every layout still builds and traverses on scalar code." — [README](https://raw.githubusercontent.com/jbikker/tinybvh/main/README.md)
- 36 contributors so far. PRs go to the `dev` branch — [README](https://raw.githubusercontent.com/jbikker/tinybvh/main/README.md)
- Used "in the wild" by Remedy's Northlight engine, EA SEED's Gigi, SideFX Houdini, TrenchBroomBFG, Wonderland Engine and unity-tinybvh, among others — [README](https://raw.githubusercontent.com/jbikker/tinybvh/main/README.md)

**Builders**
- **Bonsai** (the default): grid cells become mini trees built in parallel with the SIMD binned SAH builder, then a top tree joins them. **Bonsai HQ** (`useSpatialSplits`) adds SAH-guided triangle splitting. **Binned SAH** (AVX/NEON) becomes **SBVH** with `useSpatialSplits`, including "unsplitting". There is a scalar reference SAH, **presplitting** (`usePresplitting`, after Karras & Aila 2013), an experimental **full-sweep SAH** (`useFullSweep`), and **H-PLOC** (`useHPLOC`), which "produces trees identical to the NexusBVH CUDA code". That is a CPU implementation used for comparison, not a GPU builder — [repo](https://github.com/jbikker/tinybvh)
- `Optimize( iterations, mu, epsilon )` lowers SAH cost "by parallel reinsertion (Meister & Bittner, 2018)". The offline tool `bvhopt` "goes much further" for static scenes. Tree surgery: `Compact`, `MergeLeafs( maxLeafPrims )`, `SplitLeafs`, `CollectNodes`, `Depth`, `SAHCost`, `EPOCost`. `Save`/`Load` serialize several layouts — [README](https://raw.githubusercontent.com/jbikker/tinybvh/main/README.md)
- Basic API: `bvh.Build( triangles, TRIANGLE_COUNT )` takes a flat list of 16-byte `float4` vertices, three per triangle. `tinybvh::Ray ray( O, D ); bvh.Intersect( ray );` returns `ray.hit.t` (`1e30f` = `BVH_FAR` on a miss), `hit.prim`, `hit.u` and `hit.v`. `IsOccluded` is about 30 % faster than `Intersect`. Spatial splits plus optimization give "up to about 30 % faster traversal" for roughly 15 % more memory. "SBVHs cannot be rebuilt or refitted" — [Basic Use manual](https://jacco.ompf2.com/2025/01/24/tinybvh-manual-basic-use/)
- Indexed input `Build( verts, indices, count )` is "roughly 5 % slower during traversal". `bvhvec4slice` reads strided, interleaved vertex buffers. The SIMD builders need 16-byte-aligned vertices. `TINYBVH_USE_CUSTOM_VECTOR_TYPES` maps the `bvh*` types to your own math types. `INST_IDX_BITS` (default 32) can pack instance and primitive indices into 16-byte hit data. `HQBVHBINS` sets the SBVH bin count (default 8). There is a `BVHContext` allocator hook (`malloc64`/`free64`) and custom primitives (`customIntersect`, `customIsOccluded`). `IntersectSphere` exists for the default layout only — [Advanced manual](https://jacco.ompf2.com/2025/01/25/tinybvh-manual-advanced/)
- The manual's stance: "BVHs are built on the CPU, because CPU builders often produce better trees and CPUs are often underused" — [Advanced manual](https://jacco.ompf2.com/2025/01/25/tinybvh-manual-advanced/)

**Layouts**
- `BVH` (32-byte nodes, the base for every other layout), `BVH_Verbose` (64 B, with parent pointers), `MBVH<M>`, `BVH4_CPU` ("The fastest option on CPU", AVX2/NEON), `BVH8_CPU` (AVX2 8-wide, "left in for reference"), `BVH_GPU` (64-byte nodes holding both child AABBs, after Aila & Laine 2009, "for a simple GPU kernel"), `BVH4_GPU` ("compact quantized 4-wide format for GPU ray tracing"), `BVH8_CWBVH` ("80-byte compressed 8-wide nodes for state-of-the-art GPU traversal"), `BVH4_AMD_HW` ("AMD hardware BVH4 format (BOX32 and triangle nodes), RDNA3 and up"), `CWBVH8_AMD_HW` ("AMD RDNA4 hardware BVH8 format … up to eight triangle pairs"), `BVH_DOBB` (experimental, rotated child boxes), `VoxelSet` (256³ bricks, usable as a BLAS), and `BVH_Double`/`BVH4_Double` — [repo](https://github.com/jbikker/tinybvh), [README](https://raw.githubusercontent.com/jbikker/tinybvh/main/README.md)
- There is no `BVH_SoA` layout in the current README. It may have been removed or renamed [U].
- `BVH4_CPU` gives about "3.5× more rays per second" than the base layout, roughly doubles memory (the original BVH is kept) and needs AVX2/NEON — [Basic Use manual](https://jacco.ompf2.com/2025/01/24/tinybvh-manual-basic-use/)
- GPU layout guidance: `BVH_GPU` has "short, simple traversal kernels that work in any shader". `BVH4_GPU` is wider and shallower and suits divergent rays. `CWBVH` is "fast to traverse, costly to build, best for static geometry", with a complex kernel — [Advanced manual](https://jacco.ompf2.com/2025/01/25/tinybvh-manual-advanced/)

**Refit, TLAS**
- "`Refit` updates bounds for moved triangles: much faster than a rebuild, and fine for subtle animation. It does not work when the triangle count changes, nor for trees with spatial splits." — [README](https://raw.githubusercontent.com/jbikker/tinybvh/main/README.md). The manual gives "under 1 ms for 200k triangles" for a refit, and says a build of about 200k triangles takes under 10 ms — [Basic Use manual](https://jacco.ompf2.com/2025/01/24/tinybvh-manual-basic-use/)
- "A Top-Level Acceleration Structure is a BVH over instances, each referencing a BLAS with a 4x4 transform. … Changing transforms and rebuilding the (small) TLAS gives cheap rigid animation." BLASes of different layouts can be mixed — [README](https://raw.githubusercontent.com/jbikker/tinybvh/main/README.md)

**Performance claims**
- CPU: "TinyBVH builds a SAH BVH faster than Embree and Madmann91, and traces individual primary and occlusion rays faster than those libraries". The README calls this "preliminary" and notes that "single-ray traversal is only a small part of Embree". The numbers are only in a chart image. The manual says "hundreds of millions of rays per second" on a modest CPU — [README](https://raw.githubusercontent.com/jbikker/tinybvh/main/README.md), [Basic Use manual](https://jacco.ompf2.com/2025/01/24/tinybvh-manual-basic-use/)
- GPU: "TinyBVH traces up to 5 billion rays per second on a RTX5080 laptop GPU in Crytek's Sponza … regardless of graphics API and without relying on ray tracing hardware" ("5 rays per pixel at 4k@120Hz") — [README](https://raw.githubusercontent.com/jbikker/tinybvh/main/README.md)

**GPU kernels and demos (exact file names)**
- `kernels/`: `traverse_bvh2.cl`, `traverse_bvh4.cl`, `traverse_cwbvh.cl`, `traverse_tlas.cl`, `traverse.cl`, `traverse_amd_hw.cl`, `traverse_amd_hw_bvh4.cl`, `traverse_amd_hw_bvh8.cl`, `wavefront.cl`, `wavefront2.cl`, `wavefront_amd_hw.cl`, `wavefront_amd_hw_bvh8.cl`, `raytracer.cl`, `bench.cl`, `tools.cl`, `rocap.cl`, `hairviewer.cl`, and `traverse_cwbvh.slang` (a Slang port by Daniel Johansson that "compiles to SPIR-V, Metal, HLSL") — [kernels/](https://github.com/jbikker/tinybvh/tree/main/kernels), [README](https://raw.githubusercontent.com/jbikker/tinybvh/main/README.md)
- `examples/`: `tiny_bvh_minimal.cpp`, `tiny_bvh_minimal_gpu.cpp`, `tiny_bvh_minimal_amd_hwrt.cpp`, `tiny_bvh_viewer.cpp`, `tiny_bvh_renderer.cpp`, `tiny_bvh_pt.cpp` ("multi-threaded path tracer"), `tiny_bvh_speedtest.cpp`, `tiny_bvh_gpu.cpp`, `tiny_bvh_gpu2.cpp` ("OpenCL TLAS/BLAS traversal"), `tiny_bvh_gpu_hair.cpp`, `tiny_bvh_amd_hwrt.cpp`, `tiny_bvh_amd_hwrt_bvh8.cpp`, `tiny_bvh_instancing.cpp`, `tiny_bvh_instancing_double.cpp`, `tiny_bvh_collide.cpp`, `tiny_bvh_custom.cpp`, `tiny_bvh_custom_double.cpp`, `tiny_bvh_dobb_test.cpp`, `tiny_bvh_foliage.cpp`, `tiny_bvh_gltf.cpp`, `tiny_bvh_gltf_lib.cpp`, `tiny_bvh_optimizer.cpp`, `tiny_bvh_raybundle.cpp`, `tiny_bvh_voxel.cpp`. The README also names `tiny_bvh_benchmark.cpp` and `tiny_bvh_gl_compute` — [examples/](https://github.com/jbikker/tinybvh/tree/main/examples), [README](https://raw.githubusercontent.com/jbikker/tinybvh/main/README.md)
- "GPU traversal examples are provided for OpenCL, OpenGL compute, DirectX12 and Vulkan." "The 'external' folder holds DXR and Vulkan benchmarks against TinyBVH; the Vulkan one also has OpenCL backends for the AMD hardware layouts." The folders are `external/dx12rt`, `external/vulkanrt`, `external/embree`, `external/madmann91/bvh/v2` and `external/OpenCL` — [README](https://raw.githubusercontent.com/jbikker/tinybvh/main/README.md), [external/](https://github.com/jbikker/tinybvh/tree/main/external)
- **`external/dx12rt/`** contains `program.cpp`, `shader.hlsl`, `tiny.hlsl`, `tiny4.hlsl`, `tiny8.hlsl`, `tinyrq.hlsl`, `presplit.h` and `minimal.sln`/`.vcxproj` — [dx12rt/](https://github.com/jbikker/tinybvh/tree/main/external/dx12rt)
  - `program.cpp` compares five backends round-robin, one per frame:
    - DXR `TraceRay`, used as the reference.
    - `BVH_GPU` in compute (`tiny.hlsl`, an SBVH-built BVH2 with 64-byte nodes).
    - `BVH4_GPU` in compute (`tiny4.hlsl`).
    - CWBVH in compute (`tiny8.hlsl`).
    - Inline RayQuery against the same TLAS (`tinyrq.hlsl`, only when the device reports tier 1.1, and off by default for printing).
  - Setup: precomputed rays from `raysets/view3rays.bin` (likely primary rays [I]), scene `testdata/cryteksponza.bin`, 1024×1024, a timestamp query heap, 20 dispatches per backend, exponentially smoothed MRays/s printed as `"%s: %6.1f MRays/s"` with a % of DXR. It uses the system `d3d12.h` at feature level 12_1 and precompiled `.fxh` shader headers. There is no Agility SDK.
  - The comments contain no measured numbers, and one credits "Sonnet 5 and Claude" for help with extensions — [program.cpp](https://github.com/jbikker/tinybvh/blob/main/external/dx12rt/program.cpp)
- AMD hardware layouts: "`BVH4_AMD_HW` and `CWBVH8_AMD_HW` are traversed from OpenCL using AMD's BVH intersection intrinsics." — [README](https://raw.githubusercontent.com/jbikker/tinybvh/main/README.md)

**Comparisons with hardware RT, Embree, HIP RT**
- The README compares only against Embree and Madmann91 (CPU build and single-ray tracing). It says GPU results are achieved "without relying on ray tracing hardware" and reports no hardware-RT numbers. HIP RT is not mentioned — [repo](https://github.com/jbikker/tinybvh)
- A web search found no published head-to-head CWBVH-compute vs DXR/Vulkan-RT benchmark. A Unity compute port notes that compute ray tracing is "not as fast as hardware ray tracing" — [ComputeTinyRT](https://github.com/unitycoder/ComputeTinyRT), [unity-tinybvh](https://github.com/andr3wmac/unity-tinybvh)
- In "BVH Quality: Beyond SBVH" (2025), the GPU tests ran on an AMD 780M with compute traversal, not RT units. Comparison with hardware BVHs is left as an open question — [blog](https://jacco.ompf2.com/2025/05/20/bvh-quality-beyond-sbvh/)
- BUas lists a Bikker guest talk "Building BVHs in the Age of RTX" (12 Dec 2024) — [BUas Pure profile](https://pure.buas.nl/en/persons/dr-jacco-bikker/)

### Inferences
- [I] `external/dx12rt` is the closest existing analogue to the proposed EV "software vs hardware traversal" comparison, written in the same API. The student can read `tiny.hlsl` (BVH2 stack traversal), `tiny8.hlsl` (CWBVH) and `tinyrq.hlsl` (RayQuery) as references, then write EV's own versions. The rule "all code by the developer" stays intact if they are treated like the pbrt source: read, understand, re-implement.
- [I] Because the benchmark uses only precomputed (probably coherent primary) rays, its % of DXR will flatter compute traversal. EV should also measure incoherent secondary rays (bounce 1–3 directions dumped from the path tracer). On RDNA 4 and Ampere the hardware gap is expected to widen for incoherent rays [E].
- [E] Expected outcome on EV's GPUs: inline RayQuery / DXR beats compute CWBVH on both the RX 9070 XT (RDNA 4 RT units, BVH8 boxes) and the RTX 3070 (RT cores), by a factor of ~1.5–4× for secondary rays. This is an estimate to replace with measurements, not a published number.
- [I] The 1.9.x line changes fast: flags were removed in 1.9.2 and the DOBB layout and C API were added in the week before. EV should vendor a pinned commit (copy the headers in, note the commit hash) rather than track `main`.
- [I] MSVC: Visual Studio is named as supported and a `.sln` ships, so C++17 MSVC builds are routine. VS 2026 / MSVC 14.5x specifically was not checked [U]. `/arch:AVX2` is needed for the fast `BVH4_CPU` path, otherwise the scalar fallback is used [I].

### Gaps
- No release date for 1.9.2 beyond the Nov 14, 2026 merge "Preparing for release to main" (the Releases page is empty).
- The `BVH_SoA` status was not confirmed (absent from the current README).
- No measured DXR vs CWBVH numbers from Bikker or anyone else were found. `program.cpp` prints them but publishes none.
- The `vulkanrt` folder contents and the `tmpl8_compute` folder were not inspected. Exact CPU rays/s figures are only in a chart image.
- Whether TinyBVH has a GPU-side builder: none was found. H-PLOC is a CPU re-implementation for comparison.

## 2. Bikker's blog (jacco.ompf2.com): relevant posts and what each teaches

### Takeaway
The blog is a structured, beginner-to-intermediate curriculum that maps closely onto EV's M1–M5c:
- the 10-part "How to build a BVH" series (BVH basics → SAH → binning → refit → TLAS → shading → Whitted → OpenCL GPU);
- 2 "Probability theory for PBR" posts (Monte Carlo, pdfs, importance sampling, light picking);
- the voxel series (stochastic AA, stratification, blue noise + golden-ratio animation, reprojection, path tracing with cosine sampling);
- the "Wavefront Path Tracing" post (Lighthouse 2's generate/extend/shade/connect design);
- a 4-part optimization series (profiling, low-level, SIMD);
- "BVH Quality: Beyond SBVH" (SAH vs a ray-set metric).

There are no ReSTIR posts.

### Cited Findings
**"How to build a BVH" (all from the article index)** — [articles](https://jacco.ompf2.com/articles/)
1. Basics: data structure, simple construction, ray traversal — [part 1](https://jacco.ompf2.com/2022/04/13/how-to-build-a-bvh-part-1-basics/)
2. Surface Area Heuristic and ordered traversal — [part 2](https://jacco.ompf2.com/2022/04/18/how-to-build-a-bvh-part-2-faster-rays/)
3. Binned BVH building — [part 3](https://jacco.ompf2.com/2022/04/21/how-to-build-a-bvh-part-3-quick-builds/)
4. Animated models, refitting and rebuilding — [part 4](https://jacco.ompf2.com/2022/04/26/how-to-build-a-bvh-part-4-animation/)
   - Refit visits nodes "from the last allocated node back to the first", because a child index is always greater than its parent's. Leaves recompute bounds from their triangles; interior nodes enclose their children.
   - Example: a ~20,944-triangle rebuild took ~30 ms, a refit ~0.5 ms.
   - Quality degrades when triangles move far from their node and boxes overlap. Advice: "refit several frames, then rebuild periodically". Refit needs the same triangle count.
   - There are no numbers for the quality loss — [part 4](https://jacco.ompf2.com/2022/04/26/how-to-build-a-bvh-part-4-animation/)
5. Top-level BVH (TLAS) — [part 5](https://jacco.ompf2.com/2022/05/07/how-to-build-a-bvh-part-5-tlas-blas/)
6. TLAS & BLAS together — [part 6](https://jacco.ompf2.com/2022/05/13/how-to-build-a-bvh-part-6-all-together-now/)
7. Textured and shaded meshes — [part 7](https://jacco.ompf2.com/2022/05/20/how-to-build-a-bvh-part-7-consolidate/)
8. Whitted-style ray tracing — [part 8](https://jacco.ompf2.com/2022/05/27/how-to-build-a-bvh-part-8-whitted-style/)
9. OpenCL GPU ray tracing, part 1 — [part 9a](https://jacco.ompf2.com/2022/06/03/how-to-build-a-bvh-part-9a-to-the-gpu/)
10. OpenCL GPU ray tracing, part 2 — [part 9b](https://jacco.ompf2.com/2022/06/15/how-to-build-a-bvh-part-9b-massive/)
- The TinyBVH README points newcomers to this series ("New to BVHs? Start with this series of articles") — [README](https://raw.githubusercontent.com/jbikker/tinybvh/main/README.md). The URL that `knowledge.md` marks [U] (part 1) is **verified**.

**"Probability Theory for Physically Based Rendering"**
- Part 1 covers:
  - Riemann sums and the curse of dimensionality;
  - expected value, variance and standard deviation;
  - Monte Carlo integration (with C code);
  - stratification;
  - pdfs (they integrate to 1, and their values are densities, not probabilities);
  - importance sampling (divide by the pdf; the ideal pdf ∝ the integrand).

  It does not cover CDFs. The audience is rendering developers who are weak on probability — [part 1](https://jacco.ompf2.com/2019/12/11/probability-theory-for-physically-based-rendering/)
- Part 2 covers:
  - the rendering equation (three-point and hemispherical forms, L = Lₑ + TL);
  - path tracing as Monte Carlo;
  - the cosine pdf (ω·n)/π;
  - splitting direct and indirect light (indirect rays ignore emitters);
  - the solid-angle light pdf, approximated as SA ≈ A·(n_light·ω)/d² when the light is small;
  - picking one of n lights uniformly and multiplying by n;
  - importance-sampling lights ∝ I·SA through a CDF.

  It does **not** cover MIS or Russian roulette — [part 2](https://jacco.ompf2.com/2019/12/13/probability-theory-for-physically-based-rendering-part-2/)

**"Ray Tracing with Voxels in C++" (2024, uses github.com/jbikker/voxpopuli)**
- Part 1: the template, lights and shadows. `Scene::FindNearest`, `Scene::IsOccluded`, OpenMP rows, `WORLDSIZE 128` — [part 1](https://jacco.ompf2.com/2024/04/24/ray-tracing-with-voxels-in-c-series-part-1/)
- Part 2: Whitted (reflections, recursion, glass) — [part 2](https://jacco.ompf2.com/2024/05/01/ray-tracing-with-voxels-in-c-series-part-2/)
- Part 3: anti-aliasing and soft shadows (stochastic techniques) — [part 3](https://jacco.ompf2.com/2024/05/08/ray-tracing-with-voxels-in-c-series-part-3/)
- Part 4: stratification and blue noise — [part 4](https://jacco.ompf2.com/2024/05/15/ray-tracing-with-voxels-in-c-series-part-4/)
  - The 64×64 `LDR_RG01_0.png` from Christoph Peters is indexed `(x & 63) + (y & 63) * 64`, R and G channels.
  - It is animated per frame with `r0 = fracf( red / 256.0f + frame * 0.61803399f )`. The source is RTG II ch. 24 (Alan Wolfe).
  - It also shows the pattern artifacts that correlated stratification across pixels causes.
- Part 5: reprojection (converging under movement) — [part 5](https://jacco.ompf2.com/2024/05/22/ray-tracing-with-voxels-in-c-series-part-5/), plus the standalone post [Reprojection in a Ray Tracer](https://jacco.ompf2.com/2024/01/18/reprojection-in-a-ray-tracer/)
- Part 6: path tracing basics and importance sampling — [part 6](https://jacco.ompf2.com/2024/05/29/ray-tracing-with-voxels-in-c-series-part-6/)
  - The throughput update is `throughput *= brdf * (dot(N,R) / pdf)` with the Lambert BRDF albedo/π.
  - Cosine sampling uses `normalize( N + normalize( R ) )` with pdf `dot(N,R)/PI`.
  - The check is equal total image energy (~3834000) with lower variance.
  - Accumulation uses `scale = 1/spp++` and resets on camera move.
  - NEE and Russian roulette are only mentioned as next steps, and fireflies are not covered.
- Parts 7–9 (denoising fundamentals, multi-level grid, TLAS) are listed but not linked, so they may be unpublished — [articles](https://jacco.ompf2.com/articles/)

**"Wavefront Path Tracing" (2019)**
- Four kernels: **Generate** (primary rays plus a count), **Extend** (nearest hit), **Shade** (writes continuation rays *and* NEE shadow rays to separate buffers) and **Connect** (any-hit shadow test).
- Buffers: rays are 32 B (origin and direction padded to 16 B each for single 128-bit loads). At 1080p: primary ~64 MB, hits ~32 MB (128 bit), extension ~64 MB, shadow ~64 MB, so ≈224 MB total and ~1 GB at 4K.
- Compaction: Shade appends survivors with atomic counters. Counters stay on the GPU.
- Shadow rays: Lighthouse 2 batches them outside the bounce loop, with an upper bound of X·N rays.
- Not covered: no benchmark numbers ("not easy to prove"), no MIS. A reader comment warns that per-material queues sized to the full image scale poorly in memory — [blog](https://jacco.ompf2.com/2019/07/18/wavefront-path-tracing/)

**Optimization series** — [articles](https://jacco.ompf2.com/articles/)
- [OPT#1 Profiling](https://jacco.ompf2.com/2020/03/18/opt1profiling/)
- [OPT#2 Low-level](https://jacco.ompf2.com/2020/03/24/opt2lowlevel/)
- [OPT#3 SIMD 1](https://jacco.ompf2.com/2020/05/12/opt3simd-part-1-of-2/)
- [OPT#4 SIMD 2](https://jacco.ompf2.com/2020/05/18/opt4simd-part-2-of-2/)
- [Optimizing Trilinear Interpolation](https://jacco.ompf2.com/2020/04/10/optimizing-trilinear-interpolation/)

**"BVH Quality: Beyond SBVH" (2025-05-20)**
- SBVH cuts SAH cost by "25 % or more" in many scenes. The bin count matters: tuning beats the 8-bin default by +1.6 % (Sponza) to +9.6 % (Conference) on the ray-set metric.
- "SAH is a good metric for CPU performance, while RRS [representative ray set] seems to predict GPU performance better."
- Bistro relative to full sweep (CPU/GPU): H-PLOC −0.44 %/−9.44 %, binned −6.13 %/−21.66 %, SBVH(ours) +65.63 %/+89.99 %.
- There is a `tiny_bvh_optimizer` project to reproduce the results — [blog](https://jacco.ompf2.com/2025/05/20/bvh-quality-beyond-sbvh/)

**Other**
- 2026 post: [Rendering for DELTA – 3D on MSX2](https://jacco.ompf2.com/2026/03/07/rendering-for-delta-3d-on-msx2/) (not path-tracing related).
- Historical flipcode "Raytracing Topics & Techniques" parts 1–7 (2004): Phong, Beer's law refraction, spatial subdivision, soft shadows, kd-trees — e.g. [part 3 Refractions and Beer's Law](https://www.flipcode.com/archives/Raytracing_Topics_Techniques-Part_3_Refractions_and_Beers_Law.shtml)

### Inferences
- [I] For EV's water (M5), flipcode part 3 (Beer's law) is a gentle first read before the Pope & Fry spectral absorption in `knowledge.md` §5.1.
- [I] Bikker's "SAH for CPU, ray-set metric for GPU" finding gives a reason to measure the ocean BLAS quality with actual traced rays (e.g. the M2 debug-view rays/s) instead of trusting build flags.
- [I] Voxel part 4's blue-noise recipe (Peters' texture + golden-ratio offset per frame) is a cheap upgrade the student already knows. It fits the "sampler upgrade" step of M3 as a stepping stone before Sobol/Owen, and the blue-noise item of O1/M8b.

### Gaps
- No Bikker posts on ReSTIR, MIS or Russian roulette were found on the blog. MIS and bidirectional methods appear only in his Utrecht slides (section 3).
- The contents of parts 9a/9b (OpenCL GPU ray tracing) were not fetched in detail. Only their titles were verified.

## 3. Other renderers, frameworks, course material and talks

### Takeaway
- **Arauna** was a real-time ray tracer (2004–2010) and **Brigade** a real-time GPU path tracer for games. Brigade 2 was used by OTOY, and Bikker's 2012 Delft PhD is "Ray Tracing for Real-time Games".
- **Lighthouse 2** is an Apache-2.0 research framework with OptiX 7 wavefront, OptiX Prime and Vulkan render cores, and SVGF denoising.
- His **Utrecht INFOMAGR "Advanced Graphics"** slides (2022/23) are public PDFs and cover path tracing, variance reduction, GPU/wavefront ray tracing, bidirectional/MIS and filtering. By 2024/25 the course was coordinated by Peter Vangorp.

### Cited Findings
- PhD: "Ray Tracing for Real-time Games", 2012, Delft University — [publications](https://jacco.ompf2.com/publications/). The thesis PDF is at [jbikker.github.io](https://jbikker.github.io/literature/Ray%20Tracing%20in%20Real-time%20Games%20-%202012.pdf) (not opened).
- "The Brigade Renderer: a Path Tracer for Real-time Games", 2013, *Int. J. of Computer Games Technology* (Bikker & van Schijndel); "Improving Data Locality for Efficient In-Core Path Tracing", 2012, *CGF* — [publications](https://jacco.ompf2.com/publications/), [dblp](https://dblp.org/rec/journals/ijcgt/BikkerS13.html)
- Bikker is "the author of the Arauna real-time ray tracer and the Brigade real-time path tracer … designed specifically with games in mind". Brigade 2 was "fully GPU based" and used by OTOY — [SIGGRAPH history](https://history.siggraph.org/?p=58053), [OTOY Brigade](https://brigade.otoy.com), [raytracey blog (secondary)](https://dcmpx.remotevs.com/com/blogspot/raytracey/PL/search/label/brigade). Arauna is dated 2004–2010 in search snippets only [U].
- Supervised theses relevant to EV:
  - "Beyond Spatio-Temporal Variance Guided Filtering" (2018, Voorhuis);
  - "Q-Learned Importance Sampling … on the GPU" (2018);
  - "Fast Divergent Ray Traversal by Batching Rays in a BVH" (2016);
  - "Accelerating Ray Tracing with Origin Offsets" (WSCG 2020);
  - "Assessing Alternatives to the Surface Area Heuristic" (2023) — [publications](https://jacco.ompf2.com/publications/)
- Lighthouse 2 is a "modular framework" developed at Utrecht University:
  - Cores: OptiX 7.2 (wavefront/streaming for RTX), OptiX 5 Prime (pre-RTX), a Vulkan core, and a BDPT core.
  - Denoising: SVGF plus an improved SVGF for specular/glossy reprojection.
  - License: Apache 2.0. Speed claim: "hundreds of millions of rays per second". Requires VS 2022 / CUDA 11.x.
  - It shows no archive notice, and no recent activity was observed — [lighthouse2](https://github.com/jbikker/lighthouse2)
- The wavefront post describes the LH2 OptiX Prime core: `generateEyeRays` → `rtpQueryExecute` → `shade` in `pathtracer.cu`, with plain device pointers shared with CUDA — [blog](https://jacco.ompf2.com/2019/07/18/wavefront-path-tracing/)
- Utrecht INFOMAGR 2022/23 slide PDFs (by Bikker, "November 2022 – February 2023") are online, e.g.:
  - [lecture 01 intro](https://ics-websites.science.uu.nl/docs/vakken/magr/2022-2023/slides/lecture%2001%20-%20intro.pdf)
  - [lecture 04 path tracing](https://ics-websites.science.uu.nl/docs/vakken/magr/2022-2023/slides/lecture%2004%20-%20path%20tracing.pdf)
  - [lecture 10 GPU ray tracing (2)](https://ics-websites.science.uu.nl/docs/vakken/magr/2022-2023/slides/lecture%2010%20-%20GPU%20ray%20tracing%20(2)%20v2.pdf) (wavefront)
  - [lecture 13 bidirectional](https://ics-websites.science.uu.nl/docs/vakken/magr/2022-2023/slides/lecture%2013%20-%20bidirectional.pdf) (MIS, photon mapping)
  - [lecture 15 filtering](https://ics-websites.science.uu.nl/docs/vakken/magr/2022-2023/slides/lecture%2015%20-%20filtering.pdf)
  - These come from the search index; the PDFs were not opened — [course 2022/23](https://ics-websites.science.uu.nl/docs/vakken/magr/2022-2023/index.html)
- INFOMAGR 2024/25:
  - Lectures: Intro, Whitted, Acceleration structures, BVH, Light transport, Path tracing, Variance reduction (1)+(2), GPU ray tracing, Various, Bidirectional, Probability theory, Resampling, Filtering.
  - Coordinator: Peter Vangorp.
  - Assignments: (1) implement and compare ≥2 acceleration structures, (2) implement a recent paper.
  - Frameworks: the optional template `tmpl8rt_UU`, with Lighthouse 2 and WrldTmpl8 listed as "interesting renderers". TinyBVH is not mentioned — [course 2024/25](https://ics-websites.science.uu.nl/docs/vakken/magr/2024-2025/index.html)
- A Utrecht student template: [jbikker/advgrtmpl8](https://github.com/jbikker/advgrtmpl8) ("intended for students of Utrecht University", public domain).

### Inferences
- [I] Design ideas taught across this material that map onto EV:
  - wavefront with compacted queues and separate shadow-ray batches (M5c);
  - NEE with light picking ∝ power·solid angle (M3);
  - cosine and BRDF importance sampling (M3);
  - stratification and blue noise with golden-ratio animation (M3 sampler / O1);
  - reprojection for convergence under motion (M8b);
  - SVGF and its specular extension (M8b);
  - MIS and bidirectional methods in the slides (M3 MIS, M11 caustics).

  Firefly handling did not appear in the posts read, though it may be in the "Variance reduction" lectures [U].
- [I] The 2022/23 lecture 10 "GPU ray tracing (2)" is the best Bikker source for M5c beyond the 2019 blog post, but its contents are unverified.

### Gaps
- The slide PDFs were not opened, so their exact topics (firefly clamping, blue noise, ReSTIR in "Resampling") are [U].
- No video lectures or recorded talks were found publicly. The 2024 talk "Building BVHs in the Age of RTX" has no public recording found.
- No 2025/26 INFOMAGR page was checked.

## 4. What EV can take from this (mapped to the plan)

### Takeaway
The clear wins are cheap and educational:
- (f) Bikker's articles as M1/M3/M5c reading;
- (c) writing a small SAH/binned BVH + refit by hand on the CPU (his series is the textbook for it);
- (b) a compute-shader BVH traversal comparison against inline RayQuery, modelled on TinyBVH's own `external/dx12rt` benchmark.

A full CPU reference path tracer on TinyBVH (a) has real debugging value but duplicates the Mitsuba/pbrt references and costs a lot. It is best kept small: a "pdf/BSDF unit-test + tiny CPU tracer for one scene". Non-render uses (d) and the WARP fallback are weak, because WARP already does DXR 1.1.

### Cited Findings (facts each proposal rests on)
- WARP NuGet supports DXR 1.1 since 1.0.8 (latest 1.0.22), so WARP does not need a software-BVH fallback for tracing — `knowledge.md` §6.9, citing [NuGet](https://www.nuget.org/packages/Microsoft.Direct3D.WARP)
- TinyBVH ships HLSL compute traversal for `BVH_GPU`, `BVH4_GPU` and CWBVH next to DXR `TraceRay` and inline RayQuery in one D3D12 program — [program.cpp](https://github.com/jbikker/tinybvh/blob/main/external/dx12rt/program.cpp)
- CPU path tracer example: `tiny_bvh_pt.cpp` ("multi-threaded path tracer") — [README](https://raw.githubusercontent.com/jbikker/tinybvh/main/README.md)
- Refit cannot be used on SBVH, and the triangle count must stay fixed. It takes <1 ms for 200k triangles on the CPU — [Basic Use manual](https://jacco.ompf2.com/2025/01/24/tinybvh-manual-basic-use/)
- MIT license, no dependencies, C++17, scalar fallback without AVX2 — [repo](https://github.com/jbikker/tinybvh)
- TinyBVH has `bvh.IntersectSphere` and `tiny_bvh_collide.cpp`, so it can do picking and collision-style queries — [Advanced manual](https://jacco.ompf2.com/2025/01/25/tinybvh-manual-advanced/), [examples/](https://github.com/jbikker/tinybvh/tree/main/examples)

### Inferences (per option: value / cost / milestone / risks)

**(a) CPU reference path tracer on TinyBVH ("M3-ref")**
- *Value*:
  - ground truth computed from EV's own scene data (`.evscene`, same meshes, same material struct, same camera), with no Mitsuba/pbrt export step;
  - pixel-level debugging: the same seed and the same path → print every pdf, MIS weight and throughput in a debugger, which is impossible in HLSL;
  - unit tests for BSDF sampling and evaluation (pdf integrates to 1, sample/eval consistency, white furnace) that run on **CI without a GPU**.

  [I]
- *Cost*: medium to high [E]. A second implementation of the path loop, BSDFs and lights in C++. Spectral (M4) and water (M5) roughly double it. The cost drops a lot if the BSDF/sampling math is written in a shared header that compiles as both HLSL and C++ (macro shims for `float3`, `dot`, `lerp`, ...). That is a learning exercise in itself, with the usual traps: HLSL `lerp` vs C++, `mul` order, `asuint` [I].
- *Milestone*: right after M3 step (3) (Lambert + white furnace) as an optional side track. Extend it at M4 (spectral round-trip tests) and M5 (TIR angle, slab transmittance tests, which are perfect CPU unit tests).
- *Risks*:
  - it can drift from the GPU code unless the math is shared;
  - TinyBVH API churn (pin a commit);
  - `/arch:AVX2` must be on for `BVH4_CPU` speed (CI x64 runners support AVX2 [I]; the scalar path works anyway);
  - VS 2026 compile not verified [U];
  - the ocean needs the displaced grid on the CPU (read back the M2 displace buffer once at frozen time, or reimplement the displacement; readback is simpler).

  License (MIT) is fine: keep the license text next to the vendored headers.
- *Verdict* [I]: worth it in a **reduced form**. A CPU "math test bench" (BSDF/pdf/spectral unit tests + a tiny tracer for a Cornell box and the frozen ocean patch at low resolution) gives most of the value. Mitsuba 3 / pbrt-v4 stay the independent ground truth: a self-written CPU tracer can share the GPU tracer's bugs if it shares its math.

**(b) Software BVH traversal in compute vs DXR hardware ("M5d")**
- *Value*:
  - EV's AS is opaque. A self-built BVH in a `StructuredBuffer` that the student traverses in HLSL teaches stack traversal, ordered child visits, ray/AABB slabs, the watertight ray/triangle test and (with CWBVH) node compression.
  - It also makes the M5c divergence story concrete: traversal divergence shows up in RGP/Nsight as a compute shader, whereas RayQuery hides it in fixed-function units.
  - The `dx12rt` benchmark is a ready reference design and a sanity check for the student's numbers on the 9070 XT and the 3070.
- *Cost*: low to medium for BVH2 (`BVH_GPU` layout, ~100–150 lines of HLSL [E]) and high for CWBVH (complex decoding; treat as stretch).
- *Milestone*: after M3 (the path tracer exists and its rays can be dumped). Most natural is next to M5c, as the same "software vs hardware" experiment family (M5c = sorting vs SER, M5d = traversal vs RT units). Measure primary, NEE-shadow and bounce-2 rays separately.
- *Fallback value*: low [I]. WARP already supports DXR 1.1 (§6.9), and both GPUs have RT hardware. The only real fallback case is a GPU without tier 1.1, which EV doesn't target.
- *Risks*: none on license or maintenance if the student writes the HLSL and only uses TinyBVH (vendored) on the CPU to build and convert. The ocean case needs a per-frame CPU refit + upload of 524,288 triangles (~2–3 ms CPU [E], ~10 MB upload [E]), or a GPU refit kernel written by the student — a nice exercise.
- *Expected result* [E]: hardware wins on both GPUs, especially for incoherent rays. The learning is in *why* (memory traffic per node, divergence, RT-unit box/triangle throughput).

**(c) Building a BVH by hand (SAH, binning, refit)**
- *Value*: high for understanding DXR build flags (`PREFER_FAST_TRACE` vs `FAST_BUILD`, `ALLOW_UPDATE`, why refit degrades: D6), and it fills the [U] "refit quality" risk in `plan.md` with intuition.
- *Cost*: low, about a weekend [E]: Bikker parts 1–4 on the CPU, in a tiny test project or as the CPU side of (b).
- *Milestone*: during M1 (concepts) or as a prelude to M2 (ocean refit).
- *Check*: compare the student's SAH cost against TinyBVH's `SAHCost()` on the same mesh (TinyBVH as the "answer key").

**(d) TinyBVH for non-render needs**
- *Picking*: TinyBVH `Intersect` on CPU-side mesh copies gives exact triangle picking for the editor gizmo/selection [I]. Value depends on how EV picks today (not checked here).
- *NRC training data*: no value [I]. NRC records come from GPU paths (M10).
- *Physics/buoyancy*: low value for the ocean [I]. The ocean is a heightfield, so sampling the displacement is cheaper than a BVH.
- *Offline tools*: `bvhopt` / `Save`/`Load` are only relevant if EV ever builds its own GPU BVH (b).

**(e) Wavefront material for M5c**
- Already cited in `knowledge.md` (buffer budgets, 16-B alignment). Additions worth taking:
  - compaction by atomic append in Shade;
  - shadow rays batched outside the bounce loop (X·N bound);
  - the "per-material full-size queues explode memory" warning, which argues for a counting sort / prefix sum over one buffer (EV's plan) rather than per-material queues;
  - the four-kernel split as a starting structure.

  TinyBVH's `wavefront.cl`/`wavefront2.cl` are concrete OpenCL references [I, not read].

**(f) Learning resources for M1–M5**
- M1: BVH parts 1–6.
- M2: part 4 (refit) and flipcode part 3 (Beer's law) as a pre-read for M5.
- M3: probability parts 1–2 (MC, pdfs, light picking), voxel parts 3, 4 and 6 (stochastic, blue noise, cosine sampling), and the Utrecht lecture 04 path tracing / 13 bidirectional slides (MIS).
- M5c: the wavefront post and lecture 10.
- O1: the OPT#1–4 series (profiling discipline, SIMD for the CPU side).
- Value high, cost nil.

**(g) Refitting deforming meshes (ocean grid)**
- Bikker's guidance ("refit several frames, then rebuild periodically"; quality drops when triangles move far from their node) matches D6.
- The ocean grid keeps its topology, and its displacement is bounded (choppy waves move vertices horizontally by up to ~λ·choppiness [I]). Refit quality loss should therefore be small and *stationary*: the waves oscillate and don't drift, so N can probably be large [I/E, to measure].
- TinyBVH's `SAHCost()` on CPU copies of the refitted vs rebuilt grid gives a **measurable proxy for DXR refit degradation** (the DXR AS is opaque). This is a cheap M2 experiment if (a) or (c) exists [I].
- Bikker's "SAH predicts CPU, ray-set predicts GPU" finding means the decisive measurement is still rays/s on the GPU.

### Gaps
- How EV currently does editor picking was not checked (affects the value of (d)).
- No measured compute-vs-RT numbers exist for RDNA 4 or Ampere. The M5d figures are estimates.
- Whether TinyBVH compiles warning-free under EV's warning level / `/W4 /WX` with VS 2026 is unknown [U]. If not, vendor it behind `#pragma warning(push, 0)` [I].

## 5. BUas-specific material the student already knows

### Takeaway
Yes. The student's own folders show they did Bikker's BUas first-year voxel ray tracing work:
- `T:\Projects\Buas\Y1C\voxpopuli-main` and `RubiksCube\voxpopuli-main` are his **voxpopuli** template;
- `JaccoBase\WrldTmpl8` is his **2021 Voxel World template**;
- the RubiksCube project is a Whitted-style voxel renderer with diffuse/mirror/glass materials, a float4 accumulator, a skydome and Hable filmic tonemapping.

The tutor can anchor new concepts to that code: `Renderer::Trace`, `Scene::FindNearest`/`IsOccluded`, the accumulator, glass refraction, blue noise. Bikker founded and is senior lecturer of BUas CMGT (ex-IGAD).

### Cited Findings
- Local files:
  - `RubiksCube\voxpopuli-main\README.md`: "This template is intended for students of Breda University. Code is fully public domain." The template headers say "IGAD/NHTV/BUAS/UU - Jacco Bikker - 2006-2023" and "Get the latest version from: https://github.com/jbikker/tmpl8". `template\LICENSE` is CC0. [local, read-only]
  - `JaccoBase\WrldTmpl8\README.md`: "BRIEF INFO ON THE 2021 WORLD TEMPLATE", project site jacco.ompf2.com/voxel-world-template/. It claims "Up to 2 billion rays per second … on a RTX3080Ti GPU – but without using RTX" and is "completely free to use". WrldTmpl8 includes `template\bluenoise.h`. [local, read-only]
- The student's renderer (`RubiksCube\voxpopuli-main\renderer.h`) has:
  - `float3 Trace(Ray&, int reflectAmount, ...)`;
  - `DiffuseRender`, `MirrorRender` and `GlassRender`;
  - `float4* accumulator`;
  - `TmoHableFilmic`;
  - `Skydome`, `SpotLight`, `LightSource`;
  - `scene.FindNearest(mouseRay)` used for mouse picking.

  [local, read-only]
- The voxel series uses github.com/jbikker/voxpopuli and runs "out-of-the-box on a Windows system: just open the .sln" — [voxel part 1](https://jacco.ompf2.com/2024/04/24/ray-tracing-with-voxels-in-c-series-part-1/)
- BUas: Bikker is "Senior Lecturer and Program Founder, Academy for AI, Games & Media". He is senior lecturer of CMGT (formerly IGAD), which he founded in 2006 with Frank Peters. His listed outputs include WrldTmpl8 (2022) and tinybvh (2025) — [BUas Pure](https://pure.buas.nl/en/persons/dr-jacco-bikker/). His blog says he teaches "game development, graphics, and software optimization" at BUas — [about](https://jacco.ompf2.com/about-me/)
- The FASTTRACK C++ tutorial PDFs (IGAD lecturers, 2017) are the classic BUas intake material, e.g. [01 Getting Started](https://jbikker.github.io/files/fasttrack_01_getting_started.pdf) — [articles](https://jacco.ompf2.com/articles/)

### Inferences
- [I] The folder names (Y1C = year 1 block C) and the voxpopuli template suggest a first-year block-C ray tracing project. The voxel blog series parts 1–6 is effectively its companion text, so the student has probably met Whitted recursion, stratification, blue noise, accumulation and the first path-tracing steps.
- [I] Tutor hooks:
  - "your `GlassRender` is the Fresnel/refraction of §3.7 without the stochastic choice";
  - "your accumulator is §3.10";
  - "your `FindNearest` grid walk is what the BVH/RayQuery replaces";
  - "the WrldTmpl8 2-billion-rays/s claim is a software traversal like M5d";
  - "the blue noise from voxel part 4 / `bluenoise.h` is a valid M3 sampler step".
- [I] `T:\Projects\Buas\Y1C\Zenterra\2425-Y2C-PR-Kudzu` suggests a 2024/25 year-2 block-C team project. Not relevant to path tracing.

### Gaps
- No public BUas course syllabus (block names, graphics course codes, which year uses which template, whether Y2/Y3 graphics courses use TinyBVH) was found. Only the student can confirm what was taught.
- The jacco.ompf2.com/voxel-world-template/ page was not fetched.

## Integration notes for knowledge.md and plan.md

Proposals only, for the student to choose. Nothing has been edited.

### knowledge.md

1. **§11 Learning resources, "Path tracing" row**: replace the [U] Bikker BVH row with verified entries:
   > | | Jacco Bikker (BUas CMGT founder): [How to build a BVH 1–9b](https://jacco.ompf2.com/articles/) (basics → SAH → binning → **refit** → TLAS → Whitted → OpenCL GPU), [Probability theory for PBR 1](https://jacco.ompf2.com/2019/12/11/probability-theory-for-physically-based-rendering/)/[2](https://jacco.ompf2.com/2019/12/13/probability-theory-for-physically-based-rendering-part-2/) (MC, pdfs, light picking; no MIS/RR), [voxel series 3/4/6](https://jacco.ompf2.com/articles/) (stochastic, blue noise + golden ratio, cosine sampling), [Wavefront PT](https://jacco.ompf2.com/2019/07/18/wavefront-path-tracing/), [BVH Quality: Beyond SBVH](https://jacco.ompf2.com/2025/05/20/bvh-quality-beyond-sbvh/), Utrecht INFOMAGR 2022/23 slides ([path tracing](https://ics-websites.science.uu.nl/docs/vakken/magr/2022-2023/slides/lecture%2004%20-%20path%20tracing.pdf), [GPU RT 2](https://ics-websites.science.uu.nl/docs/vakken/magr/2022-2023/slides/lecture%2010%20-%20GPU%20ray%20tracing%20(2)%20v2.pdf), [bidirectional/MIS](https://ics-websites.science.uu.nl/docs/vakken/magr/2022-2023/slides/lecture%2013%20-%20bidirectional.pdf)) | The developer's own BUas teacher; matches M1–M5c |

   Also add a "BVH / software traversal" row: [TinyBVH](https://github.com/jbikker/tinybvh) (MIT, v1.9.2, 2026-11-14) + `external/dx12rt/` (DXR vs compute BVH2/BVH4/CWBVH vs RayQuery benchmark in D3D12).
2. **§10 M1 "Read"**: drop the [U] on the Bikker link. Add: "Concept check: build a binned-SAH BVH2 on the CPU by hand (Bikker parts 1–3), compare its `SAHCost` with TinyBVH's on the helmet. This makes `PREFER_FAST_TRACE` vs `FAST_BUILD` tangible."
3. **§6.4 / §6.5 (ocean refit, D6)**: add a paragraph:
   > Refit keeps topology, so BVH quality degrades only as triangles move relative to their original node (Bikker, [BVH part 4](https://jacco.ompf2.com/2022/04/26/how-to-build-a-bvh-part-4-animation/): "refit several frames, then rebuild periodically"). Ocean waves oscillate rather than drift, so degradation is expected to be bounded [I]. The DXR AS is opaque, so a CPU proxy is possible: refit vs rebuild a TinyBVH of the read-back grid and compare `SAHCost()`. The GPU rays/s is still decisive: SAH predicts CPU, not GPU, performance well ([Bikker 2025](https://jacco.ompf2.com/2025/05/20/bvh-quality-beyond-sbvh/)). TinyBVH reference: refit <1 ms per 200k triangles on CPU; no refit on SBVH.
4. **§3.9 Samplers**: add the student-known step:
   > Blue-noise texture (Peters' 64² `LDR_RG01_0.png`, `(x&63)+(y&63)*64`) + per-frame golden-ratio offset `frac(v + frame·0.618034)`, as in [Bikker voxel part 4](https://jacco.ompf2.com/2024/05/15/ray-tracing-with-voxels-in-c-series-part-4/) / WrldTmpl8 `bluenoise.h`. Good for low dimensions (NEE light sample, first bounce). Higher dimensions need Sobol/Owen.
5. **§10 M5c**: add to "Teach first"/"Read":
   > Bikker's four-kernel split (Generate/Extend/Shade/Connect), atomic-append compaction in Shade, shadow rays traced as a separate batch, 32-B rays for 128-bit loads; reader warning: per-material full-size queues explode memory, so sort one buffer (counting sort) instead. References: TinyBVH `kernels/wavefront.cl`, `wavefront2.cl` [not read]; Utrecht lecture 10 "GPU ray tracing (2)".
6. **§0.1 / §2 tutor hooks (new short subsection "2.9 Developer background")**:
   > The developer did Bikker's BUas Y1 voxel ray tracer (`T:\Projects\Buas\Y1C\RubiksCube\voxpopuli-main`: `Renderer::Trace` recursion, Diffuse/Mirror/Glass, float4 accumulator, Hable tonemap, skydome; `JaccoBase\WrldTmpl8` with `bluenoise.h`). Anchor new concepts to that code (glass → §3.7, accumulator → §3.10, `FindNearest` grid walk → BVH/RayQuery).
7. **§12 Open questions**: add the following.
   - (a) TinyBVH compile under VS 2026 with EV's warning flags.
   - (b) Compute-BVH vs RayQuery ratio on RDNA 4 / Ampere for primary vs incoherent rays (no published numbers).
   - (c) Whether the INFOMAGR "Variance reduction" slides cover firefly clamping.

### plan.md (optional milestones / additions, not decisions)

- **M3-ref (optional, after M3 step 3): CPU math test bench + mini reference tracer.**
  - Shared BSDF/sampling header compiled as HLSL and C++.
  - CPU unit tests: pdf normalization, sample/eval consistency, white furnace, later HWSS round-trip and the water TIR/slab numbers.
  - A tiny multithreaded CPU tracer on a vendored, pinned TinyBVH (`BVH4_CPU`, `/arch:AVX2`) for one Cornell box + frozen ocean patch, runnable on CI without GPU.
  - Verify: unit tests green on CI; FLIP(CPU, GPU) small at equal spp on the Cornell box.
  - Mitsuba 3 / pbrt-v4 stay the independent references.
- **M1 addition (small, optional):** a hand-written CPU binned-SAH BVH + refit (Bikker parts 1–4) as a concept exercise; compare `SAHCost` with TinyBVH.
- **M2 addition (small, optional):** an ocean refit-degradation proxy (CPU TinyBVH refit vs rebuild SAH over N frames, from one read-back per frame in a debug mode), next to the GPU refit/rebuild timings. It helps choose N in D6.
- **M5d (optional, after M5c): software BVH traversal vs RT hardware.**
  - The student's own HLSL compute traversal of a CPU-built BVH2 (TinyBVH `BVH_GPU` layout as input, or their own BVH from M1), with stretch goals BVH4_GPU / CWBVH.
  - A toggle in the panel: RayQuery / compute-BVH2 (/ compute-CWBVH).
  - Measure rays/s for primary, NEE-shadow and bounce-2+ rays on the RX 9070 XT and RTX 3070, plus RGP/Nsight occupancy and divergence.
  - Cross-check against TinyBVH's `external/dx12rt` benchmark run on both PCs.
  - Verify: same hits (t, prim) as RayQuery within ε; results in `performance.md`.
  - Value is learning only, because WARP and both GPUs already have DXR 1.1, so no fallback role.
- **Reading list in the roadmap table "Verify/Read"** (if wanted): M1 → Bikker BVH 1–6; M3 → Bikker probability 1–2 + voxel 3/4/6 + INFOMAGR lecture 04/13; M5c → Bikker wavefront post + INFOMAGR lecture 10.
- **Risks to note if any of this is adopted**:
  - TinyBVH changes fast (pin a commit, keep its MIT `LICENSE` next to the vendored headers, never track `main`);
  - AVX2 compile flag for the CPU fast path (scalar fallback exists);
  - VS 2026 compile unverified;
  - a CPU tracer that shares math with the GPU can share its bugs, so keep an external reference.
