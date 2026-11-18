# Path tracing (and spectrally rendering) a dynamic FFT ocean in a real-time DX12 engine

Scope: adding DXR ray/path tracing to an existing Tessendorf FFT ocean (compute FFT, displacement + normal/foam maps, rasterized clipmap grid, IBL sky) in a student C++ DX12 engine. Development is on an NVIDIA RTX PC, and the engine is also tested on an AMD RX 9070 XT (RDNA 4). Research date: 2026-11-18.

Source note: items marked "(canonical URL, not fetched this session)" are well-known papers or pages whose URL and content come from background knowledge. The research budget ran out before they could be re-verified. Everything else was fetched or searched in this session.

## Q1. Geometry: getting a displaced ocean into DXR acceleration structures

### Takeaway
Use a fixed-topology triangle grid. A compute shader writes displaced positions into the BLAS vertex buffer, the BLAS is built once with ALLOW_UPDATE and refit every frame on the async compute queue, and the TLAS is rebuilt each frame. A 512x512 grid is comfortably inside budget. A 1024x1024 grid is about 2 ms to refit by NVIDIA's rule of thumb. The new DXR clustered-geometry features (CLAS templates, partitioned TLAS) are the 2026 "next step", but they are still preview-only. Displacement micromaps are a dead end: NVIDIA archived the toolkit in favour of RTX Mega Geometry.

