# World-space and surface-attached radiance caches for real-time GI (follow-up research, 2026-11-22)

Scope: the radiance caches that real-time renderers use to make diffuse (and some glossy) global illumination cheap:
EA SEED / Frostbite **surfel GI (GIBS)**, Embark's **kajiya** irradiance cache with its ReSTIR GI final gather,
**DDGI** probes (Majercik 2019, 2021, RTXGI-DDGI SDK), AMD **GI-1.0 / 1.1 / 1.2** (Capsaicin) and **Brixelizer GI**,
UE5 **Lumen** as an architecture reference, and the most relevant newer work (on-surface caches, HPG 2024; Split
Radiance Cascades, 2026). For each one: the idea in plain words, data structures and memory, per-frame passes, what
is stored, how it plugs into a path tracer, artefacts, published timings, open source, and how hard it is in HLSL
with DXR 1.1 inline RayQuery. It ends with how the caches would store light in EV's **spectral** (hero-wavelength)
renderer and with milestone options and a comparison table. SHaRC and NRC are covered in `neural_radiance_cache.md`
and `industry_realtime_path_tracing.md`. This note only compares against them.

Labels: [T] training knowledge, [I] inference, [E] estimate, [U] unverified. Facts without a label come from the
linked source, fetched or downloaded 2026-11-22. Slide decks were downloaded and converted to text locally
(`pdftotext`). Speaker notes in the 2021 and 2024 GIBS decks and the 2022 Lumen deck were read in full. No EV file
other than this note was changed.

## 0. Words used in this note (plain-language glossary)

These are [T] definitions, written for someone who has just met BVH/SAH/pdf.

- **Radiance** L(x, ω): light travelling through point x in direction ω. It is what a camera ray or a path vertex
  "sees". It depends on direction, so a radiance cache must store something per direction, or be view-dependent.
- **Irradiance** E(x, n): all the light arriving at a surface point with normal n, each direction weighted by cos θ.
  That is the hemispherical integral of radiance. A matte (Lambertian) surface reflects `albedo/π · E`. Irradiance
  has no view direction, which is why diffuse caches can store so little.
- **Cache**: store an expensive answer (here: incoming light) and reuse it for many pixels and frames, accepting some
  error (bias) and delay (lag).
- **Probe**: a point in empty space that records light from all directions around it, like a tiny 360° camera.
  Shading points in between probes interpolate them.
- **Surfel** ("surface element"): a small disc stuck to a surface (position, normal, radius) that records the light
  arriving at that patch of surface.
- **Octahedral map**: a way to store a function on the sphere in a small square texture. Fold the sphere onto an
  octahedron, then unfold that onto a square. DDGI, GIBS probes, kajiya and GI-1.0 all use it.
- **Spherical harmonics (SH)**: a few numbers per colour channel that describe a smooth function on the sphere, like
  a Fourier series on a sphere. L1 SH means 4 coefficients per channel.
- **Hysteresis / exponential moving average (EMA)**: `new = lerp(sample, old, h)`. With h = 0.97 the stored value
  moves only 3 % towards each new sample. High h means less noise but more lag.
- **Chebyshev visibility test**: store the mean distance μ and the mean squared distance (so the variance σ² =
  E[d²] − μ²) of what a probe or surfel sees in each direction. Chebyshev's inequality gives an upper bound on the
  chance that a point at distance d (d > μ) is visible: `σ² / (σ² + (d − μ)²)`. This is the variance-shadow-map trick.
  It turns "is this probe behind a wall?" into a smooth weight, which is what stops light leaking.
- **Hash grid**: a world-space grid that is never stored densely. Each cell's integer coordinates are hashed to an
  index in a fixed-size table, so only cells that are actually used take memory (SHaRC, GI-1.0).
- **Clipmap / cascade**: nested grids centred on the camera, each level twice as coarse and twice as large as the one
  inside it. Detail is high near the camera and coverage is wide far away. When the camera moves, the grids "scroll".
- **Path terminator**: in a path tracer, stop a path at bounce N and read "the rest of the light" from a cache, instead
  of tracing more bounces. SHaRC and NRC are used this way.
- **Hybrid renderer**: rasterise the primary hits (the G-buffer), add direct light with shadow maps or rays, and use
  the cache as the whole diffuse-GI term. DDGI, GIBS and Lumen are used this way.
- **ReSTIR / reservoir**: keep one "winning" light sample per pixel (or cache entry) plus a running weight. Reuse
  winners from neighbours and from the previous frame (resampled importance sampling). ReSTIR GI does this for
  indirect light samples.
- **Lag / ghosting**: the cache still shows old light after a change. **Leak**: light appears through a wall because
  the cache sample on the other side was used. **Flicker**: low-frequency temporal noise from a cache that never
  fully converges.

## 1. Surfel GI: EA SEED "GIBS" (PICA PICA 2018 → SIGGRAPH 2021 → shipping 2024)

### Takeaway
GIBS covers the visible scene with surfels spawned from the G-buffer wherever coverage is low. Each surfel follows its
object through a transform ID (one bone for skinned meshes), traces a few rays per frame, and integrates irradiance
with a variance-adaptive moving average, the **multi-scale mean estimator (MSME)**. A 4×4 radial depth/depth² map per
surfel hemisphere prevents leaks (the DDGI trick). Screen pixels gather the surfels in their grid cell. The grid is a
**uniform centre grid plus trapezoidal grids along each axis**, so cells grow with distance like the surfels do.
Transparents and missing coverage use a separate clipmap of probes.

Cost:
- 2021, PS5, worst case: ~6–7 ms.
- 2024, Xbox Series X, 1440p (*Skate*): **3.2 ms average, 2.5 ms after optimisations**, ≈100k live surfels and 200k
  rays/frame. *College Football 25* shipped it in 2024.

It is a hybrid diffuse-GI solution (opaque only), not a path terminator, though a path could query it [I].

