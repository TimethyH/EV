# Radiance Cascades for real-time GI, especially in 3D (follow-up research, 2026-11-22)

Scope: **Radiance Cascades (RC)**, the GI method Alexander Sannikov (Grinding Gear Games) built for *Path of Exile 2*.
The note covers:
- the original method: the penumbra condition, radiance intervals, cascade layout, merging and its artefacts (ringing,
  the bilinear fix);
- every serious 3D variant found:
  - screen-space "flatland" RC (PoE2);
  - screen-space probes with world-space intervals (SPWI);
  - dense world-space 3D grids;
  - surface and surfel RC;
  - Holographic RC;
  - **Split Radiance Cascades** (Freeman, Sannikov, Pitt-Francis, arXiv 2026), which the sibling note
    `followup_gi_world_caches.md` mentions but did not read. This note reads it in full;
- what "Spatium Radiance Cascades" in "Black Engine" is;
- artefacts, published timings and memory, worked examples of cascade sizes;
- how RC fits a spectral path tracer, and a milestone ladder for EV.

The note compares RC against the caches in `followup_gi_world_caches.md` (surfels, ircache, DDGI, GI-1.0, Lumen) and
does not repeat them.

Labels: [T] training knowledge, [I] inference, [E] estimate, [U] unverified. Facts without a label come from the linked
source, fetched or downloaded 2026-11-22.
- **Read in full**: the Split RC and Holographic RC PDFs (converted locally with `pdftotext`), the original paper's
  LaTeX source from its GitHub repo, and the Gavras 2025 thesis.
- **Read in part**: the Osborne & Sannikov 2024 arXiv HTML (first ~100k characters), and the BTH 2025 thesis
  (abstract, setup, Table 5.3).
- **Read from repos**: READMEs and a few shader files.
- **YouTube metadata** (titles, dates, descriptions) for the Black Engine videos came from the public playlist RSS feed.
  The videos themselves were not watched.

No EV file other than this note was changed.

## 0. Words used in this note (plain-language glossary)

The terms *radiance*, *irradiance*, *probe*, *octahedral map*, *SH*, *hash grid*, *clipmap*, *path terminator*,
*hybrid renderer*, *lag*, *leak* and *flicker* are defined in `followup_gi_world_caches.md` §0. The new ones below are
[T] definitions unless a source is given.

- **BVH** (bounding volume hierarchy): a tree of boxes around the triangles, so a ray only tests the few triangles whose
  boxes it passes through. DXR builds it for you (BLAS per mesh, TLAS over instances). **SAH** (surface area
  heuristic) is the usual rule for choosing where to split boxes when building it. RC does not care how rays are traced,
  as long as you can trace a ray *segment*.
- **pdf** (probability density function): how likely a sampler is to pick each direction. A path tracer divides by it.
  RC mostly avoids pdfs, because it uses fixed, evenly spaced directions instead of random ones.
