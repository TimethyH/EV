# Closing the gaps of the profiling/optimization run (Laine 2013, Cycles sorting, Ampere RT cores, RDNA 4 RT, §12 items)

Researched 2026-11-18 as a follow-up to `followup_performance_optimization.md` (that run was cut off by a usage
limit). Labels as in `knowledge.md`: no label = verified against the linked primary source on 2026-11-18; **[T]**
training knowledge, not re-fetched; **[I]** inference; **[E]** estimate; **[U]** unverified.

Process notes: PDFs (Laine 2013, NVIDIA GA102 whitepaper, AMD Hot Chips 2025 slides) were downloaded and their text
extracted locally with a small PowerShell script (inflate FlateDecode streams, read `Tj`/`TJ` operators), so the
numbers below are read from the papers themselves, not from secondary sources. Blender commit messages were read via
the GitHub API mirror (`api.github.com/repos/blender/blender/commits/<sha>`) and Cycles source via
`raw.githubusercontent.com` (projects.blender.org still not used). The fetch tool summarises long commit messages, so
the per-scene timings of commit 523bbf7 are as returned by that summary (numbers, not wording).

---

## 1. Laine, Karras, Aila 2013 – "Megakernels Considered Harmful" (HPG 2013): the measured numbers

### Takeaway
On a Kepler Tesla K20, the wavefront path tracer beat an equally optimized megakernel in **all three** scenes – by
**36 % even with a single (expensive, ~4200-instruction) material**, 79 % with three complex materials (CITY) and
**221 %** in CONFERENCE (three expensive materials with very different costs). Pool: **1M paths × 212 B = 212 MB**,
queues 4 MB each, SoA layout (+80 % total) and warp-coalesced queue atomics (+40 % total) were essential. The paper
never measures a case where the megakernel wins; its only "cheap material" statement is that cheap materials should
be merged into one kernel because launches/queues have non-zero cost.

