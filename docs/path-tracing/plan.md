# Path tracing in EV – plan and decisions

Branch: `path-tracing` (from `scene-save-load`). Started 2026-11-18.

Goal: a spectral path tracer (hero wavelength sampling) as a **toggleable core-engine feature** of EV, first used by
OceanRenderer, denoised with OIDN and the OptiX denoiser, using GPU compute (HIP on AMD, CUDA on NVIDIA) where it fits,
and growing into a self-written **Neural Radiance Cache** (NRC).

**Who writes the code:** from M0 on, all code is written by the developer (learning project). Claude acts as tutor: it
explains, points to files, reviews and helps debug, using `knowledge.md` as its reference.

## Documents

| File | What it is |
|---|---|
| `plan.md` | This file: hardware, the 18 decisions, the roadmap |
| `knowledge.md` | Tutor reference: theory, formulas, APIs, EV specifics, pitfalls, per-milestone guidance, sources |
| `build-log.md` | What was done in each step, with results and measurements |
| `research/00_report_2026-11-18.md` | The full research report (industry survey, theory primer, options per decision) |
| `research/*.md` | Raw research notes with sources (see `research/README.md`) |

The research report was written before two facts were known: development happens mainly on the **AMD** PC, and the
NVIDIA card is an **RTX 3070**. Where the report and this file disagree, **this file wins** (D12, D14, D15, D16).

## Hardware