### Cited Findings
**Build vs. update (refit), and the flags**
- NVIDIA: "Continually updating a BLAS degrades its effectiveness as a spatial data structure." Refit suits geometry that deforms gently ("tree in wind"). Geometry that "explodes" should be rebuilt. NVIDIA recommends deciding by experiment. — [NVIDIA RTX Best Practices](https://developer.nvidia.com/blog/rtx-best-practices/)
- NVIDIA's flag recipes:
  - ALLOW_UPDATE + PREFER_FAST_TRACE gives the "fastest trace against updateable AS" (updates are slower). Use it for dynamic objects refit every frame.
  - PREFER_FAST_BUILD without update is for geometry fully rebuilt every frame (particles, destruction).
  - PREFER_FAST_BUILD + ALLOW_UPDATE is for low-LOD dynamic objects that are rarely hit.
  - ALLOW_COMPACTION is for static geometry and long-lived updateable BLAS ("compaction and update are not mutually exclusive!"). It is not useful for geometry rebuilt every frame.
  - MINIMIZE_MEMORY usually costs build and trace speed.
  - Source: [NVIDIA RTX Best Practices](https://developer.nvidia.com/blog/rtx-best-practices/)
- NVIDIA's throughput rule of thumb at maximum occupancy:
  - Full builds run at "O(100 million) primitives/sec" and updates at "O(1 billion) primitive/sec".
  - Cost is "mostly a linear relationship" with primitive count, plus a constant overhead at low counts.
  - The total AS build/update budget should be "at most 2ms".
  - Source: [NVIDIA RTX Best Practices](https://developer.nvidia.com/blog/rtx-best-practices/)
- NVIDIA also advises:
  - Rebuild the TLAS rather than update it, because refit savings "likely aren't worth sacrificing quality of TLAS".
  - Keep the sky out of the TLAS and shade it in the miss shader.
  - Batch builds, since one build can take several geometry descriptors.
  - Use a single barrier between the BLAS and TLAS builds.
  - Move AS builds and updates to async compute, which "in many cases hides the cost almost completely".
  - Make the prebuild-info inputs match the build inputs exactly.
  - Source: [NVIDIA RTX Best Practices](https://developer.nvidia.com/blog/rtx-best-practices/)
- The DXR spec's flag table describes PREFER_FAST_BUILD as suited to fully dynamic geometry, ALLOW_UPDATE as a "slightly slower build" in exchange for very fast refits, and PREFER_FAST_TRACE as the fastest traversal with the slowest build. — [DXR Functional Spec](https://microsoft.github.io/DirectX-Specs/d3d/Raytracing.html) (via search summary)
- An update must keep the same geometry count, topology and index buffer, and only vertex positions may change. An update may be done in place (source == destination) by passing PERFORM_UPDATE with the previous AS as the source. Vertex formats include DXGI_FORMAT_R32G32B32_FLOAT, R16G16B16A16_FLOAT and R16G16_FLOAT (z = 0). — [DXR Functional Spec](https://microsoft.github.io/DirectX-Specs/d3d/Raytracing.html) (canonical URL; these details are from background knowledge, not fetched this session)
- Memory and scale: an HPG 2020 keynote gives dynamic BVH memory of roughly 60–80 bytes per triangle. It also reports BLAS updates of about 3.2 ms for a test scene of roughly 19M triangles, and animation plus refit of about 6 ms for 225 skinned characters of 20k triangles each. — [Gruen, HPG 2020 "Are we done with hardware ray tracing?"](https://highperformancegraphics.org/slides20/monday_gruen.pdf) (via search summary)
- One paper says refitting is about an order of magnitude faster than rebuilding when topology is fixed, but repeated refits degrade quality. — search summary of [arXiv 2402.14801 (Mochi)](https://arxiv.org/pdf/2402.14801)

**New DXR features (2026): clusters and partitioned TLAS**
- Status: DXR Functional Spec Part 2 (v0.39) defines Clustered Geometry (CLAS), Partitioned TLAS (PTLAS) and Indirect AS operations. "These features are currently in preview." Support is reported through `ClustersAndPTLASSupported`, and the features pair with Shader Model 6.10. — [DXR Spec Part 2](https://microsoft.github.io/DirectX-Specs/d3d/Raytracing2.html)
- CLAS basics: a CLAS holds at most 256 triangles and 256 unique vertices. "For assembling BLAS out of CLAS of 100 triangles a ~100x speedup is reasonable to expect." — [DXR Spec Part 2](https://microsoft.github.io/DirectX-Specs/d3d/Raytracing2.html)
- Cluster templates: a template stores topology without positions and is instantiated each frame with positions written by a compute shader. The spec frames this as "an upgrade versus traditional updates / refits", with performance "comparable with traditional refit". Unlike refit, it allows topology changes by swapping templates. This maps directly onto an FFT grid split into 16x16-quad tiles (512 triangles, so two CLAS per tile, or 8x16-quad clusters). — [DXR Spec Part 2](https://microsoft.github.io/DirectX-Specs/d3d/Raytracing2.html)
- PTLAS: unchanged partitions are reused, and a global partition holds instances that move every frame. Partition translation helps large-world precision. Explicit instance bounding boxes let a BLAS be swapped (for example for LOD) without a rebuild. "Generally small partitions (100-1000 instances) should offer the majority of build speed improvements." — [DXR Spec Part 2](https://microsoft.github.io/DirectX-Specs/d3d/Raytracing2.html)
- AMD support:
  - Clustered Geometry, PTLAS and indirect AS builds run on Radeon RX 9000 (RDNA 4).
  - They need the "AMD Software: Agility SDK Developer Preview Edition 26.20.12.01" driver (28 Oct 2026) and Agility SDK 1.722-preview with SM 6.10 preview.
  - Microsoft plans to move SM 6.10 to retail in Agility SDK 1.621, due in early December 2026.
  - Radeon Raytracing Analyzer (RRA) 1.11.1 can inspect these structures.
  - Source: [AMD GPUOpen, Oct 2026](https://gpuopen.com/learn/amd-directx12-agility-sdk-october-2026/)
- Microsoft's sample on an RX 9070: a traditional BLAS rebuild took 788 ms per frame (1.2 FPS) versus 3.11 ms with Clustered Geometry (79 FPS). AS memory fell from 7,519 MiB to 774 MiB. Microsoft warns that results vary and games should not assume gains this large. No refit comparison was reported. — [AMD GPUOpen](https://gpuopen.com/learn/amd-directx12-agility-sdk-october-2026/); [xenospectrum summary](https://xenospectrum.com/en/dxr-clustered-geometry-partitioned-tlas-preview/)

**Micromaps**
- Displacement micromaps: NVIDIA's Displacement Micro-Map Toolkit repository is archived. NVIDIA points developers to RTX Mega Geometry as offering "similar functionality with more flexibility". DMMs are not part of DXR 1.2. — [DMM toolkit (ecosyste.ms mirror)](https://awesome.ecosyste.ms/api/v1/projects/github.com%2Fnvidiagameworks%2Fdisplacement-micromap-toolkit)
- No source was found confirming hardware DMM removal on Blackwell.
- Opacity micromaps (OMMs) are part of DXR 1.2 and reduce any-hit calls. They are irrelevant for an opaque water surface, though potentially useful for alpha-tested spray or foam cards. — [Microsoft DirectX blog: OMM](https://devblogs.microsoft.com/directx/omm/)

### Inferences
**Grid cost estimates.** These come from applying NVIDIA's O(100M/s) build and O(1G/s) update rule of thumb to a grid with 2 triangles per quad. They are order-of-magnitude estimates, not measurements.

| Grid | Triangles | Full build | Refit | BVH memory (60–80 B/tri) |
|---|---|---|---|---|
| 256x256 | ~131k | ~1.3 ms | ~0.13 ms | ~8–10 MB |
| 512x512 | ~524k | ~5 ms | ~0.5 ms | ~31–42 MB |
| 1024x1024 | ~2.1M | ~21 ms | ~2.1 ms | ~126–168 MB |

Measure these on both GPUs with PIX or Nsight, and with RRA on AMD.

**Refit quality on ocean waves.** Waves are a smooth, bounded deformation of a fixed grid, the "tree in wind" case, so refit quality should hold up. Choppy (horizontal) displacement shifts triangles sideways by up to roughly the choppiness times the amplitude, which inflates node bounds. A cheap safeguard is a periodic full rebuild, for example every N frames or when significant wave height changes.

**Recommended pipeline.**
1. Run the FFT on the compute queue.
2. Run a "displace-to-VB" compute pass that samples the cascades at the grid vertices and writes R32G32B32 (or R16G16B16A16_FLOAT) positions into a UAV buffer, which is the BLAS vertex buffer.
3. Issue a UAV barrier.
4. Refit the BLAS with PERFORM_UPDATE on async compute.
5. Rebuild the TLAS.

Normals and foam should not go in the BLAS. The hit shader samples the existing normal and foam maps at the hit point's undisplaced (x, z), which is reconstructable from barycentrics and the vertex index. The traced geometry can therefore be much coarser than the shading detail, the same "geometry to BRDF" idea as Bruneton et al. 2010.

**LOD and clipmaps.**
- A camera-centred clipmap BLAS can still be refit each frame as long as the topology stays fixed. Only vertex positions change, including when the rings snap to the camera.
- Ring seams and T-junctions can leak rays. Use stitched rings, or a single radially graded grid.
- Alternatively, tile the world-fixed ocean as N instanced patches in the TLAS: a few BLAS LOD levels (for example 128², 64², 32²) instanced by distance.
- Because the FFT is periodic, every patch can reference the same BLAS of one period. This works only if all tiles share displacement (no local wakes) and you accept visible repetition at distance. This is the cheapest option of all: one refit per LOD per frame.

**Heightfield or procedural alternative.** Use AABB geometry and a custom intersection shader that ray-marches the displacement map. This avoids per-frame AS work, but intersection shaders are much slower than hardware triangle tests, and choppy displacement is not a true heightfield, so marching is approximate. Keep it as a fallback or experiment only.

**What to target.** For a student engine in 2026: classic refit as the baseline, which works on any DXR 1.1 GPU on both vendors. A CLAS-template path can be added later as an optional experiment, since it is preview-only and needs preview drivers on AMD. NVIDIA support for the DXR Part-2 preview was not checked here.

### Gaps
- No measured BLAS refit or build timings for an ocean grid on RTX 40/50 or RX 9070 XT were found. The table above is derived from rules of thumb.
- No primary source was found on how shipping games put their ocean into the BLAS. Topics without a confirmed source include Cyberpunk and Battlefield water and whether Crest or Unity HDRP water enters the RT acceleration structure.
- Unity HDRP 14 docs say water surfaces can receive RT reflections but "cannot contribute to them": if you hold up a mirror, the water does not reflect in it. That suggests HDRP water is not in the RTAS, at least in that version. — [Unity HDRP Water overview](https://docs.unity3d.com/Packages/com.unity.render-pipelines.high-definition@14.0/manual/WaterSystem-Overview.html)
- Crest 4.15 added HDRP ray-traced reflections *onto* the ocean in Unity 2021.2. — [Crest release notes](https://crest.readthedocs.io/en/4.15/about/history.html)
- Battlefield V's DXR covered reflections only. A GTC 2019 talk covers it. — [NVIDIA GTC 2019 S91023 PDF](https://developer.download.nvidia.com/video/gputechconf/gtc/2019/presentation/s91023-it-just-works-ray-traced-reflections-in-battlefield-v.pdf); [Digital Trends](https://digitaltrends.com/?p=2238428)
- Cyberpunk 2077's launch RT reflections were used "on all surfaces" and could trace "up to several kilometers". — [NVIDIA GeForce news](https://www.nvidia.com/en-us/geforce/news/cyberpunk-2077-ray-tracing-dlss-geforce-now-screenshots-trailer.md)
- Nothing was found specifically on Cyberpunk path-traced water geometry.
- No information was found on WaveWorks 2 ray tracing integration. Its NVIDIA blog exists but was not read: [WaveWorks 2.0 blog](https://developer.nvidia.com/blog/three-things-you-need-to-know-about-wave-works-2-0).

## Q2. Water optics for a physically based path tracer

### Takeaway
Treat the ocean surface as a smooth or rough dielectric interface (IOR ~1.33–1.34, wavelength dependent) above a homogeneous absorbing and weakly scattering medium. Seawater's spectral absorption (Pope & Fry 1997) is tiny in the blue (~0.005–0.01 m^-1) and large in the red (~0.4–0.6 m^-1 at 660–700 nm). Its molecular scattering is ~0.003 m^-1 at 500 nm with a λ^-4.32 dependence. Foam is a separate diffuse layer. The sun needs next-event estimation (NEE) as a small disk. Caustics on the seafloor seen through the surface are the hard case for a unidirectional path tracer.

### Cited Findings
**Absorption: Pope & Fry (1997) pure water**
- Citation: R. M. Pope and E. S. Fry, "Absorption spectrum (380-700 nm) of pure water. II. Integrating cavity measurements", *Appl. Opt.* 36, 8710–8723 (1997). The absorption minimum is 0.0044±0.0006 m^-1 at 418 nm. — [OMLC Pope97 page](https://omlc.org/spectra/water/abs/pope97.html)
- Tabulated values (file in 1/cm, converted to 1/m here by ×100). — [OMLC pope97.dat](https://omlc.org/spectra/water/data/pope97.dat)

| nm | a_w (1/m) | nm | a_w (1/m) | nm | a_w (1/m) |
|---|---|---|---|---|---|
| 380 | 0.01137 | 500 | 0.0204 | 620 | 0.2755 |
| 400 | 0.00663 | 520 | 0.0409 | 640 | 0.3108 |
| 420 | 0.00454 | 540 | 0.0474 | 660 | 0.410 |
| 440 | 0.00635 | 560 | 0.0619 | 680 | 0.465 |
| 460 | 0.00979 | 580 | 0.0896 | 700 | 0.624 |
| 480 | 0.0127 | 600 | 0.2224 | 720 | 1.231 |

- A NASA erratum notes that the corrected ocean-colour protocol table uses Pope & Fry values for 400–715 nm. Earlier versions mistakenly used Pope's 1993 thesis values. — [NASA Ocean Optics Protocols erratum](https://oceancolor.gsfc.nasa.gov/files/resources/docs/technical/volivch1err1.pdf)
- Smith & Baker (1981) is the classic clearest-natural-water absorption and attenuation set (200–800 nm). It is commonly used above 700 nm or as an alternative. — [Smith & Baker 1981 PDF](https://frouingroup.ucsd.edu/Aw_Chl/REFRENCES/Smith-Baker-1981.pdf) (table not extracted this session)

**Scattering and bio-optical models**
- Morel (1974) measured pure seawater scattering b(500 nm) ≈ 0.00288 m^-1 (depolarization ratio 0.09, 20 °C), with a best power-law exponent of λ^-4.32. — [DRDO review](https://publications.drdo.gov.in/ojs/index.php/dsj/article/download/6048/3176/15909) (via search summary)
- Natural seawater (salinity 38.4‰) scatters about 30% more than pure water. Buiteveld et al. (1994) with the [1+0.3S/37] salinity factor is considered among the most accurate. — [Zhang et al. 2009](https://misclab.umeoce.maine.edu/ftp/classes/OO2011/incoming/zhang_et_al_2009_oe.pdf); [Biogeosciences 4, 1041 (2007)](https://bg.copernicus.org/articles/4/1041/2007/bg-4-1041-2007.pdf)
- The "classic Case 1 water" model (Morel-type) parameterizes absorption and scattering by chlorophyll concentration, which gives one artist knob from blue open ocean to green coastal water. — [Ocean Optics Book: classic IOP model Case 1](https://oceanopticsbook.info/view/optical-constituents-of-the-ocean/level-2/classic-iop-model-case-1-water/pdf); [Ocean Optics Book absorption overview](https://oceanopticsbook.info/view/absorption/absorption-overview/pdf)
- Morel et al. 2007 covers optical properties of the clearest natural waters. — [Morel et al., L&O 2007](https://www.obs-vlfr.fr/proof/ftpv/biosope/prj_life/others_papers/accepted/Morel_et_al_LO_07.pdf)

**Index of refraction**
- Quan & Fry (1995), *Appl. Opt.* 34(18) 3477–3480, DOI 10.1364/AO.34.003477, is a ten-parameter empirical n(λ, T, S) for seawater at atmospheric pressure.
  - It fits Austin & Halikas's data.
  - It is valid in the visible range, and Huibers (1997) says it can be extrapolated to about 220–1100 nm.
  - It gives the index relative to air, so add the dry-air correction to get values relative to vacuum.
  - Source: [doi.org](https://www.doi.org/10.1364/AO.34.003477); [R `rho::n_water` docs](https://rdrr.io/github/AlexCast/rho/man/n_water.html); [source code with coefficients](https://rdrr.io/github/AlexCast/rho/src/R/refractive_indexes.R)
- Frisvad (2009) extends the fit to freezing brine. — [openpolar record](https://openpolar.no/Record/ftdtupubl:oai:pure.atira.dk:publications%2F7a586cff-37e9-410f-b637-a0dd2f7d62db)

**Interface BSDF (canonical URL, not fetched this session)**
- Walter, Marschner, Li, Torrance, "Microfacet Models for Refraction through Rough Surfaces" (EGSR 2007) defines the GGX rough dielectric BRDF plus BTDF. It is the standard model for a rough air–water interface, with the half-vector for refraction h_t = −(η_i·i + η_o·o). Use exact dielectric Fresnel, which gives total internal reflection automatically for rays inside the water beyond the critical angle (~48.6° for n = 1.333). — [Walter et al. 2007 PDF](https://www.cs.cornell.edu/~srm/publications/EGSR07-btdf.pdf)
- Bruneton, Neyret, Holzschuch, "Real-time Realistic Ocean Lighting using a Seamless Transition from Geometry to BRDF" (Eurographics 2010) treats sub-pixel waves as a roughness (slope variance) term. In a path tracer this means rough GGX on the coarse traced mesh, with roughness driven by the slope variance of the cascades truncated below the mesh resolution. — [Bruneton 2010 (HAL)](https://hal.inria.fr/inria-00443630); slides: [Charles Univ. course copy](https://cgg.mff.cuni.cz/%7Ejaroslav/teaching/2012-npgr031/prezentace/Realtime%20Realistic%20Ocean%20Lighting.pdf)

**Caustics**
- NVIDIA (Holger Gruen, GDC 2019): ray traced water caustics and volumetric shadows in DXR. "Rasterization is only great at doing visibility from one point of view." Caustics need visibility "from an arbitrary point of view on the water surface". The blog post has no implementation details. — [NVIDIA blog](https://developer.nvidia.com/blog/working-with-ray-traced-water-caustics-in-dxr/)
- Ray Tracing Gems II chapter 30, "Real-Time Ray Traced Caustics" (Yang and Ouyang, NVIDIA), has two techniques. The first is adaptive anisotropic photon scattering (AAPS) for multi-bounce mesh caustics, which uses temporal feedback to control screen-space photon density. The second is a water method for single-bounce reflection or refraction caustics over large water areas. — [RTG2 ch.30 preprint](https://developer.download.nvidia.com/ray-tracing-gems/rtg2-chapter30-preprint.pdf); [NVIDIA blog](https://developer.nvidia.com/blog/learn-more-about-real-time-ray-traced-caustics-in-free-ray-tracing-gems-ii-chapter/); [UE4 caustics blog part 1](https://developer.nvidia.com/blog/generating-ray-traced-caustic-effects-in-unreal-engine-4-part-1/)
- Kougianos and Moustakas (2021, *Computers & Graphics*), "Large-scale ray traced water caustics in real-time using cascaded caustic maps". — [VVR Upatras](https://vvr.ece.upatras.gr/?p=3741)

**Atlas (GDC 2019)**
- "Wakes, Explosions and Lighting: Interactive Water Simulation in Atlas", by Mark Mihelich (Grapeshot Games) and Tim Tcheblokov (NVIDIA). It covers sea states (a Tessendorf spectrum approach), buoyancy, rendering, wakes and explosions. — [GPUOpen slides PDF](https://gpuopen.com/gdc-presentations/2019/gdc-2019-agtd6-interactive-water-simulation-in-atlas.pdf) (PDF >10 MB, could not be parsed this session); [GDC Vault](https://gdcvault.com/play/1025819/Advanced-Graphics-Techniques-Tutorial-Wakes)

### Inferences
**Surface hit material.** Shade a hit as a dielectric (smooth or GGX-rough) between n_air = 1.000293 and n_water(λ) from Quan & Fry (~1.34 at 400 nm, ~1.33 at 700 nm for S = 35‰, T = 15 °C; verify with the R code). Choose reflection or transmission stochastically by the Fresnel weight. This is a single-sample estimator, the standard approach in pbrt/Mitsuba dielectric BSDFs.

**Foam.**
- Blend a Lambertian (or slightly forward-scattering) white layer over the dielectric using the existing foam map as a stochastic mix weight: with probability foam, sample diffuse; otherwise sample the dielectric.
- Whitecap coverage is driven by the Jacobian of the choppy displacement (Tessendorf).
- In a path tracer, foam is also where the diffuse-albedo material makes NEE very effective.

**Inside the water: homogeneous medium.**
- Sigma_a(λ) = a_w(λ) (plus chlorophyll and CDOM terms if wanted), and sigma_s(λ) = b_w(λ) + particles.
- For clear water, sigma_s is far smaller than sigma_a in the red but comparable in the blue, so blue light scatters back out (deep-water colour) and red is absorbed.
- Use free-flight distance sampling against sigma_t per wavelength. In spectral mode with hero wavelengths, use the hero λ's sigma_t and spectral MIS, or delta and ratio tracking with a majorant.
- Phase function: Rayleigh-like for pure water, plus a strongly forward Henyey–Greenstein or Fournier–Forand term for particles.
- Single-scatter NEE toward the sun must cross the wavy interface, which a straight shadow ray cannot do (refraction). The practical trick is to approximate refracted sun direction using the flat-interface Snell direction and accept missing caustics. This is how most offline renderers handle underwater sun, and it is why "god rays" and caustics need special handling.

**Wave-tip glow ("subsurface scattering").** In a true volumetric path tracer this emerges naturally: sunlight enters the thin wave crest, single-scatters, and exits toward the camera, with a green-blue tint from the spectral absorption. Raster engines (Atlas, Crest, and so on) fake it with a view-and-sun-dependent term scaled by wave height. A path-traced reference mode is therefore a good way to calibrate that fake.

**Caustics.** Caustics on the seafloor or on submerged objects need L-S-D-S-E paths: the sun is a delta-like light, then a specular refraction, then a diffuse floor, then a specular refraction back to the eye. A unidirectional path tracer with NEE cannot connect a shadow ray through a specular interface. Caustics then appear only through BSDF-sampled paths that randomly hit the 0.5°-wide sun, which is extremely noisy.

Tricks, roughly in order of effort:
1. Roughen the interface for shadow and NEE rays.
2. Make the sun larger, or "regularize" it.
3. Run a separate caustic photon or light-tracing pass from the sun through the BLAS into a caustic map, as in RTG2 ch.30 water or cascaded caustic maps.
4. Use manifold next-event estimation (MNEE, Hanika et al. 2015), solving for the specular path through one refractive interface.
5. Use full BDPT or MLT, which is offline only.

**Sky and sun.**
- Remove the sun from the environment map. Sample the sun as a separate disk light (angular radius ~0.267°) with NEE and MIS against BSDF sampling. Importance-sample the remaining environment map (2D CDF over luminance, or hierarchical warping) for diffuse foam and rough water.
- Physical sky options:
  - Preetham 1999 (analytic, RGB or xyY).
  - Hosek–Wilkie 2012: spectral radiance coefficients across the visible range, with a solar-disc model. — [project page](https://cgg.mff.cuni.cz/projects/SkylightModelling/) (canonical URL, not fetched this session)
  - Bruneton's precomputed atmospheric scattering (2008; 2017 reimplementation), whose demo can precompute with 15+ wavelengths and convert to RGB, so it fits a spectral path tracer. — [GitHub ebruneton/precomputed_atmospheric_scattering](https://github.com/ebruneton/precomputed_atmospheric_scattering) (canonical URL, not fetched this session)

### Gaps
- The exact Pope & Fry values above 700 nm are uncertain: the citation says 380–700 nm, but the .dat file lists 720 nm and NASA says 400–715 nm. Smith & Baker 1981 or Segelstein are usually used beyond 700 nm, but no table was extracted this session.
- The Smith & Baker 1981 and Morel 1974 numeric tables were not extracted.
- The Atlas slides could not be read (PDF too large), so their lighting details (SSS approximation, reflections) remain unverified.
- No source was fetched for MNEE, delta tracking or Walter 2007 this session. They are cited from canonical URLs.

## Q3. Spectral effects that matter for water

### Takeaway
Spectral rendering matters for water mainly because absorption is strongly wavelength dependent and enters an exponent (Beer–Lambert over metres). RGB averages of a_w(λ) give the wrong colour shifts with depth. Dispersion (n varies by ~0.01 across the visible range) matters for spray, droplets and rainbows, and slightly for caustic colour fringes. It is negligible on the bulk surface.

### Cited Findings
- Pope & Fry absorption rises by about 100x from 420 nm (0.0045 m^-1) to 700 nm (0.62 m^-1), with a steep rise above 600 nm. — [OMLC pope97.dat](https://omlc.org/spectra/water/data/pope97.dat)
- Molecular scattering follows λ^-4.32, so it is about 3.5x stronger at 450 nm than at 600 nm. — [DRDO review citing Morel 1974](https://publications.drdo.gov.in/ojs/index.php/dsj/article/download/6048/3176/15909)
- The seawater index of refraction is a function of λ, T and S (Quan & Fry 1995). This dispersion produces rainbows and coloured caustics. — [rho::n_water docs](https://rdrr.io/github/AlexCast/rho/man/n_water.html)
- Hosek–Wilkie and Bruneton skies are natively spectral, so a spectral path tracer can use sky radiance per wavelength rather than converting from RGB. — see the Q2 sky links (canonical URLs, not fetched)

### Inferences
**Transmittance over 10 m**, from T = exp(−a·d) with the table above:

| Wavelength | Transmittance |
|---|---|
| 440 nm | ~94% |
| 550 nm | ~58% |
| 600 nm | ~11% |
| 680 nm | ~1% |

A single RGB "red" channel averaging 600–700 nm would mix ~11% and ~1%. The exponential of an average differs from the average of exponentials, so RGB Beer–Lambert systematically mis-predicts hue with depth. This is the core argument for spectral water.

**Dispersion.** Δn ≈ 0.01 across the visible range moves the rainbow angle by about 2° between red and violet. The bulk-surface effect is invisible, but spray particles (if modelled as a medium with a Lorenz–Mie or rainbow phase function) and caustics show colour.

**Implementation.** Use hero-wavelength sampling (Wilkie et al. 2014): 4 wavelengths per path in a float4, with absorption and the IOR looked up per λ. When the path refracts, either keep all 4 wavelengths (if the interface is treated as non-dispersive) or terminate the secondary wavelengths (when dispersion is on). Upsample RGB textures with Jakob–Hanika 2019 spectral upsampling. These are standard spectral-renderer techniques, not water-specific sources.

### Gaps
- No water-specific publication was found quantifying RGB-vs-spectral error. The argument above is derived.
- Wilkie 2014 and Jakob–Hanika 2019 were not fetched this session.

## Q4. Hybrid options and denoising

### Takeaway
The pragmatic path:
1. Rasterize primary visibility, which the engine already does.
2. Trace secondary rays from the water G-buffer: reflection (1 spp), refraction into an analytic or tracked medium, and sun shadow.
3. Separately, build an offline-ish progressive path-tracing "reference mode" that accumulates many frames with the simulation frozen, to validate the raster ocean.

Denoising a moving specular surface is the main real-time difficulty. Motion vectors from the surface describe the surface, not the reflected image.

### Cited Findings
- Shipping examples:
  - Battlefield V's RT was reflections only, with screen-space generation and denoising discussed at GTC/GDC 2019. — [GTC 2019 S91023](https://developer.download.nvidia.com/video/gputechconf/gtc/2019/presentation/s91023-it-just-works-ray-traced-reflections-in-battlefield-v.pdf)
  - Cyberpunk 2077 layers screen-space reflections onto RT reflections, per a forum report of Digital Foundry's analysis (secondary source). — [Overclockers forum](https://forums.overclockers.co.uk/threads/cyberpunk-2077-ultra-performance.18910855/page-78)
- Unreal:
  - An Epic forum reply says Single Layer Water and the ocean system are supported in the UE5 path tracer, including the underwater post-process volume. Twinmotion staff later said water looked opaque in its path tracer, a "known limitation". — [UE forum](https://forums.unrealengine.com/t/is-it-possible-to-render-ocean-with-path-tracing/649814); [Twinmotion thread](https://forums.unrealengine.com/t/water-tw-2022-2/1828785)
  - Single Layer Water uses Opaque or Masked blend modes, with translucency driven by the opacity input, plus scattering, absorption, reflection and refraction. — [Epic docs: Single Layer Water](https://dev.epicgames.com/documentation/unreal-engine/single-layer-water-shading-model-in-unreal-engine)
- Unity HDRP water can receive RT reflections but cannot contribute to them. — [Unity docs](https://docs.unity3d.com/Packages/com.unity.render-pipelines.high-definition@14.0/manual/WaterSystem-Overview.html)
- Raster FFT reference point: a WebGL FFT ocean reports FFT simulation ~0.45 ms, half-resolution planar reflection, and depth-validated refraction. — [Ultra-Water GitHub](https://github.com/Humpalumps/Ultra-Water)

### Inferences
**Hybrid.**
- Water pixels from the raster G-buffer spawn rays (inline RayQuery in a compute pass works well on both NVIDIA and AMD, with no SBT needed).
- The reflection ray hits the TLAS (terrain, ships, the ocean's own BLAS for self-reflection of wave crests) or misses to the sky. The refraction ray goes through the medium.
- Inline RayQuery avoids hit-shader complexity while the scene is small.

**Reference mode.**
1. Freeze the FFT time.
2. Use a camera-ray path tracer with the same BLAS.
3. Accumulate N frames.
4. Compare against the raster output via image differencing, especially for the wave-tip SSS fake, the foam albedo, the sky IBL and the Fresnel at grazing angles. Bruneton's BRDF transition is exactly what a reference makes visible: compare the far-field glitter pattern.

**Denoising problems.**
- Reflections on a moving surface need reflection-specific motion vectors: reproject the virtual reflected hit point, not the surface point.
- Sun glints are sub-pixel and high-energy, so they become fireflies. Clamp, or use Bruneton-style roughening at distance.
- Disocclusion happens behind every crest.
- Most denoisers (NRD ReLAX/ReBLUR, AMD FidelityFX denoisers, DLSS Ray Reconstruction) expect hitT and roughness. Low-roughness water at 1 spp is their hardest case, and a slightly higher minimum roughness on far water helps.

### Gaps
- No primary source was found describing the ocean-specific denoising approach of any shipped title.
- NRD and DLSS-RR documentation was not fetched this session.

## Q5. References and example projects

### Takeaway
The core canon:
- Tessendorf's course notes for the spectrum and FFT.
- Horvath 2015 for JONSWAP/TMA spectra plus directional spreading.
- Bruneton 2010 for the geometry-to-BRDF transition.
- Walter 2007 for the rough dielectric.
- Pope & Fry, Smith & Baker and Morel for water IOPs.
- The Atlas GDC 2019 talk and RTG2 ch.30 for game practice.
- DXR spec Parts 1 and 2 plus NVIDIA RTX Best Practices for acceleration structures.

### Cited Findings
- Tessendorf, "Simulating Ocean Water", course notes 2002 and 2004. — [2004 PDF](https://jtessen.people.clemson.edu/reports/papers_files/coursenotes2004.pdf); [2002 PDF](https://jtessen.people.clemson.edu/reports/papers_files/coursenotes2002.pdf)
- Horvath, "Empirical directional wave spectra for computer graphics", DigiPro 2015. — [ACM DL](https://dl.acm.org/doi/10.1145/2791261.2791267) (canonical URL, not fetched)
- Bruneton et al. 2010. — [HAL](https://hal.inria.fr/inria-00443630) (canonical URL, not fetched)
- Atlas GDC 2019. — [slides](https://gpuopen.com/gdc-presentations/2019/gdc-2019-agtd6-interactive-water-simulation-in-atlas.pdf); [GDC Vault](https://gdcvault.com/play/1025819/Advanced-Graphics-Techniques-Tutorial-Wakes)
- Secondary overviews:
  - Konstantin's ocean blog: [konstantinkz.github.io](https://konstantinkz.github.io/blog/water)
  - "Water in modern games": [j111m.github.io](https://j111m.github.io/posts/Ocean)
  - Call of Duty: WWII oceans: [80.lv](https://80.lv/articles/creating-real-time-oceans-for-call-of-duty-wwii?amp=1)
  - World of Warships sea surface (SIGGRAPH): [history.siggraph.org](https://history.siggraph.org/?p=87572)
- Ray tracing:
  - [DXR spec](https://microsoft.github.io/DirectX-Specs/d3d/Raytracing.html)
  - [DXR spec Part 2](https://microsoft.github.io/DirectX-Specs/d3d/Raytracing2.html)
  - [NVIDIA RTX Best Practices](https://developer.nvidia.com/blog/rtx-best-practices/)
  - [AMD Agility SDK Oct 2026](https://gpuopen.com/learn/amd-directx12-agility-sdk-october-2026/)
  - [RTG2 ch.30 caustics](https://developer.download.nvidia.com/ray-tracing-gems/rtg2-chapter30-preprint.pdf)
  - [Gruen HPG 2020](https://highperformancegraphics.org/slides20/monday_gruen.pdf)
- Ocean optics: [Ocean Optics Book](https://oceanopticsbook.info/view/absorption/absorption-overview/pdf); [OMLC water spectra index](https://omlc.org/spectra/water/abs/)
- Open-source and other projects:
  - Crest (Unity), whose docs and release notes include HDRP RT reflections. — [crest.readthedocs.io](https://crest.readthedocs.io/en/stable/about/history.html)
  - Ultra-Water (three.js FFT, raster). — [GitHub](https://github.com/Humpalumps/Ultra-Water)
  - Older GPU heightfield ray tracing of water (TU Wien seminar, 2008). — [PDF](https://www.cg.tuwien.ac.at/courses/Seminar/WS2008/arbeit_fleck.pdf)
  - Hu et al., MSR, real-time ocean waves with displacement and bump maps. — [PDF](https://www.visgraf.impa.br/Data/RefBib/PS_PDF/lvelho-cavw04/rtwave.pdf)

### Inferences
- gasgiant's FFT-Ocean (Unity, an implementation of Tessendorf with JONSWAP cascades, GitHub gasgiant/FFT-Ocean) is a good raster reference for cascades and foam, but it has no ray tracing (background knowledge; repository not fetched).
- Shadertoy "Seascape" by TDM is a raymarched procedural heightfield ocean, not path traced and not FFT (background knowledge; not verified this session).
- Blender Cycles' Ocean modifier (Tessendorf-based) with a Principled glass or volume-absorption material is the easiest offline spectral-ish ground truth. Cycles is RGB, not spectral, so Mitsuba 3 in a spectral variant is better for the absorption-colour comparison (background knowledge; not verified this session).

### Gaps
- No open-source DX12 or Vulkan project that path traces an FFT ocean with a refit BLAS was found in the searches.
- No 2020–2026 SIGGRAPH ocean-rendering talk was verified this session.
- WaveWorks 2 ray tracing status was not checked.
