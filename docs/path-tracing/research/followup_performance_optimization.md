# GPU path tracer performance: profiling methodology and optimization strategies (EV, RX 9070 XT + RTX 3070)

Researched 2026-11-18 for the EV path tracer (DXR 1.1 inline RayQuery compute megakernel, spectral float4, GGX+Lambert +
spectral water, ocean BLAS refit, progressive accumulation, OIDN/OptiX, later real-time + SHaRC + own NRC). Feeds
`knowledge.md` §10.1 (profiling routine), §10.2 (strategy catalogue), M5c, O1, O2.

Labels as in `knowledge.md`: no label = verified against the linked page on 2026-11-18; **[T]** training knowledge,
not re-fetched; **[I]** inference; **[E]** estimate; **[U]** unverified. Note on process: the research session was
interrupted by a usage limit; the full text of Laine et al. 2013 could not be extracted (PDF is compressed, no local
PDF tooling: no Python, no poppler), so the paper's internal numbers are marked [T]/[U].

Already covered in `knowledge.md` §6.4–6.7 and not repeated here: AS flag recipes, compaction, AS budget ~2 ms, async
compute hides AS cost (NVIDIA), RDNA group size 8×4 / NVIDIA 8×8–16×8, little groupshared memory in RT shaders on AMD,
tool versions (PIX, RRA 1.8+/1.11.1, RGP 2.7.1, Nsight 2026.x), DRED op names.

---

## 1. Megakernel vs wavefront path tracing, material sorting, SER

### Takeaway
A wavefront design (trace → sort/bin by material → shade → next bounce) wins when there are **many and/or expensive,
code-divergent materials**; with one or a few cheap materials the megakernel's on-chip state beats the wavefront's
memory traffic. Production renderers (Cycles, pbrt-v4) are wavefront on the GPU and sort/queue by material; the
measured gain of *sorting itself* in Cycles is modest (2–15 % overall, Metal-tuned), while hardware SER on RTX 40+
reports 20–50 % in path tracers. Neither of EV's GPUs reorders with SER, so software sorting (M5c) is the only way to
measure coherence effects on both PCs.