| Machine | GPU | Role | Notes |
|---|---|---|---|
| Main dev PC | AMD Radeon RX 9070 XT (RDNA 4, gfx1201) | Daily development | DXR 1.1/1.2 API; SER accepted but does not reorder; HIP SDK for Windows supports gfx1201; no CUDA/OptiX |
| Second PC | NVIDIA RTX 3070 (Ampere, compute capability 8.6, 8 GB) | CUDA/OptiX builds, comparisons, NVIDIA references | SER does not reorder on RTX 30 (only RTX 40+); no hardware OMM; 8 GB VRAM is a budget to watch |
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
| **M0** Capabilities & plumbing | Agility SDK 1.620.x: exports in **each app's `main.cpp`** (the engine is a static lib, exports there would be dropped) + `D3D12\` runtime folder next to the exe; base device + `As<ID3D12Device5>`; query `OPTIONS5` (raytracing tier), shader model, binding tier, adapter LUID + vendor ID into a `RenderCaps` struct; `ID3D12GraphicsCommandList4`; DRED names for DXR ops; re-enable the frame counter; "Path Tracing" editor panel (disabled when unsupported) | CI (WARP) green with panel "unsupported"; both PCs log caps; Agility runtime confirmed loaded; debug layer silent |
| **M1** Acceleration structures | AS buffer creation path (RT-AS state, never transitioned); BLAS per `Mesh`; TLAS per frame from a scene visitor; render-graph access kinds for AS build/read (UAV barriers); scratch buffers; upload path > 2 MB | PIX / Radeon Raytracing Analyzer (AMD) / Nsight (NVIDIA) show correct BLAS/TLAS; build pass + timing in Render Graph panel |
| **M2** RayQuery debug views + ocean BLAS | "Ocean Displace Vertices" compute pass; ocean BLAS refit + periodic rebuild; compute pass writing primary-hit depth/normal/instance/barycentrics; freeze-time toggle; make the ocean reproducible (ocean time is a function-local static, the spectrum seed comes from `std::random_device`, `ResizeViewport` resets the FOV to 45°); fix the copy-queue `GenerateH0` sync; fix the ocean object-vs-world uv mismatch | Traced depth − raster depth ≈ 0; wave silhouettes match at frozen time; refit/build timings on both GPUs |
| **M3** RGB reference path tracer | Camera rays; RGBA32F accumulation + reset (scene-version counter); GPU material struct + staged texture table; Lambert + GGX (VNDF); NEE to sun disk + point lights; env-map importance sampling; MIS; Russian roulette; sampler; reuse Tonemap | White furnace test; noise ∝ 1/√spp; helmet vs raster; simple scene vs Mitsuba 3 / pbrt-v4 |
| **M4** Spectral (HWSS) | `SampledWavelengths` (float4), `SampleVisible`, Jakob–Hanika table lookup behind the swappable RGB→spectrum interface, D65 illuminant, CMFs, XYZ → linear sRGB; RGB/spectral toggle | Grey/white round-trip neutral; colour chart ≈ RGB mode; overhead measured; compare to Mitsuba 3 `scalar_spectral` |
| **M5** Spectral water | Water dielectric with n(λ), rough BTDF, absorption + scattering medium (hero-wavelength distance sampling + spectral MIS), foam layer, optional dispersion, optional seafloor | 10 m slab ≈ 94/58/11/1 % at 440/550/600/680 nm; TIR ≈ 48.6°; RGB vs spectral deep-water A/B; tune the raster SSS fake against it |
| M5b (optional, any time after M4) | Peters' Fourier method as a second RGB→spectrum implementation | A/B vs Jakob–Hanika on the same frozen shot (colour difference, noise, cost) |
| **M6** GPU compute bring-up | Choose the AMD toolchain (open choice, see `knowledge.md` §7: legacy HIP SDK 7.2 with VS plugin + ROCgdb, or ROCm Core SDK 10.1); `EV-Hip.dll` project (kernels built with `hipcc --genco` into a fatbin, loaded with `hipModuleLoadData`) + portability header; **spike: D3D12 → HIP → D3D12 buffer + shared fence round trip**; `CommandQueue::Signal/Wait(fence, value)`; render-graph `.External()` pass; same source built as `EV-Cuda.dll` on the RTX 3070; first kernels (image difference metric, Jakob–Hanika coefficient baker) | Bit-exact round trip; timeline in RGP/Nsight Systems without CPU stalls; resize/teardown ×100 without leaks; identical results HIP vs CUDA |
| **M7** Denoising | `IDenoiser`; pack/unpack passes (texture ↔ linear buffers); **OIDN** (HIP on AMD, CUDA on NVIDIA) with albedo + normal guides (RGB albedo, not per-wavelength); **OptiX AOV** on NVIDIA; denoise every N frames or when the camera stops | FLIP/MSE vs ~16k-spp ground truth at 1/4/16/64 spp; timings after warm-up; OIDN vs OptiX on the 3070; WARP accumulate-only |
| **M8a** Persistent bindless heap | One shader-visible heap (static + per-frame ring), frame-based deferred release, SM 6.6 `ResourceDescriptorHeap[]` | All raster passes pixel-identical (CI screenshot diff); no descriptor leaks |
| **M8b** Real-time mode | G-buffer primaries + motion vectors (incl. ocean), 1–2 bounces at 1 spp, temporal denoiser (NRD or own SVGF), firefly clamp, ocean time running | Frame budget on both GPUs; ghosting on crests; error vs frozen reference |
| **M9** SHaRC | Update (~4 % of pixels) / resolve / query; spectral → RGB at the cache boundary | Cache debug view; bias vs reference; performance both PCs |
| **M10** Own NRC | (a) CPU MLP fitting an image (backprop + Adam by hand); (b) same in HIP/CUDA; (c) offline fit of radiance records dumped from the tracer; (d) online NRC in EV via `.External()` (query + self-training records); (e) speed: fp16, fused layers, WMMA (RDNA 4) / Tensor Cores (3070); (f) compare against NVIDIA NRC SDK + tiny-cuda-nn on the 3070; (g) optional `dx::linalg` port | Loss curves; cache converges in ~8 frames; query+train ms; equal-time error vs SHaRC and plain path tracing |
| M11 (optional) | DXR pipeline (+ SER, only reorders on RTX 40+); ReSTIR DI; spectral sky; cluster-template ocean; caustics (photon pass / MNEE) | Speed-up per GPU; no regression |

Cross-cutting risks: TDR (keep per-dispatch work small, progressive); the debug layer breaks on warnings (keep DXR
validation clean); refit quality (log refit vs rebuild timings, tune N from data); 8 GB VRAM on the 3070.

Not part of this work, still open from the editor branches: the IBL bake fix (see `docs/editor-handoff.md`, "TODO: sky
lighting streaks and blocks"). It makes a cleaner raster reference for the path tracer, so it is worth doing first or
early.