### Cited Findings
All from the author PDF, [laine2013hpg_paper.pdf](https://users.aalto.fi/~laines9/publications/laine2013hpg_paper.pdf)
(paper page: [NVIDIA Research](https://research.nvidia.com/publication/2013-07_megakernels-considered-harmful-wavefront-path-tracing-gpus)).

**Hardware and setup**
- GPU: **NVIDIA Tesla K20** (GK110 Kepler, 5 GB); "can accommodate up to 26624 resident threads" (vs 128 for an
  8-core hyperthreaded AVX CPU with ispc). Resolutions 1024×1024 (CARPAINT) and 1024×768 (CITY, CONFERENCE).
- Wavefront timings taken after the path mixture reaches a "stationary distribution" (performance is higher at the
  start due to startup coherence). Megakernel batch = 1M paths, with path regeneration enabled.
- Both variants "contain essentially the same code"; only the organisation differs.
- Integrator details: MIS between light sample and extension; **Russian roulette starts after eight path segments**,
  continuation probability = path throughput clamped to **0.95** (as in Mitsuba); Sobol sequences for the first **32
  dimensions** (precomputed on CPU, shared by all pixels) with a per-pixel, per-dimension random **XOR scramble
  mask**, then hashed random numbers; one quasirandom number = two array lookups + XOR.
- Ray casts: Aila–Laine CUDA traversal kernels (software traversal), SBVH acceleration structures.

**Materials**
- Car paint (from Bunkspeed): 4 layers – Fresnel-weighted angle-dependent diffuse base, two procedural flake layers
  (normalised Blinn-Phong), Fresnel-weighted mirror coat; 4 noise evaluations; the proprietary noise function is 80
  lines of C++ → **477 Kepler assembly instructions**; whole material ≈ **4200 instructions**. For comparison, casting
  one extension ray in CONFERENCE executes **2000–3000 instructions**.
- CITY (879K triangles): **three complex materials** – asphalt (re-purposed car paint), sidewalk (diffuse tiles with
  procedural noise texture displacement), windows (tinted mirrors with low-frequency normal noise); rest simple
  diffuse or diffuse+glossy; sky HDR + sun.
- CONFERENCE (283K triangles): **three expensive materials** – car-paint chairs, procedural Voronoi floor coating
  with noise-switched base colour, procedural **Mandelbrot** wall fractals (proxy for iterative evaluators); rest
  simple dielectrics / two-layer diffuse+glossy; two quad area lights.

**Megakernel analysis (§4.1)**
- Three divergence points: different path lengths (idle lanes until the warp finishes), **material evaluation**
  ("the main source of performance loss in scenes with multiple complex materials"), and skipping shadow rays for
  purely specular (Dirac) BSDFs.
- Path regeneration "whenever more than half of the threads in a warp are idling" gave only **1–5 %**; regeneration
  costs and reduces coherence.
- High register use from material hot spots (noise, BSDF math) lowers resident threads; "ray casts suffer from this
  especially badly" (memory-heavy); the megakernel overruns the per-SM **instruction cache**.

**Wavefront design (§4.2–4.4)**
- Pool of **1M (2^20) paths** always alive; each iteration advances every path by one segment; terminated paths are
  regenerated in the same iteration.
- Path state in DRAM: **212 bytes per path** (including extension + shadow rays and ray-cast result space) →
  **212 MB**. Pool 1M → 8M (1.7 GB) gives only **≈ 5 %** more speed; all tests use 1M.
- Three stages: **logic** (one kernel over all paths, no queue: MIS weights, throughput update, shadow-ray result
  accumulation, termination by escape / zero throughput / RR, pixel accumulation, next light sample, material lookup,
  enqueue material request), **material** (a **new-path** kernel + material kernels; "expensive" materials each get
  their own kernel, "simple" materials are merged into one kernel – chosen by hand; optimal assignment called an open
  problem), **ray cast** (separate **extension** and **shadow** ray-cast kernels). Light sampling is *not* a separate
  stage. Results go to buffers at the request index; path state stores those indices.
- Queues: preallocated, fixed max size (indices of every path), **4 MB each**, one global item counter incremented
  atomically; clear = set counter to 0.
- **SoA vs AoS** (CARPAINT): SoA gives **+80 % total**; logic kernel +147 %, new-path kernel **+790 %**, material
  kernel +68 %; ray casts unaffected (they don't read path state). In the logic kernel each SoA access is a contiguous,
  1024-bit-aligned read of 32 × 32-bit words.
- **Warp-coalesced atomics** (ballot within the warp, one atomic per warp): **+40 % total**; logic +75 %, new path
  +240 %, material +35 %, and ray casts **+32 %** purely from improved ray coherence (neighbouring queue entries stay
  together).

**Results (§5)**

| Scene | Triangles | Megakernel (Mpaths/s) | Wavefront (Mpaths/s) | Speed-up |
|---|---|---|---|---|
| CARPAINT (1 material, env map) | 9.5K | 42.99 | 58.38 | **36 %** |
| CITY (3 complex materials) | 879K | 5.41 | 9.70 | **79 %** |
| CONFERENCE (3 expensive, very uneven materials) | 283K | 2.71 | 8.71 | **221 %** |

Table 2 – wavefront time per iteration (1M path segments), ms:

| Scene | logic | new path | materials | ray cast | (sum) |
|---|---|---|---|---|---|
| CARPAINT | 2.40 | 0.86 | 2.31 | 4.31 | 9.88 |
| CITY | 3.42 | 0.86 | 5.47 | 12.53 | 22.28 |
| CONFERENCE | 3.01 | 0.79 | 6.37 | 9.62 | 19.79 |

(The "sum" column is my addition [I]; it reproduces the paper's ray-cast shares.)
- Ray casts = **44 % / 56 % / 49 %** of render time (CARPAINT / CITY / CONFERENCE); "approximately half" of the time
  is path logic + material evaluation – "fast ray casts do not alone ensure good performance".
- CARPAINT's 36 % with one material: the cost of path state in memory "is more than compensated for by the faster ray
  casts enabled by running the ray cast kernels with low register counts and hence better latency hiding", and the
  ray-cast code fits the instruction cache.
- Nsight: in CONFERENCE the megakernel's **thread utilisation is 23 %**, the wavefront's **53 %** overall (60 % logic,
  99 % new path, 71 % materials – lowered by the variable Mandelbrot iteration count – and 35 % ray casts).
- New-path generation is relatively expensive, which favours its own compact kernel.
- Ray-cast time is "largely unaffected by the materials" and material time is independent of geometric complexity.
- Conclusion: overheads (state in memory, queue management, launches) "are well outweighed by the benefits";
  similar gains expected on other vendors "due to architectural similarities"; volume marching suggested as its own
  kernel.
- Related-work claim: Hoberock et al. 2009 (material sorting inside one kernel) report benefits that **diminish as
  the number of materials grows**; with one kernel per material + implicit compaction via queues, Laine et al.'s
  performance is "practically independent of the number of materials as long as enough rays hit each material".

### Inferences
- [I] Laine's single-material 36 % gain came largely from **register-lean software traversal kernels**. EV traces with
  inline RayQuery: on the RTX 3070 traversal runs in the RT core, so the "lean traversal kernel" benefit should be
  smaller (the SM only waits), while on the RX 9070 XT traversal is a shader loop (BVH8 intersect instruction + LDS
  stack, §4) whose latency hiding *does* depend on the kernel's VGPR count – so the 1-material case may favour the
  wavefront more on RDNA 4 than on Ampere. This is exactly what M5c measures; the 2013 numbers are Kepler-era and
  must not be quoted as an expectation for EV.
- [I] EV's estimated path state (110–130 B + 16–24 B hit + 8 B sort) is below Laine's 212 B because EV keeps no
  separate shadow-ray buffer in the state; Laine's ~5 % gain from 8× larger pools supports capping EV's pool at ~1M
  paths (~150 MB) instead of one path per pixel at 1440p.
- [I] The two cheap implementation wins to copy into M5c are SoA (+80 %) and **wave-aggregated queue atomics**
  (+40 %, including +32 % ray coherence) – both already planned in M5c's counting-sort recipe; the paper quantifies
  them.
- [I] Laine's material grouping rule ("expensive materials get their own kernel, cheap ones share one") is the
  natural middle ground between M5c variants (a) one uber shade kernel and (b) one dispatch per material bin.

### Gaps
- The paper has no experiment where the megakernel wins and no break-even material count; Kepler, software traversal.
- No register counts per kernel are given in the text.

---

## 2. Blender Cycles: sorting by shader – actual figures and current heuristics

### Takeaway
Cycles always sorts `shade_surface` work by shader on the GPU; the figures that exist measure **refinements of the
sort**, not sorting vs no sorting: chunked/partitioned sorting (locality) gave "up to 15 %" on Apple M1-family GPUs and
0–13 % less time per sample on an RTX A6000 (OptiX) across 12 scenes (one scene 0.4 % slower); local-atomic per
partition sorting 2–3 % (Metal); on Intel Arc B570 replacing the global `sorted_paths_array` kernel by local
partitioned sorting gave **25 %** in Barbershop (many shaders), 2 % on Arc A770. Current code: partitions of 65536
states when the scene has < 300 shaders, otherwise one global sort; state pool = 16 × max resident threads.

### Cited Findings
- **Commit 4b1d315017e** (Michael Jones, Apple, 2022-07-13, D15331) "Cycles: Improve cache usage on Apple GPUs by
  chunking active indices": splits active indices into chunks before sorting by material, trading material coherence
  for memory locality; "up to 15 %" overall render-time speed-up on Apple Silicon (esp. higher-end M1-family);
  partition size **65536** for M1/M2 — [GitHub API: 4b1d315](https://api.github.com/repos/blender/blender/commits/4b1d315017e).
- **Commit 523bbf7065** (Brecht Van Lommel, 2022-07-14, D15331 / T87836) "Cycles: generalize shader sorting /
  locality heuristic to all GPU devices": OptiX on **RTX A6000**, time per sample new vs old —
  barbershop_interior 0.0730 vs 0.0727 s, bmw27 0.0047 vs 0.0053, classroom 0.0428 vs 0.0464, fishy_cat 0.0102 vs
  0.0108, junkshop 0.0366 vs 0.0395, koro 0.0567 vs 0.0578, monster 0.0206 vs 0.0223, pabellon 0.0158 vs 0.0174,
  sponza 0.0088 vs 0.0100, spring 0.1267 vs 0.1280, victor 0.0524 vs 0.0531, wdas_cloud 0.0817 vs 0.0816 —
  [GitHub API: 523bbf7](https://api.github.com/repos/blender/blender/commits/523bbf70655). Derived [I]: −11 % bmw27,
  −12 % sponza, −10 % pabellon, −8 % classroom/monster, −7 % junkshop, −6 % fishy_cat, −1–2 % koro/spring/victor,
  +0.4 % barbershop, ±0 wdas_cloud (time per sample).
- **Commit 654e1e9** (Michael Jones, 2023-02-06, D16909): new `SORT_BUCKET_PASS` + `SORT_WRITE_PASS` kernels replace
  `PREFIX_SUM` + `SORTED_PATHS_ARRAY` on supported devices (Metal only at the time), sorting each partition with
  **local atomics**; "2–3 %" overall speed-up depending on architecture; falls back to non-partitioned sorting when the
  shader count is too high — [GitHub API: 654e1e9](https://api.github.com/repos/blender/blender/commits/654e1e9).
- **Commit 2df163a** (Xavier Hallade, 2025-06-18, PR 140308): oneAPI scenes with many shaders were slow because of
  the `sorted_paths_array` kernel; switching to local sorting + partitioning → **25 %** overall on **Arc B570**
  (Agent 327 Barbershop), no `shade_surface` regression; **2 %** on Arc A770 —
  [GitHub API commit search](https://api.github.com/search/commits?q=repo:blender/blender+cycles+sorting+shader+partition).
- **Commit bfb5775** (Patrick Mours, NVIDIA, authored 2026-09-02, committed 2026-09-15, PR 163436): enables local
  partitioned shader sorting for **CUDA/OptiX** (previously only Metal/oneAPI benefited); no figures — same search.
- **Commit 2f94212** (Xavier Hallade, 2026-11-16, PR 166558): oneAPI work-group sizes from per-kernel properties;
  disables sort partitioning when per-shader counters don't fit in local memory; no figures — same search.
- Current default heuristic (`device/queue.h`): `num_sort_partitions` = `max(max_num_paths / 65536, 1)` if
  `max_scene_shaders < 300`, else 1 ("Sort partitioning becomes less effective when more shaders are in the
  wavefront"); comment: partitions improve "memory locality of integrator state fetch at the cost of decreased
  coherence for shader kernel execution"; `supports_local_atomic_sort()` defaults to false —
  [queue.h](https://raw.githubusercontent.com/blender/blender/main/intern/cycles/device/queue.h).
- CUDA (`device/cuda/queue.cpp`): partitions only if `max_scene_shaders < min(300, max_shared_mem_bytes / sizeof(int))`;
  `supports_local_atomic_sort()` returns `DebugFlags().cuda.use_local_atomic_sort`; state count baseline =
  `max(SMs × max_threads_per_SM, 65536) × 16` ("Growing up to 10x does show performance improvements, however it
  causes TDR timeouts, too much memory usage…") — [cuda/queue.cpp](https://raw.githubusercontent.com/blender/blender/main/intern/cycles/device/cuda/queue.cpp).
- HIP (`device/hip/queue.cpp`): same baseline (`(max_num_threads == 0 ? 65536 : max_num_threads) × 16`), min =
  baseline / 2; no `num_sort_partitions` / `supports_local_atomic_sort` override in that file (so the generic
  defaults apply: partitions yes when < 300 shaders, local-atomic sort no) —
  [hip/queue.cpp](https://raw.githubusercontent.com/blender/blender/main/intern/cycles/device/hip/queue.cpp).
- Scheduler (`integrator/path_trace_work_gpu.cpp`): sorting used only for `SHADE_SURFACE` and
  `SHADE_SURFACE_RAYTRACE` kernels ("Compute array of active paths, sorted by shader"); `min_num_active_main_paths_ =
  max_num_paths_ / 4` – "A 1:4 busy:total ratio was found to give good performance, independent of the total state
  count"; state array compacted when the number of paths becomes small relative to the max path index —
  [path_trace_work_gpu.cpp](https://raw.githubusercontent.com/blender/blender/main/intern/cycles/integrator/path_trace_work_gpu.cpp).

### Inferences
- [I] On the RTX 3070 Cycles' rule gives 46 SMs × 1536 threads = 70,656 → × 16 ≈ **1.13M states** (≈ Laine's 1M
  pool) and 17 sort partitions of 65536. Two independent production designs therefore converge on ~1M paths in
  flight – a good M5c default for EV on both GPUs.
- [I] The published Cycles numbers measure *how* to sort (global vs partitioned vs local atomics), with gains of
  0–25 %; the order of magnitude for "sorting at all" in Cycles is unpublished. For EV, M5c's
  unsorted-vs-sorted wavefront comparison is still the only source.
- [I] Cycles disables partitioning above 300 shaders because partitions of 65536 states then hold too few paths per
  shader – the same "enough rays per material" condition Laine names. With EV's ≤ 64 material programs a partitioned
  counting sort (per-partition histograms in groupshared) is in the regime where Cycles keeps it on.

### Gaps
- No Cycles figure for sorting on/off, and none for HIP/RDNA.
- Cycles X 2021 blog numbers not re-read (code.blender.org not fetched; existing quote kept).

---

## 3. NVIDIA Ampere (GA10x) second-generation RT cores – whitepaper figures

### Takeaway
The GA102 whitepaper claims **2× ray/triangle intersection rate** vs Turing (no claim of faster box tests), RT core
work **concurrent with graphics or compute** in the same SM (new), hardware **motion blur** via an Interpolate Triangle
Position unit (up to 8× vs Turing; measured up to 5× in Blender 2.90/OptiX 7 on RTX 3080 vs 2080 Super), and instance
transform acceleration on both generations. The RTX 3070 (GA104) has 46 RT cores. Also: on GA10x, **non-tensor FP16
runs at the FP32 rate** (RTX 3070: 20.3 TFLOPS both), so fp16 saves registers/bandwidth, not ALU time.

### Cited Findings
All from [NVIDIA Ampere GA102 GPU Architecture whitepaper v2](https://www.nvidia.com/content/PDF/nvidia-ampere-ga-102-gpu-architecture-whitepaper-v2.pdf).
- "Ampere architecture RT Cores double the ray/triangle intersection testing rate over Turing architecture RT
  Cores"; RT core does all BVH traversal and triangle tests and returns hit/no hit to the SM; MIMD design that
  processes many rays at once.
- RTX 3080: RT cores "offer double the throughput for ray/triangle intersection testing, resulting in **58 RT
  TFLOPS** (compared to 34 in Turing)"; RTX 3080 "up to 2x faster in ray tracing workloads" than RTX 2080 Super
  (marketing, games at 4K).
- Table 4 (TU102 vs GA102 full chip): RT cores 72 vs 84; ray/box, ray/triangle, tree traversal and **instance
  transform acceleration: yes on both**; **concurrent RT and shading: no (Turing) / yes (Ampere)**; dedicated L1
  interface yes/yes; ray/triangle intersection test culling rate speed-up 1.0× / 2.0×; overall GPU RT speed-up 1.0× /
  2.0×.
- Concurrency: GA10x lets "RT Core and graphics, or RT Core and compute workloads" be processed concurrently in each
  SM; the SM can run two compute workloads simultaneously (e.g. "a compute-based denoising algorithm to run
  concurrently with RT Core-based ray tracing work"); priorities can be assigned.
- Motion blur: new **Interpolate Triangle Position** unit; ray-traced motion blur "up to eight times faster than the
  Turing GPU architecture"; "measured up to 5x faster motion blur rendering with a GA102-based RTX 3080 compared to
  an RTX 2080 Super" (Blender Cycles 2.90, OptiX 7.0). Exposed through OptiX 7 (motion paths + per-ray time).
- Wolfenstein: Youngblood frame (whitepaper example): RTX 2080 Super 51 ms shaders only → 20 ms with RT cores → 12 ms
  with DLSS; RTX 3080 6.7 ms.
- RTX 3070 (Table 10, GA104 vs RTX 2070 Super TU104): 46 SMs, 5888 CUDA cores (128/SM), **46 RT cores (2nd gen)**,
  184 Tensor cores, boost 1725 MHz, **20.3 TFLOPS FP32**, **20.3 TFLOPS FP16 (non-tensor)** (2070 Super: 9.1 FP32 /
  18.1 FP16), 8 GB GDDR6 14 Gbps, 256-bit, **448 GB/s**, L2 **4096 KB**, L1/shared total 5888 KB (= 128 KB/SM),
  register file 11776 KB (= 256 KB/SM), 220 W TGP, 17.4 B transistors, 392.5 mm², Samsung 8N.

### Inferences
- [I] The doubled rate is for **triangle** tests; box-test throughput per RT core is not claimed to change. Scenes
  dominated by box traversal (EV's ocean grid with many grazing rays, deep TLAS) gain less than triangle-heavy ones.
- [I] "Concurrent RT and shading" is what lets an inline-RayQuery compute kernel overlap traversal with other
  warps' ALU work on the same SM – occupancy (registers) still decides how much other work is available.
- [I] Correction for EV: the earlier "[T] Ampere has 2× fp16 rate on ALU paths" is wrong for GA10x (Turing had 2×,
  Ampere GA10x has 1×). fp16 in EV's shaders is worth it on the 3070 only for **register/live-state and bandwidth**
  savings; on RDNA 4 packed fp16 may additionally save ALU time ([T], not verified here).
- [I] Motion-blur hardware is irrelevant to EV unless motion-blurred BLASes are used (DXR has no motion-blur AS; it is
  OptiX-only).

### Gaps
- No absolute box/triangle tests-per-clock figure for Ampere RT cores in the whitepaper.

---

## 4. AMD RDNA 4 ray tracing: primary-source facts and best practices

### Takeaway
AMD's own sources (Hot Chips 2025 slides, RRA 1.8 post, RDNA performance guide, LLVM AMDGPU patches) confirm: 8
ray/box + 2 ray/triangle tests per Ray Accelerator per clock (2×), BVH8, compressed primitive nodes (up to 8
triangles per node), OBBs (≈ 10 % traversal gain, geometry-dependent), a dedicated hardware instance-transform unit,
hardware traversal-stack management in LDS, out-of-order memory returns, and **dynamic VGPR allocation** (wave32
compute only, via a compiler mechanism for chained shader calls – i.e. not for a single inline-RayQuery megakernel).
There is still **no RDNA 4-specific GPUOpen RT best-practices page**; the general RDNA guide applies (8×4 groups, little
groupshared in RT shaders, one RayQuery object live, high-quality build flags, TLAS on the compute queue).

### Cited Findings
- AMD Hot Chips 2025 (A. Pomianowski, "AMD RDNA 4 Radeon 9000 Series GPU", Aug 2025) —
  [slides PDF](https://hc2025.hotchips.org/assets/program/conference/day1/8_amd_pomianowski_final.pdf):
  - "Enhanced Ray Accelerators: 8 Ray/Box, 2 Ray/Triangle Units – 2x Increase"; "Dedicated Hardware Instance
    Transform"; "Ray Hardware Stack Management acceleration"; "BVH8 for reduced traversal steps and latency
    reduction"; "New primitive node compression reducing BVH size"; "Oriented bounding boxes reducing false
    intersections"; "Dynamic VGPR management increasing Ray Occupancy"; "Out-of-order memory". Block diagram: Ray
    Accelerator = intersection engines + ray transform + traversal stack management + pack and sort, next to the LDS.
  - OBB slide: boxes encode a rotation per box node; "Ray direction is transformed on entry to the box"; traversal
    steps "significantly reduced on average", peak cost reduced; "Performance of traversal improves approximately 10%
    (geometry dependent)" (Resident Evil 3 traversal-step heatmap).
  - Out-of-order memory: RDNA 3 data returns were strictly ordered; RDNA 4 lets requests from different shaders be
    satisfied out of order; "raytracing workloads [are] highly sensitive to this factor" (e.g. an uncached leaf-node
    access no longer holds up other waves' requests).
  - Summary: "RDNA 4 CUs deliver approximately 2x Ray Traversal performance – Compared to RDNA 3 at equal clock rates
    and bandwidth" (modelled data); "OBB impact depends on orientation of geometry in the world"; "Traversal and
    shading efficiency has dependencies on ray coherence".
  - Dynamic registers slide: "Raytracing shaders show large variance in live registers – During traversal, a low
    number of registers is needed – Result shading often requires a larger number"; "On RDNA 3, shader allocates for
    the worst case"; "On RDNA 4, shaders have the **option** to allocate registers dynamically" – request from / release
    to a pool, "Software manages the condition where we need to wait for an allocation"; more waves in flight → better
    memory-latency hiding.
  - Slide also shows a BVH-memory chart "Relative BVH Memory (lower is better)" for RDNA 4 vs previous (exact ratio
    not legible in the extracted text [U]).
- RRA 1.8 (GPUOpen, 2025-03-11): RDNA 4 BVH has "a new node format that supports 8 child nodes instead of the
  previous 4" (shown as "Bvh8"), **OBBs** "not restricted to being aligned with the X, Y, and Z axes" (rotation matrix
  per node shown in RRA; identity for AABB nodes), and "each triangle node can now contain up to 8 triangles instead of
  the previous 2"; scaffolding example (diagonal rods) shows OBBs "greatly alleviate" wasted box tests; needs Adrenalin
  25.3.1+; these structures only appear in captures from RDNA 4 devices —
  [RRA 1.8 post](https://gpuopen.com/learn/radeon_raytracing_analyzer_v1-8_support_for_rx9000_series_gpus_and_more/).
- RDNA performance guide (originally posted 2023-03-22, no RDNA 4 section): "Our traversal system runs on the RDNA WGP
  and uses LDS extensively" → "Avoid using too much groupshared memory in your ray tracing shaders"; "Use an 8×4
  threadgroup size"; "Stop traversing after first hit for opaque shadows"; skip procedural/non-opaque via ray flags;
  "Smaller BVHs are always better"; "Use high-quality build flags wherever you can"; don't rebuild dynamic geometry
  every frame, refit/rebuild only needed LODs; "Rebuild your TLAS every frame on the compute queue if possible"; DXR 1.1:
  "The best performance comes from using it in compute shaders, on a compute queue" and "**Always have just 1 active
  RayQuery object in scope in your shader at any time**"; FP16 and intrinsics (readFirstLane) to cut VGPRs; no
  separate wave-size advice for RT (RDNA is wave32-native; multiples of 64 for cross-generation compatibility) —
  [RDNA performance guide](https://gpuopen.com/learn/rdna-performance-guide/).
- LLVM AMDGPU (2025): `S_ALLOC_VGPR` "only works in wave32 mode" on GFX12; dynamic VGPR mode "can only be used with
  wave32 compute shaders"; used through `llvm.amdgcn.cs.chain` (flags operand requests VGPR reallocation before the
  chained call); compiler emits `S_ALLOC_VGPR 0` before `S_ENDPGM`; later turned into the `amdgpu-dynamic-vgpr`
  function attribute ("Represents the 'Dynamic VGPR' hardware mode, introduced in GFX12") —
  [PR 130018 thread](https://lists.llvm.org/pipermail/llvm-commits/Week-of-Mon-20250303/1580690.html),
  [cs.chain dVGPR commit](https://llvm.googlesource.com/llvm-project/+/e17b3cdfb3626e03cdefaad42db1e2374da9e9ca),
  [attribute PR 133444](https://lists.llvm.org/pipermail/llvm-commits/Week-of-Mon-20250421/1616678.html) (the
  wave32-only and cs.chain statements are from search snippets of these threads; the attribute text was read).
  Consistent with Chips and Cheese's observation that dynamic VGPRs were seen only with indirect (non-inlined) RT
  stages — [C&C](https://chipsandcheese.com/p/dynamic-register-allocation-on-amds) (already in the earlier notes).
- [U] HIP RT 3.0.9 adds RDNA 4 support for compressed BVH8 + triangle packets, intersectable instances and OBBs (HQ
  build) — [HIP RT page](https://gpuopen.com/hiprt/) (search snippet only).

### Inferences
- [I] For EV's inline-RayQuery compute megakernel, dynamic VGPRs do **not** help: the mechanism is tied to chained
  shader functions (`cs.chain`, as used by the driver for DispatchRays pipelines with separate traversal/hit stages).
  The megakernel's occupancy is set by its worst-case VGPR count → live-state reduction remains the main lever, and
  this is an argument (on AMD only) where a DispatchRays variant could beat RayQuery (§12 Q38).
- [I] AMD's "1 active RayQuery object in scope" rule matters for EV's NEE: declare the shadow `RayQuery` in a scope
  that ends before the extension query begins (or reuse one object), otherwise both objects' state is live.
- [I] OBBs and BVH8 are driver-built and invisible to the API; their benefit grows with off-axis geometry (rotated
  meshes, diagonal structures). EV's axis-aligned ocean grid gains little from OBBs; rotated scene meshes gain more.
  RRA on the 9070 XT shows the OBB rotation per node, so one can check whether the ocean tiles got identity rotations.
- [I] Out-of-order memory returns soften the penalty of incoherent BVH/texture fetches, so the coherence gain from
  material sorting may be smaller on RDNA 4 than on older AMD parts – another reason to measure rather than assume.

### Gaps
- No GPUOpen RDNA 4-specific RT best-practices page or GDC 2025/2026 AMD RT talk found; the general RDNA guide is
  from 2023.
- AMD does not document whether the driver ever applies dynamic VGPRs to inline RayQuery (no evidence that it does).
- Exact RDNA 4 BVH memory reduction factor not legible from the slide text.

---

## 5. Other §12 items settled or narrowed (41–52)

### Takeaway
Settled: Q48 (constants confirmed from Cycles source, removal commit read), Q49 (FLIP default 67 ppd, ACES, automatic
exposure range), Q50 (whitepaper read), Q45 partly (RDP CLI flags documented). Narrowed but still open: Q42 (GeForce
`-lgc`: only user reports), Q43 (CLI offers only `normal|stable`; frequency unknown). Q40 resolved by §1, Q41 changed by
§1. Not attempted: Q44, Q46, Q47, Q51, Q52 (need measurements or were out of reach).

### Cited Findings
- **Q48 – ray offset constants**: Cycles' `ray_offset(P, Ng)` cites "A Fast and Robust Method for Avoiding
  Self-Intersection", Ray Tracing Gems ch. 6, and uses `int_scale = 256.0f`, `origin = 1.0f / 32.0f`,
  `float_scale = 1.0f / 65536.0f` (integer ULP offset along Ng, with the float fallback for |P| < origin) —
  [Cycles kernel/bvh/util.h](https://raw.githubusercontent.com/blender/blender/main/intern/cycles/kernel/bvh/util.h).
  The same file has `intersection_skip_self` (skip if hit prim **and** object equal the ray's `self`),
  `intersection_skip_self_shadow` (also skips the light's prim/object) and `intersection_skip_self_local` (prim only).
- **Q48 – what replaced offsetting**: commit 74afc86 "Cycles: remove ray offsetting" (William Leeson, 2022-01-13,
  committed 2022-01-26, D12954): offsets caused artifacts on small, large or distant objects with no good user fix;
  self-intersection is now avoided by comparing **primitive and object IDs**; trade-off: overlapping primitives are
  handled poorly (particle systems can create such overlaps → CPU/GPU differences); `ray_offset()` itself was
  rewritten to the RTG integer-scale method and kept for some uses; no performance figures —
  [GitHub API: 74afc86](https://api.github.com/repos/blender/blender/commits/74afc86d4bf1a559e5b7a0cf5cea29cb508a3e99).
- The RTG1 chapter PDF itself still could not be read (Springer redirects to a login/cookie page).
- **Q49 – FLIP**: `FLIP.h` `Parameters`: `PPD = FLIP::calculatePPD(0.7f, 3840.0f, 0.7f)` (distance 0.7 m,
  3840 px horizontal, monitor width 0.7 m → ≈ 67 ppd); `startExposure = stopExposure = +inf` and `numExposures = -1`
  as "compute automatically" sentinels for HDR input; default `tonemapper = "aces"` —
  [FLIP.h](https://raw.githubusercontent.com/NVlabs/flip/main/src/cpp/FLIP.h). Example HDR-FLIP run output: "Pixels per
  degree: 67", "Assumed tone mapper: ACES", start exposure −12.5423, stop 0.9427, 14 exposures (one example, not
  defaults) — [Python README](https://raw.githubusercontent.com/NVlabs/flip/main/src/python/README.md); v1.7: the
  automatic stop exposure now handles references with median luminance 0 — [NVlabs/flip README](https://raw.githubusercontent.com/NVlabs/flip/main/README.md).
  Option names: `flip -h` (not listed in the READMEs).
- **Q45 / Q43 – RadeonDeveloperPanelCLI** (RDP v3.5.1 manual): `-p/--process NAME`, `--remote-host`, `--remote-port`
  (27300); `--clock-mode normal|stable` (no `peak`), `--gpu-index`; `-m/--mode profiling` (default) or `raytracing`;
  `-a/--rgp-auto-capture frame[:N]` (N ≥ 5, default 5) or `dispatch[:start[:count]]`; `--rgp-capture-mode
  default|frame|draw|dispatch`; `--rgp-render-op-count`; `--rgp-auto-capture-delay <ms>`; **`--rgp-shader-instrumentation`**
  (needed for "Average active lanes"); `-o/--output PATH`; RRA: `--rra-delay-ms`, `--rra-marker-capture` with
  `--rra-marker-begin/--rra-marker-end` (driver 26.20+), `--rra-collect-ray-dispatch-data`,
  `--rra-ray-history-buffer-size disabled|minimum|low|default|high|maximum`; no launch flag documented —
  [RDP CLI manual](https://gpuopen.com/manuals/rdp_manual/radeon_developer_panel_cli/).
- **Q42 – nvidia-smi on GeForce/Windows**: no NVIDIA statement found. A vendor support guide (Zivid) says "not all
  NVIDIA GPUs support the --lock-gpu-clocks and/or --lock-memory-clocks options" — [Zivid](https://support.zivid.com/support/unstable-capture-time-windows.html);
  an NVIDIA forum thread on an RTX 3070 shows "Setting applications clocks is not supported" for the deprecated
  application-clocks option, not for `-lgc` — [NVIDIA forum](https://forums.developer.nvidia.com/t/cant-set-clocks-on-my-3070-setting-applications-clocks-is-not-supported/166643).

### Inferences
- [I] Q48 for EV: Cycles' ID-based self-skip is hard to copy into DXR inline RayQuery on **opaque** geometry – opaque
  triangle hits are committed by the hardware without a candidate step, so a "skip my own primitive" test needs the
  geometry non-opaque (candidate loop in `Proceed()`), which costs traversal speed. For EV the Wächter–Binder offset
  (with Cycles' constants) stays the right default; an ID check is an option only for non-opaque/alpha geometry
  already handled in the candidate loop.
- [I] Q49 for EV: use FLIP defaults (67 ppd) unless the viewing setup is documented; for HDR-FLIP on linear EXRs keep
  the automatic exposure range but **log** the start/stop/num exposures FLIP prints, and pass the same
  `--start-exposure/--stop-exposure` (names per `flip -h` [U]) for all variants of one shot so error numbers are
  comparable across runs.
- [I] Q45: a scripted AMD capture for the PT dispatch is e.g. `RadeonDeveloperPanelCLI -p EV --clock-mode stable -a
  dispatch:<index>:1 --rgp-capture-mode dispatch --rgp-shader-instrumentation -o pt.rgp` (flags from the manual; the
  combination is untested).

### Gaps
- Q42: still needs a test on the 3070 (`nvidia-smi -lgc` in an admin console, then `--query-gpu=clocks.sm`).
- Q43: Stable-clock frequency and thermal behaviour of the 9070 XT – measure (RGP shows clocks in captures [T]).
- Q45: overhead of shader instrumentation on timings – not documented; measure once.
- Q44, Q46, Q47, Q51, Q52: not researched here (measurement items, or no new source within budget).

---

## Integration notes for knowledge.md

Each entry: where, status, replacement text. Labels inside the replacement text are as they should appear in
knowledge.md; links point to the sources above.

**§12 Q40 → RESOLVED (delete from §12 or mark done).** Replacement for the M5c "Teach first" bullet on Laine
(knowledge.md ~line 2100–2104, the sentence starting "Design [T][U]:"):
> Design (from the paper): pool of **1M paths** always alive, **212 B of state per path** (incl. extension + shadow
> rays and ray results) = 212 MB in DRAM (8M paths: only ≈ 5 % faster); stages **logic** (one kernel over all paths,
> no queue) → **material** (new-path kernel + one kernel per *expensive* material, cheap materials merged into one
> kernel) → **ray cast** (separate extension and shadow kernels); queues = preallocated index buffers (4 MB each) with
> one atomic counter. SoA instead of AoS: **+80 %** total; warp-coalesced queue atomics: **+40 %** total (ray casts
> +32 % from coherence alone). Tesla K20 (Kepler), software traversal: megakernel vs wavefront 42.99 vs 58.38 Mpaths/s
> (CARPAINT, 1 material, **+36 %**), 5.41 vs 9.70 (CITY, 3 complex materials, **+79 %**), 2.71 vs 8.71 (CONFERENCE,
> 3 very uneven materials, **+221 %**; thread utilisation 23 % vs 53 %); ray casts are 44–56 % of the wavefront's time.
> The single-material gain came from register-lean traversal kernels – with RT-core traversal (3070) it should shrink,
> with shader-loop traversal (9070 XT) less so [I]
> ([paper PDF](https://users.aalto.fi/~laines9/publications/laine2013hpg_paper.pdf)).

Also in `followup_performance_optimization.md` §1 the "[T][U] Laine et al. design" bullet is superseded by §1 here.

**§12 Q41 → CHANGED (keep open, add context).** New text:
> 41. Megakernel vs wavefront break-even on RDNA 4 / Ampere – still unpublished; M5c measures it. Laine 2013 (Kepler,
> software traversal) found the wavefront faster even with one expensive material (+36 %); no study shows the
> megakernel winning except with cheap materials (Dr.Jit/Mitsuba 3 fused kernel). Expected shape in M5c stays [E].

M5c "Expected shape" (~line 2159) – replace the first clause:
> Expected shape [E, to be replaced]: N = 1 *cheap* material (Lambert): wavefront 10–30 % slower than the megakernel;
> N = 1 *expensive* material: possibly faster already (Laine 2013: +36 % on Kepler, from lean trace kernels – likelier
> on the 9070 XT, whose traversal is a shader loop, than on the 3070, whose traversal runs in the RT core [I]); N = 4: …
> (rest unchanged).

M5c "Order (hints)" step (1)/(4) – add: "pool of ~1M paths (Laine 1M; Cycles ≈ 16 × resident threads ≈ 1.13M on the
3070), SoA, wave-aggregated atomics – Laine measured +80 % and +40 % for these two alone."

**M5c Cycles bullet (~line 2110–2114) → CORRECTION of attribution + new figures.** Replace "measured +2–3 % (local
atomics) to +15 % (partitioned, Apple M1/M2) overall ([commit 523bbf70655](…))" with:
> measured gains are for *how* to sort, not sorting on/off: chunked/partitioned sort (65536 states per partition)
> "up to 15 %" on Apple M1-family ([4b1d315](https://api.github.com/repos/blender/blender/commits/4b1d315017e)); the same
> heuristic on OptiX/RTX A6000: 0–12 % less time per sample over 12 scenes, barbershop +0.4 % slower
> ([523bbf7](https://api.github.com/repos/blender/blender/commits/523bbf70655)); local-atomic per-partition sort 2–3 %
> ([654e1e9](https://api.github.com/repos/blender/blender/commits/654e1e9)); Intel Arc B570, local partitioned instead of
> global sort: 25 % in Barbershop, Arc A770 2 % (commit 2df163a, 2025); CUDA/OptiX got local partitioned sorting in
> 2026-09 (bfb5775). Current rule: partitions only if the scene has < 300 shaders; states = 16 × max resident threads;
> new work is fetched when < 1/4 of states are busy
> ([queue.h](https://raw.githubusercontent.com/blender/blender/main/intern/cycles/device/queue.h),
> [cuda/queue.cpp](https://raw.githubusercontent.com/blender/blender/main/intern/cycles/device/cuda/queue.cpp),
> [path_trace_work_gpu.cpp](https://raw.githubusercontent.com/blender/blender/main/intern/cycles/integrator/path_trace_work_gpu.cpp)).

**§10.2 "Material sorting / wavefront (M5c)" row** – Expected impact cell, replace "Cycles sort 2–15 %" with
"Cycles sort *refinements* 0–25 % (partitioning, local atomics; device-dependent); Laine 2013 wavefront +36–221 %
(Kepler)". **"Locality-preserving sort" row**: Vendor notes → "Cycles: 65536-state partitions, only with < 300
shaders; generalized to all GPUs 2022 (OptiX A6000: 0–12 %)"; Source → links 4b1d315 / 523bbf7 above (instead of the
developer.blender.org link).

**§12 Q50 → RESOLVED (delete).** Replacement for `followup_performance_optimization.md` §5 "[T][U] Ampere 2nd-gen RT
cores" bullet and wherever knowledge.md states Ampere RT facts (§1 hardware matrix if it has an RT-core line):
> Ampere GA10x 2nd-gen RT cores: **2× ray/triangle intersection rate** vs Turing (box-test rate not claimed to
> change); RT core work **concurrent with graphics or compute in the same SM** (Turing: no); instance-transform
> acceleration on both generations; Interpolate Triangle Position unit for motion blur (up to 8× vs Turing, OptiX
> only). RTX 3070 (GA104): 46 SMs / 46 RT cores, 5888 CUDA cores, 1725 MHz boost, 20.3 TFLOPS FP32, 448 GB/s, 4 MB L2,
> 128 KB L1/shared and 256 KB registers per SM
> ([GA102 whitepaper](https://www.nvidia.com/content/PDF/nvidia-ampere-ga-102-gpu-architecture-whitepaper-v2.pdf)).

**§10.2 fp16 row → CORRECTION.** Vendor notes "RDNA 4 and Ampere both fast fp16 [T]" → "Ampere GA10x: non-tensor
FP16 = FP32 rate (RTX 3070: 20.3 TFLOPS both; Turing was 2×) → gain on the 3070 is registers/bandwidth only
([GA102 whitepaper](https://www.nvidia.com/content/PDF/nvidia-ampere-ga-102-gpu-architecture-whitepaper-v2.pdf));
RDNA 4 packed fp16 [T]". Same correction in `followup_performance_optimization.md` §4 inference "Ampere has 2× fp16
rate on ALU paths [T]".

**RDNA 4 facts (followup §5 Gaps "No AMD primary doc"; knowledge.md §6.6 / §1 wherever RDNA 4 RT is described) →
ADD:**
> AMD (Hot Chips 2025): Ray Accelerator 8 ray/box + 2 ray/triangle per clock (2×), dedicated hardware instance
> transform, hardware traversal-stack management, BVH8, compressed primitive nodes (RRA 1.8: up to 8 triangles per
> node instead of 2), OBBs (traversal ≈ 10 % faster, geometry-dependent), out-of-order memory returns, ≈ 2× traversal
> vs RDNA 3 at equal clock/bandwidth; dynamic VGPRs are an *option* for wave32 compute used with chained shader calls
> (LLVM `cs.chain` / `amdgpu-dynamic-vgpr`) – not for an inline-RayQuery megakernel, whose VGPRs are fixed by its worst
> case ([HC 2025 slides](https://hc2025.hotchips.org/assets/program/conference/day1/8_amd_pomianowski_final.pdf),
> [RRA 1.8](https://gpuopen.com/learn/radeon_raytracing_analyzer_v1-8_support_for_rx9000_series_gpus_and_more/),
> [LLVM](https://llvm.googlesource.com/llvm-project/+/e17b3cdfb3626e03cdefaad42db1e2374da9e9ca)). RDNA guide (2023, no
> RDNA 4 section): RayQuery best in compute on a compute queue, **only one RayQuery object in scope at a time**, 8×4
> groups, little groupshared, high-quality build flags, smaller BVHs always better
> ([RDNA performance guide](https://gpuopen.com/learn/rdna-performance-guide/)).

§10.2 "Reduce live state" row – Vendor notes: append "RDNA 4 dynamic VGPRs do not apply to inline RayQuery [I from
AMD/LLVM sources]". §10.2 Rays table – add a row: "One RayQuery object in scope at a time (end the shadow query's scope
before the extension query) | VGPRs ↓ | VGPRs, trace ms | AMD rule | RDNA performance guide".

§12 Q38 (RayQuery vs DispatchRays) – append: "On RDNA 4 DispatchRays can use dynamic VGPRs (traversal stage small,
shading stage large) while RayQuery cannot – a concrete reason the answer may differ per vendor [I]."

**§12 Q48 → RESOLVED (delete).** §10.2 "Robust self-intersection" row – Source cell: "Cycles `ray_offset()` uses these
constants and cites RTG ch. 6 ([util.h](https://raw.githubusercontent.com/blender/blender/main/intern/cycles/kernel/bvh/util.h))".
Add to Vendor notes: "Cycles 2022 removed most offsetting in favour of skipping the origin's prim+object ID
([74afc86](https://api.github.com/repos/blender/blender/commits/74afc86d4bf1a559e5b7a0cf5cea29cb508a3e99)); in DXR this
needs non-opaque geometry (candidate test in `Proceed()`), so for EV's opaque geometry the offset stays the default
[I]." Also replace the followup §4 "[U: not read; T: it skips the originating primitive instead]" with the commit facts.

**§12 Q49 → RESOLVED (delete).** §10.1 setup item 5 (FLIP) – append: "Defaults: 67 ppd (0.7 m viewing distance, 3840
px across a 0.7 m wide monitor), HDR tone mapper ACES, start/stop exposure and number of exposures computed
automatically when not given – log the values FLIP prints and reuse them for all variants of a shot
([FLIP.h](https://raw.githubusercontent.com/NVlabs/flip/main/src/cpp/FLIP.h))." Followup §2 "[T] … FLIP default viewing
condition 67 pixels per degree [T][U]" → verified.

**§12 Q45 → PARTLY RESOLVED.** New text: "45. Overhead of RGP shader instrumentation (`--rgp-shader-instrumentation`,
needed for Average active lanes) on timings – measure once." §10.1 last paragraph: replace "(RDP 3.5; flags not
documented in the announcement)" with: "`RadeonDeveloperPanelCLI` (RDP 3.5.1 manual): `-p <process>`, `--clock-mode
normal|stable`, `-a dispatch:<start>:<count>` or `frame:<N>`, `--rgp-capture-mode dispatch`,
`--rgp-shader-instrumentation`, `-o <file>`; RRA: `-m raytracing`, `--rra-marker-capture --rra-marker-begin/-end`
(driver 26.20+) ([RDP CLI](https://gpuopen.com/manuals/rdp_manual/radeon_developer_panel_cli/))."

**§12 Q43 → NARROWED.** Append: "The CLI offers only `normal|stable` clock modes (no peak switch); the frequency is
not documented – read it from an RGP capture or a monitoring tool."

**§12 Q42 → UNCHANGED, add note.** Append: "Only third-party/user reports (Zivid: not all GPUs support `-lgc/-lmc`);
test on the 3070 and record the result here."

**Not changed by this run:** §12 Q44, Q46, Q47, Q51, Q52.