### Cited Findings
- Laine, Karras, Aila (HPG 2013): "simply porting a large CPU program into an equally large GPU kernel is generally not
  a good approach"; SIMT control-flow divergence is costly and high register usage reduces the latency hiding memory
  access depends on; the wavefront formulation is better suited to "real-world scenes with multiple complex materials"
  — [NVIDIA Research page](https://research.nvidia.com/publication/2013-07_megakernels-considered-harmful-wavefront-path-tracing-gpus),
  PDF [author copy](https://users.aalto.fi/~laines9/publications/laine2013hpg_paper.pdf).
- [T][U] Laine et al. design (from memory, not re-read): a *logic* kernel (advances path state, RR, decides next
  stage), a *new path* kernel, one *material* kernel per material type, separate *extension ray* and *shadow ray* cast
  kernels; work passed through queues of path indices filled with atomic counters; a fixed pool of ~1M paths in
  flight with ~200 B of state per path; the test scenes included a complex car-paint material where the megakernel
  suffered most. Exact numbers must be checked in the PDF before quoting.
- pbrt-v4 GPU: separate kernels pass work through queues; "rays that hit surfaces go into queues partitioned by
  material type", other queues for escaped rays, emitter hits and media; each material type is evaluated "in its own
  kernel" (builds the BSDF and samples a light); media/subsurface kernels skipped when unused — [pbr-book 4ed 15.1](https://pbr-book.org/4ed/Wavefront_Rendering_on_GPUs/Mapping_Path_Tracing_to_the_GPU).
- pbrt-v4 states the trade-off: wavefront kernels start "mostly converged" and each kernel gets its own register
  allocation, but data does not stay on chip between kernels, so the design "is more likely to be bandwidth-limited";
  the megakernel keeps intermediates on chip but suffers divergence across materials/textures and its register
  allocation must cover the worst case; smaller objects reduce bandwidth and register use — [pbr-book 4ed 15.1](https://pbr-book.org/4ed/Wavefront_Rendering_on_GPUs/Mapping_Path_Tracing_to_the_GPU).
- Cycles (Blender) GPU: wavefront, each kernel advances a path to another kernel or terminates it; per-path
  `IntegratorState` stored as **SoA** on the GPU, "as compact as possible", memory reserved for "millions of states";
  scheduler: if fewer than half the paths are active, fetch a new tile and init from camera; then run "the kernel that
  most active paths need next" over gathered active indices; active paths are **compacted** to the start of the array
  before more tiles are scheduled; shadow paths in a separate array; `shade_surface` **sorts paths by shader** —
  [Cycles kernel scheduling](https://developer.blender.org/docs/features/cycles/kernel_scheduling/).
- Cycles X announcement: biggest gains "in interior scenes with many light bounces and shaders, where the new kernels
  can achieve higher occupancy and coherence" — [Cycles X blog, 2021](https://code.blender.org/2021/04/cycles-x/).
- Cycles sort key: "Sort first by truncated state index (for good locality), then by key (for good coherence)" —
  partitions trade shader coherence for memory locality; partitioned sorting gave "up to 15 %" overall render speedup on
  Apple M1/M2 (partition 65536); per-partition sort with local atomics 2–3 % depending on architecture; the heuristic
  was later generalized to all GPU devices with "good results with CUDA and OptiX" (no figures) —
  [Cycles commit 523bbf70655](https://developer.blender.org/rB523bbf70655),
  [local atomics commit 654e1e9](https://developer.blender.org/rB654e1e9),
  [chunking commit 4b1d315017e](https://developer.blender.org/rB4b1d315017e) (commit messages via search snippets; the
  Blender project site returned 403 to direct fetches).
- Dr.Jit / Mitsuba 3 compiles the whole simulation (loops + polymorphic calls) into one "megakernel" and reports
  geometric-mean GPU speedups of 3.70× over Mitsuba 2 and 2.14× over pbrt-v4 — [Dr.Jit paper page](https://rgl.epfl.ch/publications/Jakob2022DrJit).
  [T][U] Dr.Jit also offers "evaluated" (wavefront-style) loops/calls as an alternative to "symbolic" ones.
- Bikker's wavefront write-up (Lighthouse 2): Generate → Extend → Shade → Connect; Shade writes **compacted** extension
  and shadow buffers with atomic counters; 1080p buffer budget ≈ 224 MB (32 B rays, 16 B hits, double-buffered
  extension rays, shadow rays), ~1 GB at 4K; no quantitative megakernel comparison; the outcome depends on kernel time,
  path length, megakernel occupancy and register pressure; reader comments note per-material queues sized for the worst
  case waste memory — [Bikker 2019](https://jacco.ompf2.com/2019/07/18/wavefront-path-tracing/).
- SER (NVIDIA, Oct 2022, UE5 NvRTX on RTX 40): path tracing 20–50 % faster from reordering material evaluation
  ("40 % faster" in one view); Lumen: removing compaction/relaunch passes "often worth a 20 % savings"; reordering has a
  cost, so it was applied selectively — [NVIDIA SER blog](https://developer.nvidia.com/blog/improve-shader-performance-and-in-game-frame-rates-with-shader-execution-reordering/).
- SER in DXR 1.2 (Microsoft, 2026-02-26): ~40 % on RTX 4090 and ~90 % on Intel Arc B in the
  `D3D12RaytracingHelloShaderExecutionReordering` sample (synthetic best case); sort key can be shader-defined (sample
  uses a 1-bit "heavy work" key); a device query reports whether reordering really happens — [MS SER post](https://devblogs.microsoft.com/directx/shader-execution-reordering/).
- Indiana Jones (NVIDIA): SER saves 24 % of GPU time on TraceMain; reducing RT live state (spills 222 → 84 B, e.g. a
  float4 to fp16, removing a loop) cut TraceMain 3.63 → 3.08 ms (~15 %) on an RTX 5080 at 4K —
  [NVIDIA blog](https://developer.nvidia.com/blog/path-tracing-optimization-in-indiana-jones-shader-execution-reordering-and-live-state-reductions/)
  (via search snippet; page not fetched in full).
- NVIDIA: "Avoid divergent shading with inline ray tracing", use DispatchRays when several shading models are needed;
  "use a separate hit shader for each material model", avoid übershaders — [NVIDIA RTX best practices (updated)](https://developer.nvidia.com/blog/best-practices-for-using-nvidia-rtx-ray-tracing-updated/).

### Inferences
- [I] Sorting buys **coherence** (lanes of a wave run the same material code) but not **register allocation** if the
  shade pass is still one uber-shader with a `switch(materialType)`: the compiler still allocates VGPRs for the worst
  case. To also get per-material register allocation (the pbrt-v4/Laine benefit), each material type needs its own
  dispatch (one PSO per material, launched with `ExecuteIndirect` from GPU-written counts). M5c should measure both:
  (a) sort + one uber shade kernel, (b) sort + one dispatch per material bin.
- [I] Divergence only hurts when the *code* differs. Materials that share code and differ only in parameters/textures
  (EV's metal-roughness materials) diverge little; sorting them mainly improves texture/cache locality. The benchmark
  must therefore use materials with genuinely different instruction streams.
- [I] Counting sort recipe for a small key space (≤ 64 materials): (1) histogram pass – per-group histogram in
  groupshared (`InterlockedAdd`), then one global atomic per non-zero bin; (2) exclusive prefix sum over ≤ 64 bins
  (one group, or `WavePrefixSum` in a single wave); (3) scatter – each path claims `offset[key] + atomic` and writes
  its index into a permutation buffer (unstable order is fine); (4) shade reads `perm[i]` → path state (gather). Wave
  aggregation (`WaveActiveCountBits`/`WavePrefixCountBits` per key, one atomic per wave per key) cuts atomic contention.
  Dead-path compaction is the same scan with key = alive/dead. Optionally add a high-order "locality" partition like
  Cycles (sort within chunks of N paths) to keep gathers cache-friendly.
- [I] GPU-driven sizes: the per-bin counts become `D3D12_DISPATCH_ARGUMENTS` for `ExecuteIndirect`, so the CPU never
  reads counts back (EV already has an `IndirectArgument` access kind in `render_graph.h:88`).
- [E] Path-state memory for EV (spectral): origin 12 B + direction 12 B (or 8 B octahedral) + throughput float4 16 B +
  wavelengths float4 16 B + wavelength pdfs float4 16 B + radiance accumulator float4 16 B + pixel index 4 B + RNG/
  sample index 8 B + bounce/flags/medium 4 B + previous BSDF pdf (MIS) 4 B + specular-bounce/eta-scale 4–8 B ≈ 110–
  130 B; hit record (t, barycentrics, instance + primitive ID, material key) 16–24 B; sort key + permutation 8 B. At
  1080p (2.07 M paths) ≈ 300 MB; 1440p (3.7 M) ≈ 530 MB; fp16 for wavelength pdfs/throughput would save ~25 %. A fixed
  pool of ~1 M paths refilled per bounce (Laine/Cycles style) caps this at ~150 MB [E].
- [E] Expected behaviour (to be replaced by measurements): 1 material – wavefront slower than megakernel by ~10–30 %
  (memory round trips, more dispatches, barriers); 4 materials – near break-even; 16–64 code-divergent materials –
  sorted wavefront wins and unsorted wavefront is no better (or worse) than the megakernel; bounce 0 is coherent anyway
  (camera rays), the gain shows from bounce 1 on.
- [I] RDNA 4 and Ampere both execute compute in 32-lane groups (RDNA wave32, NVIDIA warp32), so the divergence math is
  the same; differences come from occupancy limits (VGPR budgets, §5) and memory systems. Ampere has no SER reorder;
  RX 9070 XT accepts the SER API but does not reorder (plan.md). So the M5c numbers are the only coherence numbers
  available on both machines.

### Gaps
- Laine et al. exact numbers (paths in flight, bytes per path, speedups per scene, GPU) – PDF text not extractable this
  session; re-read the PDF before quoting.
- No published Cycles benchmark of sort-by-shader on CUDA/OptiX/HIP alone (only Metal figures).
- No 2024–2026 megakernel-vs-wavefront benchmark on RDNA 4 or Ampere found (search budget exhausted by the limit).
- pbrt-v4 GPU performance numbers vs a megakernel: none on the page.

---

## 2. Profiling methodology for GPU path tracers

### Takeaway
Measure **quality per time**, not only speed: equal-time error (relMSE / HDR-FLIP vs a high-spp reference) and
convergence curves decide whether a change helps. For speed, record per-pass GPU time with D3D12 timestamps, rays/s
and samples/s, plus occupancy/divergence from vendor tools. Lock or stabilise clocks for A/B comparisons, warm up,
take medians over many frames, and re-check the final result at normal clocks.

### Cited Findings
- Timestamps: `D3D12_QUERY_TYPE_TIMESTAMP`; direct and compute queues use `D3D12_QUERY_HEAP_TYPE_TIMESTAMP` and
  always support timestamps; copy queues use `..._COPY_QUEUE_TIMESTAMP` only if `OPTIONS3::CopyQueueTimestampQueriesSupported`;
  frequency is **per queue** (`ID3D12CommandQueue::GetTimestampFrequency`, E_FAIL if unsupported); a timestamp is
  sampled when the GPU has finished all preceding work (**bottom of pipe**); resolve with `ResolveQueryData` into
  UINT64 ticks; divide by the queue frequency in floating point; `GetClockCalibration` correlates GPU ticks with
  `QueryPerformanceCounter`; "GPU timestamp clocks can stop ticking on some hardware when the GPU is idle, so the
  correlation can drift" – use `SetStablePowerState` during development — [MS Learn: Timing](https://learn.microsoft.com/en-us/windows/win32/direct3d12/timing).
  The page recommends PIX timing captures before writing custom timestamp infrastructure.
- `SetStablePowerState`: development-time only; only works in Windows **Developer Mode**, otherwise **device
  removal**; "typically fixes GPU clock rates at a slower setting that is significantly lower" than normal; affects the
  entire system; conclusions "should be double-checked with supporting results from real usage scenarios" —
  [MS Learn: SetStablePowerState](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-setstablepowerstate).
- NVIDIA: `SetStablePowerState` "does not lock the memory clock, which makes the results less comparable"; lock with
  `nvidia-smi --lock-gpu-clocks=<MHz>` and `--lock-memory-clocks=<MHz>`, reset with `--reset-gpu-clocks` /
  `--reset-memory-clocks`, query with `nvidia-smi --query-supported-clocks=...`; run in an administrator console;
  script lock → run → reset; Nsight Graphics GPU Trace has a "Lock Clocks to Base" option; "Don't rely solely on the
  `SetStablePowerState` function when profiling" — [NVIDIA Advanced API Performance: SetStablePowerState](https://developer.nvidia.com/blog/advanced-api-performance-setstablepowerstate).
- `nvidia-smi -lgc` / `-rgc` are "Supported on Volta+. Requires root" (admin on Windows); `-lmc` requires root;
  `--applications-clocks` is deprecated — [nvidia-smi docs](https://docs.nvidia.com/deploy/nvidia-smi/index.html). The
  page does not state GeForce support for `-lgc` explicitly [U]; [T] it works on GeForce RTX 30 in practice.
- Nsight Graphics "Lock Clocks to Base" does the same as `SetStablePowerState(TRUE)` (search summary of NVIDIA docs/
  forum, see [GPU Trace overview](https://docs.nvidia.com/nsight-graphics/UserGuide/gpu-trace-overview.html)) [U wording].
- AMD Radeon Developer Panel (v3.5.1): **Normal** clock mode varies clocks so "profiles taken of the same application
  may differ significantly"; **Stable** mode "will run the GPU at a lower, fixed clock rate … much easier to compare
  profiles"; during an RGP capture "clock modes for the device will be changed to peak" — [RDP manual: System / Device
  clocks](https://gpuopen.com/manuals/rdp_manual/system/). The RDP CLI has `--clock-mode normal|stable` —
  [RDP CLI](https://gpuopen.com/manuals/rdp_manual/radeon_developer_panel_cli/) (via search snippet). The Device Clocks
  tab only appears while the target app runs.
- Occupancy matters mainly for latency-bound shaders; it "will only improve performance if the GPU can use the occupancy
  to hide latency"; higher occupancy can hurt memory-bound shaders (cache thrashing); RGP Pipeline tab shows theoretical
  occupancy, the limiting resource and spills; the Wavefront occupancy tab shows measured occupancy over time; RGA shows
  live VGPRs per instruction — [GPUOpen: Occupancy explained](https://gpuopen.com/learn/occupancy-explained/).
- FLIP v1.7: LDR-FLIP and HDR-FLIP; `pip install flip-evaluator`; `flip -r reference.exr -t test.exr`; C++/CUDA
  single-header `FLIP.h` — [NVlabs/flip](https://github.com/NVlabs/flip).
- [T] Standard MC efficiency: efficiency = 1 / (variance × time) (pbrt "Efficiency of an estimator"; Veach); for an
  unbiased estimator MSE ∝ 1/N, so log(MSE) vs log(time) has slope −1, and an equal-time comparison is the fair test of
  a change that alters cost per sample.
- [T] relMSE = mean over pixels of (x − ref)² / (ref² + ε), ε ≈ 1e-2 (Rousselle-style), robust to bright pixels; also
  report plain MSE and HDR-FLIP mean. FLIP default viewing condition 67 pixels per degree [T][U].

### Inferences
- [I] Metrics per run: per-pass GPU ms (trace / sort / shade / NEE / accumulate / denoise / AS refit / TLAS), frame
  ms, **samples/s** (pixels × spp / s), **rays/s** split by type (primary/extension/shadow; count with a GPU counter
  buffer of `InterlockedAdd` in a debug permutation, *not* in the timed build), average path length, bounce-wise active
  path count (from M5c), VRAM, BVH build/refit ms + size; from tools: occupancy (waves per SIMD / warps per SM), average
  active lanes (divergence), VGPR count + spills, cache hit rates, RT unit utilisation, traversal steps (RRA heatmap).
- [I] Stable-timing recipe: Release build (no debug layer, no GBV; EV forces DRED auto-breadcrumbs on – keep it
  constant across A/B runs and note it, its cost is small but non-zero [U]), vsync off, fixed window size, same driver noted, other apps closed, AC power; warm-up ≥ 300
  frames or ~30 s [E] to reach steady thermal state and fill shader caches; measure ≥ 500 frames; report **median and
  p95** (plus min); run 3 times and keep the median of medians; A/B runs interleaved (A, B, A, B) to cancel drift.
- [I] Clock policy: for small A/B deltas (< 5 %) use locked clocks (RDP Stable on the 9070 XT; `nvidia-smi -lgc/-lmc`
  or Nsight "Lock Clocks to Base" on the 3070); for milestone baselines in `performance.md` record both locked and
  normal-clock numbers, because the MS docs warn conclusions under stable power must be double-checked at real clocks.
  `SetStablePowerState` only behind an off-by-default `--stable-power` switch (needs Developer Mode; device removal
  otherwise).
- [I] EV's `GpuTimer` (`source/DX12/gpu_timer.cpp`) takes the **direct queue's** frequency (line 33) and records begin/
  end `EndQuery` pairs per scope (max 256/frame, 4 frames in flight, results read back 4 frames later, no averaging).
  For async compute (O2) a second timer on the compute queue needs that queue's own frequency and a
  `GetClockCalibration` pair to place both on one timeline; overlapping passes' times then do not sum to the frame time.
- [I] Timestamps are bottom-of-pipe; without a barrier between two dispatches the GPU may overlap them, so a scope's
  time can include or hide the neighbour. For per-pass profiling a UAV barrier between passes (which the render graph
  usually inserts anyway) makes scopes meaningful; in a "throughput" run, compare total frame time instead.
- [I] Equal-time protocol: render the frozen benchmark shot for a fixed wall-clock GPU time (e.g. 10 s and 60 s) per
  variant, save the linear HDR EXR, compute relMSE/MSE/HDR-FLIP vs a ~16k–64k spp reference rendered by the
  *unoptimised* reference build (so an optimization cannot move the reference); also plot error vs time on log-log axes
  (slope −1 check, and any bias shows as a floor).

### Gaps
- No primary source found for AMD's peak-clock profiling caveats beyond the RDP page (e.g. whether "peak" holds under
  thermal load on RDNA 4).
- Exact FLIP default ppd not confirmed from the repo page this session.

---

## 3. Tools in practice (RX 9070 XT and RTX 3070)

### Takeaway
On the RX 9070 XT: RGP 2.7 (occupancy, instruction timing, **Average active lanes** for divergence on RX 9000, HLSL
source next to ISA, pure-compute "Dispatch" capture mode) + RRA 1.11 (BVH, SAH colouring, traversal-counter
heatmaps, ray inspection) + PIX (timing captures, AMD counters). On the RTX 3070: Nsight Graphics GPU Trace (Turing+;
real-time shader profiler Ampere+; only the Hardware Events/Compute row needs Blackwell) + Nsight Compute for CUDA
kernels + PIX. rocprofv3 is not a native-Windows tool (WSL2 experimental) – use RGP for HIP on Windows.

### Cited Findings
- RGP 2.4 (2025-03-11): full feature support for RX 9000; **pure compute capture**: in RDP set Capture mode =
  **Dispatch** (or Auto capture mode = Dispatch range with start index + count); graphics-only panes hidden; HIP/OpenCL
  capture uses a similar mechanism — [GPUOpen RGP 2.4](https://gpuopen.com/learn/radeon_gpu_profiler_2-4_support_rx9000_series_pure-compute_applications/).
- RGP 2.7 / tool suite (2026-06-11): ISA tab renamed **Shader**, shows HLSL source next to ISA (compile with DXC `-Zi`
  or `-Zs` + `-Qsource_in_debug_module`, set "Shader debug information search paths"); **"Average active lanes"**
  column in Instruction timing (thread divergence), needs shader instrumentation enabled in RDP, RX 9000+; extended PIX
  markers need Agility `D3D12SDKVersion` ≥ 721 + `SetUserDefinedAnnotationMode(DRIVER_RETAIL)` + driver 26.10.07.02;
  new **RadeonDeveloperPanelCLI** for scripted capture; RRA 1.11: hover statistics, geometry filter slider (e.g. lowest
  SAH), separate internal/leaf bounding-volume toggles — [GPUOpen tool suite update](https://gpuopen.com/learn/radeon-developer-tool-suite-shader-source-code/).
- RRA: traversal counter mode counts instance hits, box hit/miss, triangle hit/miss per pixel as a heatmap (tooltips
  per pixel); a traversal-count histogram (1.2); Ray tab with real (not simulated) per-ray traversal data (1.3);
  "Max anyhit invocations" flag (1.11.1) — [RRA 1.2](https://gpuopen.com/learn/rra_1_2/),
  [RRA 1.3](https://gpuopen.com/learn/rra_1_3/), [RRA docs](https://radeon-raytracing-analyzer.readthedocs.io/en/stable/).
- RRA performance guide: SAH colour mode (0–1, low = poorly aligned triangles); traversal counts above ~2000 per pixel
  are "a likely framerate risk"; profile TLAS build time in RGP when adding instances — [GPUOpen: Improving RT perf with RRA](https://gpuopen.com/learn/improving-rt-perf-with-rra).
- RGP 2.6 added LDS bank-conflict and memory counters for RDNA 4 (Phoronix via search); RGP 1.11 follows control flow
  between RT pipeline shaders in Instruction timing — [RGP releases](https://github.com/GPUOpen-Tools/radeon_gpu_profiler/releases) (attribution of items to versions [U]).
- Nsight Graphics GPU Trace: supports **Turing and newer**; the **Real-Time Shader Profiler** needs **Ampere or
  newer** (works on the RTX 3070); "Hardware Events and Compute row" only on the latest **Blackwell** GPUs; the **Ray
  Tracing Live State** view is filled when Shader Pipeline collection is enabled (no GPU requirement stated) —
  [Nsight Graphics GPU Trace overview](https://docs.nvidia.com/nsight-graphics/UserGuide/gpu-trace-overview.html).
- NVIDIA used GPU Trace to find RT live-state bytes in Indiana Jones (see §1) — [NVIDIA blog](https://developer.nvidia.com/blog/path-tracing-optimization-in-indiana-jones-shader-execution-reordering-and-live-state-reductions/).
- GPU Trace advanced-mode metrics for DXR — [NVIDIA blog](https://developer.nvidia.com/blog/optimizing-vk-vkr-and-dx12-dxr-applications-using-nsight-graphics-gpu-trace-advanced-mode-metrics/) (not fetched [U]).
- Nsight Compute on Ampere+ can lock to boost instead of base (`--clock-control` `boost`/`force-boost` on supported
  drivers) — [Nsight Compute release notes](https://docs.nvidia.com/nsight-compute/ReleaseNotes/index.html) (via search snippet [U] option spelling).
- rocprofv3 / ROCprofiler-SDK: experimental under **WSL2**; requires the Linux KFD driver; no native-Windows evidence —
  [rocprofv3 on WSL2](https://rocm.docs.amd.com/projects/rocprofiler-sdk/en/develop/how-to/using-rocprofv3-on-wsl.html),
  [ROCm profiling tools overview](https://rocm.docs.amd.com/en/latest/components/profiling-tools-overview.html).
  [T][U] Omniperf (now "ROCm Compute Profiler") is Linux/Instinct-focused.
- PIX: AMD counters built in (RX 9070 XT since May 2025) — already in knowledge.md §6.7 ([PIX counters](https://devblogs.microsoft.com/pix/hardware-counters-in-gpu-captures/)).

### Inferences
- [I] Per-question tool choice: "which pass is slow" → EV Render Graph panel / `--benchmark` CSV, PIX timing capture;
  "why is the PT dispatch slow" → RGP (occupancy, VGPRs, instruction timing, active lanes) / Nsight GPU Trace
  (throughput units, warp stall reasons, live state, shader profiler); "is the BVH bad" → RRA traversal heatmap + SAH
  view (AMD only; on NVIDIA, Nsight AS viewer has no traversal heatmap [U]); HIP kernels → RGP Dispatch capture;
  CUDA kernels → Nsight Compute; CPU↔GPU/interop stalls → Nsight Systems (both vendors' D3D12) / RGP timeline.
- [I] Instrumented captures change timing (RGP peak clocks, shader instrumentation for active lanes) – use them for
  *why*, the benchmark CSV for *how much*.

### Gaps
- Whether Nsight Graphics on RTX 30 shows RT-core (traversal) throughput metrics separately in GPU Trace – not
  confirmed this session.
- No traversal-heatmap equivalent confirmed for NVIDIA tools.

---

## 4. Optimization strategies with evidence

### Takeaway
The highest-value levers for EV are: shadow-ray flags + opaque geometry (cheap, certain), ocean BLAS tiling and
rebuild cadence (measurable in RRA), register pressure / live state (Indiana Jones: ~15 % from spills alone), sorting by
material when materials diverge (M5c), and sampling efficiency (fewer samples for the same error). Each must be judged
by equal-time error, not only ms.

### Cited Findings
**BVH**
- Split BLASes whose world AABB contains lots of empty space (terrain example) into chunks → tighter bounds, less
  overlap, at the cost of TLAS build time; avoid instance overlap; one BLAS with several geometries (one per material)
  beats splitting BLASes by material; axis-align meshes in BLAS space (scaffold: traversal >2000 → ~1000); splitting
  can backfire when instances overlap (tree example) — [GPUOpen RRA guide](https://gpuopen.com/learn/improving-rt-perf-with-rra).
- NVIDIA: merge BLASes whose instance AABBs overlap significantly; split when an instance AABB has much empty space;
  avoid elongated triangles; use triangles — [NVIDIA best practices (updated)](https://developer.nvidia.com/blog/best-practices-for-using-nvidia-rtx-ray-tracing-updated/).
- Updates: fine for smooth deformation (bow example: no meaningful penalty); bad when nearby triangles tear apart
  (door: ~1100 traversal steps); rebuild every n frames, update the rest; avoid large stretch/skew in instance
  transforms — [GPUOpen RRA guide](https://gpuopen.com/learn/improving-rt-perf-with-rra).
- Flags/compaction/budget: see knowledge.md §6.4 (NVIDIA best practices).

**Rays**
- `RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH` for shadow rays; face culling "only when required for correctness" (it
  slightly slows traversal); mark geometry/instances opaque; prefer **compile-time** ray flags (RayQuery template
  flags) — [NVIDIA best practices (updated)](https://developer.nvidia.com/blog/best-practices-for-using-nvidia-rtx-ray-tracing-updated/).
- AMD: avoid face culling (culled triangles are still tested and cannot end traversal early); prefer compile-time ray
  flags over per-instance flags; `RAY_FLAG_FORCE_OPAQUE` at trace time avoids any-hit and helps the compiler —
  [GPUOpen RRA guide](https://gpuopen.com/learn/improving-rt-perf-with-rra).
- Iterative instead of recursive tracing: 69.6 % of the recursive time (16 bounces, RX 6700 XT, 1080p) — same page
  (EV is already iterative in a compute loop).
- Self-intersection: Wächter & Binder, RTG1 ch. 6 – offset along the **geometric normal** in integer ULP space
  (`int_scale = 256`), fixed float offset near the origin (`origin = 1/32`, `float_scale = 1/65536`); "more robust than
  common practices", minimal overhead, no parameter tweaking — [RTG1 ch. 6 PDF](https://link.springer.com/content/pdf/10.1007/978-1-4842-4427-2_6.pdf),
  [NVIDIA Research page](https://research.nvidia.com/publication/2019-03_fast-and-robust-method-avoiding-self-intersection),
  constants as implemented in Cycles' `ray_offset` (search snippet). Cycles later removed ray offsetting in favour of
  other measures — [Cycles commit "remove ray offsetting"](https://developer.blender.org/rB74afc86d4bf1a559e5b7a0cf5cea29cb508a3e99)
  [U: not read; T: it skips the originating primitive instead].
- [T] Tight TMax for shadow rays (distance to light × (1 − ε)) and TMin = 0 with an offset origin (instead of a large
  TMin) shorten traversal and avoid light leaks.

**Shading / registers**
- Keep payload/live state small; "minimize live state across ray-trace calls"; "monitor the register consumption of the
  query objects" (RayQuery state lives in registers) — [NVIDIA best practices (updated)](https://developer.nvidia.com/blog/best-practices-for-using-nvidia-rtx-ray-tracing-updated/).
- Indiana Jones: spills 222 → 84 B, TraceMain −15 % (fp16 for a float4, branch instead of loop) — [NVIDIA blog](https://developer.nvidia.com/blog/path-tracing-optimization-in-indiana-jones-shader-execution-reordering-and-live-state-reductions/).
- RDNA occupancy: 16 wave slots per SIMD (RDNA 2+); waves = VGPR budget / VGPRs per wave, allocated in blocks;
  wave64 needs more resources — [GPUOpen occupancy](https://gpuopen.com/learn/occupancy-explained/).

**Memory**: pbrt-v4 – smaller objects reduce bandwidth and register use; coalesced access into few cache lines —
[pbr-book 15.1](https://pbr-book.org/4ed/Wavefront_Rendering_on_GPUs/Mapping_Path_Tracing_to_the_GPU); Cycles – SoA path
state on GPU — [Cycles kernel scheduling](https://developer.blender.org/docs/features/cycles/kernel_scheduling/);
Bikker – 16-B padded rays for single 128-bit loads — [Bikker 2019](https://jacco.ompf2.com/2019/07/18/wavefront-path-tracing/).

**Sampling, frame structure**: [T] MIS (power heuristic), RR with throughput-based survival (e.g. q = min(1,
max(throughput)) after bounce 3), low-discrepancy (Sobol + Owen scrambling, Burley 2020) and blue-noise error
distribution (Heitz & Belcour 2019) reduce error at equal spp; path guiding (Müller et al. 2017 "Practical Path
Guiding") is a larger project — references [U] in knowledge.md §11. Async compute for AS work: knowledge.md §6.4.

### Inferences
- [I] Ocean 513×513 grid as one BLAS: one instance with a huge, flat AABB; for a camera near the surface most rays
  enter it. Splitting into e.g. 8×8 tiles of 64×64 quads (≈ 8k tris each) gives 64 BLASes/instances: tighter bounds
  for grazing rays, per-tile refit, the option to rebuild only the worst tiles, and per-tile culling/LOD later. Cost:
  more TLAS instances (cheap at 64) and seams must share vertices exactly (tile edges duplicate the border row). Measure
  with RRA traversal heatmap + trace ms at frozen time, and refit ms with time running [E: gain unknown, could be small
  because the BVH is already spatial within one BLAS – the main gain is refit quality and partial rebuild].
- [I] Instance masks: give the ocean, meshes, light proxies and "invisible to camera" objects separate mask bits so
  shadow rays can skip light-proxy spheres and underwater rays can skip above-water-only geometry.
- [I] fp16: DXC `-enable-16bit-types` + `min16float`/`float16_t` for throughput/wavelength pdfs/BSDF intermediates
  (RDNA packed math: 2 ops per lane per instruction [T]; Ampere has 2× fp16 rate on ALU paths [T]); keep positions,
  t values and accumulation fp32. Verify no colour shift (FLIP) and no NaN.
- [I] Spectral cost: 4 lanes in a float4 multiplies the per-hit math for spectra (rgb2spec eval, Fresnel(λ), absorption)
  but not traversal; measure the M4 overhead as (spectral − RGB) shade ms, and consider precomputed coefficient
  textures (D10) and analytic CMF fits (Wyman) to cut lookups.
- [I] Denoise cadence: in reference mode denoise every N frames / on camera stop (M7); in real-time mode the denoiser
  is a fixed per-frame cost → resolution scaling (render at 67–77 % and upscale) trades directly against it.

### Gaps
- No primary evidence on ocean-grid tiling gains; must be measured.
- No source found for RDNA 4 vs Ampere fp16 gains in RT shaders specifically.

---

## 5. RDNA 4 and Ampere specifics

### Takeaway
RDNA 4 doubles intersection throughput per Ray Accelerator (8 box / 2 triangle tests per clock), uses BVH8 nodes,
oriented bounding boxes, compressed primitive nodes and a hardware ray-transform block – all chosen by the driver's
BVH builder (no developer action), so the developer's levers are BVH quality (geometry layout), ray flags, VGPR count
(192 KB VGPR file per SIMD, 16 wave slots, ≤ 96 VGPRs for full occupancy) and LDS use (traversal stack). Ampere: 48
warps/SM, 64K registers/SM, 255 registers/thread, 100 KB shared memory/SM, no SER reorder.

### Cited Findings
- RDNA 4 Ray Accelerator: two intersection engines → **8 box tests or 2 triangle tests per cycle** (RDNA 2/3: 4 box or
  1 triangle); **8-wide box nodes** (`IMAGE_BVH8_INTERSECT_RAY`) – shallower trees, fewer traversal steps, more box
  tests per step; box nodes stay 128 B (one cache line); **OBBs**: one rotation index per node from a table of 104
  predefined matrices; new 128-B compressed primitive nodes (up to 8 triangle pairs / 16 unique vertices); a **ray
  transform block** offloads the TLAS→BLAS transition; traversal stack in LDS with new `DS_BVH_STACK_PUSH8_POP1/POP2`
  instructions; BVHs come from AMD's GPURT build code (no documented developer control); 3DMark DXR test: RX 9070
  111.76 G box tests/s and 19.61 G triangle tests/s (RX 6900 XT 38.8 G / 10.76 G), ~24 % estimated RA utilisation —
  [Chips and Cheese: RDNA 4's raytracing improvements](https://chipsandcheese.com/p/rdna-4s-raytracing-improvements).
- RDNA 4 dynamic VGPR allocation: only **wave32 compute** shaders; `s_alloc_vgpr` in blocks of 16/32; seen only with
  *indirect* ray tracing (separate function calls), not inlined stages – inline RT must allocate for the largest stage;
  desktop RDNA 4 has a **192 KB VGPR file per SIMD**, **16 wave slots**, static mode fills all 16 slots at **≤ 96
  VGPRs**; Quake 2 RTX example limited to 9/16 slots by VGPRs; max 256 VGPRs per thread —
  [Chips and Cheese: dynamic register allocation on RDNA 4](https://chipsandcheese.com/p/dynamic-register-allocation-on-amds).
- AMD RDNA guidance (8×4 groups, little LDS in RT shaders, TLAS on compute queue) — knowledge.md §6.6
  ([RDNA performance guide](https://gpuopen.com/learn/rdna-performance-guide/)).
- Ampere CC 8.6: max 48 warps/SM, 16 blocks/SM, 255 registers/thread, 64K 32-bit registers/SM, shared memory up to
  100 KB/SM (carveouts 0/8/16/32/64/100), 128 KB combined L1/tex/shared, 2× FP32 per clock per SM vs CC 8.0 (compile
  for sm_86) — [CUDA Ampere tuning guide](https://docs.nvidia.com/cuda/ampere-tuning-guide/index.html).
- [T][U] Ampere 2nd-gen RT cores: ~2× triangle-intersection throughput vs Turing and hardware motion-blur
  interpolation — GA102 whitepaper (https://www.nvidia.com/content/PDF/nvidia-ampere-ga-102-gpu-architecture-whitepaper-v2.pdf [U]).
- SER reorders on RTX 40+ only (plan.md hardware table); MS post names RTX 4090 and Arc B — [MS SER post](https://devblogs.microsoft.com/directx/shader-execution-reordering/).

### Inferences
- [I] Occupancy arithmetic for the PT megakernel. RDNA 4 wave32: 192 KB / (32 lanes × 4 B) = 1536 VGPRs per SIMD
  per lane-column → 16 waves at ≤ 96 VGPRs, 12 at 128, 8 at 192, 6 at 256 (block rounding ignored). Ampere: 65536
  regs / 32 lanes = 2048 per SM per lane → 48 warps need ≤ 42 regs/thread; 64 regs → 32 warps; 128 regs → 16 warps;
  255 → 8 warps. A spectral megakernel with RayQuery state will likely sit at 100–160 VGPRs/registers [E], i.e. ~40–60
  % theoretical occupancy on RDNA 4 and ~25–40 % on Ampere – so live-state reduction is the first shading lever.
- [I] Because inline RT on RDNA 4 does not use dynamic VGPRs and the traversal stack uses LDS, a groupshared-heavy
  sort *inside* the trace kernel would compete with traversal; keep the material sort in its own pass.
- [I] BVH8/OBB mean RRA (BVH viewer) shows different node structure on the 9070 XT than an NVIDIA AS viewer; compare
  BVH *sizes* across GPUs only as "bytes per triangle", not node counts.

### Gaps
- No AMD primary doc found for RDNA 4 RT best practices beyond the general RDNA guide (GPUOpen pages not reachable
  during the limit window).
- Ampere GA102 whitepaper figures not re-fetched.
