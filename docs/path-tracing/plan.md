# Path tracing in EV – plan and decisions

Branch: `path-tracing` (from `scene-save-load`). Started 2026-11-18.

Goal: a spectral path tracer (hero wavelength sampling) as a **toggleable core-engine feature** of EV, first used by
OceanRenderer, denoised with OIDN and the OptiX denoiser, using GPU compute (HIP on AMD, CUDA on NVIDIA) where it fits,
and growing into a self-written **Neural Radiance Cache** (NRC).

**Scope update 2026-11-22:** the ocean is **no longer the main target**. The developer will build proper test
environments later. A second goal was added: **learn and fairly compare real-time global illumination techniques**
(SHaRC, NRC, DDGI probes, surfels, radiance cascades, ReSTIR) on performance *and* visual fidelity, with one
measurement harness. See "GI techniques track" below. Ocean-specific items (D6, M2 ocean BLAS, M5 water) stay in the
plan but are no longer on the critical path.

**Who writes the code:** from M0 on, all code is written by the developer (learning project). Claude acts as tutor: it
explains, points to files, reviews and helps debug, using `knowledge.md` as its reference.

## Patroni board (built 2026-11-22)

The **Patroni version of this plan** exists: Home → `EV (Claude)` → `path-tracing`
(https://patroni.abodi.nl/w/01a1a76e-ee9e-74eb-9983-1d4c92cd8375#/b/01a204b0-f060-73b7-8333-8c053ab7df68).
It holds the roadmap (every milestone as a node with a "you are here" marker), one board per milestone (what + why,
knowledge + exact reading links, a visual, tasks, verify + pitfalls), the architecture (built vs planned), all
decisions, the GI track and profiling. Formatting rule for every update: no overlapping items, notes as a title line +
bullets, never a text blob.

What the board contains (the original spec):
1. **Roadmap overview**: every milestone (M0 … M11, O1/O2, the GI track, side tracks) as a flow with dependencies and
   a clear **"you are here"** marker (done / current / next / later). Updated after every step.
2. **Per milestone (nested nodes)**:
   - goal and why;
   - the knowledge: Claude's explanations from `knowledge.md` and `research/`, plus **links to the exact paper or
     article sections** to read;
   - **visualizations that explain the concepts** (diagrams, graphs, e.g. noise vs spp, cascade sizes, the
     reservoir merge, probe layouts);
   - the **tasks** as checklists, in the same order as the "what / knowledge / tasks" step format.
3. **How the systems work together**: an architecture board showing the modules and the data flowing between them
   (render graph, acceleration structures, path tracer passes, spectral code, denoiser interface, HIP/CUDA DLLs + fence
   interop, radiance-cache interface, harness). Things are marked *planned* vs *built*. It is drawn ahead of
   development and refined as each part gets built.
4. **Decisions**: D1–D18 and G1–G6, with the chosen option and a short reason.
5. **GI techniques track**: the techniques, how they compare, and the comparison harness and metrics.

Rule from then on: **Claude keeps the board up to date on its own, without being asked**. After every step, every new
decision and every plan change, it updates the status marker, ticks the tasks, adds the next step's knowledge and
tasks, and marks architecture parts as built, along with `build-log.md`.

## Documents

| File | What it is |
|---|---|
| `plan.md` | This file: hardware, the 18 decisions, the roadmap |
| `knowledge.md` | Tutor reference: theory, formulas, APIs, EV specifics, pitfalls, per-milestone guidance, sources |
| `build-log.md` | What was done in each step, with results and measurements |
| `performance.md` | Performance log: milestone baselines on both GPUs and before/after tables for every optimization |
| `research/00_report_2026-11-18.md` | The full research report (industry survey, theory primer, options per decision) |
| `research/*.md` | Raw research notes with sources (see `research/README.md`) |

The research report was written before two facts were known: development happens mainly on the **AMD** PC, and the
NVIDIA card is an **RTX 3070 Ti Laptop GPU** (a laptop; "the 3070" in this file means that GPU). Where the report and this file disagree, **this file wins** (D12, D14, D15, D16).

## Hardware

| Machine | GPU | Role | Notes |
|---|---|---|---|
| Main dev PC | AMD Radeon RX 9070 XT (RDNA 4, gfx1201) | Daily development | DXR 1.1/1.2 API; SER accepted but does not reorder; HIP SDK for Windows supports gfx1201; no CUDA/OptiX |
| Laptop | NVIDIA GeForce RTX 3070 Ti Laptop GPU (Ampere GA104, compute capability 8.6, 8 GB) | CUDA/OptiX builds, comparisons, NVIDIA references | SER does not reorder on RTX 30 (only RTX 40+); no hardware OMM; 8 GB VRAM is a budget to watch. Laptop: hybrid graphics (EV's `CreateAdapter` picks it by most VRAM, confirmed by the M0 `RenderCaps` line), timings depend on power mode → profile plugged in, same power mode every run. Driver reported SM 6.8 on 2026-11-23 (update before SM 6.9 shaders); Debug crash at swapchain creation (#989 from `dxgi.dll`, build log M0 step 3) |
| CI | WARP (no GPU) | Build + smoke test | Path tracing stays off; raster path must keep working |

Consequence: everything traced and everything in HLSL runs on both PCs. GPU compute is written once and built twice
(HIP for AMD, CUDA for NVIDIA). Vendor-only extras (OptiX, NVIDIA NRC SDK, tiny-cuda-nn, DLSS-RR; FSR Ray Regeneration)
are optional add-ons, never required.

## Decisions (all made 2026-11-18)

| # | Topic | Decision |
|---|---|---|
| D1 | Where it lives | Engine-core module (`EV-Engine/header/raytracing/`, `source/raytracing/`) owning acceleration structures, path tracer passes, accumulation, denoiser interface and the editor panel. Apps register *geometry providers* (OceanRenderer: displaced ocean vertex buffer, index buffer, material ID, cascade textures) and lights. |
| D2 | How rays are traced | DXR 1.1 **inline RayQuery in a compute pass** first. Path loop, BSDFs and spectral code in tracing-API-agnostic `.hlsli` files so a DXR pipeline (DispatchRays, later SER) can reuse them. |
| D3 | D3D12 runtime | **Agility SDK retail** (≥ 1.619, SM 6.9 / DXR 1.2) + a newer DXC shipped next to the exe. Preview SDKs only on experiment branches. |
| D4 | Descriptors | **Staged fixed-size descriptor table** under a global root signature for M1–M7; a **persistent bindless heap** (SM 6.6 `ResourceDescriptorHeap[]`) as its own milestone (M8a) before real-time mode and NRC. |
| D5 | Render graph + GPU compute | New **`.External()` pass**: the graph submits the work so far, signals a shared fence, runs the HIP/CUDA callback, waits on the GPU and continues on a new command list. Reused for denoising and NRC training. |
| D6 | Ocean in the BVH | Compute pass writes the **displaced 513×513 grid** to a buffer; BLAS built once with `ALLOW_UPDATE`, **refit every frame**, full rebuild every N frames. With frozen time: build once with `PREFER_FAST_TRACE`. Normals/foam stay in textures (hit shader samples them). |
| D7 | Time and accumulation | Reference mode **auto-freezes** ocean time and the animated point light (manual override). A **scene-version counter** resets accumulation on camera, gizmo, hierarchy, material, environment, parameter and shader-reload changes and on viewport resize. |
| D8 | Modes | **Both**, sharing material and light code: **reference (progressive accumulation) first**, real-time mode later. |
| D9 | Wavelengths | **4 wavelengths in a `float4`**, 360–830 nm, pbrt-v4 `SampleVisible` distribution, one new hero per camera ray. Secondary wavelengths terminated on dispersion. |
| D10 | RGB → spectrum | **Jakob–Hanika** (rgb2spec), **runtime lookup table first**, precomputed coefficient textures later (a first real GPU-compute kernel). Behind **one swappable HLSL interface** so **Peters' Fourier method** can be added later and compared (A/B toggle). Film stays RGB/XYZ. |
| D11 | Materials | Port EV's metal-roughness **GGX + Lambert** to a shared BSDF (VNDF sampling) + a dedicated **spectral water** material: dielectric Fresnel with n(λ) (Quan & Fry), rough GGX BTDF, absorption (Pope & Fry), scattering (Morel), foam layer, optional dispersion. |
| D12 | Denoisers | `IDenoiser` interface. **OIDN first** (HIP on AMD, CUDA on NVIDIA, CPU fallback), **OptiX AOV second** (NVIDIA only, compare), accumulate-only fallback everywhere (CI). Real-time mode later: NRD or own SVGF; DLSS-RR / FSR Ray Regeneration optional extras. |
| D13 | Shader compilation | **Runtime DXC** for the engine's path tracer shaders (engine shader folder registered with `ShaderLibrary`), so they hot reload like app shaders. |
| D14 | GPU compute | **One CUDA-style source** with a small portability header, built as **`EV-Hip.dll`** (hipcc, AMD) and **`EV-Cuda.dll`** (nvcc, NVIDIA). EV loads the DLL that matches the D3D12 adapter (vendor ID + LUID). First task: prove D3D12 ↔ HIP buffer + fence sharing on Windows. |
| D15 | NRC route | **SHaRC** (plain HLSL, both PCs) → **own NRC** from the D14 source: CPU MLP → naive HIP kernels → offline fit of dumped records → online NRC in EV → fast kernels (fp16, fused, WMMA). NVIDIA's **NRC SDK and tiny-cuda-nn only as references** on the RTX 3070. Optional later port to HLSL `dx::linalg` (SM 6.10). |
| D16 | Fallbacks | **Capability-gated**: path tracing wherever `RaytracingTier ≥ 1.1`; HIP/CUDA DLL only when it loads and matches the adapter; otherwise accumulate-only. WARP CI keeps the raster path, the panel says "unsupported". Found later: the WARP NuGet package (1.0.8+) supports DXR 1.1, so a CI path tracer smoke test is possible as an optional extra. |
| D17 | Lights and sky | Current **HDR sky map with importance sampling** + a **separate sun disk** (NEE + MIS) + point lights. Spectral physical sky (Hosek–Wilkie / Bruneton) later. |
| D18 | Branch, docs | Branch `path-tracing` from `scene-save-load` (scene files make reference shots repeatable). Docs in `docs/path-tracing/`, separate build log. |

## Roadmap

Every milestone ends in something visible and checkable. M0–M5 need no HIP/CUDA and run on both PCs.

| M | Build | Verify |
|---|---|---|
| **M0** Capabilities & plumbing | Agility SDK 1.620.x: exports in **each app's `main.cpp`** (the engine is a static lib, exports there would be dropped) + `D3D12\` runtime folder next to the exe; base device → `As<ID3D12Device13>` (inherits Device5, no separate query); query `OPTIONS5` (raytracing tier), shader model, binding tier, adapter LUID + vendor ID into a `RenderCaps` struct; path-tracing minimum (decided 2026-11-23) **RT tier 1.1, SM 6.6, binding tier 3** in `RenderCaps::GetPathTracingUnsupportedReason` (SM 6.9 extras optional, checked where used); `ID3D12GraphicsCommandList4`; DRED names for DXR ops; re-enable the frame counter; "Path Tracing" editor panel (disabled when unsupported) | CI (WARP) green with panel "unsupported"; both PCs log caps; Agility runtime confirmed loaded; debug layer silent |
| **M1** Acceleration structures | AS buffer creation path (RT-AS state, never transitioned); BLAS per `Mesh`; TLAS per frame from a scene visitor; render-graph access kinds for AS build/read (UAV barriers); scratch buffers; upload path > 2 MB | PIX / Radeon Raytracing Analyzer (AMD) / Nsight (NVIDIA) show correct BLAS/TLAS; build pass + timing in Render Graph panel |
| M1b (extra; decided 2026-11-23: after M1, before M2) Hand-built SAH BVH | On the CPU in plain C++: a small binned-SAH BVH builder + refit, following Jacco Bikker's "How to build a BVH" series. Learning what DXR builds in hardware; EV keeps DXR for rendering | Tree quality (SAH cost) close to TinyBVH's `SAHCost()` on Sponza / helmet / ocean grid; SAH cost of the displaced ocean after 1…N refits vs a rebuild (data for D6's rebuild interval N) |
| **M2** RayQuery debug views + ocean BLAS | "Ocean Displace Vertices" compute pass; ocean BLAS refit + periodic rebuild; compute pass writing primary-hit depth/normal/instance/barycentrics; freeze-time toggle; make the ocean reproducible (ocean time is a function-local static, the spectrum seed comes from `std::random_device`, `ResizeViewport` resets the FOV to 45°); fix the copy-queue `GenerateH0` sync; fix the ocean object-vs-world uv mismatch | Traced depth − raster depth ≈ 0; wave silhouettes match at frozen time; refit/build timings on both GPUs |
| **M3** RGB reference path tracer | Camera rays; RGBA32F accumulation + reset (scene-version counter); GPU material struct + staged texture table; Lambert + GGX (VNDF); NEE to sun disk + point lights; env-map importance sampling; MIS; Russian roulette; sampler = **counter-based, replayable RNG** keyed by pixel / frame / bounce / dimension (G5); reuse Tonemap | White furnace test; noise ∝ 1/√spp; helmet vs raster; simple scene vs Mitsuba 3 / pbrt-v4 |
| M3-ref (next to M3) CPU reference bench | (a) CPU test bench for the BSDF/pdf maths (energy conservation, pdf integrates to 1, samples follow the pdf, reciprocity); (b) a tiny CPU path tracer on a pinned TinyBVH copy (MIT, `/arch:AVX2`) rendering small test images, also in CI without a GPU. Mitsuba 3 / pbrt-v4 stay the independent reference | All BSDF tests pass before the GPU uses them; CPU and GPU images of the test scenes agree within noise (FLIP / MSE) |
| **M4** Spectral (HWSS) | `SampledWavelengths` (float4), `SampleVisible`, Jakob–Hanika table lookup behind the swappable RGB→spectrum interface, D65 illuminant, CMFs, XYZ → linear sRGB; RGB/spectral toggle | Grey/white round-trip neutral; colour chart ≈ RGB mode; overhead measured; compare to Mitsuba 3 `scalar_spectral` |
| **M5** Spectral water | Water dielectric with n(λ), rough BTDF, absorption + scattering medium (hero-wavelength distance sampling + spectral MIS), foam layer, optional dispersion, optional seafloor | 10 m slab ≈ 94/58/11/1 % at 440/550/600/680 nm; TIR ≈ 48.6°; RGB vs spectral deep-water A/B; tune the raster SSS fake against it |
| M5b (optional, any time after M4) | Peters' Fourier method as a second RGB→spectrum implementation | A/B vs Jakob–Hanika on the same frozen shot (colour difference, noise, cost) |
| **M5c** Software coherence (wavefront) | A second path tracer mode next to the single compute "megakernel": **wavefront** passes per bounce – trace (write hit records: position, material ID, path state) → **sort/bin by material** (counting sort / prefix sum in compute) → shade → next bounce. Toggle megakernel / wavefront-unsorted / wavefront-sorted in the panel. Teaches the divergence problem that SER solves in hardware, measurable on both PCs | **Profile and compare** all three modes on both GPUs, with **different numbers of materials** (a test scene with 1, 4, 16 and 64 distinct materials, plus the ocean shot): per-pass GPU time, total frame time, rays/s, wave occupancy and divergence in RGP (AMD) / Nsight (3070), record memory cost. Find the break-even point where sorting pays off |
| **M6** GPU compute bring-up | Choose the AMD toolchain (open choice, see `knowledge.md` §7: legacy HIP SDK 7.2 with VS plugin + ROCgdb, or ROCm Core SDK 10.1); `EV-Hip.dll` project (kernels built with `hipcc --genco` into a fatbin, loaded with `hipModuleLoadData`) + portability header; **spike: D3D12 → HIP → D3D12 buffer + shared fence round trip**; `CommandQueue::Signal/Wait(fence, value)`; render-graph `.External()` pass; same source built as `EV-Cuda.dll` on the RTX 3070; first kernels (image difference metric, Jakob–Hanika coefficient baker) | Bit-exact round trip; timeline in RGP/Nsight Systems without CPU stalls; resize/teardown ×100 without leaks; identical results HIP vs CUDA |
| **M7** Denoising | `IDenoiser`; pack/unpack passes (texture ↔ linear buffers); **OIDN** (HIP on AMD, CUDA on NVIDIA) with albedo + normal guides (RGB albedo, not per-wavelength); **OptiX AOV** on NVIDIA; denoise every N frames or when the camera stops; GI harness steps 1–3 (EXR dump, two-seed reference mode, `metrics.py`, G3) | FLIP/MSE vs ~16k-spp ground truth at 1/4/16/64 spp; timings after warm-up; OIDN vs OptiX on the 3070; WARP accumulate-only |
| **O1** Optimize the reference renderer | Dedicated optimization phase over everything up to M7 (strategy list below): profile → pick the biggest cost → change one thing → re-measure → keep or revert. Goal: highest quality per second (error vs ground truth at equal time), not just higher spp | Before/after table per optimization in `performance.md`, both GPUs; no image regression (FLIP vs reference) |
| **M8a** Persistent bindless heap | One shader-visible heap (static + per-frame ring), frame-based deferred release, SM 6.6 `ResourceDescriptorHeap[]` | All raster passes pixel-identical (CI screenshot diff); no descriptor leaks |
| **M8b** Real-time mode | G-buffer primaries + motion vectors (incl. ocean); **reprojection texture** right after the motion vectors (added 2026-11-22: one compute pass per frame writing previous-frame uv, a 4-bit bilinear-tap validity mask and an accuracy value; every temporal pass — R2, denoiser, M8d, M8e, TAA — reads it instead of doing its own disocclusion test, so history decisions are consistent and the GI comparison stays fair); 1–2 bounces at 1 spp, temporal denoiser (NRD or own SVGF), firefly clamp, ocean time running; **R2 temporal ReSTIR DI** (moved here from M11, G2) | Frame budget on both GPUs; ghosting on crests; error vs frozen reference |
| **M8c** GI comparison harness | See "GI techniques track". EXR dumps, reference mode (two seeds), Python metrics, `.evscene` v2 (lights, camera paths, events, seed), `--benchmark` CLI, temporal + bias scripts, test scenes + loader fixes | Same cell twice → same metrics; PT relMSE slope −1 vs spp; one full report from one `run_matrix.py` call per PC |
| M8d RC screen-space (SPWI) | Radiance cascades with screen probes + ray-traced world intervals as hybrid diffuse GI (see GI track) | Equal-time vs M8b's 1-spp bounce + denoiser, M8c rows |
| M8e ReSTIR GI (R3) | Reuse of secondary-bounce samples (see GI track) | Equal-time vs 1–2 bounce PT; lag after sun jump, M8c rows |
| **M9** SHaRC | Update (~4 % of pixels) / resolve / query; spectral → RGB at the cache boundary; behind the shared `RadianceCache` interface (GI track) | Cache debug view; M8c row set (static, path, event shots), terminator + hybrid mode, both PCs |
| M9b DDGI probes | Probe volume (see GI track) | M8c rows; leak box |
| **M9c** Surfel GI (GIBS-lite) | Surfel cache (see GI track) | M8c rows; leak box vs DDGI |
| M9r Split radiance cascades | Sparse world-space RC (see GI track) | M8c rows; penumbra/AO vs DDGI |
| **M9x** Cache bake-off | All caches and GI techniques built so far, same scenes, terminator + hybrid, RGB vs spectral storage, both GPUs | One comparison report; becomes the baseline M10 must beat |
| **M10** Own NRC | (a) CPU MLP fitting an image (backprop + Adam by hand); (b) same in HIP/CUDA; (c) offline fit of radiance records dumped from the tracer; (d) online NRC in EV via `.External()` (query + self-training records); (e) speed: fp16, fused layers, WMMA (RDNA 4) / Tensor Cores (3070); (f) compare against NVIDIA NRC SDK + tiny-cuda-nn on the 3070; (g) optional `dx::linalg` port | Loss curves; cache converges in ~8 frames; query+train ms; M8c row set vs every M9x technique and plain path tracing |
| **O2** Optimize the real-time renderer | Second optimization phase over real-time mode, SHaRC and NRC: frame budget per pass (e.g. 16.6 ms / 33 ms targets at 1080p per GPU), async compute for the BLAS refit / NRC training, resolution and spp scaling, denoiser cost, NRC kernel speed | Frame-time budget table per GPU in `performance.md`; before/after per change |
| M11 (optional) | DXR pipeline (+ SER via `HitObject::FromRayQuery` + `MaybeReorderThread` in raygen, only reorders on RTX 40+; compare with M5c's software sorting); ReSTIR PT (R4/R5, GI track); spectral sky; cluster-template ocean; caustics (photon pass / MNEE) | Speed-up per GPU; no regression |

## GI techniques track (added 2026-11-22)

Goal: build several real-time global illumination (GI) techniques yourself, understand how each works, and compare
them **fairly** on performance and visual fidelity. Research notes (all 2026-11-22, with sources):

| Note | Covers |
|---|---|
| `research/followup_gi_world_caches.md` | Surfels (EA SEED GIBS), kajiya irradiance cache, DDGI / RTXGI probes, AMD GI-1.0/1.1 (Capsaicin), Lumen (reference only), spectral storage in caches |
| `research/followup_restir.md` | RIS → ReSTIR DI → ReSTIR GI → ReSTIR PT (GRIS, Enhanced 2026), spectral ReSTIR with 4 hero wavelengths, ladder R0–R5 |
| `research/followup_radiance_cascades.md` | Radiance cascades (Sannikov / PoE2), bilinear fix, 3D variants (screen-space SPWI, Split RC 2026, dense grids), "Spatium" in Black Engine, ladder RC-0 → M8d → M9r |
| `research/followup_gi_comparison_method.md` | Metrics (relMSE, HDR-FLIP, tSMAPE flicker, lag90, bias split), equal-time protocol, test scenes + licences, EV loader gaps, the M8c harness |

**Choices G1–G6, made by the developer.** G1, G2, G3 and G5 were decided on 2026-11-22. G4 and G6 still use the
proposed default and can be changed.

| # | Choice | Options | Decision |
|---|---|---|---|
| G1 | Which techniques are core, which optional | DDGI, surfels, SPWI RC, Split RC, ReSTIR GI, kajiya-style cache, GI-1.0 hash grid, dense RC grid, ReSTIR PT | **Decided 2026-11-22.** Core: DDGI (M9b), radiance cascades (M8d SPWI + M9r Split RC), ReSTIR GI (M8e), surfel GI (M9c). Optional: kajiya-style cache (M9d), GI-1.0 grid (M9e), dense RC grid (M9r-b), ReSTIR PT (R4/R5) |
| G2 | Where temporal ReSTIR DI goes | M8b (real-time mode without good direct-light sampling is the plan's weak spot) or stay in M11 | **Decided 2026-11-22: M8b** (as R2) |
| G3 | When the harness is built | All in M8c, or steps 1–3 (EXR dump, reference mode, metrics script) already in M7, where "FLIP/MSE vs 16k-spp" is needed anyway | **Decided 2026-11-22: steps 1–3 in M7**, the rest in M8c |
| G4 | Order of the caches | DDGI before M8b (terminator-only, needs no G-buffer) or after SHaRC | Open. Default: after M9 SHaRC |
| G5 | Random numbers | A counter-based, replayable RNG (keyed by pixel / frame / bounce / dimension); ReSTIR PT random replay needs it, and it makes runs reproducible | **Decided 2026-11-22: yes, from M3** |
| G6 | Test scene support | Fix EV's loader gaps (no lights or cameras from files, `.evscene` has no lights, no glTF alpha mode/cutoff or emissive strength, `.glb` embedded textures fail, no EXR) inside M8c, or as its own milestone before M8c | Open. Default: inside M8c, step 7 |

### Shared groundwork (built once, used by every technique)
- **One HLSL interface** for caches: `RadianceCache_Query(x, n, ω, λ4)` + `RadianceCache_Update(...)`, and a panel toggle
  `None / SHaRC / DDGI / Surfels / Split RC / NRC`. Every place a path ends calls the same function, so techniques swap
  without touching the path loop.
- **Two usage modes, measured separately**:
  1. **Terminator** (reference mode): paths stop after N = 1 or 2 bounces and read the cache. Metric: error vs the
     high-spp reference at equal time.
  2. **Hybrid** (M8b real-time mode): the first diffuse bounce comes from the GI technique, specular stays traced.
     Metric: frame time + FLIP + flicker + lag.
- **Spectral storage**: caches store **linear RGB/XYZ at the cache boundary** (as SHaRC in M9). Never average rgb2spec
  coefficients (they are non-linear). Optional original experiment: store Peters' Fourier moments (linear, so they
  average like RGB; needs M5b). ReSTIR has its own spectral rules (see `followup_restir.md` §5).
- **Debug views** per technique: cache-only view, probe/surfel/cascade visualisation, sample-count and variance heat
  maps, memory occupancy.
- **Test environments** (built by the developer; contents per `followup_gi_world_caches.md` §8 and
  `followup_gi_comparison_method.md` §5): leak box (closed room, one door, sunlit outside, 5/10/20 cm walls), lag test
  (light switching on/off, sliding coloured block), flicker test (static scene + camera), scale test (Sponza → Bistro),
  glossy test (rough metal floor), penumbra test (small light + occluder), many-light test (for ReSTIR DI). Public
  scenes: Cornell, RTXGI furnace + two-rooms, Crytek Sponza, Bistro interior/exterior + Kitchen (RTXPT glTF), Intel
  Sponza + candles (BC7 textures for the 8 GB 3070), Zero Day, San Miguel (stress).

### Milestones of the track (in proposed order)

| M | Build | Verify / measure |
|---|---|---|
| RC-0 (decided 2026-11-23: after M0, before M1) 2D radiance cascades toy | Flatland RC in compute on a paintable 1024² canvas: c0 every 2 px with 4 directions, 6 cascades, ping-pong RGBA16F; toggles for branching factor, pre-averaging, bilinear fix, interval overlap. 3–5 days | Same time for 2, 100, 1,000 lights; ringing around a 2-px light without the fix, gone with it; vs brute-force 1,024-ray raymarch |
| R0 (side track after M3) RIS-only direct light | Streaming RIS over M light candidates, one shadow ray, no reuse | Converges to the NEE+MIS image (unbiased check); equal-time FLIP/MSE; cost per candidate count |
| R1 (side track after M4) Spatial reuse | Reservoirs + spatial merge (3–5 neighbours, 30 px), 1/M → pairwise MIS, biased/unbiased toggle; RGB/spectral toggle | Equal-time error vs R0; bias map (mean of 1k frames − reference); cost per pass |
| M7 (part, G3) Harness steps 1–3 | EXR writer (tinyexr) + `--dump`; reference mode with two-seed noise-floor check; `metrics.py` (relMSE, MRSE, MAPE, HDR-FLIP) | Noise floor ≥ 10× below the smallest relMSE; PT relMSE ∝ 1/spp |
| M8b + R2 (G2) Temporal ReSTIR DI | Temporal reuse with motion vectors, confidence cap 20, visibility reuse | Equal-time error during a 20-frame fly-through; flicker; disocclusion recovery frames; many-light scaling |
| **M8c** GI comparison harness | `.evscene` v2 (lights, camera path with fixed dt, events, seed); `--benchmark` CLI (warm-up, per-pass CSV p50/p95/p99, VRAM, JSON run manifest); `temporal.py` + `bias.py` (tSMAPE, lag90/99, ghosting, K = 32 runs → rBias²/rVar); `run_matrix.py` + `report.py` (tables, log–log error-vs-time, Pareto plot GI-ms vs error, crop grids); test scenes + loader fixes (G6); first matrix run with PT + OIDN and PT + real-time denoiser as the baseline | Same cell twice → same metrics; reference shows ~0 light in the leak box's dark room; one full report from one call on each PC |
| M8d SPWI radiance cascades (hybrid GI) | c0 probes every 4 px on the depth buffer, 16 octahedral directions, 5–6 cascades, intervals `0.016·4^i` m traced with RayQuery TMin/TMax, hits shaded with the path tracer's direct-light code; depth-aware merge (bilateral → bilinear 3D); cosine integration + upsample; optional bilinear fix and half-res probes. 2–4 weeks. Reference: ClaikoCode/DX12RadianceCascades (MIT, DXR) | ms per pass + rays/frame; FLIP vs path-traced 1-bounce diffuse; flicker on a slow pan; ringing near small lights; equal time vs M8b |
| M8e ReSTIR GI (R3) | Store secondary-bounce sample points, reconnection Jacobian, validation every N frames; biased (Lambertian L_o) → unbiased (full re-evaluation); later the GI sample's radiance comes from SHaRC / NRC | Equal-time vs 1–2 bounce PT and vs PT + SHaRC; lag after a sun jump; memory (~0.3–0.5 GB at 1080p); cost of initial / reuse / validation passes |
| M9 SHaRC | As in the roadmap, behind the shared interface; experiment: short rays (e.g. 20 m) with cache fallback | M8c rows, terminator + hybrid |
| M9b DDGI probe volume | (1) fixed grid (e.g. 32×8×32), all probes every frame, 8×8 irradiance + 16×16 distance/distance² octahedral atlases, trace with RayQuery, blend + border-copy shaders; (2) Chebyshev visibility + self-shadow bias + backface rule; (3) probe relocation + sleeping states; (4) scrolling camera volume + 2–3 cascades; (5) terminator mode. 2–4 weeks | Leak box vs reference; timings vs the 2019 table (3070 ≈ 2080 Ti class); sleeping saves 30–50 % |
| M9r Split radiance cascades | Per the 2026 paper (Algorithms 1–3): per-cascade hash maps (64-bit key, LOD), probe insertion from pixels, ray generation (count → prefix sum → offsets), **one ray per pixel split into every cascade it crosses** (quantised integer atomics), top-down merge with pre-averaging, 6×6 octahedral irradiance per c0 probe, sparse trilinear shading, temporal blend, secondary cache (multi-bounce + terminator). 5–8 weeks. Reference: the paper + entropylost/chandra (read only) | The paper's per-stage table on both GPUs (Sponza + foliage); VRAM fp32 vs fp16; single-frame and 10-frame FLIP; penumbra test; AO near walls vs DDGI; leak box; "Split RC as denoiser" vs OIDN |
| **M9c** Surfel GI, GIBS-lite | (1) spawn in 16×16 tiles + camera-relative grid, naive apply, fixed rays + EMA; (2) multi-scale mean estimator + variance-driven ray counts + global ray budget; (3) 4×4 radial depth + Chebyshev; (4) surfels follow moving instances; (5) recycling + free-ID stack; (6) non-linear grid; (7) optional ray guiding, DDGI fallback. Needs the M8b G-buffer. 4–8 weeks. Reference: W298/SurfelGI (MIT) | No holes K frames after a camera cut; stable surfel count; leak box vs DDGI; VRAM within 8 GB |
| M9r-b (optional) Dense RC cascades on the DDGI grid | 2–3 finer cascades under the DDGI spacing, trilinear fix toggle. 2–3 weeks after M9b | VRAM per added cascade; penumbra/AO gain vs plain DDGI |
| M9d (optional) kajiya-style clipmap cache + ReSTIR GI gather | 12 × 32³ clipmap, entry pool, rank voting, octahedral reservoirs, L1 SH; half-res ReSTIR GI that reads the cache only off screen. 4–6 weeks | M8c rows vs M8e + SHaRC |
| M9e (optional, small) GI-1.0 hash-grid variations | Direction-keyed cells, short-ray bit, tile mips, integer atomics on the M9 grid. ~1 week | A/B vs SHaRC on leak box + glossy test |
| **M9x** Cache bake-off | Every technique built so far, same scenes and camera paths, terminator + hybrid, RGB vs spectral storage, both GPUs. RC-specific extras: single-frame error, lag (should be 0), flicker under motion, penumbra sharpness vs occluder distance | One report (tables, convergence + Pareto plots, crops); the baseline M10 must beat |
| R4/R5 (optional, M11) ReSTIR PT | Whole-path reservoirs, reconnection shift → hybrid shift (random replay, needs G5), pairwise MIS, ReSTIR PT Enhanced tricks | Glossy/refractive scenes vs R3; replay the Enhanced paper's per-step ablation on both GPUs |

Proposed order: RC-0 after M0, before M1 (decided 2026-11-23) · R0/R1 next to M3/M4 · M7 (+ harness 1–3) · M8a · M8b (+ R2) · **M8c** · M8d · M8e ·
M9 · M9b · M9r · M9c · (optional M9r-b, M9d, M9e) · **M9x** · M10 · O2 · M11 (+ R4/R5).

Expectations from the research (published numbers, other GPUs; EV's numbers will be new — no RX 9070 XT or RTX 3070
results exist for any of these):
- DDGI 2.6 ms for 8k probes (RTX 2080 Ti); GIBS 2.5–3.2 ms (Xbox Series X, 1440p); kajiya cache ~0.55 ms (RX 6800 XT);
  GI-1.0 1.9–3.4 ms (RX 6900 XT / RTX 3080, 1080p); SPWI RC 2.36–8.66 ms (RTX 3080, DXR, 1080p); Split RC 8.6–11.5 ms
  (RTX 3080 Laptop, unoptimised); ReSTIR GI +8–18 ms (RTX 3090), ReSTIR PT Enhanced ~13–16 ms (RTX 5880 Ada);
  NRC query 1.59 + train 1.11 ms (RTX 3090).
- EV has few lights (sun, sky, some point lights), so ReSTIR DI gains less than in the papers. The many-light test scene
  is needed to see it.

"Spatium" radiance cascades (Black Engine, YouTube channel Blackline Interactive): a world-probe-based 3D RC with 7
cascades and 4–5 bounces. The claim is Bistro at 7.4–9.9 ms on a Radeon Pro 5500M laptop GPU without RT hardware. There
is no paper, write-up or public code, so it is not a milestone. It is kept as an inspiration and a sanity check for
M9r's numbers (`followup_radiance_cascades.md` §6).

## Profiling and performance (every milestone)

Performance is measured all the way through, not only at the end. Results go in `performance.md`.

- **Benchmark setup first** (part of M1–M3): fixed benchmark shots saved as `.evscene` files (camera, models,
  environment), ocean with **frozen time and a fixed seed** (M2), fixed resolution (1080p, and 1440p), fixed spp and
  bounce counts, Release build, same driver noted per run. A `--benchmark <shot>` command-line mode that runs N frames
  and writes per-pass GPU times (EV's `GpuTimer` / Render Graph panel) to a CSV makes every later measurement a
  one-command job.
- **When to profile**:
  1. **After every milestone** – a full run on **both GPUs**, one row set in `performance.md` (baseline for the next).
  2. **Before and after every change to a hot path** (path loop, BSDFs, BVH flags, sorting, denoiser, NRC kernels) – a
     quick A/B on the main PC, so regressions are caught the day they happen, not one milestone later.
  3. **Deep captures when something looks off**: RGP / Radeon Raytracing Analyzer on the RX 9070 XT, PIX on both,
     Nsight Graphics / Systems / Compute on the RTX 3070.
- **What to record**: per-pass GPU time, frame time, rays per second, samples per second, BVH build/refit times and
  sizes, VRAM use, and **quality per time** (FLIP or MSE against a high-spp ground truth after a fixed number of
  seconds). The last one matters most for a path tracer: a change that is slower per sample but converges faster wins.
- **GI comparisons** (from M8c on, details in `research/followup_gi_comparison_method.md`): GI cost = the sum of the
  GI passes, reported as p50/p95/p99; equal-time GI budgets of 2 / 4 / 8 ms at 1080p plus "time to equal relMSE";
  relMSE (ε = 0.01) + HDR-FLIP; bias split rBias² / rVar over K = 32 seeds; temporal tSMAPE (flicker) and lag90 (frames
  until 90 % of the converged value after an event).
- **Optimization strategy list** (worked through in O1/O2, also used ad hoc; details in `knowledge.md` §10):
  BVH (build flags, compaction, refit vs rebuild interval, splitting the ocean into tiles, instance culling) ·
  rays (ray flags such as accept-first-hit and skip-closest-hit for shadow rays, tight TMin/TMax, culling, fewer
  shadow rays) · shading (divergence and branch structure, register pressure and occupancy, fp16 math, wave intrinsics,
  material sorting from M5c) · memory (packed path/hit records, texture formats, bandwidth, cache-friendly layouts) ·
  sampling efficiency (better importance sampling, MIS weights, Russian roulette tuning, blue-noise / low-discrepancy
  samplers – fewer samples for the same quality) · spectral cost (lookup table vs analytic CMFs, coefficient textures
  vs runtime table) · frame structure (async compute for refit/training, denoise every N frames, resolution scaling)
  · CPU side (command recording, TLAS instance updates).

Cross-cutting risks: TDR (keep per-dispatch work small, progressive); the debug layer breaks on warnings (keep DXR
validation clean); refit quality (log refit vs rebuild timings, tune N from data); 8 GB VRAM on the 3070.

Ideas looked at but not chosen (2026-11-18): M5d – own BVH traversal in an HLSL compute shader compared with
hardware RayQuery (TinyBVH's `external/dx12rt` benchmark does this on Sponza); can still be added later.

Not part of this work, still open from the editor branches: the IBL bake fix (see `docs/editor-handoff.md`, "TODO: sky
lighting streaks and blocks"). It makes a cleaner raster reference for the path tracer, so it is worth doing first or
early. **Planned (2026-11-22): Claude implements this fix itself (exception to "the developer writes the code", at the
developer's request) right after the developer's #989 task is finished and committed.**
**Done (2026-11-23, Claude):** the two shader edits from `Dads-Birthday` `11bf7d9` are on this branch (both compile with
DXC cs_6_0) and the developer checked the result visually (see build-log.md, 2026-11-23).