- **Radiance interval** `L_{a,b}(p, ω)`: the light arriving at point p from direction ω that comes only from the part
  of the ray between distance a and distance b. Plain radiance is the interval [0, ∞). This is RC's core idea —
  [original paper](https://github.com/Raikiri/RadianceCascadesPaper).
- **Transmittance / transparency interval** `β_{a,b}`: does the segment [a, b] let light through? For opaque scenes it
  is 1 (nothing hit) or 0 (something hit). For volumes it is `exp(−∫σ)`.
- **Merging**: joining a near interval with a far one, like stacking a transparent sheet on a picture:
  `L_near + β_near · L_far` and `β = β_near · β_far`. It is the same formula as pre-multiplied alpha blending.
- **Cascade**: one grid of probes that stores intervals for one distance range. Cascade 0 (c0) is dense, with few
  directions and short intervals. Each higher cascade has sparser probes, more directions and longer, farther intervals.
- **Branching factor**: how many times more directions each cascade has than the one below. **Length scaling**: how many
  times longer each cascade's interval is.
- **Penumbra condition**: the rule that decides those factors. Close to an occluder you need fine *spatial* steps and
  only coarse *angular* steps. Far away you need the opposite.
- **Cone / pre-averaging**: a c_i direction stands for a cone of directions. Its value is the average of the 2 (2D) or 4
  (3D) c_{i+1} directions inside it. Pre-averaging stores that average straight away instead of the separate rays.
- **Ringing**: concentric bright and dark rings around small bright lights, caused by merging probes that sit at
  different positions (§2).
- **Bilinear fix**: the standard ringing fix. A c_i ray is traced towards each of the neighbouring c_{i+1} probes it
  interpolates from, instead of from its own centre (§2).
- **Flatland / 2.5D RC**: RC run on a 2D grid, for a 2D game or for the screen of a 3D game, where each probe only stores
  directions in the image plane. PoE2 uses this.
- **SPWI**: *Screen-space Probes with World-space Intervals*. Probes sit on the depth buffer, but their rays are traced
  through the real 3D scene with full 3D directions (term used by the Gavras thesis and ClaikoCode's DX12 repo).
- **Bilateral / depth-aware interpolation**: interpolation that lowers the weight of neighbours at a very different
  depth, so light does not bleed across silhouettes.
- **SDF** (signed distance field): a 3D texture or function giving the distance to the nearest surface. Lets a ray
  "sphere-trace" without a BVH. **Voxel DDA**: stepping a ray cell by cell through a voxel grid (Amanatides & Woo).
- **RayQuery TMin/TMax**: DXR 1.1 lets you trace only the part of a ray with `TMin ≤ t ≤ TMax`. An RC interval
  [t_i, t_{i+1}] maps straight onto that [I].
- **Morton order**: an ordering that keeps neighbouring items next to each other in memory. **R2 sequence**: a 2D
  low-discrepancy (evenly spread) point sequence by Martin Roberts.
- **Coupon collector problem**: you need about `n·ln n` random draws to hit each of n buckets at least once. That is why
  random ray directions leave high cascades (many buckets) with holes — [Split RC §5.1](https://arxiv.org/abs/2607.20384).

## 1. The original method (Sannikov 2023, "Radiance Cascades: A Novel Approach to Calculating Global Illumination")

### Takeaway
RC stores the radiance field as **several cascades of probes, each holding radiance intervals for one distance band**.
The key observation is the **penumbra condition**: to show a shadow cast by a light of size w correctly at distance D,
you need
- spatial step `Δp ≲ D` (probes can be farther apart for far-away content);
- angular step `Δω ≲ 1/D` (but need more directions).

So each cascade doubles its probe spacing and its interval range, and refines its angles.

In 2D, and in a full 3D grid, each cascade then costs about **half** of the one below it. All cascades together cost
**less than 2× cascade 0** but act like exponentially many rays per probe. For a probe-on-surface layout ("surface
radiance field", which is the screen-space case), every cascade costs the same, so the total grows linearly with the
cascade count.

Diffuse lighting comes from merging all cascades top-down into c0 and integrating c0's few wide cones. A rough specular
term can be read as a cone from a chosen cascade.

PoE2 ships a **screen-space, flatland-like** hierarchy filled by **screen-space raymarching** with a 128-bit occlusion
bitmask: about **3 ms on a GTX 1050**. The paper also describes, without shipping, a world-space 3D grid ("Radiance
3D", voxelised) and SPWI (screen probes, world-space SDF raymarch, 30–50 ms on an RTX 3060).

The paper is unpublished ("[WIP]", JCGT template, "never sent to any journal"). Its own analysis is asymptotic only.

### Cited Findings
**Status and core idea**
- The repo README: "I never sent it to any journal -- it uses the jcgt template, but was never published there." The
  PDF is at `out_latexmk2/RadianceCascades.pdf`. The licence image in the repo is CC-BY-ND — [repo](https://github.com/Raikiri/RadianceCascadesPaper)
- Abstract: "in order to resolve radiance emitted by an object, one needs to have higher linear resolution next to it,
  and higher angular resolution farther away from it". RC "allows to effectively remove the total number of rays as the
  primary constraint for calculating global illumination in 2d and 3d."
- Radiance fields "are built 'from scratch' every frame without reusing any data from the previous frame". They are
  "completely geometry-agnostic", with "constant cost that is independent of scene complexity, number of light sources
  or polygons".
- PoE2: "uses a flatland-like screenspace radiance cascade hierarchy and populates them using screenspace raymarching --
  a choice motivated by PoE2's fixed camera".

  — [RadianceCascades.tex](https://github.com/Raikiri/RadianceCascadesPaper/blob/main/RadianceCascades.tex)

**Penumbra condition and cascade scaling**
- For a light of size w at small distance d from an occluder edge, the penumbra has angle `α ≈ w/d`. Resolving it at
  distance D needs `Δp < αD` and `Δω < w/D`, i.e. "Δp <~ D, Δω <~ 1/D". This is "the penumbra condition".
- A cascade covering interval [a, b] needs `Δp <~ a` and `Δω <~ 1/b`. If probe spacing doubles per cascade
  (`Δp ~ 2^i`), then `t_i ~ 2^i` and `Δω ~ 1/2^i`: "if every cascade has twice greater probe linear stepping than the
  previous cascade, it also needs to have twice smaller angular stepping and it needs to cover twice longer radiance
  interval".
- Merging: `L_{a,c} = L_{a,b} + β_{a,b}·L_{b,c}` and `β_{a,c} = β_{a,b}·β_{b,c}`. Cascades can be merged back-to-front
  or front-to-back with identical results.
- "Extending" intervals: an interval [a, a+x] stored in a regular grid can be shifted by x along ω and merged with
  itself to get [a, a+2x]. "it is possible to avoid raymarching long paths and instead raymarch a tiny path and then
  keep iteratively doubling it". This works for "rectilinear grids… (so flatland and 3d world space cascades) and
  unfortunately not possible for 3d screenspace cascades where every probe has its own depth value."

  — [RadianceCascades.tex](https://github.com/Raikiri/RadianceCascadesPaper/blob/main/RadianceCascades.tex)

**Memory scaling per layout (the paper's §"Representing radiance cascades in memory")**
- **Flatland** (2D position + 1 angle): `M_i ~ M_0/2^i`, total "exactly equal to twice the amount of memory required to
  store cascade 0". Example atlas: c0 32×32 probes × 8×8 texels, c1 16×16 probes × 8×16 texels.
- **3D space** (5D field): probes `P_i ~ (1/2^i)³`, directions `Q_i ~ (2^i)²`, so `M_i ~ M_0/2^i` and the total is again
  `2·M_0`.
- **Surface radiance field in 3D** (2D surface or screen + 2D direction): `M_i = M_0` for every cascade, so the total is
  `N·M_0`. "just storing M_0 for full 3d is usually the limiting factor… whereas storing M_0 for a surface is usually
  perfectly acceptable." Example: c0 16×16 probes × 8×8, c1 8×8 × 16×16.
- Diffuse: merge cascades N−1..0 into c0 by "averaging 2 or 4 texels of cascade i+1". This introduces "only a small
  bias for diffuse lighting" because the BRDF×cosine term has "very low directional frequency".
- Specular: a cone whose angle is set by roughness. The narrowest cone is the directional step of the top cascade, the
  widest that of c0, and anything in between comes from mixing per-cascade queries with a merged cascade K.

  — [RadianceCascades.tex](https://github.com/Raikiri/RadianceCascadesPaper/blob/main/RadianceCascades.tex)

**"Infinite rays" claim**
- "adding one more cascade always costs half as much as adding the previous one… but effectively increases the number
  of approximated maximum-range radiance intervals twice": `rays(N) = 2^N·rays(0)`, `cost(N) < 2·cost(0)`.
- The caveat: the number of cascades "can not actually be greater than log(P_0)". The result holds only "if and only if
  the penumbra hypothesis holds". It breaks for "a point light that is just too small or… a penumbra that is too sharp,
  or" badly chosen `P_0`, `Q_0`, `t_0`.

  — [RadianceCascades.tex](https://github.com/Raikiri/RadianceCascadesPaper/blob/main/RadianceCascades.tex)

**Implementations described in the paper (with the only timings it gives)**
- **Radiant 2d**: "about 30ms on a GTX3060" [sic]. Figure: "calculate lighting for 2, 102 and 1002 particles in the same
  amount of time of about 12ms". It used **4× angular per cascade** instead of 2×, so "all cascades are encoded with the
  same amount of storage", and this "seemingly produced higher quality:cost ratio".
- **PoE2 GI**: "running in ~3ms on GTX1050". It "raymarches 4 rays simultaneously… and stores occlusion in 128-bit
  bitmask, that is equivalent to storing 128 binary occlusion rays per radiance cascade texel". It also shows diffuse +
  glossy specular from the same cascades.
- **Radiance 3d**: an "experimental testbed… 3d regular grid of radiance probes encoded in a volume texture, uses a 3d
  voxelization". It captures off-screen light. "storing a 3d grid associated with just cascade 0 comes with a very high
  cost… scale as the size of the scene cubed". With interval extension, "3d grids being acceptable in terms of
  computation time, but quite expensive in terms of storage". "a hierarchy of 3d radiance cascades should be basically a
  drop-in improvement for any implementation that uses a uniform grid of radiance probes."
- **Screen-space probes with world-space intervals**:
  - cascade-i probes hold octahedral maps of `~2^i × 2^i` texels and are placed `~2^i` pixels apart on the depth buffer;
  - interpolation is bilateral in space and bilinear in direction;
  - tracing is SDF raymarching "purely because of convenience", but "any acceleration structure can work, as long as it
    allows querying ray segments";
  - it uses temporal reprojection;
  - "Each frame takes about 30-50ms to calculate on RTX3060";
  - compared with PoE2's `~2^i × 1` probes, PoE2 "works 1 to 2 orders of magnitude faster".
- **Direct radiance field renderer**: a precomputed volume stored in surface cascades on one quad, rendered "in less than
  0.5ms on GTX3060".

  — [RadianceCascades.tex](https://github.com/Raikiri/RadianceCascadesPaper/blob/main/RadianceCascades.tex)

**Limitations stated by the author**
- "just storing a cascade 0 is practically equivalent to voxelizing the entire scene -- this is often a 'dealbreaker' for
  large-scale scenes."
- "calculating just cascade 0 in a high level of detail can take milliseconds".
- Parameters "dramatically affect both accuracy and performance… and can take some amount of fiddling to get right."
- PoE2's screen-space limits come from screen-space *tracing*, "**not** due to the fact that cascades themselves are
  stored in screenspace". With hardware RT, "the constant cost would be an order of magnitude higher compared to
  raymarching in screenspace."

  — [RadianceCascades.tex](https://github.com/Raikiri/RadianceCascadesPaper/blob/main/RadianceCascades.tex)

### Inferences
- [I] For EV the interesting sentence is the last one: SPWI with DXR is exactly "screen-space cascades + world-space
  intervals with hardware ray tracing". The author expected it to be about 10× the cost of PoE2's screen-space
  raymarching. The 2025 BTH measurements (§4, §8) show it is still real-time on an RTX 3080 at coarse settings:
  2.4–8.7 ms at 1080p with 4→2 px probe spacing.
- [I] RC's quality argument is *single-frame*, with no temporal accumulation. That is the opposite design choice to
  DDGI, surfels and SHaRC, which all integrate over frames. It makes RC's lag essentially zero, but every frame pays the
  full cost.

### Gaps
- No ExileCon 2023 slides or transcript were found. The PoE2 bitmask raymarcher is "outside of the scope" of the paper,
  so its exact settings (cascade count, c0 spacing, resolution) are unpublished.
- Community timings for PoE2 exist but could not be opened or verified:
  - a Beyond3D post (search snippet): "about 1.6ms at 1440p native on my RTX 3080" [U];
  - a Habr explainer: "~3 ms" with "128×128 textures" [U], with no source.

## 2. Merging artefacts and their fixes (ringing, bilinear fix, overlap, pre-averaging, depth-aware merging)

### Takeaway
Plain RC merging takes a c_i probe's interval and appends the *bilinearly interpolated* c_{i+1} interval for the same
direction. Because the c_{i+1} probes sit at different positions (parallax), the same bright source can be counted both
by the c_i interval and by the interpolated far interval. That shows up as **ringing**.

Osborne & Sannikov's **bilinear fix** traces the c_i interval from the probe to the start of each neighbouring
c_{i+1} probe's interval, merges each with its own neighbour, and then blends the four results with bilinear weights.
That removes the double count at **4× the rays in 2D** (and, by the same logic, 8× with trilinear neighbours in 3D [I]).

Other practical fixes:
- a small **interval overlap** against gaps;
- **pre-averaging**, which stores one averaged value per 4 child rays and cuts merge lookups 16 → 4 and memory by 75 %;
- a **direction-first** layout, useful for debugging and hardware filtering, but "NOT actually faster";
- for screen-space probes, **depth-aware merging** (bilateral weights, Sannikov's "Bilinear 3D", or the proposed min-max
  probes).

**Holographic RC** removes ringing and penumbra errors in 2D by using anisotropic probe grids. Its own authors say it
costs O(n⁴) memory in 3D, and it is not a 3D path.

### Cited Findings
**Ringing and the bilinear fix (Osborne & Sannikov 2024, arXiv 2408.14425, astrophysics radiative transfer, "DexRT")**
- Ringing "originates from a situation where both a probe in cascade i, and some of the bilinear probes sampled in
  cascade i+1, sample the same source". It is caused by "parallax between the different probes of cascade i+1 and
  cascade i, leading to non-conservation of energy."
- The fix traces each c_i interval "from its usual starting position to the starting position of the associated child
  cone" of each of the four bilinear neighbours. The four distinct intervals are "each merged with their associated
  samples from cascade i+1" and "then averaged using the usual bilinear weights". It "leads to a 4× increase in the
  number of rays that need to be traced on every cascade except" the first (wording garbled in the fetched HTML). It
  also helps with leaks through occluders that are too small.
- In 3D, "8 samples of cascade i+1 are therefore required for each sample of cascade i" (trilinear).
- The paper uses branching α = 2 in its examples, with scaling `Δs ∝ 2^i`, `Δω ∝ 1/2^{αi}`, `t_i ∝ 2^{αi}`.
- GPU vs CPU: on an RTX 4070 the GPU code is "80× faster" than the i7-13700 version.

  — [arXiv 2408.14425 (HTML v1)](https://arxiv.org/html/2408.14425v1)

**Practical 2D implementation guides**
- GM Shaders part 2 (Yaazarai and Xor, 2024-07-13):
  - each cascade is 4× longer, starts 4× farther and has 4× the rays, with probe spacing 2× per axis;
  - "the cascade surface stays the same size across cascades";
  - a minor interval overlap equal to the c_{N+1} probe spacing fixes leaks;
  - pre-averaging "reduces memory requirements from one pixel per ray to one pixel per four rays";
  - direction-first layout: "one block of memory represents a group of rays each cast from their respective probes".
    The author's correction: "the direction-first layout is NOT actually faster".

  — [GM Shaders: Radiance Cascades 2](https://mini.gmshaders.com/p/radiance-cascades2) (part 1: [GM Shaders: Radiance Cascades](https://mini.gmshaders.com/p/radiance-cascades), not read)
- Jason McGhee's interactive tutorial uses base ray counts 4 or 16 (`base^(i+1)` rays per cascade) and probe spacing ×√base
  per level. It turns on linear filtering on the upper cascade as "nearly free smoothing". A single bright dot shows
  "serious ringing artifacts". Its cost argument: "base 16" RC costs about two 16-ray passes, "but with 256 effective
  angular rays" — [jason.today/rc](https://jason.today/rc)
- mxcop's "Fundamentals of Radiance Cascades" (2024-10-22): ringing "comes from the disconnect between probes across
  cascades". With 4× branching "each cascade has the same interval count", and the author recommends 4× for simplicity —
  [m4xc.dev](https://m4xc.dev/articles/fundamental-rc/)
- Community fix shaders are listed on the community hub: a "Bilinear and forking fix" (MytinoDev) and a "Parallel fix"
  (mxcop), plus an interval calculator by Kornel — [radiance-cascades.com](https://radiance-cascades.com/). The
  Shadertoys themselves were not opened (Shadertoy returned HTTP 403).

**Depth-aware merging for screen-space probes (Gavras 2025 thesis, Chalmers)**
- Naive bilinear merging between probe layers at different depths gives outlier probes equal weight.
- Bilateral version: if `depthRange/avgDepth > 0.1`, the weights are multiplied by `1/(|Δdepth| + 0.0001)` and
  renormalised.
- "Alexander Sannikov came up with a better idea for depth aware upscaling which he showcased in shadertoy". "Bilinear
  3D" iteratively projects the target point onto interpolation lines between the 3D probe positions.
- Proposed future work, **min-max probes**: two probes per cell on a min/max depth pyramid, rays split between them.

  — [Gavras, "Exploration and Optimization of Radiance Cascades for Real-Time Applications" (PDF)](https://odr.chalmers.se/bitstreams/256da4eb-990c-4e51-aae1-c7878609e314/download)
- three-rc uses "A novel upscaler by Alexander Sannikov… for depth-aware interpolation, which cannot be licensed"
  (Shadertoy `4XXSWS`). Because of that, the repo ships without a licence — [three-rc README](https://github.com/CodyJasonBennett/three-rc)

**Holographic Radiance Cascades (Freeman, Sannikov, Margel, arXiv 2505.02041, 2025), 2D only**
- Standard RC "has noticeable artifacts manifesting as rings around light sources" (fixable with the bilinear fix) and
  "has errors when resolving small penumbras". The reason is that the cascade that resolves an occluder at distance r
  has spacing that grows with r, so a narrow penumbra falls between probes (their Fig. 5).
- HRC keeps full spatial resolution *perpendicular* to each direction quadrant and reduces it only along the direction.
  It merges 4 quadrants, and it replaces long ray traces by combining short intervals (its own "acceleration
  structure").
- Results on an **RTX 3080 Laptop**, 16-bit float RGB radiance + transmittance:
  - 256² 0.55 ms, **512² 1.85 ms**, **1024² 7.67 ms**, 2048² 33.9 ms;
  - constant cost for a given size;
  - at 1024², RMSE is 10× lower than naive path tracing at equal samples ("52spp");
  - 52 ray intervals per base probe at 1024², or 19 with the acceleration structure.
- Artefacts: checkerboard patterns (fixed with a 1-px cross blur) and moiré for "light sources… smaller than 8 times
  the base probe resolution".
- 3D: "poor scaling in 3 dimensions, taking up O(n⁴) memory for a n × n × n scene". There is an experimental 128³
  voxel version.

  — [arXiv 2505.02041 (PDF)](https://arxiv.org/pdf/2505.02041)

### Inferences
- [I] EV gets ringing for free in any naive implementation. The bilinear (trilinear) fix is therefore the first
  experiment worth A/B-ing: rays ×4 (screen-space probes, 4 neighbours) vs visible ringing around small emissive
  objects.
- [I] For SPWI probes on a depth buffer, the bilinear fix has to trace from the c_i probe to each c_{i+1} neighbour's
  *3D* position. That is a ray to a specific point plus the rest of the interval: one more RayQuery with a different
  origin and TMin/TMax. It is cheap to write and expensive to run.

### Gaps
- The Osborne & Sannikov HTML was read only up to ~100k of ~140k characters. No GPU timings for the bilinear fix in
  graphics were found.
- The MytinoDev / mxcop fix Shadertoys and Sannikov's "Bilinear 3D" Shadertoy could not be fetched (HTTP 403). Their
  descriptions come from secondary sources.

## 3. Worked numbers: how big are the cascades?

### Takeaway
These are [E] examples built from the published scaling rules. Use them for intuition, not as measurements.

| Setup | c0 | Per cascade | Cascades | Rays/frame (no bilinear fix) | Memory (RGBA16F = 8 B per interval) |
|---|---|---|---|---|---|
| A. 2D toy, 1024² canvas, spatial ×2 per axis, angular ×4, length ×4 | 512² probes (every 2 px) × 4 dirs | 1,048,576 intervals (constant) | 6 (t0 = 2 px → c5 ends at 2,730 px) | ≈ 6.3 M short marches | 8 MiB per cascade, 48 MiB all, 16 MiB if only 2 are live at once |
| B. 1080p SPWI, rays ×4, probes ½ per axis (Gavras layout) | 480×270 = 129,600 probes × 16 dirs (4×4 octa) | ≈ 2.07 M intervals ≈ 1 per pixel | 6 | ≈ 12.5 M RayQuery segments (≈ 50 M with the bilinear fix) | 15.8 MiB per cascade, ≈ 95 MiB all, ≈ 32 MiB ping-pong |
| C. Dense world 3D grid, 32×16×32 m, spacing ×2 and dirs ×4 per cascade | 0.25 m spacing → 128×64×128 = 1.05 M probes × 16 dirs | c_i = c0 / 2^i | 5–6 | ≈ 33 M if all traced every frame | c0 128 MiB, total ≤ 256 MiB. At 0.125 m: 1 GiB c0, ≈ 2 GiB total |
| D. Split RC (paper settings), sparse hash | 32 dirs per c0 probe (Θ0 = 4 → 8×4 lat-long), t0 ≈ 1.6·Δ0 | 32 / 128 / 512 / 2,048 dirs per probe | 4 | ≈ 2.07 M (1 per pixel at 1080p) | Not published. [E] ~65k c0 probes × 32 dirs ≈ 2.1 M dirs per cascade, × 4 cascades × ~20 B (fp32 RGB + β + count) ≈ 170 MB, plus a 6×6 (+border) irradiance map per probe |

### Cited Findings
- Gavras's 512×512 layout: c0 128×128 probes × 16 rays, c1 64×64 × 64, … c5 4×4 × 16,384, spacing 4 → 128 px.
  "six cascade levels seemed to provide the optimal balance". All scenes use "6 cascades, where each cascade has a ray
  per pixel, which would be equivalent to 6 samples per pixel (spp)".
- Intervals: `start = 0.016·4^(i−1)` (0 for c0), `end = 0.016·4^i` (scene units), "The factor of 4 used in the interval
  scaling matches the cascade branching factor".

  — [Gavras thesis](https://odr.chalmers.se/bitstreams/256da4eb-990c-4e51-aae1-c7878609e314/download)
- Split RC settings: "length scaling l = 4, branching factor b = 4, base number of directions |Ω0| = 32 (using Θ0 = 4),
  base ray length t0 ≈ 1.6Δ0, and N = 4 cascades". Directions use an equal-area latitude-longitude mapping with twice
  the resolution in φ. DDGI was given "an equal ray count to Split RC (1 frame); it has 1666 probes contributing to
  surfaces on-screen with 1245 rays each" — [Split RC (PDF)](https://arxiv.org/pdf/2607.20384)
- 3D grid scaling `M_i ~ M_0/2^i`, total `2·M_0` — [original paper](https://github.com/Raikiri/RadianceCascadesPaper/blob/main/RadianceCascades.tex)

### Inferences
- [I] Split RC's ray count is 1666 × 1245 ≈ 2.07 M = 1920×1080. So its timings are most likely 1080p with one bounce
  ray per pixel. The paper text does not state the resolution.
- [E] For comparison: DDGI 32×8×32 probes ≈ 30 MiB (sibling note), and kajiya ircache ≈ 75 MiB. Setup C shows why
  "just storing c0 is like voxelising the scene". It only fits EV's RTX 3070 (8 GB) at coarse spacing or as a small
  camera-centred clipmap.
- [E] Ray time for setup B, with two different direction-scaling choices:
  - **Rays ×4 per cascade** (Gavras): every cascade has ≈ 2.07 M rays, ≈ 12.5 M for 6. Split RC's trace stage ran
    ≈ 2.07 M rays in 1.3 ms (Sponza, ≈ 1.6 Grays/s) and 4.5 ms (San Miguel, ≈ 0.46 Grays/s) on an RTX 3080 Laptop. At
    those rates 12.5 M rays would cost ≈ 8–27 ms.
  - **Rays ×2 per cascade, probes ¼** (the BTH thesis's "2x/4x"): the ray count halves per cascade, so the total is
    ≈ 2 × c0 ≈ 4.1 M rays. The BTH thesis *measured* exactly this c0 (4 px spacing, 16 rays) at 1080p on a desktop
    RTX 3080: **2.36 ms and 34 MB** for all RC passes (§4, §8), i.e. roughly 2 Grays/s on short rays in Sponza [I].
  - So the direction-scaling choice changes the cost about 3×. Both are worth an A/B in EV.

## 4. 3D variants

### Takeaway
There are six families, ordered from cheapest to most general:

1. **Screen-space flatland RC (PoE2, "2.5D")**: probes on a 2D screen grid, directions in the image plane only, traced
   by screen-space raymarching or a visibility bitmask.
   - Fastest (≈ 3 ms on a GTX 1050).
   - Nothing off-screen, nothing behind the depth buffer.
   - Works best with a fixed top-down camera.
2. **SPWI (screen probes, world-space intervals)**: probes on the depth buffer, full sphere or hemisphere of directions,
   traced through the scene (SDF, BVH or hardware RT).
   - Captures off-screen emitters and occluders.
   - Memory is linear in screen size × cascade count.
   - The probes move with the camera, so it **flickers under motion**.
   - Published: **2.36–8.66 ms at 1080p on an RTX 3080 with DX12 hardware RT** (BTH 2025, 4→2 px spacing, 16 rays);
     30–50 ms (SDF, RTX 3060); 33–156 ms (M1 MacBook Air, 1080p).
3. **Dense world-space grid** ("Radiance 3D", tmpvar, jumeder): view-independent and usable for volumes, but memory
   scales with volume / spacing³ (§3 C).
4. **Surface / surfel RC** (mxcop SRC-DGI, "Love and Deepspace" GDC 2026, Mathis's texture-space Shadertoy): probes are
   attached to geometry (surfels or textures), which turns 3D into "surface RC" with per-cascade cost M_0.
5. **Split RC** (2026), the most complete 3D design:
   - sparse **per-cascade hash maps** of world-space probes spawned only near visible surfaces;
   - **LODs** by Chebyshev distance to the camera;
   - **ray splitting**: one ray per pixel from visible surfaces, cut into the cascade intervals it crosses and deposited
     (summed) into each cascade, instead of tracing every interval from every probe;
   - R2 low-discrepancy ray directions with a hierarchical prefix sum, so every cascade's directions get covered;
   - Morton-ordered merging with warp shuffles;
   - a 6×6 octahedral irradiance per probe for shading;
   - an optional temporal blend and a secondary cache for multibounce.

   **8.6 ms (Sponza) / 11.5 ms (San Miguel) single-frame on an RTX 3080 Laptop**, unoptimised (fp32). It beats DDGI
   on penumbrae and medium-scale AO. It misses detail below the c0 spacing, overblurs hard shadows and can leak from
   merge interpolation.
6. **Holographic RC in 3D**: experimental, O(n⁴) memory, not practical (§2).

No "frustum-aligned cascades" RC variant was found [U]. Spherical harmonics are not used in any RC implementation
read here. Probe direction storage is octahedral (Gavras, mxcop) or equal-area lat-long (Split RC, which found
octahedral and Clarberg *worse*).

### Cited Findings
**Split Radiance Cascades (Freeman, Sannikov, Pitt-Francis, arXiv 2607.20384, v2 2026-10-11)**
- Motivation:
  - "leading implementations of RC either run in 2D or screenspace, due to the prohibitive costs of storing high-detail
    volumetric radiance information";
  - in dense 3D, "each lower cascade becomes progressively more expensive, and so the total cost becomes dominated by
    the lower cascades… most of the probes in the lower cascades will be wasted".
- Related 3D work they list:
  - Mathis 2024 stores probes in 2D "using texture mapping";
  - "Surfel RC [Coppen 2025] uses surfels… however interpolation artifacts are noticeable";
  - "Love and Deepspace utilizes Radiance Cascades in 3D [Ruan 2026] to increase the efficiency of standard irradiance
    probes for use on mobile devices";
  - HRC in 3D "would require O(n⁴) memory".
- **Error argument for l = 4**: angular error at the end of a ray grows with ray length. With b = 4 and l = 4 the
  angular error scales like 2^i, matching the spatial spacing, so you can set "ε_ω = ε_p for all cascades". "Prior to
  this work, most RC implementations have actually aimed for constant ε_ω across cascades by setting l = 2, however in
  our experiments aiming for equal angular and spatial error using l = 4 reduces artifacts significantly" (Appendix A).
  Footnote: "Path of Exile 2's implementation actually uses l = 2, however this only functions due to tracing the depth
  buffer, the top-down camera view, and the vertical size/complexity of the scene not scaling with distance."
- With l = 4, "typically only 4 or 5 cascades are necessary to have rays spanning the entire scene (with the last
  cascade's rays sampling the environment map upon miss)".
- **Sparse RC** (Algorithm 1):
  - for each pixel, insert the nearest c0 probe;
  - for each c_{k−1} probe, insert the nearest c_k probe;
  - trace intervals, merge top-down, shade pixels.

  Probes use half-integer offsets per cascade. Sparse trilinear interpolation sums weight × value over existing probes
  only and renormalises. "We do not initialize all trilinear neighbors… avoiding it halves the amount of probes for
  little quality loss."
- **LODs**: "Each probe stores an additional coordinate indicating its LOD". Each higher LOD doubles Δ0 and t0. The LOD
  is the binary log of the "Chebyshev distance from the camera (it produces fewer artifacts than Euclidian)". The LOD
  seam overlaps by a factor 0.9 and is linearly blended.
- **Problem with probe-centred rays**: probes "recessed into surfaces" give "unnecessary darkening" (≈ 25 %
  over-occlusion in their Fig. 7).
- **Ray splitting**:
  - "we take inspiration from SHaRC: we do not need to spawn rays exactly from the probe positions; instead, we can spawn
    rays from surfaces on-screen";
  - a ray hitting at distance s with t_{k−1} < s ≤ t_k gives c_k "radiance L, transmittance 0" and all lower cascades
    "(0, 1)" (transparent);
  - higher cascades get nothing ("the latter option results in noticeable bias");
  - deposits are atomic sums plus a count per direction, divided at merge time, and zero-count directions are ignored.
- **Sampling**:
  - "by the coupon collector's problem… the amount of samples required to fill n buckets scales with O(n log n)";
  - the fix is "the R2 low-discrepancy sequence", with "probes that share the same nearest next-cascade probe" assigned
    "to contiguous segments", via a "hierarchical prefix-sum" (Algorithm 3);
  - the sequence is jittered per frame, and inward-pointing directions are flipped.
- **Temporal**: "we can blend the previous frame's cascades with the new intervals". It runs fully in world space, so
  there is no reprojection, but "we leave handling movable objects to future work". Benchmarks assume every frame casts
  all rays.
- **Implementation**:
  - Rust + LuisaCompute over CUDA, "Ray tracing is handled by OptiX";
  - one hash map per cascade, with a 64-bit key "18 bits for each coordinate axis and 10 for the LOD", lock-free linear
    probing, and values that are an index into an indirection buffer. "we recreate our hashmaps every frame", so no
    deletion is needed;
  - Morton-ordered direction data, coherent reads and "warp-wide shuffles" in merging, plus **pre-averaging**;
  - one block per probe, with the parent probes in shared memory;
  - shading: a "6x6 octahedral-mapped irradiance field" plus a 1-texel border, so at most "8 filtered texture samples
    per pixel". Naive gathering would be "up to 256 unfiltered texture samples per pixel" for 32 c0 directions.
- **Multibounce**: probes are spawned at previous-frame ray hit points into a "secondary cache" that samples itself for
  "infinite" bounces. Secondary probes are "two levels of detail higher".
- **Table 2** (RTX 3080 Laptop, single frame, ms; excludes the G-buffer and shadow rays):

  | Stage | Sponza | San Miguel |
  |---|---|---|
  | Initialize probes | 1.2 | 1.3 |
  | Generate rays | 0.4 | 0.6 |
  | Trace rays | 1.3 | 4.5 |
  | Deposit | 2.2 | 1.9 |
  | Merge | 1.0 | 0.8 |
  | Shade pixels | 2.5 | 2.4 |
  | **Total** | **8.6** | **11.5** |

  Fig. 1 (San Miguel, another view): "13.2ms, of which 6.1ms consists of 1spp raytracing". "currently using 32-bit floats
  to store radiance", and many "micro-optimizations" are missing.
- **Quality**:
  - "can capture soft penumbrae particularly well";
  - "medium-scale ambient occlusion", where "DDGI does not show any ambient occlusion" (DDGI grid 32×16×32 in their
    comparison);
  - "misses details smaller than the base probe spacing", "light leaking artifacts from interpolation during merging",
    and "overblurring when trying to resolve hard shadows".
  - Ten frames of accumulation give little extra, "as most remaining error is from interpolation".
- **Extensions**:
  - rough specular by reading the cascade whose cone width matches the lobe, but "we cannot use RC for precise effects
    such as reflections";
  - "C(−1) merging" with a per-pixel screen-space raymarch (visibility-bitmask style) recovers sub-probe detail, but
    the unoptimised directional gather adds 23 ms;
  - "Split RC as a denoiser" vs OIDN on the same 1-spp rays: Split RC reconstructs undersampled detail better
    ("somewhat unfair comparison").
- **Future work**:
  - fixed ray budgets per probe (pixel spawning gives "up to a 4x ray quantity difference" within one LOD);
  - MSME-style adaptive temporal accumulation;
  - "it may be possible to represent Split RC entirely as a denoiser without hashing or additional storage… a drop-in
    replacement for existing denoisers over low sample count path tracers".

  — [arXiv 2607.20384 (PDF)](https://arxiv.org/pdf/2607.20384), [abstract page](https://arxiv.org/abs/2607.20384)
- Reference code: **entropylost/chandra**, "an implementation of Split Radiance Cascades". It is Rust, with modules
  `rc.rs`, `trace.rs`, `splat.rs`, `cache.rs`, `table.rs`, a DDGI comparison (`ddgi.rs`), a RIS experiment and OIDN
  bindings. GitHub shows **no licence**; last push 2026-11-18 — [repo](https://github.com/entropylost/chandra) (GitHub API 2026-11-22)

**SPWI implementations (screen probes, world-space intervals)**
- **Gavras (Chalmers 2025)**:
  - ray tracing instead of raymarching, and Sannikov's "Bilinear 3D" upscaling;
  - M1 MacBook Air (8 GB unified), 1920×1080, 6 cascades: 32.8 / 34.4 / 37.1 / 39.9 ms (simple cube and bunny scenes),
    **143.7 ms** (Sponza, sky) and **155.9 ms** (Sponza + emissive hornbugs);
  - 147–408.5 MiB total frame memory;
  - "performance is primarily bounded by acceleration structure traversal";
  - "screen-space placement of probes introduces temporal instability during camera movement, creating flickering
    artifacts".

  — [thesis PDF](https://odr.chalmers.se/bitstreams/256da4eb-990c-4e51-aae1-c7878609e314/download)
- **ClaikoCode/DX12RadianceCascades** (MIT): built on Microsoft MiniEngine, with "hardware accelerated raytracing
  implementation of RC… Uses Screen-space Probes with World-space cascade Intervals (SPWI)". The highest cascade samples
  an HDR skybox. It also has a DXC-DLL shader compiler with hot reload, ImGui and a GPU profiler with VRAM accounting.
  It uses an RTPSO (DispatchRays), not inline RayQuery — [repo](https://github.com/ClaikoCode/DX12RadianceCascades)
- **The thesis behind it**: Jonathan Dell'Ova, "Analyzing the Performance Scaling of Radiance Cascades" (BTH 2025).
  This is **the most EV-like measurement found: DX12 + hardware RT on an RTX 3080 10 GB**.
  - Setup: 48 configurations at 1920×1080 and 2560×1440, each run 30 times, Sponza (262k triangles), pre-averaging to
    cut VRAM 4×, cascade scaling "2x/4x" (2× rays per probe, ¼ probes per cascade), ray length 5.0;
    `fr = albedo` with the cosine term set to 1 (simplified).
  - "The RC Gathering pass (ray tracing pass) consumed 87-96% of frame time". "At 1080p, 79.2% of configurations
    achieved frame times below 33.33 ms, with 62.5% falling under 16.67 ms when measuring only RC-specific passes."
    Probe spacing has "the strongest performance impact, suggesting a super-linear scaling", ray count scales
    sub-linearly, and resolution scales with the pixel ratio.
  - Table 5.3 (1080p, max 8 cascades, RC passes only; VRAM MB / ms):

    | Spacing (px) | 16 rays | 36 rays | 64 rays |
    |---|---|---|---|
    | 4 | 34 / 2.36 | 72 / 4.97 | 125 / 8.59 |
    | 3 | 58 / 3.95 | 129 / 8.55 | 222 / 14.96 |
    | 2 | 133 / 8.66 | 286 / 19.16 | 499 / 33.81 |
    | 1 | 519 / 34.04 | 1,159 / 76.19 | 2,019 / 134.82 |

  - With 5 cascades at 1080p, R16-S1 (spacing 1 px, 16 rays) and R64-S2 (spacing 2 px, 64 rays) trace equal total rays
    and cost the same: 335.6 MB / 24.74 ms vs 323.4 MB / 24.78 ms.
  - Not covered: visual evaluation ("future work is left for: visual evaluation").

  — [BTH thesis PDF (diva-portal)](https://www.diva-portal.org/smash/get/diva2:2007296/FULLTEXT01.pdf)
- **three-rc**: "Radiance Cascades GI in 3D using screen-space probes and a BVH" (three-mesh-bvh). "Probe storage is
  fixed-cost and stored in a screen-sized texture". It notes that "min/max probes can be added without much
  performance penalty to stabilize probes during camera motion" — [repo](https://github.com/CodyJasonBennett/three-rc)
- **alexmalyutindev/unity-urp-radiance-cascades** (MIT): screen-space RC in Unity URP, "an attempt to generalize it for
  3D case", direction-first layout — [repo](https://github.com/alexmalyutindev/unity-urp-radiance-cascades)

**Screen-space RC in a shipped-style game context**
- Abou Dan & Jensen (Chalmers 2025), for the game *As We Descend*: RC + visibility-bitmask GI with screen-space probes,
  "2 ms (low), 6 ms (medium), and 26 ms (high), using 0.03 GiB to 3.9 GiB". Limits: no off-screen contribution,
  "background over-brightening", upscaling artefacts, flicker during small movements. GPU not stated in the abstract —
  [Chalmers record](https://odr.chalmers.se/items/9e42007a-bb60-440f-ba28-04410822758f/full)
- DiGRA 2026 (Gerassimov): Unity URP on an **RTX 4060 Laptop**. In an isometric scene, RC and realtime lightmaps ran
  ≈ 220 FPS (RC 5–10 FPS faster) vs SSGI ≈ 105 FPS. The RC variant is not clear from the text read [U] —
  [DiGRA paper](https://dl.digra.org/index.php/dl/article/view/2775)

**Dense world-space grids, surfel RC, mobile**
- tmpvar/radiance-cascades-3d-grid: "rough/unoptimized implementation of Radiance Cascades using world space dense
  grids" (C++/OpenGL, not runnable) — [repo](https://github.com/tmpvar/radiance-cascades-3d-grid)
- jumeder/dx12-radiance-cascades (MIT): "Coarse implementation of Radiance Cascades in 3D", DX12, CMake + vcpkg —
  [repo](https://github.com/jumeder/dx12-radiance-cascades)
- **mxcop/src-dgi** (MIT, archived): "Surfel Radiance Cascades Diffuse Global Illumination", written "while studying at
  BUAS". Vulkan + Slang, with surfel maintenance (spawn and recycle) feeding gather → merge → composite.
  `cascade.slang`:
  - `SPATIAL_FACTOR = 4`, `ANGULAR_FACTOR = 4`;
  - probe capacity `c0_probe_capacity / 4^i`, intervals per probe `c0_count · 4^i`;
  - interval `[4^i, 4^{i+1}] · base` with `base = max_solid_angle · c0_count / 4π`;
  - merge `near.rgb + near.w · far.rgb`.

  — [repo](https://github.com/mxcop/src-dgi), [cascade.slang](https://github.com/mxcop/src-dgi/blob/main/assets/shaders/surfels/cascade.slang), [surfel maintenance post](https://m4xc.dev/articles/surfel-maintenance/)
- "Love and Deepspace" (Infold Games, GDC 2026, Tianlong Ruan, presented by Arm). From a search summary of the session
  abstract (the page returned HTTP 403): a real-time GI "built on surfels and 3D Radiance Cascades" for mobile, with
  "surfel BVH and a hash cache", and surfels pre-generated at asset import (secondary) — [GDC Vault entry](https://gdcvault.com/play/1035619/The-Evolution-of-Real-Time)
- Chogan-Studios/RadianceCascade (O3DE gem) advertises a "sparse clipmap probe grid", "adaptive temporal accumulation"
  and an optional hardware-RT path. Its README status is only a compiling skeleton and debug shader, so it is not a
  reference yet — [repo](https://github.com/Chogan-Studios/RadianceCascade)
- Community hub (papers, explainers, fix Shadertoys, LegitScript editor, Discord) — [radiance-cascades.com](https://radiance-cascades.com/)

### Inferences
- [I] **Split RC is the RC variant closest to what EV already plans.**
  - Its probe store is a SHaRC-like hash map (M9).
  - Its rays are "one ray per pixel from visible surfaces", which is the bounce ray of M8b's real-time mode.
  - Its probe irradiance is a 6×6 octahedral map like DDGI (M9b).
  - It was benchmarked against DDGI and OIDN, both of which EV will have.
- [I] The difference between Split RC and SHaRC is *what a hash cell stores*:
  - SHaRC stores one radiance value per voxel (outgoing radiance at a surface);
  - Split RC stores, per probe and per cascade, **incoming radiance per direction for one distance band** plus
    transmittance, and merges bands into wide cones before shading.

  That is how Split RC keeps penumbrae and AO that a voxel cache blurs.
- [I] Screen-space RC (flatland) is not a good EV target. EV has a free camera and an ocean horizon, and PoE2's l = 2
  trick depends on a top-down view (Split RC footnote).

### Gaps
- No public numbers for Love and Deepspace's mobile RC. The BTH thesis reports cost only (no image-quality metrics).
- Split RC's memory footprint and resolution are not stated in the paper text read. Resolution is inferred in §3.
- The Mathis 2024 Shadertoy (`X3XfRM`) could not be opened (HTTP 403).

## 5. How 3D cascades trace rays (and what is natural for EV)

### Takeaway
RC only needs "the first hit within [t_a, t_b] along a ray, and what is its outgoing radiance", plus a hit/no-hit flag.
Every tracing back-end works:
- **screen-space raymarch / bitmask** (PoE2, fastest, screen-limited);
- **SDF raymarch** (Sannikov's SPWI demo, Brixelizer-style);
- **voxel DDA plus interval extension** (Radiance 3D, HRC);
- **hardware RT** (Split RC with OptiX, Gavras with RT, ClaikoCode with DXR).

For EV, **DXR 1.1 inline RayQuery in a compute shader** is the natural choice:
- `RayDesc.TMin = t_i`, `TMax = t_{i+1}` is exactly an interval;
- the hit shading (direct light + emission + optional cache) is the same code the path tracer already uses;
- the BLAS/TLAS exist from M1.

Interval extension (cheap long intervals from short ones) only applies to regular grids, which matters only for a
dense-grid experiment.

### Cited Findings
- Interval sources: "any acceleration structure can work, as long as it allows querying ray segments" — [original paper](https://github.com/Raikiri/RadianceCascadesPaper/blob/main/RadianceCascades.tex)
- Interval extension is only for rectilinear grids, not for screen-space probes with per-probe depth — [original paper](https://github.com/Raikiri/RadianceCascadesPaper/blob/main/RadianceCascades.tex)
- Split RC: "Ray tracing is handled by OptiX" — [Split RC](https://arxiv.org/pdf/2607.20384)
- SPWI with hardware RT is "primarily bounded by acceleration structure traversal" — [Gavras](https://odr.chalmers.se/bitstreams/256da4eb-990c-4e51-aae1-c7878609e314/download)
- HRC traces its base intervals with 2D DDA (Amanatides & Woo) and builds longer ones by combining — [HRC](https://arxiv.org/pdf/2505.02041)
- Brixelizer GI replaces hardware RT with SDF tracing (not a fit for EV) — `followup_gi_world_caches.md` §4

### Inferences
- [I] RayQuery mapping:
  1. one thread per (probe, direction) for probe-centred RC, or one per pixel for Split RC;
  2. a `RayQuery<RAY_FLAG_NONE>` with `TMin = t_i`, `TMax = t_{i+1}` (`t_0` > 0 to skip the probe's own surface);
  3. `Proceed()` loop, then `CommittedStatus()`;
  4. hit → `L = Le + direct(hit)` (+ last frame's cache for multibounce), `β = 0`; miss → `L = 0`, `β = 1`; the last
     cascade samples the sky on a miss.
- [I] Alpha-tested geometry needs the same any-hit handling as the path tracer. Opaque-only is enough to start.
- [I] RDNA 4 notes (from `followup_gaps_completion.md`): inline RayQuery keeps the BVH stack in hardware, and "1
  RayQuery in scope" matters. RC threads do a single short query, which is a good fit.

## 6. "Spatium Radiance Cascades" in "Black Engine"

### Takeaway
**Found, but only as YouTube demos. There is no paper, write-up, source code or technical description.**
- **Who**: "Black Engine" is a playlist on the YouTube channel **Blackline Interactive** (handle
  `@BlacklineInteractive`; links to X `@demonv559` and GitHub `BlackLineInteractive`).
- **What "Spatium" is**: the name appears in exactly one video description, "My Spatium Radiance Cascades Global
  Illumination - Light Leaking Test Demonstration" (2026-10-20). The other RC videos (2026-08-29 → 2026-10-20) describe
  "Real-time Global Illumination demo based on the Radiance Cascades approach originally formulated by Alexander
  Sannikov - but with my own custom 3D architecture", "fully world-probe-based… not screen-space", "7 active cascades
  with 4–5 diffuse bounces", "0ms temporal latency (no temporal accumulation / ghosting)", "No TAA, SSGI, DDGI, or
  denoising", "zero light leaks".
- **Hardware and numbers**: an AMD Radeon Pro 5500M laptop GPU (MacBook Pro 16" 2019, Metal; other descriptions also
  name Vulkan and OpenGL backends). NVIDIA Bistro (~2 M triangles) at 720p–1080p, "Stable 60 FPS (GPU time ~7.4–9.9 ms)".
  Sponza 4K textures (~5.7 M tris) at 720p, 50–60 FPS.
- **Openness**: "All code in this project is written by me and has never been publicly released." The author's GitHub
  has no RC repository.
- **What it changes vs plain RC**: not documented. "Spatium" (Latin for "space") plus the "world-probe-based" wording
  suggests a world-space probe variant [I], but how it stores, traces or merges is unknown. The claims ("zero light
  leaks", "Near Path Tracing quality") are self-reported and unverified.

### Cited Findings
- Video metadata from the channel's public playlist RSS feed and video pages (titles, dates, descriptions; the videos
  were not watched) — [Black Engine playlist](https://www.youtube.com/playlist?list=PLN2aU9DIlwzfKE8f-4tUWdiesvL58XNJZ), [RSS](https://www.youtube.com/feeds/videos.xml?playlist_id=PLN2aU9DIlwzfKE8f-4tUWdiesvL58XNJZ)
  - "New Fast Global Illumination without TAA" (2026-10-20, ~11k views): "My Spatium Radiance Cascades Global
    Illumination - Light Leaking Test Demonstration… Without TAA, SSGI, DDGI, Voxels, or other. Testing is performed at
    Full HD / 2K on a 16-inch MacBook Pro (2019) with an AMD Radeon Pro 5500M GPU (50W)." — [video](https://www.youtube.com/watch?v=awam874hcxI)
  - "New fast Global Illumination" (2026-10-03, ~68k views): "Real-Time Radiance Cascades Global Illumination 3D…
    These aren't lightmaps… THIS IS NOT a game-ready solution… FXAA 3… Everything runs natively without upscaling. All
    code in this project is written by me and has never been publicly released." — [video](https://www.youtube.com/watch?v=TTI1QoHlCHE)
  - "Real-time Global Illumination on Radiance Cascades" (2026-08-29): "fully world-probe-based global illumination
    technique, not screen-space illumination (SSGI)… 7 active cascades with 4–5 diffuse bounces… NVIDIA Bistro Scene: ~2
    million triangles | 720p–1080p | Stable 60 FPS (GPU time ~7.4–9.9 ms)… Sponza Scene: 4K textures | ~5.7 million
    triangles | 720p | 50–60 FPS" — [video](https://www.youtube.com/watch?v=O3C3aoVbTIk)
  - "Radiance Cascades Global Illumination - without TAA, SSGI, DDGI..." (2026-09-12): "my own custom 3D architecture…
    zero light leaks, and stable performance on older GPU hardware… 0ms temporal latency… Near Path Tracing quality in
    real time… AMD Radeon Pro 5500M - 8GB VRAM (Metal API)" — [video](https://www.youtube.com/watch?v=-fBe0ZIu39Q)
  - "Radiance Cascades 3D GI glass demo" (2026-09-05): "Cascaded irradiance caching for indirect diffuse lighting… BVH ray
    tracing… Apple Metal, Vulkan 1.2+, OpenGL 4.3+ Core… ~50-60 FPS at around 2K resolution on an AMD Radeon Pro 5500M…
    experimental research demo / proof of concept" — [video](https://www.youtube.com/watch?v=fWnQvWBIZTs)
  - The playlist also has "BlackHair system" videos: "a real-time realistic hair system with Black Engine". That is what
    ties the playlist name to an engine — [video](https://www.youtube.com/watch?v=go3m8M33KDA)
- The GitHub account `BlackLineInteractive` (created 2025-08-11) has game-port/recompilation repos and a "Hybrid CPU+GPU
  Raytracer OpenGL", but **no** RC or engine repo (GitHub API 2026-11-22) — [GitHub](https://github.com/BlackLineInteractive)
- Not to be confused with Pearl Abyss's **BlackSpace Engine** (Crimson Desert), which a search also surfaced.

### Inferences
- [T] The Radeon Pro 5500M is an RDNA 1 (Navi 14) GPU without hardware ray-tracing units. So "BVH ray tracing" in these
  demos must be a compute/shader BVH traversal. That makes the 7.4–9.9 ms Bistro figure notable if true [I]. It also
  means none of it maps directly to EV's RayQuery path [I].
- [I] "No temporal accumulation" with "4–5 diffuse bounces" implies either feedback from the previous frame (which *is*
  a form of temporal reuse) or a multi-pass bounce per frame. The descriptions don't say which.

### What was searched (for the record)
- Web: "Spatium" + radiance cascades + "Black Engine"; "Black Engine" game engine GI; "Blackline Interactive" + RC +
  Spatium; "Spatium" GI YouTube.
- YouTube oEmbed, the video pages and the playlist RSS.
- GitHub user repos (API).
- The radiance-cascades.com resource list (no mention).
- arXiv (only 3 papers mention RC in their abstracts: Osborne & Sannikov 2024, HRC 2025, Split RC 2026; none mention
  Spatium).

## 7. Artefacts, limits, and how RC relates to EV's path tracer and spectral storage

### Takeaway
| Issue | Screen-space / SPWI RC | Split RC (world, sparse) | Dense 3D RC grid |
|---|---|---|---|
| Light leaking | Bilateral or "Bilinear 3D" merge needed. Flatland RC leaks behind the depth buffer | Some, from merge interpolation. Misses occluders < c0 spacing | Like any probe grid between probes (no Chebyshev test) [I] |
| Ringing | Yes without the bilinear fix | Reported issues are blur and leaks rather than rings [I] | Yes without the trilinear fix (8 neighbours) |
| Memory | Linear in pixels × cascades (≈ 16 MiB per cascade at 1080p, §3 B) | Sparse; not published (fp32 now) | 2·M0, scales with volume/spacing³ |
| View dependence | Strong. Probes on the depth buffer flicker under camera motion | Probes near visible surfaces, LOD by camera distance. Off-screen only via the secondary cache | None (view-independent) |
| Dynamic scenes and lag | Single-shot, so no lag | Single-frame mode has no lag. Temporal mode ignores moving objects (future work) | Single-shot, or amortised (then lag) |
| Specular | Rough glossy via cones. PoE2 shows glossy | Rough specular demo. "cannot use RC for precise effects such as reflections" | Same |
| Temporal stability | Flicker (Gavras, Abou Dan) | Stable in world space. Per-frame jitter of R2 directions [I] | Stable |
| Small lights / sharp shadows | Fails when the penumbra hypothesis breaks; overblur | Overblurs hard shadows | Same |

**Relation to the path tracer** [I]:
1. **Hybrid diffuse GI for M8b**, the natural role. RC replaces the bounce-1 diffuse path; specular stays traced.
2. **Path terminator** (like SHaRC/DDGI in M9/M9b). Only world-space RC can do this, and Split RC only where probes
   exist, i.e. near visible surfaces plus its secondary cache at previous-frame hit points.
3. **"Denoiser" for the M8b bounce rays**. Split RC's own framing: deposit the path tracer's one-ray-per-pixel diffuse
   bounce into the cascades instead of feeding it to SVGF/NRD. The catch is that ray splitting needs Split RC's evenly
   spread directions (R2, hierarchical offsets), not cosine/BSDF-sampled ones. EV's diffuse bounce would then have to
   use that direction generator and weight by cosine at shading.

**Spectral storage** [I]: the sibling note's conclusion holds for RC.
- Store **linear RGB (or XYZ) radiance** per interval and project the hit's spectral radiance at the cache boundary.
- Opaque-scene **transmittance is a single scalar** (hit or no hit), with no wavelength dependence at all.
- RC adds one wrinkle: in the *volumetric* form (water), transmittance `exp(−∫σ)` becomes wavelength-dependent. Storing
  β per RGB channel (HRC stores "3 vectors of 16 bit floats" for radiance and transmittance) is only approximate for
  spectral absorption [I]. EV should keep RC to opaque surfaces and let the path tracer handle the water medium.

### Cited Findings
- Split RC limitations: "As RC captures the radiance field up to a certain frequency, it is incapable of resolving
  certain phenomena such as sharp mirror reflections… is inherently biased due to use of interpolated probes" — [Split RC](https://arxiv.org/pdf/2607.20384)
- Split RC is limited to one bounce per frame, with multibounce through the secondary cache: "We do not initialize probes
  that require three bounces currently." — [Split RC](https://arxiv.org/pdf/2607.20384)
- SPWI flicker: "temporal instability during camera movement, creating flickering artifacts" — [Gavras](https://odr.chalmers.se/bitstreams/256da4eb-990c-4e51-aae1-c7878609e314/download).
  Screen-space RC "flickers during small movements" — [Abou Dan & Jensen](https://odr.chalmers.se/items/9e42007a-bb60-440f-ba28-04410822758f/full)
- Holographic RC: "For simplicity, we will not concern ourselves with multiple wavelengths of light, as they are
  typically independent". It stores radiance and transmittance as "3 vectors of 16 bit floats" — [HRC](https://arxiv.org/pdf/2505.02041)
- Original paper: RC "degrade[s] in a very interesting way where normally sharp shadows become blurrier as if they were
  cast by an area light source" — [original paper](https://github.com/Raikiri/RadianceCascadesPaper/blob/main/RadianceCascades.tex)
- RGB/XYZ-at-boundary and "coefficients must not be averaged" reasoning — `followup_gi_world_caches.md` §7

### Inferences
- [I] RC hit shading in EV:
  1. evaluate the hit's emission + direct light at the 4 hero wavelengths of a per-ray (or per-probe) random hero
     wavelength, with the same NEE code as the path tracer;
  2. project to XYZ with the CMFs (`Σ x̄(λ_i)·L(λ_i)/p(λ_i)/4`, …) → linear RGB;
  3. deposit or store RGB.

  Since RC merges and averages many rays per cone, the wavelength noise averages out like any other sample noise.
  Reading back for a path vertex is the sibling note's `RGBIlluminantSpectrum` upsample.
- [I] Because RC is linear in radiance (merge = add + scalar multiply by β, cones = averages), RGB, XYZ, spectral bins
  and Fourier moments all work. Bins or moments only cost memory (8/3× or m/3×), as in the sibling note. RC's interval
  storage is already large (§3), so RGB is strongly preferred.
- [I] As a terminator, RC (like DDGI and surfels) returns diffuse irradiance, so the bias on glossy vertices is the same
  as in the sibling note. Split RC's directional cones could do rough glossy at extra cost.

## 8. Published timings and memory, and the difficulty for EV

### Takeaway
No RC timings exist on an RX 9070 XT or RTX 3070. The closest are below; all EV numbers will be new.

| Variant | Source | GPU | Resolution / scene | Time | Memory |
|---|---|---|---|---|---|
| Flatland screen-space (PoE2) | Sannikov paper | GTX 1050 | PoE2 scene | ~3 ms | n/p |
| Radiant 2d | Sannikov paper | "GTX3060" | 2D | ~12–30 ms | n/p |
| SPWI, SDF raymarch | Sannikov paper | RTX 3060 | SDF scenes | 30–50 ms/frame, + 0.1–0.5 s to converge | n/p |
| SPWI, HW RT | Gavras 2025 | Apple M1 (MacBook Air) | 1080p, 6 cascades | 32.8–155.9 ms | 147–408.5 MiB (whole frame) |
| **SPWI, DX12 HW RT** | Dell'Ova 2025 (BTH) | **RTX 3080 10 GB** | 1080p Sponza, max 8 cascades | **2.36 ms** (4 px, 16 rays) … 8.66 ms (2 px, 16 rays) … 134.8 ms (1 px, 64 rays) | 34 MB … 133 MB … 2,019 MB |
| Screen-space RC + VBGI | Abou Dan & Jensen 2025 | n/p | game (As We Descend) | 2 / 6 / 26 ms (low/med/high) | 0.03–3.9 GiB |
| Holographic RC (2D) | Freeman et al. 2025 | RTX 3080 Laptop | 512² / 1024² | 1.85 / 7.67 ms | O(n²) in 2D |
| Split RC, single frame | Freeman et al. 2026 | RTX 3080 Laptop | ≈ 1080p [I], Sponza / San Miguel | 8.6 / 11.5 ms (13.2 ms Fig. 1) | n/p (fp32) |
| "Spatium" RC (Black Engine) | YouTube description | Radeon Pro 5500M (no HW RT) | Bistro 720p–1080p | 7.4–9.9 ms (self-reported) [U] | n/p |
| Unity URP RC | DiGRA 2026 | RTX 4060 Laptop | isometric scene | ≈ 220 FPS whole frame | n/p |

(n/p = not published)

Difficulty in HLSL with DXR 1.1 inline RayQuery in compute [E]:
- **2D toy**: 3–5 days. Pure compute, no DXR.
- **SPWI in M8b**: 2–4 weeks.
- **Split RC**: 5–8 weeks. The hash maps, the hierarchical R2 ray generator and atomic deposits are the hard parts.
- **Dense-grid RC on top of M9b's DDGI grid**: 2–3 weeks.

### Cited Findings
- All rows: see the citations in §1–§6.

### Inferences
- [E] GPU scaling, as an order of magnitude only:
  - a desktop RTX 3070 is in the same class as an RTX 3080 Laptop (similar GA104 die [T]), so Split RC's 8.6–11.5 ms
    is a fair first expectation on the 3070;
  - the BTH SPWI numbers are from a desktop RTX 3080 (GA102), so expect the 3070 to be somewhat slower than that table
    [T][E]. The RX 9070 XT has
  stronger RT hardware than both [T], but EV's numbers depend on its own hit shading and BVH quality.
- [I] HLSL-specific work items vs the CUDA/OptiX reference:
  - **atomic float adds** for deposits. Core HLSL has no float `InterlockedAdd` on buffers [T][U], so use
    integer-quantised atomics as Capsaicin does (`followup_gi_world_caches.md` §4), or per-probe gathering;
  - **warp shuffles** → `WaveReadLaneAt` / `WaveActiveSum`. RDNA can run wave64, so check `WaveGetLaneCount()` and do
    not hard-code 32 [T];
  - **64-bit hash keys** need SM 6.6 64-bit integer atomics, or a 2×32-bit key with a CAS loop [T][U];
  - **8 GB on the 3070**: budget for fp16 storage (the paper uses fp32).

## 9. Options for EV

### Takeaway
Proposed ladder (milestone letters are suggestions, not decisions):
1. **RC-0, the 2D toy (any time, even before M1)**: flatland RC in one compute shader on a paintable 2D texture.
   - Covers: intervals, merging, the branching/length factors (l = 2 vs 4), pre-averaging, ringing, and the bilinear
     fix on/off.
   - Needs no DXR, no G-buffer, no path tracer. It is the cheapest way to *understand* RC.
2. **M8c: SPWI screen-space RC as hybrid diffuse GI (after M8b)**.
   - Probes on the M8b depth buffer, intervals traced with RayQuery TMin/TMax, depth-aware merging (bilateral →
     Bilinear 3D), sky in the top cascade.
   - First A/B against M8b's 1-spp + denoiser diffuse bounce at equal time.
3. **M9r: Split RC as a world-space sparse cache (after M9 SHaRC, ideally after M9b DDGI)**.
   - Reuses SHaRC's hashing, DDGI's 6×6 octahedral irradiance and M8b's per-pixel bounce rays.
   - Mirrors the paper's own comparison (Split RC vs DDGI vs OIDN).
   - Also usable as a terminator through its secondary cache.
4. **Optional M9r-b: dense RC cascades on the M9b DDGI grid**. Tests Sannikov's "drop-in improvement for any uniform
   probe grid" claim against Split RC's argument that dense lower cascades waste memory.

All of them go into the **M9x bake-off** before M10 (NRC).

### Inferences (proposal)

**RC-0: 2D toy** [E: 3–5 days]
- Build:
  - 1024² canvas (emissive + occluder paint);
  - c0 every 2 px with 4 directions, spatial ×2, angular ×4, length ×4, 6 cascades (§3 A);
  - one compute dispatch per cascade, top-down, ping-pong RGBA16F;
  - final per-pixel fluence;
  - toggles: l = 2/4, pre-averaging, bilinear fix, interval overlap, t0.
- Verify:
  - the same scene with 2, 100 and 1,000 lights takes the same time (Sannikov's Fig.);
  - ringing visible around a 2-px light without the fix and gone with it;
  - compare with a brute-force 2D raymarch at 1,024 rays per pixel (reference).
- Reading: jason.today/rc, GM Shaders 1–2, mxcop fundamentals, Gigi HLSL 2D (Kosmokleaner/GigiRadianceCascades).

**M8c: SPWI hybrid GI** [E: 2–4 weeks; needs M8b G-buffer, M3 hit shading, M1 TLAS]
- Build:
  1. c0 probes every 4 px on the depth buffer, 16 directions (octahedral) on the normal hemisphere, 5–6 cascades
     (§3 B), intervals `0.016·4^i` m (tune to scene scale);
  2. trace each (probe, direction) with RayQuery `[t_i, t_{i+1}]`, shade hits with the path tracer's direct-light code;
  3. merge top-down with bilateral, then "Bilinear 3D", weights;
  4. integrate c0 with cosine, then upsample to pixels;
  5. optional: the bilinear fix (×4 rays) and half-resolution probes.
- Measure:
  - ms per pass and rays per frame on both GPUs;
  - FLIP vs a path-traced 1-bounce diffuse reference;
  - flicker (temporal std-dev) on a slow camera pan;
  - ringing around small emitters;
  - equal-time comparison with M8b's 1-spp bounce + denoiser.
- Reference code: ClaikoCode/DX12RadianceCascades (DX12, MiniEngine, MIT, RTPSO) and three-rc (no licence; read only).

**M9r: Split RC** [E: 5–8 weeks; after M9 SHaRC; best after M9b DDGI]
- Build in steps, following the paper's Algorithm 1–3:
  1. per-cascade hash maps, 64-bit key (18 bits/axis + 10-bit LOD), rebuilt each frame from an indirection buffer;
  2. probe insertion (pixel → nearest c0 → nearest c1 …), LOD from the Chebyshev camera distance, 0.9 LOD overlap;
  3. ray generation: count per probe → hierarchical prefix sum → R2 offsets → equal-area lat-long directions (32 at c0,
     ×4 per cascade), flip to the normal hemisphere;
  4. trace one RayQuery per pixel and split: for the hit distance s, deposit `(L, β=0)` into the cascade containing s
     and `(0, 1)` into the lower ones, with quantised integer atomics;
  5. merge top-down with pre-averaging and Morton-ordered directions, using wave intrinsics;
  6. 6×6 + border octahedral irradiance per c0 probe, then per-pixel sparse trilinear shading;
  7. temporal blend (static scenes);
  8. secondary cache at previous-frame hit points (two LODs coarser) for multibounce and as a path terminator.
- Measure:
  - the same per-stage table as the paper's Table 2, on both GPUs, for Sponza and a foliage scene;
  - VRAM (fp32 vs fp16);
  - single-frame and 10-frame FLIP vs reference;
  - penumbra test (small light + occluder);
  - AO near walls vs DDGI;
  - leak box (sibling note);
  - the "Split RC as denoiser" test vs OIDN on the same 1-spp rays (M7 already has OIDN).
- Reference code: entropylost/chandra (Rust/LuisaCompute/OptiX, no licence, read only), with the paper as the spec.

**M9r-b (optional): dense RC cascades on the DDGI grid** [E: 2–3 weeks after M9b]
- Add 2–3 finer cascades under the DDGI spacing with intervals `[t_i, t_{i+1}]`, and the trilinear (8-neighbour) fix
  as a toggle.
- Measure VRAM growth per added cascade (should halve per step upward, and doubles per step finer) and penumbra/AO
  gains vs plain DDGI.

**Where it fits** [I]
- Order: M8b → **(RC-0 at any time)** → M8c (SPWI) → M9 SHaRC → M9b DDGI → M9r Split RC → (M9c surfels) → M9x bake-off
  → M10 NRC → O2.
- SPWI and Split RC are both *hybrid* GI first. Only Split RC's secondary cache and the dense grid can serve as path
  terminators, so in the bake-off, RC rows are mainly in the "hybrid" column.
- In the M9x harness, add RC-specific metrics:
  - single-frame error (RC's selling point: no temporal reuse);
  - lag frames (should be 0 in single-frame mode);
  - flicker under camera motion (screen-space RC's weakness);
  - penumbra sharpness vs distance to the occluder.

**Rows for the sibling comparison table** (published numbers, GPU named; [E] where noted)

| Technique | Stores | Memory (published or [E]) | Update cost (published) | Leaks | Lag | Needs denoiser | Difficulty in HLSL + RayQuery [E] | Good open-source reference |
|---|---|---|---|---|---|---|---|---|
| **RC, screen-space probes + world-space intervals (SPWI)** (M8c) | Per screen probe and direction: RGB radiance interval + transmittance, per cascade (directions ×4, probes ¼ per cascade) | 34 MB (4 px, 16 rays) to 133 MB (2 px, 16 rays) at 1080p with pre-averaging (BTH); [E] ≈ 16 MiB per cascade without it | **2.36–8.66 ms** at 1080p on an RTX 3080, DX12 HW RT (BTH, 4 px → 2 px, 16 rays); 30–50 ms SDF on RTX 3060 (Sannikov); 33–156 ms on an Apple M1 (Gavras). PoE2 flatland variant ~3 ms on GTX 1050 | Behind the depth buffer, and across depth edges unless the merge is depth-aware; ringing without the bilinear fix | None (single shot), but flickers under camera motion | No | Medium–high (2–4 weeks) | ClaikoCode/DX12RadianceCascades (MIT, DXR); three-rc (no licence) |
| **Split RC** (M9r) | Sparse hashed world probes (4 cascades, 32→2,048 dirs): radiance + transmittance sums and counts; 6×6 octa irradiance per probe | Not published (fp32); [E] ~170 MB at 1080p in fp32, half in fp16 | 8.6 ms (Sponza), 11.5 ms (San Miguel), single frame, RTX 3080 Laptop, ≈ 1080p [I] | Merge-interpolation leaks; misses detail < c0 spacing; overblurs hard shadows | None in single-frame mode; optional temporal blend (static only) | No (works as one) | High (5–8 weeks) | entropylost/chandra (Rust/OptiX, no licence); the paper |
| **Dense world-space RC grid** (M9r-b, optional) | Uniform 3D probe grid per cascade: intervals + transmittance per direction | [E] c0 128 MiB at 0.25 m for 32×16×32 m, ≈ 2× c0 in total; 8× per spacing halving | No published GPU timing (Sannikov's "Radiance 3D" testbed only) | Like probe grids (no Chebyshev) [I]; ringing without the trilinear fix | None (single shot) or amortised | No | Medium on top of DDGI (2–3 weeks) | jumeder/dx12-radiance-cascades (MIT, coarse); tmpvar 3D grid |

### Gaps (for the whole note)
- No RC timings on an RX 9070 XT or RTX 3070, and no RC memory numbers for Split RC.
- No primary source for PoE2's shipped settings or its ExileCon 2023 talk content.
- "Spatium" has no technical description anywhere public. Everything known comes from YouTube descriptions.
- Love and Deepspace's mobile 3D RC is known only from a GDC session abstract summary.
- The BTH thesis (DX12 + DXR SPWI on an RTX 3080) was read for its abstract, setup and Table 5.3 only. Its 1440p tables
  and per-pass breakdown were not transcribed.
- The difficulty estimates are the author's judgement.