### Cited Findings
**What it is and where it came from (2021 talk, Halén, Hayward, Brinck, Bei)**
- "GIBS is a solution for calculating indirect diffuse illumination in real-time. The solution combines hardware ray
  tracing with a discretization of scene geometry… It requires no pre-computation, no special meshes, and no special
  UV sets." The basic algorithm was written in 2018 for PICA PICA, "primarily by Tomasz Stachowiak", and later
  extended to skinned characters and large worlds — [SIGGRAPH Advances 2021 slides](https://www.advances.realtimerendering.com/s2021/SIGGRAPH%20Advances%202021%20-%20Surfel%20GI.pdf), [EA SEED page](https://www.ea.com/seed/news/siggraph21-global-illumination-surfels)
- A surfel "is defined by a position, a radius, and a normal". Surfels are persistent and resolution independent.
  "In contrast to probe grid solutions, we perform and cache ray tracing operations exactly where they are needed" —
  [2021 slides](https://www.advances.realtimerendering.com/s2021/SIGGRAPH%20Advances%202021%20-%20Surfel%20GI.pdf)

**Spawning, following geometry, recycling**
- Spawning: "The screen is split into 16x16 texel tiles, each tile finds the texel which has the least coverage
  currently. If this coverage passes a randomized threshold we spawn a new surfel using the geometric information from
  the GBuffer. Once a certain amount of coverage is achieved, no more surfels are spawned for that tile."
- Moving objects: surfels "track a unique transform identifier" written to the G-buffer. They store their position
  local to that transform and are re-positioned each frame from a "global transform buffer… including skinned bones".
  Skinned meshes write "the ID of the bone with the highest skinning weight", which is "essentially one bone skinning
  of surfels".
- Scale: "Surfels are scaled so that their projection in screen space is roughly constant." They shrink when you
  approach, so more spawn. When you back away they grow and extra ones are removed.
- Memory is fixed: "The surfel algorithm allocates all the resources required for surfel management up front." A GPU
  stack of free surfel IDs is popped and pushed with atomics. Recycling is random, weighted by "how many surfels are
  currently live", "when the surfel last contributed to the lighting of the scene" and "how far away the surfel is" —
  [2021 slides](https://www.advances.realtimerendering.com/s2021/SIGGRAPH%20Advances%202021%20-%20Surfel%20GI.pdf)

**Acceleration grid**
- PICA PICA used a uniform grid. Surfels insert themselves into their cell and the neighbour cells they overlap, and
  "the radius of a surfel is never larger than the side of a grid cell". Distant surfels are large, so a uniform grid
  "did not scale". The fix: "a uniform grid for a small area close to the camera, combined with a trapezoidal grid
  along each principal axis", where slice thickness grows with distance. "Since the trapezoids are essentially regular
  grids, just with a non-linear transformation, lookups and insertions are still very fast."
- Applying to the screen: "we reconstruct the world space position of each pixel, find which cell the position is
  inside, and then fetch the first N surfels in the cell", weighted "by their orientation and distance". If the summed
  weight is below one, "the weighted average irradiance of the grid cell" is added — [2021 slides](https://www.advances.realtimerendering.com/s2021/SIGGRAPH%20Advances%202021%20-%20Surfel%20GI.pdf)

**Irradiance integration**
- Rays from surfels hit any geometry in the TLAS, including skinned meshes. At hits the renderer evaluates direct light
  and shadowing, plus "the existing surfel lighting at those locations. This gives us effectively infinite bounce over
  time".
- MSME: "in addition to accumulating a longer term moving average, we also track a shorter term mean and a short term
  variance estimator", which "adjust the blend factor for the long term average".
- Ray count per surfel depends on variance, recent contribution to the image and age. Newly spawned surfels get more.
  Converged surfels go "almost dormant… just sending enough rays to detect that there is a change". A content-set
  global ray budget is enforced in two passes: a ray-count pass, then proportional allocation in the binning pass.
- Ray guiding: a per-surfel 6×6 hemisphere map, "8 bits per component, plus a single 16 bit scaling value". It is
  sampled by walking the discrete CDF (inverse-CDF sampling). It helps when light comes from directions that cosine
  sampling rarely picks.
- Irradiance sharing between neighbouring surfels in the same cell is used "when variance is high".
- Rays are binned by surfel cell and direction (like Battlefield V) to improve coherence — [2021 slides](https://www.advances.realtimerendering.com/s2021/SIGGRAPH%20Advances%202021%20-%20Surfel%20GI.pdf)
- The MSME code is public as `MultiscaleMeanEstimator.hlsl` from Ray Tracing Gems ch. 25 (PICA PICA). It tracks
  mean, short-window mean, variance, an "inconsistency" value and a variance-based blend reduction, with firefly
  suppression at `shortMean + 8σ` — [SurfelGI copy of MSME](https://github.com/W298/SurfelGI/blob/main/RenderPasses/Surfel/SurfelGI/MultiscaleMeanEstimator.slang), [RTG ch. 25 source](https://github.com/Apress/ray-tracing-gems/blob/master/Ch_25_Hybrid_Rendering_for_Real-Time_Ray_Tracing/MultiscaleMeanEstimator.hlsl)

**Leak prevention (radial depth)**
- Surfels don't know their neighbourhood, so "surfels outside of the wall, in a much brighter area, apply lighting
  through the geometry". Fix: a radial depth function initialised to the surfel diameter, storing "a moving average of
  depth and depth square" and tested with "Chebychev's inequality", "inspired by Variance Shadow Maps and DDGI". Only
  depth "within the surfel diameter, and… in the hemisphere" matters. The default is "just 4x4 texels per surfel
  hemisphere" — [2021 slides](https://www.advances.realtimerendering.com/s2021/SIGGRAPH%20Advances%202021%20-%20Surfel%20GI.pdf)

**Lights, transparency, timings (2021)**
- Many lights: stochastic lightcuts (2–8 node cut) vs reservoir sampling ("randomly sample four to eight lights, and
  choose one winner").
- Transparency: "we apply the surfel algorithm for light probes" in a 4-level camera-centred **clipmap**, with SH
  irradiance, the same MSME integrator and Sloan de-ringing. Probes scroll with the camera, and new ones are seeded from
  the coarser level. Level transitions are blended or blue-noise dithered.
- Setup: "PS5 with 4k resolution. Which means our screen-space passes run at 1080p", starting from an empty surfel
  scene (worst case).
  - PvZ town centre, sun only: "roughly 7ms total".
  - 1400-light stress test with a 1 M ray budget: "still pushing 11 milliseconds".
  - Filtering: "averaging around .2ms".
  - Probe sampling at 900p: "roughly .2ms".
- Limitations: procedural geometry "will continuously spawn and recycle surfels", and high-detail geometry can
  "over-spawn" — [2021 slides](https://www.advances.realtimerendering.com/s2021/SIGGRAPH%20Advances%202021%20-%20Surfel%20GI.pdf)

**Shipping version (2024, Diede Apers, Frostbite)**
- In 2021 "a total average gpu time for GIBS of 6-7ms". Now shipped in **College Football 25** ("fully relies on GIBS
  for all indirect diffuse lighting") and used by **Skate** (in development).
- Probes became **octahedral, "very similar to DDGI"**:
  - Up to 3 clips and about 100k probes, in 100 MB, so "about 1kb available per probe".
  - Variance depth 14×14 (incl. border) and irradiance 6×6 ("4x4 with 16 directions").
  - 100k rays/frame, so only ~6k probes are traced per frame, one clip level per frame, by priority. Hysteresis is
    driven by the MSME.
- Surfel fixes:
  - export the vertex-interpolated normal during G-buffer laydown, because noisy shading normals over-spawned surfels;
  - store the depth-reconstruction error per surfel for the ray offset, because the old offset leaked light out of a
    garage ceiling;
  - "rely on the probes to give us an initial estimate when surfel coverage is low".
- GPU work: "over 50 compute shaders each frame, and only 6 of those are issuing ray intersections". Surfel loops are
  scalarised for RDNA. Surfels are binned into the grid twice per frame. Screen tiles are classified into surfel-lit and
  probe-lit.
- Ray tracing:
  - "We moved our ray tracing calls to compute and use inline tracing much like dxr 1.1", with indirect arguments;
    BLAS/TLAS builds run async with the G-buffer;
  - only opaque, non-alpha-tested geometry is in the AS, and the albedo is simplified to "one color per mesh-material
    subset";
  - ray length 1000 m, optionally shortened to 20 m with a fallback to the probe clipmap: "biased… but we actually get a
    significant improvement to our convergence rate";
  - shadow maps replace shadow rays where available.
- Skate fly-through, **XBSX, 1440p, no upscaling**, max 200k rays/frame, ~100k live surfels, ~25k probes in 3 clipmaps:
  - misc 0.5 ms, apply 0.5 ms (quarter rate + upscale), probes 0.7 ms, surfels 1.64 ms;
  - "about 3.2ms on average", peak 4 ms; after the optimisations "2.5ms, without going over the 3ms mark".
- CFB25 freezes GIBS during 60 Hz gameplay (2 ms budget) and only applies the cached lighting.
- Ray binning "is no longer the case", but compaction still matters. ReSTIR-based surfel application was slower than
  the naive loop at half resolution.
- On XBSS "we fit within 500mb for the entirety of GIBS including all raytracing resources" — [SIGGRAPH Advances 2024, Apers](https://advances.realtimerendering.com/s2024/content/EA-GIBS2/Apers_Advances-s2024_Shipping-Dynamic-GI.pdf)

**Open-source implementations**
- **W298/SurfelGI** (MIT, Falcor, "based on GIBS"):
  - requires SM 6.5 and RT tier 1.1;
  - passes: prepare → collect/accumulate cell info → cell-to-surfel buffer → evaluation (per pixel) → ray trace →
    integrate → generation;
  - limits: `kTotalSurfelLimit = 150000`, `kRayBudget = 150000 * 64`, 16×16 tiles;
  - a camera-relative uniform grid, `cellUnit = 0.05`, `cellDim = 250` (250³ cells), `perCellSurfelLimit = 1024`;
  - 4–64 rays per surfel, `kMaxLife = 240`;
  - a 7×7 (5×5 + border) hemisphere-octahedral surfel depth atlas, MSME, irradiance sharing and optional ray guiding;
  - the `Surfel` struct holds position, normal, radius, radiance, MSME state and a ray offset/count.

  — [repo](https://github.com/W298/SurfelGI), [SurfelGI.h](https://github.com/W298/SurfelGI/blob/main/RenderPasses/Surfel/SurfelGI/SurfelGI.h), [SurfelTypes.slang](https://github.com/W298/SurfelGI/blob/main/RenderPasses/Surfel/SurfelGI/SurfelTypes.slang), [SurfelUtils.slang](https://github.com/W298/SurfelGI/blob/main/RenderPasses/Surfel/SurfelGI/SurfelUtils.slang)
- Secondary summary of surfel spawning ("lowest-coverage pixel… found with group atomic/wave operations"). It estimates
  "about 6-8 weeks to implement surfels, and 2-4 more weeks to build GI on top of them" — [teamwisp research page (secondary)](https://teamwisp.github.io/research/surfels.html)
- A 2022 Chalmers MSc thesis, "Diffuse Global Illumination using Surfels", exists. Only its table of contents was seen
  [U] — [thesis PDF](https://odr.chalmers.se/bitstreams/4195a05f-6ddd-4694-a7ba-58b06c9ecab5/download)

### Inferences
- [I] Per-frame order for an EV version:
  1. G-buffer (M8b) with an instance ID and the geometric normal;
  2. transform persistent surfels by instance transform;
  3. recycle (random heuristic, push free IDs);
  4. bin into the grid (count → prefix sum → scatter);
  5. ray-count request + budget allocation;
  6. trace (compute + RayQuery, indirect dispatch);
  7. integrate (MSME + radial depth update);
  8. apply to screen (half resolution + upsample);
  9. coverage evaluation + spawn in 16×16 tiles;
  10. bin again for the new surfels (GIBS 2024 bins twice).
- [I] Memory sketch per surfel (fp32 throughout):
  - position/normal/radius: 28 B;
  - instance ID + local position: 16 B;
  - irradiance + MSME state (mean, short mean, variance, 2 scalars): ~44 B;
  - 4×4 depth/depth² at fp16: 64 B;
  - ray guiding 6×6×8 bit + 16 bit: ~38 B.

  That is ≈190 B/surfel, so 150k surfels ≈ 28 MB [E]. The W298 dense 250³ grid (8 B `CellInfo` × 15.6 M cells
  ≈ 125 MB [E]) shows why GIBS moved to a non-linear grid. A hashed or trapezoidal grid is much smaller.
- [I] Surfels store **irradiance (no direction)**, so they are only valid for the diffuse lobe. As a path terminator in
  EV, a path ending on a surface with a surfel would return `albedo/π · E`, which is biased on glossy vertices.
- [I] RDNA-relevant lesson for the RX 9070 XT: GIBS's biggest compute win was **scalarised loads** in surfel loops.
  Loading per-cell surfel data through `WaveReadLaneFirst`-style uniform indices puts it on the scalar path [T].
- [I] The ocean is "procedural geometry" in GIBS terms. Its moving surface would keep respawning surfels. That is not a
  problem, since the ocean is no longer the target.

### Gaps
- No byte-exact GIBS surfel layout or grid dimensions (cell counts, trapezoid parameters) are published.
- No GIBS numbers on PC GPUs. All timings are PS5 or Xbox Series X.
- The original Stachowiak 2018 surfel talk / PICA PICA material was not re-read. The MSME details come from the RTG
  ch. 25 source (via the SurfelGI copy).

## 2. kajiya (Embark Studios, Tomasz Stachowiak): irradiance cache + ReSTIR GI

### Takeaway
kajiya is an archived, open-source Rust/Vulkan research renderer. Its diffuse GI is a **half-resolution ReSTIR GI
final gather** (one ray per 2×2 pixels, temporal + spatial reservoir reuse, temporal denoiser). Hits that are on screen
reuse last frame's irradiance. Hits off screen read the **irradiance cache ("ircache")**.

The ircache:
- is 12 camera-centred **32³ clipmap cascades** (cell 2 cm at cascade 0, doubling per cascade), with a pool of at most
  **65,536 entries** allocated on demand;
- has entries that each run their own small ReSTIR (4×4 octahedral reservoirs, 4 rays/frame + 4 validation rays) and
  store **L1 SH directional irradiance**;
- feeds back into itself for multiple bounces.

Cost: about **0.55 ms** for the ircache inside an 8.4 ms 1080p frame on an **RX 6800 XT**. It is the closest
open-source example of a cache used as the *tail* of a short path, which is exactly EV's M9 shape.

### Cited Findings
**Status and license**
- `EmbarkStudios/kajiya`: archived, last push 2025-07-07. GitHub reports license Apache-2.0 (Embark projects are often
  dual MIT/Apache [U]) — [repo](https://github.com/EmbarkStudios/kajiya) (GitHub API, 2026-11-22)

**Design (from `docs/gi-overview.md`)**
- The cache is "a set of 12 camera-aligned sparsely-allocated 32x32x32 clip maps". A dense indirection array points
  into payload buffers "pre-allocated to a max number (65536) of entries". Entries are allocated on demand and
  "deallocated a few frames after they're last used". It is not a hash map.
- It is "never queried directly from any screen pixel". Diffuse and reflection rays read it at their hit points when
  the hit is not visible on screen. Otherwise "the irradiance from the previous frame is reprojected".
- Placement: each query is a candidate trace origin, and one per voxel is picked per frame "with uniform probability".
  "Ranking" stores the lowest bounce index that touched an entry (rank 1 = screen-traced). With it in place, the Sun
  Temple leaks "disappear". Normal biasing offsets lookups by half a cell along the normal.
- Lighting: temporal reservoir resampling stratified by a 4×4 octahedral map. "At hit positions of candidate rays,
  direct lighting from the sun is calculated, and indirect lighting from the irradiance cache is fed back into itself"
  (no double buffering; "race conditions are fine here"). A pass converts reservoir radiance to "L1 spherical
  harmonics".
- Rays per entry per frame: 4 main (gbuffer + sun shadow), 4 ReSTIR-GI-style validation, 16 short accessibility rays.
  With "usually fewer than 16k cache entries" that is ~128k gbuffer + 384k shadow rays/frame.
- Final gather (~2.3 ms):
  - half resolution, one pixel per 2×2 per frame;
  - ReSTIR reservoirs with temporal reuse, then two spatial passes;
  - a 4-sample full-resolution resolve, a temporal denoiser and TAA;
  - "Every third frame is a validation frame".
- Timings, 1920×1080, Radeon RX 6800 XT, full frame 8.4 ms: G-buffer ~1.15, indirect diffuse ~2.3, **irradiance cache
  ~0.55**, reflections ~2.2, sun shadows ~0.52, SSAO ~0.17, sky ~0.1 ms.
- Known issues:
  - "light leaking and temporal instability" for geometry "below a certain scale";
  - in reflections "the blocky nature of the cache is revealed";
  - "stochastic interpolation of irradiance cache entries makes this problem less severe";
  - the ReSTIR diffuse is "fairly stable, but very laggy" until validation; the cache itself is "very quick to react to
    lighting changes".

  — [gi-overview.md](https://github.com/EmbarkStudios/kajiya/blob/main/docs/gi-overview.md)

**Source details (verified in code)**
- `ircache_grid.hlsl`:
  - `IRCACHE_GRID_CELL_DIAMETER = 0.16 * 0.125` (0.02 m), `IRCACHE_CASCADE_SIZE = 32`, `IRCACHE_CASCADE_COUNT = 12`;
  - the cascade is picked by `log2(max_coord / (16 − reserved))`, i.e. by the Chebyshev distance to the grid centre;
  - a normal offset of half a cell, and stochastic jitter for "stochastic interpolation".

  — [ircache_grid.hlsl](https://github.com/EmbarkStudios/kajiya/blob/main/assets/shaders/ircache/ircache_grid.hlsl)
- `ircache_constants.hlsl`: `IRCACHE_USE_SPHERICAL_HARMONICS 1`, `IRCACHE_OCTA_DIMS = 4`,
  `IRCACHE_SAMPLES_PER_FRAME = 4`, `IRCACHE_VALIDATION_SAMPLES_PER_FRAME = 4`, `IRCACHE_RESTIR_M_CLAMP = 30`, entry
  life 4 frames per rank, 3 ranks — [ircache_constants.hlsl](https://github.com/EmbarkStudios/kajiya/blob/main/assets/shaders/ircache/ircache_constants.hlsl)
- `ircache.rs`:
  - `MAX_ENTRIES = 1024 * 64`;
  - buffers: irradiance `3 × float4` per entry, aux `4 × 16 × float4` per entry (the octahedral reservoirs), grid meta
    `2 × u32` per cell (double-buffered for scrolling);
  - pass order: scroll cascades → age entries → prefix scan + compact → reset → trace accessibility → validate → trace
    irradiance → (later) sum up irradiance;
  - a code comment says `trace_rays_indirect` "seems rather broken on AMD", so the RT passes dispatch the maximum entry
    count.

  — [ircache.rs](https://github.com/EmbarkStudios/kajiya/blob/main/crates/lib/kajiya/src/renderers/ircache.rs)
- A separate experimental "world radiance cache" (`wrc`) has a hard-coded 8×3×8 probe grid (32×32 probes) and is
  bound to the ircache trace — [wrc_settings.hlsl](https://github.com/EmbarkStudios/kajiya/blob/main/assets/shaders/wrc/wrc_settings.hlsl)

**ReSTIR GI (the method kajiya's final gather builds on)**
- Ouyang, Liu, Kettunen, Pharr, Pantaleoni, HPG 2021 (CGF 40(8)). At 1 spp, "MSE improvements ranging from 9.3x to
  166x". In one RTX 3090 scene, 1-spp PT 8.0 ms vs ReSTIR GI 8.9 ms, with 15.1× lower MSE (search snippet of the paper;
  PDF not opened) — [EG Digital Library](https://diglib.eg.org/handle/10.1111/cgf14378), [NVIDIA page](https://research.nvidia.com/publication/2021-06_restir-gi-path-resampling-real-time-path-tracing)

### Inferences
- [E] ircache memory from the buffer sizes:
  - aux 65,536 × 1 KiB = 64 MiB;
  - irradiance 3 MiB, spatial 1 MiB, grid meta 2 × 3 MiB, plus small buffers;
  - total ≈ **75 MiB**, dominated by the reservoirs.
- [I] What EV can copy without ReSTIR GI: the *clipmap + entry pool* (bounded memory, scrolling with the camera), the
  *rank* idea (cache entries touched by short paths get priority) and *L1 SH* storage. With a path tracer, the "screen
  reprojection first, cache second" rule maps to: bounce-1 hits inside the screen read last frame's denoised indirect,
  and all others read the cache.
- [I] Lag behaviour differs per layer: the ircache reacts fast (4 new samples/frame per entry, small M clamp), while
  the screen ReSTIR lags until validation rays catch the change.
- [I] The AMD comment refers to Vulkan indirect ray dispatch in kajiya's era. EV uses compute + RayQuery with
  `ExecuteIndirect` dispatches, which GIBS 2024 also uses, so it does not apply directly.

### Gaps
- No timings for kajiya on NVIDIA. The 0.55 ms figure has no stated resolution beyond the 1080p frame.
- The ReSTIR GI paper's full timing tables were not read.

## 3. Probe GI: DDGI (Majercik et al. 2019, 2021) and the RTXGI-DDGI SDK

### Takeaway
DDGI puts a **uniform 3D grid of probes** in the volume (one every ~1–3 m). Each frame every active probe traces n
rays in a randomly rotated spherical Fibonacci pattern. The hits are shaded with direct light plus *last frame's
probes* (multi-bounce over time). The results are blended with hysteresis 0.85–0.98 into two small octahedral maps
per probe:
- **irradiance** at 8×8 (R11G11B10F);
- **mean distance and mean distance²** at 16×16 (RG16F).

Shading a point blends its 8 surrounding probes, weighted by trilinear weight, a backface test, a **Chebyshev
visibility test** on the distance moments and a normal/view bias. That is what stops the leaks of classic irradiance
probes.

The 2021 paper adds:
- a single "self-shadow bias";
- perceptual gamma encoding;
- change-detection heuristics that cut hysteresis;
- **probe relocation** out of walls;
- **probe states** (Off/Sleeping/Awake/Vigilant), which save 30–50 %;
- camera-tracking **scrolling volumes** and cascades.

2019 cost: 2.6 ms for 32×8×32 probes × 64 rays on an RTX 2080 Ti. It is the simplest, best-documented, most
debuggable world cache, but it only stores diffuse irradiance and has known lag on small bright lights.

### Cited Findings
**2019 paper: "Dynamic Diffuse Global Illumination with Ray-Traced Irradiance Fields", JCGT 8(2)**
- Storage: "diffuse irradiance (in GL_R11G11B10F format at 8 × 8 octahedral resolution), spherical distance and
  squared distances to the nearest geometry (both in GL_RG16F format at 16 × 16 octahedral resolution)". All probes
  are packed into one atlas "with duplicated gutter regions to allow bilinear interpolation".
- Per frame:
  1. "generate and trace n primary rays from each of the m active probes", storing hits in a G-buffer-like "surfel"
     buffer;
  2. "shade the surfel buffer with direct and indirect illumination… with the same routine used to shade final image
     pixels";
  3. "update the texels… by blending in the updated shading, distance, and square-distance results".

  The reference implementation updates **all probes every frame**.
- Update: `newIrradiance = lerp(oldIrradiance, Σ max(0, texelDir·rayDir)·rayRadiance, hysteresis)`, "We set α between
  0.85 and 0.98". Depth uses a sharper cosine-power lobe.
- Placement: "the vertices of an axis-uniform 3D grid… power-of-two resolution per axis". "For human-scale scenes, we
  found a spacing of one to two meters sufficient." Probes inside walls are ignored by the visibility weights.
- Shading weights:
  - backface cull ("soft threshold");
  - a perceptual weight that reduces irradiance below 5 % of range;
  - "mean- and variance-biased Chebyshev interpolants, as detailed in the variance shadow-mapping method";
  - a normal/view offset;
  - trilinear weights.

  "Previous work uses 128×128×6… cube maps to store depth information… we… scale down to 16 × 16."
- Lag: multi-bounce comes from feedback across frames. "Indirect illumination can sometimes appear to 'flow' in and
  out of areas with dramatic visibility changes, due to the latency."
- Formats: "11-bit floating-point representations strike a good balance". R11G11B10F "has a greenish tint because it
  cannot represent exact grays".
- Timings, RTX 2080 Ti, 32×8×32 probes, 64 rays/probe, 8×8 probes:
  - ray generation 0.1, ray cast 0.8, ray shade 0.4, probe update 0.7 (0.3 colour + 0.4 depth), sample probes for
    primary shading 0.5, deferred direct 0.1;
  - **total 2.6 ms** (+2.4 ms brute-force glossy);
  - ray throughput ~1.5–1.66 GRays/s.

  Fig. 1: "6 ms/frame, versus 1 min/frame" path traced at 1920×1080 — [JCGT 2019 paper (low-res PDF)](https://jcgt.org/published/0008/02/01/paper-lowres.pdf), [JCGT page](https://jcgt.org/published/0008/02/01/)

**2021 paper: "Scaling Probe-Based Real-Time Dynamic Global Illumination for Production", JCGT 10(2)**
- Built while integrating DDGI into "the NVIDIA RTXGI SDK, the Unity and Unreal Engine 4 game engines, and proprietary
  engines for several commercial games".
- Self-shadow bias: `BiasVector = (n·0.2 + ω_o·0.8)·(0.75·minDistanceBetweenProbes)·TunableShadowBias`, default
  `0.3`. Rays that hit backfaces "record a value of 0 for irradiance and shorten their depth values by 80%".
- Perceptual encoding: irradiance is stored with `irradianceGamma = 5`, which also reduces "low frequency flicker due
  to fireflies".
- Fast convergence:
  - per texel, a change above 25 % lowers hysteresis by 0.15, and above 80 % sets it to 0;
  - scene events cut hysteresis by 15–50 % for 4–10 frames.
- Relocation: an optimiser moves probes "through the closest backface" and away from close front faces, at most half
  the probe spacing. It runs for five iterations and is applied to static geometry only.
- States: "Off," "Sleeping," "Newly Awake," "Newly Vigilant," "Awake," "Vigilant". Probes near no geometry sleep.
  Probes in dynamic objects' expanded AABBs wake. "Probe sleeping… leads to a 30–50% average performance
  improvement."
- Compute-shader probe update with shared memory: "3x performance improvement" over the pixel-shader version.
- Scrolling: "3D tracking window" implemented as a circular buffer: probe planes "leapfrog" over the camera. Multiple
  volumes blend from dense to sparse, and the transition region is tightened by one cell for camera-locked volumes.
- Second-order glossy reflections reuse the irradiance probes as rough prefiltered radiance.
- Limitation: "still some ghosting in the indirect illumination for small, bright light sources (like flashlights)" —
  [JCGT 2021 paper (low-res PDF)](https://jcgt.org/published/0010/02/01/paper-lowres.pdf), [JCGT page](https://jcgt.org/published/0010/02/01/)

**RTXGI-DDGI SDK**
- `NVIDIAGameWorks/RTXGI-DDGI`, latest release **v1.3.7 (2023-05-08)**, last push 2024-02. License: "The NVIDIA RTX
  SDKs License" (modification allowed for sample code; attribution required; distribution as object code inside an
  application). That makes it a reading reference, not something to vendor — [repo](https://github.com/NVIDIAGameWorks/RTXGI-DDGI), [License.txt](https://raw.githubusercontent.com/NVIDIAGameWorks/RTXGI-DDGI/main/License.txt)
- Resources: Probe Ray Data (one row per probe, radiance + hit distance), Probe Irradiance, Probe Distance, Probe Data
  (xyz = relocation offset, w = classification state), Probe Variability and its average.
- Settings:
  - fixed rays default 32 (`RTXGI_DDGI_NUM_FIXED_RAYS`);
  - relocation moves at most 45 % of a cell;
  - irradiance encoding gamma 5;
  - "one probe every 2–3 meters".
- Shaders: ProbeBlendingCS, ProbeRelocationCS, ProbeClassificationCS, ReductionCS (HLSL). Bindless needs SM 6.6.
- **Probe variability**, the mean coefficient of variation, lets the app pause updates when converged — [DDGIVolume.md](https://raw.githubusercontent.com/NVIDIAGameWorks/RTXGI-DDGI/main/docs/DDGIVolume.md)
- RTXGI 2.x (the NRC + SHaRC repo) no longer contains DDGI. DDGI lives on in this separate 1.x repo — [neural_radiance_cache.md §3](neural_radiance_cache.md), [RTXGI-DDGI repo](https://github.com/NVIDIAGameWorks/RTXGI-DDGI)

**DDGI vs NRC (already in EV's notes)**
- Müller et al. 2021, fps DDGI / NRC: Zero Day 96.7 / 87.7, Living Room 134 / 111, Pink Room 141 / 124. DDGI "can
  expose visible bias (e.g. in the Pink Room)" — `neural_radiance_cache.md` §1, from [arXiv 2106.12372](https://arxiv.org/html/2106.12372v2)
- Frostbite rebuilt its GIBS probe clipmap "very similar to DDGI" (octahedral + variance depth) — [GIBS 2024](https://advances.realtimerendering.com/s2024/content/EA-GIBS2/Apers_Advances-s2024_Shipping-Dynamic-GI.pdf)

### Inferences
- [E] Memory per probe in the 2019 layout:
  - irradiance (8+2)² × 4 B = 400 B;
  - distance (16+2)² × 4 B = 1,296 B;
  - ray data 256 rays × 8 B = 2 KiB (temporary);
  - total ≈ 3.7 KiB, so 32×8×32 = 8,192 probes ≈ 30 MiB.

  GIBS 2024 fits ~1 KiB per probe with a smaller irradiance map.
- [I] Per-frame order in EV:
  1. (scroll volume if the camera moved);
  2. classify/relocate (only for new probes);
  3. trace (compute + RayQuery, one thread per ray, rays laid out probe-major);
  4. shade hits (EV's direct-light code + a probe lookup);
  5. blend irradiance (one thread group per probe, 8×8 threads);
  6. blend distance (16×16);
  7. border copy;
  8. use: hybrid shading pass, or path-terminator lookups.
- [I] As a **path terminator** in EV: at the terminating vertex, `L_o ≈ albedo(λ)/π · E(x, n)` with E from the probes.
  This is biased on glossy vertices and in thin-wall areas, but cheap (8 probe reads). As **hybrid** diffuse GI (M8b
  real-time mode): replace the bounce-1 diffuse path entirely and keep traced specular.
- [I] Difficulty is moderate because every piece can be checked in isolation: a probe debug view (spheres showing
  irradiance and depth), Chebyshev weights, and the "single room + door" leak scene from Fig. 7 of the paper.

### Gaps
- No DDGI timings on RDNA or Ampere beyond the 2019 RTX 2080 Ti table. The 2021 paper's Figure 8 timings are in an
  image (not extracted).
- The RTXGI-DDGI default ray count beyond the fixed 32 and the default texel counts were not stated in the docs read.

## 4. AMD GI-1.0 / GI-1.1 / GI-1.2 (Capsaicin) and Brixelizer GI

### Takeaway
GI-1.0 is a **two-level radiance cache**:
- **Screen probes**: one per 8×8 tile, placed on primary surfaces. Each traces into the scene and stores an 8×8
  octahedral map of incoming radiance, plus SH. Probes reproject from last frame, and spare rays go to tiles that
  failed to reproject.
- A **world-space hash-grid cache**: probe rays that hit geometry look up a cell keyed by quantised position, quantised
  direction, a distance-based LOD and a "short ray" bit. The cell stores **outgoing radiance**, lit by world-space
  ReSTIR direct lighting.

Screen probes don't leak, because they sit on visible geometry. The hash grid makes off-screen hits cheap.

Cost: about **1.9–3.4 ms at 1080p, ¼ spp** on an RX 6900 XT / RTX 3080. GI-1.1 adds glossy reflections. GI-1.2
(Nov 2025) adds a second hash-cell bounce for multibounce.

The hash grid in Capsaicin is plain MIT HLSL and the closest relative of SHaRC. The screen-probe half is the hardest
part to implement of anything in this note.

### Cited Findings
**GI-1.0 paper (Boissé et al., AMD tech report 2022, arXiv 2310.19855)**, read via ar5iv. The summary below is from
the fetched text.
- Screen cache: probes on primary surfaces, one per 8×8 tile, at a Halton-jittered pixel. Each stores an 8×8 octahedral
  map of incoming hemispherical radiance (alpha = hit distance), plus SH. Reprojection uses a plane-distance test
  against `cell_size` and normal dot > 0.95. Empty tiles get rays taken from tiles that reprojected ("ray count stays
  constant"). A persistent LRU "side cache" keeps evicted probes. Ray guiding uses a CDF of reprojected luminance.
- World cache: the key is quantised position + ray direction, with adaptive quantisation by distance, plus a boolean
  `ray_length < cell_size` "to fix light leaks". Cells store outgoing radiance with a decay counter and are freed when
  it reaches zero. Light sampling is world-space reservoir reuse with one shadow ray per reservoir.
- Pass order:
  1. reproject probes;
  2. patch spawn tiles;
  3. guiding CDF;
  4. trace;
  5. world-cache lookup / prefilter / light sampling;
  6. blend + side cache;
  7. probe filtering;
  8. per-pixel interpolation (SH);
  9. spatiotemporal denoise;
  10. short-range SSGI/AO.
- Timings, **1080p, ¼ spp, DXR 1.1, RX 6900 XT / RTX 3080** (ms):

  | Scene (triangles) | Total RX 6900 XT | Total RTX 3080 |
  |---|---|---|
  | Gas station (99k) | 1.932 | 1.906 |
  | Flying world (267k) | 2.463 | 2.326 |
  | Kitchen #1 (1.4M) | 2.734 | 2.976 |
  | Kitchen #2 (9.0M) | 3.124 | 3.426 |

  The ray-tracing part alone is 0.235–0.676 ms (RX 6900 XT) and 0.132–0.343 ms (RTX 3080). The rest is cache work and
  denoising.
- Limitations:
  - multi-bounce feedback "only works when the reflector was visible in the previous frame";
  - light-grid intensity clamping adds bias, and the single-shadow-ray ReSTIR darkens;
  - thin geometry can wobble;
  - "relaxed interpolation" can leak.

  — [arXiv 2310.19855 (ar5iv)](https://ar5iv.labs.arxiv.org/html/2310.19855), [GPUOpen tech report PDF](https://gpuopen.com/download/GPUOpen2022_GI1_0.pdf)

**Capsaicin source: the hash grid as implemented (verified in code)**
- `hash_grid_cache.hlsl` (MIT, © 2025 AMD):
  - **cell size** `max(distance(eye, p) · cell_size, min_cell_size)`, snapped to a power of two (`log2` step), so the
    LOD comes from camera distance as in SHaRC;
  - **direction key** `floor(0.5 + (0.5·d + 0.5)·4)` per axis (a coarse direction quantisation);
  - key = (LOD, tile coords, direction, `hit_distance < tile_size`), hashed twice: `pcgHash` for the bucket and
    `xxHash` for a 32-bit fingerprint;
  - linear probing inside a bucket with `InterlockedCompareExchange`;
  - cells are grouped into **tiles** of `tile_cell_ratio²` cells, projected along the dominant axis of the direction,
    with up to 4 mip levels;
  - `kHashGridCache_TileDecay = 50` frames before an unused tile is evicted;
  - radiance is stored as **half4 (8 B)**, with the sample count in `w`;
  - atomic accumulation quantises floats to integers (`kHashGridCache_FloatQuantize = 1e3`);
  - lookups climb to coarser mips while the sample count is below a threshold.

  — [hash_grid_cache.hlsl](https://github.com/GPUOpen-LibrariesAndSDKs/Capsaicin/blob/master/src/core/src/render_techniques/gi1/hash_grid_cache.hlsl)
- The GI technique folder holds `screen_probes.hlsl`, `world_space_restir.hlsl`, `gi_denoiser.hlsl` and
  `glossy_reflections.hlsl`. The framework also contains `reference_path_tracer` — [Capsaicin tree](https://github.com/GPUOpen-LibrariesAndSDKs/Capsaicin/tree/master/src/core/src/render_techniques)
- Capsaicin: MIT, "AMD ARR team rendering framework", last push 2025-11-20. It depends on AMD's `gfx` D3D12 layer and
  needs D3D12 Ultimate hardware (`industry_realtime_path_tracing.md`) — [repo](https://github.com/GPUOpen-LibrariesAndSDKs/Capsaicin)

**GI-1.1 and GI-1.2**
- GI-1.1 (posted 2024-03-19): reflection rays are sampled from GGX. Radiance comes either from screen probes (rough) or
  from ray-traced reflections that read the hash grid, switching "based on the roughness parameter". It has its own
  reflection denoiser that "can denoise and upscale simultaneously". Paper: "Real-Time Rendering of Glossy Reflections
  Using Ray Tracing and Two-Level Radiance Caching" (SIGGRAPH Asia 2023) — [GPUOpen GI-1.1](https://gpuopen.com/learn/gi-1-1-glossy-reflection-rendering/)
- GI-1.2 (2025-11-20, with Capsaicin v1.3): "rays are traced from the 1st bounce cells, and 2nd bounce cells are created
  at the hit points". The 2nd-bounce direct light becomes an indirect sample for the 1st-bounce cells "used in the next
  frame". GI-1.1 "effectively only model[led] two bounces" and looked darker than path tracing. No timings are given —
  [GPUOpen GI-1.2](https://gpuopen.com/learn/gi-1-2-multibounce-indirect-rendering/)

**Brixelizer GI**
- "A compute-based real-time dynamic global illumination solution":
  - sparse distance-field cascades of 64³ voxels with per-voxel "Bricks";
  - screen probes trace the distance field and read a radiance cache fed by the previous frame's lighting;
  - a world-space irradiance cache of SH probes per valid brick;
  - "Implemented in compute without hardware-accelerated ray-tracing";
  - "a simplified implementation of AMD GI-1.0";
  - FidelityFX SDK v1.1 (July 2024), currently v1.1.4 (May 2025), MIT;
  - no performance numbers on the page.

  — [GPUOpen Brixelizer](https://gpuopen.com/fidelityfx-brixelizer/)

### Inferences
- [I] The GI-1.0 hash grid differs from SHaRC in three ways worth measuring in EV:
  1. it is **direction-keyed** (radiance per coarse direction bin), not one value (or SH) per voxel;
  2. it has a **short-ray bit**, so hits closer than one tile don't share a cell with far hits, which is a cheap
     anti-leak trick;
  3. tiles with **mips**, so sparse cells borrow from coarser neighbours.

  Each is a small, testable variation on an M9-style hash grid.
- [I] The screen-probe level is effectively a full hybrid renderer (reprojection, adaptive placement, side cache,
  filtering, SH interpolation, denoiser). For a path tracer that already has 1-spp primary paths and a denoiser (M8b),
  most of that is redundant. Only the world level is attractive as a path terminator.
- [I] Integer-quantised atomics (×1000) are the portable answer to "HLSL has no float `InterlockedAdd`" [T]. The
  alternative is one thread per entry that gathers its own samples (kajiya/DDGI style), which needs no atomics.
- [I] Brixelizer GI is not a fit for EV: it replaces hardware RT with SDF tracing, and EV is built on RayQuery.

### Gaps
- No GI-1.0 memory figures. No GI-1.1/1.2 timings. No published `tile_cell_ratio` / `num_buckets` defaults were read
  (they are in `gi1.cpp`, not opened).
- The 8×8 tile and ¼ spp figures were read from the ar5iv HTML summary, not from the PDF tables directly.

## 5. UE5 Lumen (architecture reference only)

### Takeaway
Lumen is a layered system:
- a **surface cache**: per-mesh "cards" (orthographic captures) holding material and lighting in a 4k atlas,
  updated under a fixed texel budget, with multi-bounce through feedback;
- tracing in order screen → software distance fields (mesh SDF / global SDF clipmaps) or hardware RT, with hits lit
  from the surface cache;
- a **screen-space radiance cache** (screen probes at 1/16 resolution per axis, importance-sampled from last frame);
- a **world-space radiance cache** (clipmapped probes with higher directional resolution) for lighting more than
  ~2 m away.

On PS5 at 1080p it costs 2.77 ms (High) to 4.63 ms (Epic) in the "Land of Nanite" demo. EV won't build it. Its lessons
are the *layering*:
- trace short and fall back to a cache;
- light hits from a surface cache instead of evaluating materials;
- keep a world cache with high angular resolution for distant light.

### Cited Findings
- Surface cache:
  - cards are "uniform rectangular clusters of surfels", generated at mesh import ("~0.2ms to build a large 1.5M tri
    mesh");
  - 128×128 physical pages in a 4k×4k atlas;
  - a fixed per-frame budget: 1024×1024 texels for direct lighting and 512×512 for indirect lighting;
  - indirect lighting in the cache uses a 4×4 hemispherical probe per 4×4 texel tile;
  - "N+2 bounces through feedback";
  - global-SDF hits read a "clipmap volume of merged cards" (4 clipmaps of 64³, radiance for 6 directions);
  - limitation: cards "don't support mesh animation".

  — [SIGGRAPH 2022 Advances, Lumen (Wright, Narkowicz, Kelly)](https://advances.realtimerendering.com/s2022/SIGGRAPH2022-Advances-Lumen-Wright%20et%20al.pdf)
- Final gather:
  - "Screen Space Radiance Cache which is operating at 1/16th resolution in each dimension. It's backed up by the World
    Space Radiance Cache, which is handling distant lighting";
  - probes are subdivided where geometry is detailed, and placement is jittered and temporally accumulated;
  - product importance sampling uses "last frame's Screen Radiance Cache, reprojected" and the world cache;
  - the world cache: "Shorten Screen probe trace, interpolate from World probe on miss", "Higher directional resolution
    (16 : 1)", clipmaps for bounded screen-space probe density, and 32×32 directional resolution reused for glossy
    reflections;
  - the temporal filter rejects history on depth/normal, which "causes a slow reaction to lighting changes, which shows
    up as ghosting".

  — [Lumen 2022 slides](https://advances.realtimerendering.com/s2022/SIGGRAPH2022-Advances-Lumen-Wright%20et%20al.pdf)
- Timings, PS5, 1080p, SWRT global SDF only:
  - High: surface cache 0.47, final gather 2.07, reflections 0.23, **total 2.77 ms**;
  - Epic: 0.93 / 3.46 / 0.24, **total 4.63 ms**;
  - "the `Epic' settings have four times more rays per pixel".

  — [Lumen 2022 slides](https://advances.realtimerendering.com/s2022/SIGGRAPH2022-Advances-Lumen-Wright%20et%20al.pdf)
- Current docs:
  - 12 cards per mesh by default, 6 for skeletal meshes;
  - walls "no thinner than 10 centimeters (cm)" to avoid leaking;
  - Epic scalability "around 8 milliseconds (ms) on next-generation consoles" for GI + reflections at 1080p;
  - known issues: leaking with missed occluders, lag on fast camera moves, view-dependent GI with large indirect
    scale;
  - HWRT needs RTX 2000+ or RX 6000+.

  — [Lumen Technical Details (Epic docs)](https://dev.epicgames.com/documentation/en-us/unreal-engine/lumen-technical-details-in-unreal-engine)
- GI-1.0's paper compares itself with Lumen: "GI-1.0 with hardware traversal runs at 2.4 ms and Lumen with software
  traversal at 2.6 ms" (UE5 plugin, approximated materials) — [GI-1.0 (ar5iv)](https://ar5iv.labs.arxiv.org/html/2310.19855)

### Inferences
- [I] Lumen's two radiance caches map onto GI-1.0's two levels (screen probes + world cache). Its surface cache plays
  the role GIBS surfels and GI-1.0's hash cells play: "cheap lighting at a ray hit".
- [I] EV equivalent of "trace short, fall back": shorten bounce rays (e.g. 20 m as in GIBS 2024), and on a miss read the
  world cache instead of the sky. That is an extra, measurable knob for any cache EV builds.

### Gaps
- Lumen numbers on PC GPUs were not looked up. The rpp values for High/Epic were garbled in the PDF text (only the
  "four times more" ratio is certain).

## 6. Newer relevant work (2024–2026)

### Takeaway
Two recent directions are worth a look:
1. **On-surface (texture-space) radiance caches** (TU Graz, HPG 2024). Both the primary and the secondary cache live
   in a per-island surface atlas. Primary caches store *incoming* and secondary caches *outgoing* radiance
   hemispheres. They report a 5–10 % speed and quality gain over the state of the art (incl. GI-1.1), at 300–700 MB.
2. **Radiance-cascade-style world probes** in a sparse hashmap (Split Radiance Cascades, arXiv July 2026).

Neither has shipped. Both build on the same hash-grid and probe ideas covered above.

### Cited Findings
- Tatzgern, Weinrauch, Stadlbauer, Mueller, Winter, Steinberger, "Radiance Caching with On-Surface Caches for Real-Time
  Global Illumination", PACMCGIT 7(3) 38, 2024 — [dblp](https://dblp.org/rec/journals/pacmcgit/TatzgernWSMWS24). The
  HPG 2024 slides say:
  - "Unified radiance caching system in texture space";
  - "Primary caches store incoming radiance… Secondary caches store outgoing radiance… span ~30cm × 30cm";
  - secondary caches use ReSTIR for direct light and "sample each other → multiple bounces over time";
  - the temporal filter needs "No re-projection", an EMA with "Blend factor inverse to sample count";
  - memory "300MB – 700MB" for a single viewer;
  - limitations: "Visible borders at island borders", and foliage increases cache allocations;
  - compared against "AMD-GI1.1" in its figures.

  — [HPG 2024 slides](https://highperformancegraphics.org/slides24/hpg24_oscgi.pdf)
- Freeman, Sannikov, Pitt-Francis, "Split Radiance Cascades: Real-Time Global Illumination via Sparse Radiance Probes",
  arXiv 2607.20384 (v1 2026-07-22, v2 2026-10-11): it adapts Radiance Cascades "to real-time 3D diffuse global
  illumination using a sparse hashmap of world-space probes" with "ray splitting". The abstract gives no timings —
  [arXiv](https://arxiv.org/abs/2607.20384)
- A 2025 paper on GI for dynamic 3D Gaussian scenes reuses the GI-1.0 two-level scheme (screen probes + hash grid) —
  [arXiv 2503.17897](https://arxiv.org/html/2503.17897v1)

### Inferences
- [I] The on-surface cache needs a per-mesh "island" parameterisation (a UV-like atlas built offline), and EV has no
  such data. It is interesting as a reading reference for "incoming vs outgoing radiance at two levels", not as a
  milestone.

### Gaps
- The on-surface cache timings table is an image (not extracted). The Split RC paper body was not read.

## 7. Storing light in EV's spectral (hero-wavelength) renderer

### Takeaway
Every cache above stores RGB. EV's paths carry 4 wavelengths in a `float4` (D9). There are three workable options:
- **(a) Linear RGB/XYZ at the cache boundary** (as planned for SHaRC in M9). 3 values per stored direction or texel,
  one projection when a sample is written, one RGB→spectrum upsample when it is read. Cheapest. The bias is
  metamerism after the cache.
- **(b) A few fixed spectral bins** (e.g. 8 bins over 360–830 nm), with each hero sample splatted into the bins its
  wavelengths fall in. 8/3 ≈ 2.7× the memory and bandwidth of RGB, with more noise per bin.
- **(c) A linear spectral basis**, e.g. 4–8 Fourier/trigonometric moments (the input of Peters' method, M5b).
  Moments are linear in the spectrum, so they can be averaged in a cache exactly like RGB. Reconstruction happens only
  at read time.

For diffuse caches (DDGI, surfels, ircache) storing *irradiance*, (a) is the clear first choice. (c) is a good
research A/B once M5b exists.

### Cited Findings
- EV's existing rule for M9: "convert the path's spectral contribution to RGB/XYZ at the cache boundary (`SharcUpdateHit`
  takes RGB); at query, multiply the cached RGB by the path throughput (convert throughput to RGB, or upsample the
  cached RGB as an illuminant-like spectrum)" — `knowledge.md` §9.1
- `RGBIlluminantSpectrum` = `scale·s(λ)·D65(λ)`, used for RGB lights and environment texels, and `RGBUnboundedSpectrum`
  for unbounded emission — `knowledge.md` (§ RGB→spectrum table)
- No spectral or wavelength-conditioned radiance cache was found in the 2026-11-18 search — `neural_radiance_cache.md`
  §5
- Formats matter even in RGB: R11G11B10F "has a greenish tint because it cannot represent exact grays" — [DDGI 2019](https://jcgt.org/published/0008/02/01/paper-lowres.pdf)

### Inferences
- [I] **Writing (all caches)**: a sample's spectral radiance at the path's 4 wavelengths, `L(λ_i)/p(λ_i)`, is
  projected to XYZ with the CMFs (`Σ x̄(λ_i)·L(λ_i)/p(λ_i) / 4`, etc.) and then to linear sRGB. This is the same code
  as the film write in M4, and it is unbiased in XYZ. XYZ→RGB can be negative for saturated spectra, so either store
  XYZ, or clamp RGB at zero and accept a small bias [I].
- [I] **Reading**: upsample the cached RGB with `RGBIlluminantSpectrum` (one rgb2spec table fetch + a sigmoid
  evaluation per read, then evaluated at the 4 path wavelengths) and multiply by the spectral path throughput. Per
  query the extra cost is one 3D-texture fetch, small next to a DDGI 8-probe gather or a surfel loop [E].
- [I] **Why rgb2spec coefficients must not be stored or averaged**: Jakob–Hanika coefficients pass through a non-linear
  sigmoid, so the average of two coefficient sets is not the coefficient set of the averaged spectrum. Caches must
  accumulate a *linear* quantity (RGB, XYZ, bins or moments).
- [I] **Bins (b)**: each of a path's 4 wavelengths adds `L(λ_i)/p(λ_i)` to its bin, with a per-bin sample count.
  With 8 bins each bin gets about half the samples per frame that an RGB channel would. Expect more cache noise or lag
  at equal hysteresis.
- [I] **Moments (c)**:
  - store m real moments (m = 4–8) per texel or SH coefficient;
  - writing is a few `cos/sin(k·φ(λ_i))` terms per wavelength (φ is Peters' warp of λ to phase), averaged linearly;
  - reading runs the bounded-MESE reconstruction from M5b (tens of ALU ops) once per query and evaluates it at the
    4 wavelengths;
  - memory m/3 × RGB.
- [E] Memory per technique for RGB → 8 bins (fp16):

  | Technique | RGB | 8 bins |
  |---|---|---|
  | DDGI irradiance texel | 4 B (R11G11B10) | 16 B (4× irradiance atlas; distance atlas unchanged) |
  | Surfel irradiance + MSME state | ~44 B | ~100 B, unless the MSME runs on luminance only and only the mean is binned (~28 B) |
  | kajiya L1 SH | 12 floats | 32 floats |
  | Hash-grid cell (Capsaicin half4 / SHaRC) | 8 B | 16 B |
- [I] Where RGB at the boundary is visibly wrong: light that bounces off strongly coloured or spectrally peaked surfaces
  many times (multi-bounce colour saturation), dispersion (already terminated in D9), and spectral media such as water
  absorption *after* the cache vertex. For EV's RGB-authored materials (rgb2spec albedos), the first two effects are
  small [I]. The M9-style "bias vs reference" check in spectral vs RGB mode quantifies it.

### Gaps
- No published measurement of RGB-cache bias in a spectral renderer was found. This would be an original EV result.

## 8. Options for EV

### Takeaway
Recommended shape:
1. Keep **M9 SHaRC** as the first cache (path terminator, plain HLSL, SDK-guided).
2. Add one **probe cache (DDGI, "M9b")** as the second. It is the easiest to build and debug, works as both a path
   terminator and the hybrid diffuse GI of M8b, and teaches octahedral maps and Chebyshev visibility.
3. Then a **surfel cache (GIBS-lite, "M9c")** as the ambitious one. It is the most "attached to geometry" and has the
   best leak and scale behaviour, but needs the M8b G-buffer and 4–8 weeks of work.

kajiya-style ircache + ReSTIR GI and a GI-1.0-style direction-keyed hash grid are optional variations (M9d, M9e).
Lumen, GI-1.0 screen probes, Brixelizer and on-surface caches are reading references only.

Before M10 (own NRC), run a **cache bake-off ("M9x")** on fixed test scenes with one harness, so that NRC is judged
against everything at equal time.

### Inferences (proposal; milestone letters are suggestions, not decisions)

**Shared groundwork (part of M9, reused by all caches)**
- One HLSL interface, `RadianceCache_Query(x, n, ω, λ4) → float4 spectral` plus `RadianceCache_Update(...)`, and a
  panel toggle `None / SHaRC / DDGI / Surfels / …`. Every path-tracer termination point calls the same interface, so
  techniques swap without touching the path loop [I].
- Two usage modes, measured separately:
  1. **Terminator**: reference-mode paths stop after N bounces (N = 1, 2) and read the cache. The metric is error vs
     the frozen high-spp reference at equal time.
  2. **Hybrid**: in M8b real-time mode, bounce-1 diffuse comes from the cache and specular is traced. The metrics are
     frame time + FLIP.
- Debug views: cache-only view (SHaRC `DirectCacheView`-like), probe or surfel visualisation, per-entry sample count
  and variance heat maps, and memory occupancy.
- Test environments (the student plans to build them; suggested contents):
  1. **Leak box**: a closed room with one door and a bright sunlit exterior, plus wall-thickness variants 5 / 10 /
     20 cm (Lumen's 10 cm rule, DDGI Fig. 7).
  2. **Lag test**: a light that switches on/off and a sliding coloured block (DDGI dragon box). Measure frames until
     90 % of the final value.
  3. **Flicker test**: a static scene and static camera, with temporal standard deviation per pixel after convergence.
  4. **Scale test**: Sponza → Bistro-size; camera fly-through for spawn and scroll costs.
  5. **Glossy test**: a rough metal floor, to show the irradiance-only caches' bias as terminators.
- Measurements per technique on **both GPUs** (into `performance.md`): ms per pass, rays/frame, VRAM, convergence
  frames, lag frames, leak luminance in the dark room, FLIP vs reference (terminator and hybrid), and RGB vs spectral
  storage bias (§7).

**M9 – SHaRC (unchanged)**
- As planned. Add the "short ray + cache fallback" knob (GIBS 2024: 20 m rays with probe fallback) as an experiment.

**M9b – DDGI probe volume (recommended second cache)** [E: 2–4 weeks]
- Build in steps:
  1. a fixed grid (e.g. 32×8×32), all probes updated every frame, irradiance + distance atlases, compute + RayQuery
     trace, blend and border-copy shaders, hybrid shading;
  2. Chebyshev visibility + self-shadow bias (2021 formula), backface rule (0 irradiance, depth × 0.2);
  3. relocation + probe states;
  4. a scrolling camera volume (circular buffer) + 2–3 cascades;
  5. use as a path terminator.
- Verify:
  - the leak box matches the reference within FLIP threshold X;
  - timings near the 2019 table scaled by GPU (a 3070 is roughly 2080 Ti class [E]);
  - the sleeping gain is in the published 30–50 % range.
- Learning value: octahedral mapping, moment-based visibility, hysteresis vs lag, thread-group layout (8×8 / 16×16
  texels per probe = one group, as in the 2021 paper).

**M9c – Surfel GI, GIBS-lite (ambitious third cache)** [E: 4–8 weeks; secondary source says 6–8 + 2–4 weeks]
- Prerequisite: M8b G-buffer with instance ID and geometric normal (export the interpolated vertex normal, per GIBS
  2024).
- Build in steps:
  1. spawning in 16×16 tiles + a camera-relative uniform grid (W298-style) + naive apply, with fixed-count rays and an
     EMA;
  2. MSME (port of the RTG ch. 25 estimator) + variance-driven ray counts + a global ray budget (count pass → prefix sum
     → allocation);
  3. 4×4 radial depth/depth² + Chebyshev test;
  4. transforms (instance ID + local position) for moving objects;
  5. recycling heuristic + free-ID stack;
  6. a non-linear grid (GIBS uniform core + trapezoidal shells, or kajiya-style cascades) to replace the dense grid;
  7. optional: ray guiding (6×6), irradiance sharing, a DDGI probe fallback for missing coverage (GIBS 2024), and
     scalarised loads for RDNA.
- Verify: no holes after camera cuts within K frames, a stable surfel count (heat map), the leak box vs DDGI, VRAM within
  the 8 GB budget, and timings vs the Skate breakdown (as an order of magnitude only, different hardware).

**M9d (optional, after M11 ReSTIR DI) – kajiya-style clipmap irradiance cache + ReSTIR GI final gather** [E: 4–6 weeks]
- Copy the 12 × 32³ clipmap with entry pool, rank voting, 4×4 octahedral reservoirs and L1 SH. Then a half-resolution
  ReSTIR GI gather that reads the cache only off screen. It is mostly valuable if EV wants ReSTIR GI anyway.

**M9e (optional, small) – GI-1.0-style hash-grid variations on the M9 grid** [E: 1 week]
- Direction-keyed cells, the short-ray bit, tile mips, integer-quantised atomics. A clean A/B against SHaRC's
  per-voxel radiance on the leak box and glossy test.

**M9x – Cache bake-off (before M10)**
- Same scenes, same frozen camera paths, both GPUs, terminator and hybrid modes, RGB vs spectral storage. The result
  table becomes the baseline that M10's NRC must beat (NRC paper: DDGI slightly faster but more biased).

**Order relative to the roadmap** [I]
- M8b (real-time mode, G-buffer, denoiser) → M9 SHaRC → M9b DDGI → (M9c Surfels) → M9x bake-off → M10 NRC →
  O2.
- M9b can also come *before* M8b for terminator-only use in reference mode, since it needs no G-buffer. M9c cannot.
- M9d fits after M11's ReSTIR DI.

**Comparison table** (timings are as published, on the GPU named; EV's numbers will differ)

| Technique | Stores | Memory (published or [E]) | Update cost (published) | Leaks | Lag | Needs denoiser | Difficulty in HLSL + RayQuery [E] | Good open-source reference |
|---|---|---|---|---|---|---|---|---|
| **SHaRC** (M9) | Radiance per hash voxel (optional SH), RGB | 40 B/voxel, 160 MiB at 2²² | Update traces ~4 % of pixels (see `neural_radiance_cache.md`) | Some (voxel size) | Low–medium | Not by itself, but used with one | Low (SDK headers) | NVIDIA-RTX/SHARC |
| **DDGI** | Irradiance 8×8 octa + dist/dist² 16×16 per probe | ~3.7 KiB/probe [E], 30 MiB for 8k probes [E]; GIBS-style ~1 KiB | 2.6 ms, 8k probes × 64 rays, RTX 2080 Ti (2019) | Low with Chebyshev + relocation; corners/thin walls remain | Medium (hysteresis 0.85–0.98; flashlight ghosting) | No (smooth by construction) | Medium | RTXGI-DDGI (read only, NVIDIA license); JCGT supplements |
| **GIBS surfels** | Irradiance per surfel (no direction) + 4×4 radial depth | 100 MB surfels + probes budget; <500 MB all-in on XBSS | 2.5–3.2 ms total, ~100k surfels, 200k rays, XSX 1440p | Low (radial depth); missing coverage = dark gaps → probe fallback | Low–medium (MSME adapts) | Light (spatial upsample; sharing) | High | W298/SurfelGI (MIT, Falcor); RTG ch. 25 MSME |
| **kajiya ircache** | L1 SH irradiance per clipmap entry (ReSTIR reservoirs inside) | ≈75 MiB [E] for 64k entries | ~0.55 ms, RX 6800 XT, 1080p frame | Some at small scale; blocky in reflections | Cache fast; screen ReSTIR laggy without validation | Yes (screen ReSTIR GI path) | High (very high with ReSTIR GI) | EmbarkStudios/kajiya (archived, Apache-2.0) |
| **GI-1.0 hash grid** (world level) | Outgoing radiance per (pos, dir bin, LOD, short-ray) cell, half4 | Not published | Part of 1.9–3.4 ms total at 1080p, RX 6900 XT / RTX 3080 | Low (short-ray bit; screen probes on surfaces) | Low–medium (decay 50 frames) | Yes (for the screen level) | Medium for the hash grid alone; very high with screen probes | Capsaicin `gi1/` (MIT) |
| **Lumen** (reference) | Surface cache (cards) + screen and world radiance caches | Surface atlas 4k², global SDF clipmaps | 2.77 / 4.63 ms, PS5 1080p (High / Epic) | Thin walls < 10 cm; missed occluders | Medium (temporal filter ghosting) | Yes | Not a target | UE5 source (EULA) |
| **NRC** (M10, for comparison) | Learned radiance (MLP + encoding), RGB | Small network + training buffers | Query 1.59 + train 1.11 ms avg, RTX 3090 1080p | Low (no grid), but blurry detail | ~8 frames to converge | Used with one | Very high | NVIDIA NRC SDK (binary), tiny-cuda-nn |

### Gaps (for the whole note)
- None of the techniques has published timings on an RX 9070 XT or RTX 3070. All EV numbers will be new.
- GIBS's exact surfel layout, grid parameters and surfel memory per entry are unpublished.
- GI-1.0 and GI-1.2 memory, and GI-1.1/1.2 timings, are unpublished.
- The difficulty estimates are the author's judgement, plus one secondary source for surfels.
