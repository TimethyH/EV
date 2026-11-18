# Industry real-time path tracing: production architectures (state as of November 2026)

Scope: how shipped games, engines and GPU vendors build real-time path tracing (PT), written so a student can pick an architecture for their own C++ DirectX 12 engine (developed on an NVIDIA RTX PC, also tested on an AMD Radeon RX 9070 XT, which is RDNA 4).

Labels used below:
- **[NV-only]** works only on NVIDIA. **[AMD-only]** works only on AMD (often RDNA 4 only). **[Cross-vendor]** standard DX12/DXR, runs on any DXR GPU.
- **[Older]** information from before 2025 that may be out of date.
- **[Training knowledge]** a claim I know from background knowledge but did not re-fetch in this session. The URL is the canonical location; check it before relying on it.

Research method: web searches and page fetches on 2026-11-18. Many search summaries came from secondary press; where a claim comes only from press or a community source, that is stated.

---

## 1. Shipped titles and engines: which use full path tracing, and how each is built

### Takeaway
Every shipped AAA "full ray tracing / path tracing" mode so far is an NVIDIA-partnered PC mode built from the same toolkit. It keeps a **raster G-buffer for primary visibility**, uses **ReSTIR DI (RTXDI) for direct light**, adds **1–2 traced indirect bounces terminated into a world-space radiance cache** (SHaRC, or NRC in RTX Remix), relies on **SER and Opacity Micromaps** for speed, and uses **DLSS Ray Reconstruction** as a combined denoiser and upscaler. Path tracing runs at roughly a quarter of the output resolution. Pure path tracers that trace primary rays too (RTXPT, the UE5 Path Tracer, Unity HDRP) are reference or research tools, not the shipped game modes.

### Cited findings

**Cyberpunk 2077 – RT Overdrive (REDengine 4, CD Projekt Red with NVIDIA)** [Older: 2023 launch; mostly NV-centric]
- Released as a "technology preview" on April 11, 2023 (patch 1.62). NVIDIA shipped it alongside its "RTX Path Tracing SDK 1.0", which bundles DLSS 3, SER, RTXDI and NRD. — [NVIDIA blog, GDC 2023](https://blogs.nvidia.com/blog/2023/03/22/cyberpunk-2077-path-traced-visuals-gdc/)
- Pipeline split described at GDC 2023 by NVIDIA's Pawel Kozlowski: RTXDI computes all direct illumination and path tracing computes the indirect part. CDPR's Jakub Knapik called it a "technology preview" because it was still a work in progress. — [TechPowerUp, 2023-03-29](https://www.techpowerup.com/306589/cd-projekt-red-and-nvidia-talk-about-upcoming-cyberpunk-2077-path-tracing)
- NVIDIA's marketing figure is an average of 635 ray calculations per pixel, against 39 in Battlefield V. Tom's Hardware notes this is NVIDIA's own average, not a setting. — [Tom's Hardware](https://www.tomshardware.com/features/cyberpunk-2077-rt-overdrive-path-tracing-full-path-tracing-fully-unnecessary)
- Uses SER for incoherent workloads and NRD for denoising (at launch, before Ray Reconstruction existed). — [NVIDIA GeForce news, Overdrive launch](https://www.nvidia.com/en-gb/geforce/news/cyberpunk-2077-ray-tracing-overdrive-update-launches-april-11)
- ReSTIR GI was added in Patch 2.1. The community "Advanced Path Tracing" mod exposes ReSTIR DI/GI and SHaRC toggles. The mod's defaults are 2 rays per pixel and 2 bounces, which are the **mod's** values, not confirmed game defaults. — [AdvancedPathTracingCP2077 (community mod)](https://github.com/codecrafting-io/AdvancedPathTracingCP2077)
- The preview label was removed in Update 2.1 (secondary source). — [coretechtips guide](https://coretechtips.com/guides/cyberpunk-2077-path-tracing-pc-explained)
- Opacity Micromaps are listed among Overdrive's features in press coverage. I found no description of how they are used. — [Wccftech](https://wccftech.com/nvidia-unveils-ray-tracing-overdrive-rtxdi-coming-soon-to-cyberpunk-2077/)

**Alan Wake 2 (Remedy Northlight)** [2023 game; talk 2024]
- GDC 2024 talk "Alan Wake 2: A Deep Dive into Path Tracing Technology" by Kiya Kandar (Remedy) and Juha Sjöholm (NVIDIA). It covers BVH creation, ReSTIR DI, path tracing of opaque and transparent materials, SER, and DLSS. — [Remedy GDC 2024 article](https://www.remedygames.com/article/gdc2024); [Kostas Anagnostou on X (link to the recording)](https://x.com/KostasAAA/status/1781747589664129256)
- Press summary of GDC 2024: both the Cyberpunk and Alan Wake 2 sessions described ReSTIR DI as the common approach, with some "cinematic" lights weighted more heavily. — [Digital Trends](https://www.digitaltrends.com/computing/gdc-2024-in-review/)
- According to Microsoft's DXR 1.2 messaging (as quoted in search results), Remedy showed ray tracing cost cut by about one third by combining OMM and SER in Alan Wake 2. The February 2026 SM 6.9 retail post I fetched does **not** contain this claim; it most likely comes from the earlier GDC 2025 DXR 1.2 announcement. — [Microsoft SM 6.9 retail post (claim absent)](https://devblogs.microsoft.com/directx/shader-model-6-9-retail-and-more/)
- **New in 2026:** at GDC 2026, NVIDIA said Remedy applied RTX Mega Geometry to existing Alan Wake 2 assets, for a 5–20% FPS gain and 300 MB less VRAM (from a search summary). — [NVIDIA developer blog](https://developer.nvidia.com/blog/nvidia-rtx-innovations-are-powering-the-next-era-of-game-development/)

**Indiana Jones and the Great Circle (MachineGames; MOTOR engine, derived from idTech 7; Vulkan)** [Dec 2024 game; analysis 2025–2026]
- The PT mode is called "Full Ray Tracing". NVIDIA's analysis ran 4K output with DLSS-RR Performance, so **path tracing ran at 1080p**, on an RTX 5080 with a 26.9M-triangle acceleration structure. — [NVIDIA dev blog: OMMs and dynamic BLAS compaction](https://developer.nvidia.com/blog/path-tracing-optimizations-in-indiana-jones-opacity-micromaps-and-compaction-of-dynamic-blass)
- Named passes:
  - `TraceMain`: up to **2 bounces**, reuses SHaRC cached results.
  - `SharcUpdate`: up to **4 bounces**, updates the SHaRC radiance cache.
  - `SunTracing`: sun shadows.
  — same source
- The engine is hybrid. Ray-gen shaders use `traceRay` with any-hit shaders for alpha testing, while shadow rays use `rayQueryEXT` because "Forward+ rendering traces some rays inside fragment shaders", so a ray-gen shader is not an option. Before OMMs, any-hit shaders took 17% of TraceMain shader samples. — same source
- An earlier NVIDIA post on SER-related ray-gen optimizations made TraceMain 25% cheaper in the intro scene (cited in the same article). TechSpot reports about 24% from SER. — same source; [TechSpot](https://techspot.com/news/112130-nvidia-research-promises-2x-3x-faster-path-tracing.html) (403 on fetch; figure from search snippet)
- A Vulkanised 2026 talk (MachineGames/NVIDIA) says everything must be in the BVH, including hair. Because of that, the PT mode has no hair in reflections and no hair shadows on surrounding geometry. — [Vulkanised 2026, Jiho Choi (NVIDIA)](https://vulkan.org/user/pages/09.events/vulkanised-2026/1145-Jiho-Choi-NVIDIA%201.pdf)
- id Software's SIGGRAPH 2025 Advances talk (irradiance caching for GI) says Indiana Jones used "an early iteration" of that work. This is the non-PT GI path. — [SOUSA SIGGRAPH 2025](https://www.advances.realtimerendering.com/s2025/content/SOUSA_SIGGRAPH_2025_Final.pdf)
- (OMM and BLAS numbers are in section 4.)

**DOOM: The Dark Ages (id Tech 8)** [2025]
- The base game requires RT hardware and uses ray-traced GI on all platforms, plus RT reflections on PC. The path tracing plus DLSS RR update shipped June 18, 2025. — [Windows Central](https://www.windowscentral.com/gaming/pc-gaming/doom-the-dark-ages-path-tracing-june-2025); [Wccftech](https://wccftech.com/path-tracing-took-six-months-to-implement-doom-the-dark-ages/)
- Billy Khan (id, director of engine technology) on the path tracing work:
  - It took about **6 months** to implement and is now "part of id Tech 8", evaluated per game.
  - "Some of our biggest optimizations for path tracing resulted from utilizing shader execution reordering."
  - BLAS **refits "can be up to 10x faster" than full rebuilds**.
  - OMM is used for vegetation and particles.
  - Shading is among the highest PT costs.
  - Ray Reconstruction is a "strong improvement in performance and quality over traditional denoising".
  - His advice: start with general ray tracing before path tracing, and keep total shader count and complexity low.
  - PT is more accurate than probe-based GI for indirect emissive light, soft shadows, and rough reflections.
  — [NVIDIA dev blog Q&A](https://developer.nvidia.com/blog/how-id-software-used-neural-rendering-and-path-tracing-in-doom-the-dark-ages)
- The blog gives no ReSTIR, bounce-count, spp or API (Vulkan vs DX) details. — same source

**Black Myth: Wukong (Unreal Engine 5 plus NVIDIA RTX technologies)** [2024]
- The "Full Ray Tracing" mode covers ray-traced GI, AO, reflections and shadows. It uses ReSTIR for GI and shadows, and NVIDIA describes "order-independent transparencies for large sprite sets using two-level ray tracing" to show particles in reflections. — [TweakTown](https://www.tweaktown.com/news/99994/nvidia-showcases-black-myth-wukongs-impressive-full-ray-tracing-mode/index.html); [NVIDIA GeForce news](https://www.nvidia.com/en-eu/geforce/news/black-myth-wukong-full-ray-tracing-dlss-3.md)
- I could not find which NvRTX/RTXDI version it used or whether it uses ReSTIR PT. See Gaps.

**Portal with RTX / Half-Life 2 RTX / RTX Remix** [2022–2025]
- The Half-Life 2 RTX demo and the official RTX Remix release came on March 18, 2025.
  - Remix shipped **Neural Radiance Cache (NRC)** as its first public neural shader. NVIDIA describes it as "a grid of neural networks spanning screen and world space" that trains on live gameplay to compute indirect light, up to 15% faster in HL2 RTX (NVIDIA's claim).
  - **RTX Volumetrics** uses a volume-based ReSTIR for volumetric light and shadows.
  — [NVIDIA GeForce news](https://www.nvidia.com/en-eu/geforce/news/rtx-remix-half-life-2-rtx-demo-launching-march-18); [NVIDIA dev blog GDC 2025](https://developer.nvidia.com/blog/nvidia-rtx-advances-with-neural-rendering-and-digital-human-technologies-at-gdc-2025/); [TweakTown on RTX Volumetrics](https://www.tweaktown.com/news/103866/rtx-volumetrics-is-another-step-forward-for-path-tracing-visuals-on-geforce-gpus/index.html)
- NRC paper basis: Müller et al. 2021, "Real-time Neural Radiance Caching for Path Tracing". It reports that ReSTIR plus NRC reduces MRSE by 1–2 orders of magnitude at small cost. — [arXiv 2106.12372](https://arxiv.org/pdf/2106.12372)
- Portal RTX used ReSTIR for GI and shadows (TechSpot search snippet). — [TechSpot](https://techspot.com/news/112130-nvidia-research-promises-2x-3x-faster-path-tracing.html)
- [Training knowledge] The Remix runtime is open source, based on DXVK (it translates D3D8/9 to Vulkan) and path traces with RTXDI/ReSTIR GI plus NRD/DLSS-RR. — [dxvk-remix on GitHub](https://github.com/NVIDIAGameWorks/dxvk-remix)
- HL2 RTX is listed as supporting DLSS 4.5 Ray Reconstruction. — [NVIDIA Gamescom 2026](https://www.nvidia.com/en-us/geforce/news/gamescom-2026-dlss-4-5-ray-reconstruction-release-announcements-trailers/)

**Unreal Engine 5: Path Tracer vs Lumen vs MegaLights**
- The UE **Path Tracer** is a separate progressive renderer:
  - It requires Hardware Ray Tracing to be enabled and accumulates samples over time, showing pixels as they fill in.
  - It is enabled from the viewport's View Modes ("Path Tracing") and configured in the Post Process Volume "PathTracing" category.
  - Its denoiser toggle applies whichever denoiser plugin is loaded (Intel Open Image Denoise by default) to the final sample.
  - Projects can disable the path-tracer shader permutation to save compile time.
  — [Epic docs: Path Tracer](https://dev.epicgames.com/documentation/en-us/unreal-engine/path-tracer-in-unreal-engine); [Epic: RT and Path Tracer properties](https://dev.epicgames.com/documentation/unreal-engine/ray-tracing-and-path-tracer-features-properties-in-unreal-engine)
- Lumen (indirect light and reflections) and MegaLights (many shadowed direct lights, stochastic) are the real-time path. The split is "MegaLights handles direct light; Lumen handles indirect light and reflections" (community source).
  - UE 5.7 moves MegaLights to **Beta**, merges the downsample/checkerboard CVars into `r.MegaLights.DownsampleMode`, and adds `r.MegaLights.HardwareRayTracing.ForceTwoSided`.
  - Lumen in 5.7 deprecates the software-RT "detail traces" path and targets hardware RT at 60 Hz.
  — [Tom Looman: UE 5.7 performance highlights](https://tomlooman.com/unreal-engine-5-7-performance-highlights/) (secondary but detailed)
- A claim that MegaLights became production-ready in UE 5.8 (released 23 June 2026) appears only on a third-party blog and was **not verified**. — [colonized.work](https://colonized.work/blog/megalights-unreal-engine-5-explained/)
- ReSTIR PT exists in NVIDIA's **NvRTX** UE5 branch, not in stock UE (per a roughly 14-month-old NVIDIA dev blog cited in search results). — [OC3D](https://overclock3d.net/news/software/nvidia-restir-pt-update-makes-path-tracing-2-3x-faster/)

**Unity HDRP path tracer**
- Accumulates paths per pixel up to a maximum count and restarts accumulation when the camera moves. — [HDRP 13.0 Path Tracing](https://docs.unity3d.com/Packages/com.unity.render-pipelines.high-definition@13.0/manual/Ray-Tracing-Path-Tracing.html); [HDRP 17.1 "Understand path tracing"](https://docs.unity3d.com/Packages/com.unity.render-pipelines.high-definition@17.1/manual/path-tracing-understand.html)
- Denoising is available only with the Unity Denoising Package installed; the default is None. — [HDRP 17.0 path tracing reference](https://docs.unity3d.com/Packages/com.unity.render-pipelines.high-definition@17.0/manual/reference-path-tracing.html)
- It runs only on DX12 platforms. Ribbon hair does not work, and older versions also exclude MSAA and Graphics.DrawMesh. — [HDRP 16.0 limitations](https://docs.unity3d.com/Packages/com.unity.render-pipelines.high-definition@16.0/manual/path-tracing-limitations.html); [HDRP 13.1](https://docs.unity3d.com/Packages/com.unity.render-pipelines.high-definition@13.1/manual/Ray-Tracing-Path-Tracing.html)
- The Unity Recorder "Accumulation" feature is how converged path-traced frames are produced for offline output. — [Unity Recorder accumulation](https://docs.unity3d.com/Packages/com.unity.recorder@5.1/manual/RecordingAccumulationPathTracing.html)

**EA SEED PICA PICA / Halcyon, and Frostbite** [Older: 2018–2021]
- PICA PICA was built on SEED's R&D engine Halcyon with DXR. Its pipeline is **hybrid**: raster, compute and ray-tracing shaders work together. It is documented in Ray Tracing Gems ch. 25 (Barré-Brisebois, Halén, Wihlidal, Lauritzen, Bekkers, Stachowiak, Andersson). — [SEED: hybrid rendering](https://ea.com/seed/news/hybrid-rendering-for-real-time-ray-tracing); [RTG ch. 25 PDF](https://media.contentapi.ea.com/content/dam/ea/seed/presentations/2019-ray-tracing-gems-chapter-25-barre-brisebois-et-al.pdf)
- **GIBS (surfel GI)** started in PICA PICA (Tomasz Stachowiak). Surfels are spawned on the fly, with no precomputation, special meshes or UVs. The original uniform grid did not scale to game-size levels; the SIGGRAPH 2021 Advances talk (Halén and Hayward, with Frostbite contributors) covers the scaled-up version. — [SIGGRAPH Advances 2021 Surfel GI](https://www.advances.realtimerendering.com/s2021/SIGGRAPH%20Advances%202021%20-%20Surfel%20GI.pdf)
- I found no public Frostbite full path tracing game mode. See Gaps.

**Embark Studios kajiya** [Older; archived]
- **Archived and no longer maintained**; it was a spare-time experiment, not a production renderer. Rust plus Vulkan (ash), with HLSL shaders and some rust-gpu.
- Architecture is hybrid raster, compute and RT:
  - a volumetric, temporally recurrent irradiance cache handles multiple bounces;
  - RT diffuse final gather adds high-frequency detail;
  - RT specular falls back to diffuse after the first hit;
  - sun soft shadows are denoised with a FidelityFX-Shadow-Denoiser-based filter;
  - temporal super-resolution renders at reduced internal resolution and reconstructs;
  - a reference path-tracing mode is included.
- Runs on RTX and RX 6000+. Only glTF metallic-roughness materials are supported.
- The README does not mention ReSTIR, though my training knowledge says kajiya used ReSTIR-style GI resampling.
— [kajiya README](https://github.com/EmbarkStudios/kajiya)

**NVIDIA Falcor and the RTX Path Tracing SDK (RTXPT)** [current]
- RTXPT is at **v1.9.0**. It is a **pure path tracer with no rasterization**: one unified path-tracing shader handles all light transport, using light-sampling and radiance caches. It has **reference** and **real-time** modes (reference includes a photo mode with the OptiX denoiser). — [RTXPT GitHub](https://github.com/NVIDIA-RTX/RTXPT)
- Origin: the Falcor research path tracer, ported to the Donut framework. — same
- Features:
  - simple extensible BSDF, volumes, nested dielectrics with priority;
  - analytic, emissive-triangle and environment lights;
  - **NEE-AT** (feedback-based, temporally adaptive guided light importance sampling), which in 1.9.0 handles up to 2M lights and clusters emissive triangles into light LODs;
  - Owen-scrambled low-discrepancy sampling, ray cones for MIP selection, Russian roulette, firefly filter, TAA, tonemapping, bloom;
  - RTXDI for ReSTIR DI and ReSTIR GI;
  - SHaRC on by default;
  - NRD ReLAX/ReBLUR with up to 3-layer path-space decomposition, plus DLSS-RR;
  - SER (`--noSER` flag) and OMM 1.10.
  — same
- Requirements: DXR 1.1 minimum (the default Agility SDK path uses DXR 1.2 / SM 6.9), SM 6.6 and Resource Binding Tier 3, driver 595.71+, VS2022, CMake 4.1+. Vulkan is a second backend (SER/OMM on Vulkan are works in progress). — same
- **AMD:** no official support. Known issues list black or incorrect transparencies and reflections on some AMD systems (attributed to a driver bug) and a possible TDR on Vulkan TLAS builds with null instances. — same

**AMD: Capsaicin, FSR Redstone, Brixelizer, HIP RT**
- **Capsaicin** is AMD ARR's experimental D3D12 research framework (MIT). It now ships **GI-1.2**: two levels of radiance caching for diffuse and specular indirect, plus direct light from emissive meshes and skylight via the probe system. It builds on GI-1.0/1.1, includes a reference path tracer, and needs D3D12 Ultimate hardware. It depends on the `gfx` D3D12 abstraction. ReSTIR is not mentioned. — [Capsaicin GitHub](https://github.com/GPUOpen-LibrariesAndSDKs/Capsaicin)
- FSR Redstone ML denoising and radiance caching are covered in section 3. They are RDNA 4 only.
- Brixelizer GI and HIP RT: not researched in this session. See Gaps.

**Newer 2025–2026 path-traced titles** (NVIDIA-promoted)
- **007 First Light**: path tracing plus DLSS 4.5 RR update shipped Sept 15, 2026. — [NVIDIA](https://www.nvidia.com/en-us/geforce/news/007-first-light-path-tracing-dlss-4-5-ray-reconstruction-update-out-now/)
- **CONTROL Resonant** (Remedy): launched Sept 24, 2026 with path tracing, DLSS 4.5 and **RTX Mega Geometry**. — [NVIDIA Gamescom 2026](https://www.nvidia.com/en-us/geforce/news/gamescom-2026-dlss-4-5-ray-reconstruction-release-announcements-trailers/); [NVIDIA GDC 2026](https://www.nvidia.com/en-us/geforce/news/dlss-4-5-rtx-path-tracing-game-announcements-gdc-2026/)
- **Tides of Annihilation** (launches with PT); **Directive 8020** (path-traced lighting and effects with RR, May 12 launch); **Sea of Remnants** (path-traced effects with RR). — [NVIDIA GDC 2026](https://www.nvidia.com/en-us/geforce/news/dlss-4-5-rtx-path-tracing-game-announcements-gdc-2026/)
- **Resident Evil Requiem, PRAGMATA, Phantom Blade Zero** are listed as path traced on NVIDIA's RTX page. — [NVIDIA GeForce RTX](https://www.nvidia.com/en-us/geforce/rtx/)
- **Gears of War: E-Day** (UE5): RTX Mega Geometry (full Nanite detail in the BVH), Oct 6, 2026. It is an early adopter of clustered geometry through NVIDIA's vendor API. — [NVIDIA Gamescom 2026](https://www.nvidia.com/en-us/geforce/news/gamescom-2026-dlss-4-5-ray-reconstruction-release-announcements-trailers/); [Microsoft CLAS/PTLAS preview](https://devblogs.microsoft.com/directx/clas-ptlas-preview/)

### Inferences
- The common shipped recipe is: raster G-buffer → RTXDI/ReSTIR DI for direct light → 1–2 indirect bounces (often ReSTIR GI) → terminate into SHaRC or NRC → DLSS-RR. Indiana Jones is the clearest public breakdown (2 bounces + SHaRC, 4-bounce cache update, 1080p internal for 4K).
- Every shipped full-PT game mode is NVIDIA-partnered. On AMD these modes run cross-vendor DXR/Vulkan code, but the NVIDIA-tuned pieces (SER reordering, DLSS-RR) do not help there. For a student targeting both GPUs, the architecture should not depend on DLSS-RR or SER for correctness, only for speed or quality.
- For a learning engine, RTXPT is the closest open, DX12, pure-PT production-grade reference. Capsaicin is the AMD-side DX12 reference for cache-based GI.

### Gaps
- No primary source for Cyberpunk's default bounce count or spp, or for whether Cyberpunk uses SHaRC or NRC (only a community mod exposes SHaRC). The GDC 2023 talk slides were not found.
- Alan Wake 2 GDC 2024 talk slides and transcript were not retrieved (the recording sits behind the GDC Vault login), so no BVH or bounce specifics.
- Black Myth: Wukong internals (RTXDI version, bounces) were not found.
- Frostbite: no public full PT mode found. Battlefield 6 (2025) RT status was not researched.
- AMD Brixelizer GI and HIP RT were not researched. HIP RT is a HIP/ROCm library, not DX12, so it is likely irrelevant to a DX12 engine; verify.
- No Digital Foundry primary breakdown was fetched. Their videos are the usual source for spp and bounce observations.

---

## 2. Light transport algorithms in use

### Takeaway
Production real-time PT is **ReSTIR DI (RTXDI) for direct lighting plus a short indirect path (1–2 bounces, sometimes ReSTIR GI) that ends in a world-space radiance cache** (SHaRC hash grid, or NRC on RTX Remix; AMD's neural radiance cache is in preview). Shipped titles trace roughly 1 path per *internal* pixel at a quarter of output resolution. Full **ReSTIR PT** (Lin 2022) and its 2026 "Enhanced" version are research or NvRTX-only and have no confirmed shipping game.

### Cited findings
- **ReSTIR DI**: Bitterli et al. 2020, spatiotemporal reservoir resampling for many dynamic lights. [Training knowledge] — [Bitterli ReSTIR page](https://benedikt-bitterli.me/restir/); overview at [Wikipedia: Spatiotemporal reservoir resampling](https://en.wikipedia.org/wiki/Spatiotemporal_reservoir_resampling)
- **ReSTIR GI**: Ouyang et al. 2021, path resampling for indirect. [Training knowledge] — [NVIDIA Research](https://research.nvidia.com/publication/2021-06_restir-gi-path-resampling-real-time-path-tracing)
- **ReSTIR PT / GRIS**: Lin et al. 2022, generalized resampled importance sampling with shift mappings such as hybrid reconnection. [Training knowledge] — [NVIDIA Research](https://research.nvidia.com/publication/2022-07_generalized-resampled-importance-sampling-foundations-restir)
- **SIGGRAPH 2023 course** "A Gentle Introduction to ReSTIR" (Wyman, Kettunen, Lin, Bitterli, Yuksel, Jarosz, Kozlowski) is the recommended entry point. [Training knowledge] — [intro-to-restir.cwyman.org](https://intro-to-restir.cwyman.org/)
- **ReSTIR PT Enhanced** (Daqi Lin, Markus Kettunen, Chris Wyman; April 2026): NVIDIA reports 2–3x faster ReSTIR PT, up to 2.74x with no quality loss, halved spatial-reuse overhead, and lower error. It is described as closer to "production-ready", but the game timeline is unclear. — [TechPowerUp](https://www.techpowerup.com/348358/nvidia-develops-2-3x-faster-real-time-path-tracing-with-better-image-quality); [Notebookcheck](https://www.notebookcheck.net/Nvidia-s-new-technology-could-revolutionize-path-tracing-with-speeds-up-to-2-74-times-faster.1280515.0.html); [Guru3D](https://www.guru3d.com/story/nvidia-previews-faster-restir-pt-enhanced-realtime-path-tracing-technology/)
- **ReSTIR BDPT** (2025) adds bidirectional path tracing to ReSTIR PT for caustics and hard-to-reach lights. — [OC3D](https://overclock3d.net/news/software/nvidia-restir-pt-update-makes-path-tracing-2-3x-faster/)
- Per OC3D, ReSTIR PT was available only in NVIDIA's NvRTX UE5 branch, and no PC game used it. That article is older, so treat this as possibly outdated. — [OC3D](https://overclock3d.net/news/software/nvidia-restir-pt-update-makes-path-tracing-2-3x-faster/)
- **RTXDI SDK** is used by RTXPT for both ReSTIR DI and ReSTIR GI. [Cross-vendor HLSL] — [RTXPT](https://github.com/NVIDIA-RTX/RTXPT); [Training knowledge] repo at [NVIDIA-RTX/RTXDI](https://github.com/NVIDIA-RTX/RTXDI)
- **SHaRC**:
  - "operates in world space and provides a radiance value at any hit point". It is shader-only source with an integration guide; samples live in the RTXGI SDK. — [SHARC GitHub](https://github.com/NVIDIA-RTX/SHARC); [Integration.md](https://github.com/NVIDIAGameWorks/Spatial-Hash-Radiance-Cache/blob/main/docs/Integration.md)
  - RTXGI 2.0 (March 2024) ships SHaRC plus an experimental NRC, for DX12 and Vulkan. — [RTXGI QuickStart](https://github.com/NVIDIAGameWorks/RTXGI/blob/main/docs/QuickStart.md); [Wccftech on RTXGI 2.0](https://wccftech.com/nvidia-rtxgi-2-0-available-next-frontier-ray-traced-visuals-neural-radiance-cache-spatial-hash-radiance-cache-dynamic-diffuse-global-illumination/)
  - Production use: Indiana Jones `SharcUpdate` (up to 4 bounces), with `TraceMain` reusing the cache after up to 2 bounces. — [NVIDIA Indiana Jones blog](https://developer.nvidia.com/blog/path-tracing-optimizations-in-indiana-jones-opacity-micromaps-and-compaction-of-dynamic-blass)
  - [Inference] SHaRC is plain HLSL hash-grid code with no NVIDIA-only intrinsics required, so it should run on AMD. The repo page does not state vendor support; verify.
- **NRC** (neural radiance cache) is used in RTX Remix / HL2 RTX (up to 15% faster per NVIDIA) and is labeled experimental in RTXGI. [NV-oriented; it needs tensor-core-style inference] — [NVIDIA GeForce news](https://www.nvidia.com/en-eu/geforce/news/rtx-remix-half-life-2-rtx-demo-launching-march-18); [Wccftech on RTXGI 2.0](https://wccftech.com/nvidia-rtxgi-2-0-available-next-frontier-ray-traced-visuals-neural-radiance-cache-spatial-hash-radiance-cache-dynamic-diffuse-global-illumination/)
- **AMD FSR Radiance Caching** (v0.9.0, technical preview, RDNA 4 only, DX12): predicts light from sparse path-traced samples and replaces many secondary bounces. Demonstrated in Warhammer 40,000: Darktide (Fatshark Stingray), with production release slated for 2026. [AMD-only] — [GPUOpen: FSR Redstone for developers](https://gpuopen.com/learn/amd-fsr-redstone-developers-neural-rendering/); [GPUOpen FSR SDK](https://gpuopen.com/amd-fsr-sdk/)
- **Light-sampling and path guiding**: RTXPT's NEE-AT is the shipping-grade "real-time guiding" of light selection, feedback-based and temporally adaptive. RTXPT also uses Russian roulette and a firefly filter. — [RTXPT](https://github.com/NVIDIA-RTX/RTXPT)
- **World-space caches outside NVIDIA**: kajiya (volumetric temporally recurrent irradiance cache), Capsaicin GI-1.2 (two-level radiance caching), and SEED GIBS (surfels). — [kajiya](https://github.com/EmbarkStudios/kajiya); [Capsaicin](https://github.com/GPUOpen-LibrariesAndSDKs/Capsaicin); [Surfel GI 2021](https://www.advances.realtimerendering.com/s2021/SIGGRAPH%20Advances%202021%20-%20Surfel%20GI.pdf)
- **Samples and bounces in practice**:
  - Indiana Jones: 2 bounces in the main trace, 4 in the cache update, at 1080p for 4K output. — [NVIDIA Indiana Jones blog](https://developer.nvidia.com/blog/path-tracing-optimizations-in-indiana-jones-opacity-micromaps-and-compaction-of-dynamic-blass)
  - Cyberpunk mod defaults: 2 rays and 2 bounces (mod, not game). — [AdvancedPathTracingCP2077](https://github.com/codecrafting-io/AdvancedPathTracingCP2077)
  - Cyberpunk "635 ray calculations per pixel" is a marketing average, not an spp. — [Tom's Hardware](https://www.tomshardware.com/features/cyberpunk-2077-rt-overdrive-path-tracing-full-path-tracing-fully-unnecessary)

### Inferences
- A realistic student plan: (1) a reference accumulation path tracer with NEE, MIS and Russian roulette; (2) ReSTIR DI for direct light; (3) a short indirect path terminated into a SHaRC-style hash-grid cache; (4) optionally ReSTIR GI. Each step is cross-vendor HLSL. ReSTIR PT is the "stretch goal" and is still not shipping in games.
- Neural caches are vendor-split today: NRC needs NVIDIA tooling, and FSR Radiance Caching is RDNA 4-only and in preview. A cross-vendor neural approach would have to wait for SM 6.10 linear algebra (section 4).

### Gaps
- I found no primary statement of spp or bounce defaults for Cyberpunk, Alan Wake 2, Black Myth or DOOM TDA.
- I found no shipped game confirmed to use ReSTIR PT or ReSTIR PT Enhanced.
- I found no real-time path guiding (beyond light-selection guiding such as NEE-AT) confirmed in a shipped game.

---

## 3. Denoising and reconstruction pipelines

### Takeaway
The shipped standard is **one neural pass that denoises and upscales at once** (DLSS Ray Reconstruction, now DLSS 4.5 with a second-generation transformer), which replaces NRD plus a separate upscaler. NRD (ReBLUR/ReLAX/SIGMA) remains the cross-vendor fallback and runs at render resolution before the upscaler. AMD's FSR Ray Regeneration (RDNA 4 only) is a **decoupled** ML denoiser dispatched separately at render resolution, then followed by FSR upscaling.

### Cited findings
- **DLSS 4.5 Ray Reconstruction** (August 25, 2026 early access; official release in September; driver 580.88+) "unifies denoising and Super Resolution into a single model". It is a second-generation transformer with 35% more compute and 20% more parameters at similar cost, better use of engine pixel sampling and motion data, and finer developer control of temporal accumulation. 30 games were supported at that time, including Cyberpunk, Alan Wake 2, Indiana Jones, DOOM TDA, Portal RTX and HL2 RTX. It runs on all RTX GPUs. [NV-only] — [NVIDIA Gamescom 2026](https://www.nvidia.com/en-us/geforce/news/gamescom-2026-dlss-4-5-ray-reconstruction-release-announcements-trailers/)
- Billy Khan (id): transformer RR beats CNN because it combines spatial and temporal data better, evaluates all pixels together, and reconstructs surface detail more accurately. SR plus RR together improve quality *and* performance compared with traditional denoisers. — [NVIDIA dev blog, DOOM TDA](https://developer.nvidia.com/blog/how-id-software-used-neural-rendering-and-path-tracing-in-doom-the-dark-ages)
- Indiana Jones runs DLSS-RR in Performance mode, so PT runs at 1080p for 4K. — [NVIDIA Indiana Jones blog](https://developer.nvidia.com/blog/path-tracing-optimizations-in-indiana-jones-opacity-micromaps-and-compaction-of-dynamic-blass)
- **NRD**: RTXPT integrates ReLAX and ReBLUR with up to 3-layer path-space decomposition, alongside DLSS-RR and OptiX for photo mode. Cyberpunk Overdrive launched with NRD. [Cross-vendor] — [RTXPT](https://github.com/NVIDIA-RTX/RTXPT); [NVIDIA Overdrive launch](https://www.nvidia.com/en-gb/geforce/news/cyberpunk-2077-ray-tracing-overdrive-update-launches-april-11); [Training knowledge] NRD repo: [NVIDIA-RTX/NRD](https://github.com/NVIDIA-RTX/NRD) (ReBLUR = diffuse/specular radiance, ReLAX = RTXDI-tuned, SIGMA = shadows)
- **AMD FSR Ray Regeneration 1.2.1** (October 2026, FSR SDK 2.4), an ML denoiser. [AMD-only, RDNA 4]
  - Requires RX 9000 (RDNA 4), DX12 with SM 6.6, Windows 11. No Vulkan.
  - Signals: AO, direct diffuse, direct specular, dominant-light visibility, indirect diffuse, indirect specular, specular occlusion.
  - Decoupled from upscaling as its own dispatch, giving "optimal results when paired with FSR Upscaling".
  - Cost on RX 9070 XT for 4 signals: 0.99 ms at 960×540, 3.74 ms at 1080p, 7.15 ms at 1440p.
  — [GPUOpen: FSR Ray Regeneration](https://gpuopen.com/amd-fsr-rayregeneration/)
  - AMD's product page lists it as enabled for Call of Duty: Black Ops 7 only (search summary). — [AMD FSR page](https://www.amd.com/en/products/graphics/technologies/fidelityfx/super-resolution.html)
- FSR Redstone (ML upscaling, frame generation, ray regeneration, radiance caching) launched for RDNA 4. Analytical fallbacks exist only for upscaling and frame generation on older GPUs. — [GPUOpen Redstone for developers](https://gpuopen.com/learn/amd-fsr-redstone-developers-neural-rendering/); [VideoCardz](https://videocardz.com/newz/amd-fsr-redstone-launched-ml-based-upscaling-frame-gen-and-ray-regeneration-for-radeon-rx-9000-series); [TechPowerUp review](https://www.techpowerup.com/review/amd-fsr-4-redstone/)
- **Offline and reference denoisers**: the UE Path Tracer uses Intel Open Image Denoise by default on the final accumulated frame. RTXPT photo mode uses OptiX. Unity HDRP uses its Denoising package. — [Epic Path Tracer](https://dev.epicgames.com/documentation/en-us/unreal-engine/path-tracer-in-unreal-engine); [RTXPT](https://github.com/NVIDIA-RTX/RTXPT); [HDRP 17.0](https://docs.unity3d.com/Packages/com.unity.render-pipelines.high-definition@17.0/manual/reference-path-tracing.html)

### Inferences
- Frame order for a student engine:
  - **NRD path:** trace at render resolution → NRD at render resolution (needs hit distance, normal/roughness, view-Z, motion vectors; demodulate albedo) → remodulate → TAA or upscaler → post.
  - **DLSS-RR path:** trace → hand noisy color plus guide buffers directly to RR, which outputs at display resolution.
  - **FSR path:** trace → Ray Regeneration at render resolution → FSR upscaling.
- Designing the G-buffer and guide-buffer interface around NRD's inputs (which overlap heavily with RR and Ray Regeneration) keeps all three options open.
- For the RTX plus RX 9070 XT setup, the only denoiser that runs on both is NRD (or a hand-written SVGF/A-SVGF). DLSS-RR is NVIDIA-only and Ray Regeneration is RDNA 4-only.

### Gaps
- The exact Ray Regeneration input list (beyond signal types) and its position relative to upscaling are not stated explicitly. "Before upscaling at render resolution" is inferred from its render-resolution timing table.
- Intel's real-time PT denoising work (for example XeSS-related or neural denoisers) was not researched.
- No 2026 NRD version or changelog was fetched.

---

## 4. DXR API choices and acceleration-structure management

### Takeaway
**DXR 1.2 shipped in retail** with Agility SDK **1.619** (February 26, 2026) and SM 6.9. It adds Shader Execution Reordering and Opacity Micromaps, including RayQuery+OMM. SER is a required SM 6.9 feature, but **only NVIDIA RTX 40+ and Intel Arc B actually reorder; AMD RX 9000 accepts the API and does not reorder**. The next step, **Clustered Geometry, Partitioned TLAS and indirect AS builds**, entered preview in Agility **1.722** (October 28, 2026). Cooperative Vectors was deprecated in favor of SM 6.10 linear algebra. Productions mix pipelines (traceRay with any-hit and closest-hit) and inline RayQuery: refit dynamic BLASes, compact them, and use OMM for alpha.

### Cited findings
- **Agility SDK timeline**:
  - 1.616.0 (May 30, 2025): OMM retail (RayQuery+OMM still needed SM 6.9).
  - 1.717.0-preview (May 30, 2025): SM 6.9 preview with Cooperative Vectors, SER, RayQuery+OMM.
  - **1.619.0 (February 26, 2026): SM 6.9 retail**, with SER and OMM as DXR 1.2, plus long vectors.
  - 1.720.0-preview (April 27, 2026): SM 6.10 preview with linalg::Matrix and the RT intrinsics TriangleObjectPositions and ClusterID.
  - **1.722.0/1.722.1-preview (October 28 / November 9, 2026): clustered geometry, partitioned TLAS, indirect AS builds.**
  - Latest retail is 1.620.2 (November 9, 2026).
  — [Agility SDK downloads](https://devblogs.microsoft.com/directx/directx12agility/); [NuGet 1.619.6](https://www.nuget.org/packages/Microsoft.Direct3D.D3D12/1.619.6)
- **SM 6.9 retail post**:
  - DXC is version 1.9.2602.16 (the downloads section says .17).
  - What is new for SER since preview: apps can query whether the device actually reorders.
  - Cooperative Vector "has been deprecated in favor of a future design" unifying matrix-matrix and vector-matrix in SM 6.10.
  - Vendor table:
    - SER: AMD RX 9000 supports the API but **does not reorder**; Intel Arc B supports it and reorders; NVIDIA RTX 40xx+ supports it and reorders.
    - OMM: all RTX GPUs, hardware-accelerated on RTX 40xx+, software-emulated on older; AMD and Intel not listed.
  - Drivers: AMD Adrenalin 26.2.1, NVIDIA 595+.
  — [Microsoft DirectX blog: SM 6.9 retail](https://devblogs.microsoft.com/directx/shader-model-6-9-retail-and-more/)
- SER is a required SM 6.9 feature: all drivers must accept SER code, and devices exploit it if they can. — [Microsoft DirectX blog (search summary)](https://devblogs.microsoft.com/directx/shader-model-6-9-retail-and-more/); [PC Gamer](https://www.pcgamer.com/hardware/graphics-cards/microsofts-update-for-direct3d-with-opacity-micromaps-and-shader-execution-reordering-now-official-features-will-probably-mean-little-to-gamers-but-graphics-devs-are-going-to-be-happy/)
- Tom's Hardware headline: Intel Arc B-series shows up to 90% uplift with SER (benchmark not inspected). — [Tom's Hardware](https://www.tomshardware.com/pc-components/gpus/microsoft-adds-shader-execution-reordering-ser-in-latest-directx-sdk-for-more-efficient-ray-tracing-intel-arc-b-series-gpus-show-90-percent-performance-uplift)
- [Training knowledge] The DXR 1.2 SER API is `dx::HitObject` (TraceRay/FromRayQuery/Invoke) plus `dx::MaybeReorderThread(hitObject, coherenceHint, numHintBits)`, usable from raygen. Before DXR 1.2, NVIDIA exposed SER through NVAPI HLSL extensions [NV-only]. — [DirectX-Specs: Raytracing](https://microsoft.github.io/DirectX-Specs/d3d/Raytracing.html)
- **SER in production**:
  - DOOM TDA: "biggest optimizations" came from SER. — [NVIDIA dev blog, DOOM](https://developer.nvidia.com/blog/how-id-software-used-neural-rendering-and-path-tracing-in-doom-the-dark-ages)
  - Indiana Jones: TraceMain about 25% cheaper. — [NVIDIA Indiana Jones blog](https://developer.nvidia.com/blog/path-tracing-optimizations-in-indiana-jones-opacity-micromaps-and-compaction-of-dynamic-blass)
  - Alan Wake 2 GDC talk covered SER. — [Remedy GDC 2024](https://www.remedygames.com/article/gdc2024)
- **Pipelines vs inline RayQuery in production**:
  - Indiana Jones uses `traceRay` from raygen with any-hit alpha tests for main paths, and RayQuery for shadow rays because some rays are traced from Forward+ fragment shaders. — [NVIDIA Indiana Jones blog](https://developer.nvidia.com/blog/path-tracing-optimizations-in-indiana-jones-opacity-micromaps-and-compaction-of-dynamic-blass)
  - RTXPT uses a DXR pipeline (DXR 1.1 minimum). — [RTXPT](https://github.com/NVIDIA-RTX/RTXPT)
  - [Training knowledge] AMD's RDNA guidance generally favors inline RayQuery from compute for performance on RDNA. NVIDIA's best practices suggest RayQuery for simple rays (shadows/AO) and pipelines for many-material shading with SER. — [GPUOpen RDNA performance guide](https://gpuopen.com/learn/rdna-performance-guide/); [NVIDIA RT best practices](https://developer.nvidia.com/blog/best-practices-for-using-nvidia-rtx-ray-tracing-updated/)
  - SER can also wrap RayQuery results via HitObject in DXR 1.2 [training knowledge, verify in the spec].
- **Opacity Micromaps**: Indiana Jones data. [HW-accelerated on NV RTX 40/50; DXR 1.2 or NVAPI on DX12]
  - The four states are Opaque, Unknown-Opaque, Unknown-Transparent and Transparent. Unknown states fall back to any-hit or RayQuery.
  - Shadow rays use 4-state (conservative). Indirect rays force the 2-state approximation (no shader alpha test at all).
  - Results: TraceMain 7.90 → 3.58 ms (−55%), SharcUpdate 1.99 → 0.89 ms (−55%), SunTracing 0.83 → 0.71 ms (−14%).
  - PREFER_FAST_TRACE micromap builds gave about 5% faster TraceMain.
  - Baking is offline and CPU-only in the cooker (no GPUs on cook machines), with format OC1_4_State, ForceOpaque unknown-state promotion, max subdivision 6 and dynamicSubdivisionScale 2.0.
  - Budget: 0.5 MB per OMM and 128 MB per map. One OMM hit 43 MB. 95% of Peru OMMs baked in under 1 s and are under 200 kB. OMM SDK 1.7.0 added `maxArrayDataSize`.
  — [NVIDIA Indiana Jones blog](https://developer.nvidia.com/blog/path-tracing-optimizations-in-indiana-jones-opacity-micromaps-and-compaction-of-dynamic-blass)
- **Dynamic BLAS: refit vs rebuild vs compaction**:
  - Indiana Jones compacts **dynamic** vegetation BLASes too, with flags `PREFER_FAST_BUILD | ALLOW_UPDATE | ALLOW_COMPACTION`.
  - Constraint: a compacted BLAS can be refit but **not rebuilt**, because its size may change.
  - Vegetation sways and returns to a rest pose, so refit-only keeps quality.
  - BLAS VRAM went from 1027 MB to 606 MB (−41%). Compacted sizes are typically about 50% (range 30–60%). This let full PT run on 12 GB GPUs.
  — [NVIDIA Indiana Jones blog](https://developer.nvidia.com/blog/path-tracing-optimizations-in-indiana-jones-opacity-micromaps-and-compaction-of-dynamic-blass)
  - Refit is "up to 10x faster" than rebuild (id), and understanding the BLAS/TLAS relationship is key. — [NVIDIA dev blog, DOOM](https://developer.nvidia.com/blog/how-id-software-used-neural-rendering-and-path-tracing-in-doom-the-dark-ages)
  - Everything visible must be in the BVH; Indiana Jones' hair was not, causing artifacts. — [Vulkanised 2026](https://vulkan.org/user/pages/09.events/vulkanised-2026/1145-Jiho-Choi-NVIDIA%201.pdf)
- **Clustered geometry and partitioned TLAS** (DXR "Part 2", preview):
  - The spec is v0.20, dated March 18, 2026. The preview arrived October 28, 2026 in Agility 1.722, which Microsoft calls the most significant DXR update since 2018.
  - Clusters hold up to 256 vertices and 256 triangles, with per-cluster BVH builds. Cluster BLASes are built from CLAS references, and there is no position-only refit.
  - PTLAS splits the TLAS into partitions and updates only changed ones.
  - The Microsoft sample showed AS build time of about 788 ms → 3.11 ms on an RX 9070. Reported as needing no new hardware.
  — [Microsoft: CLAS/PTLAS preview](https://devblogs.microsoft.com/directx/clas-ptlas-preview/); [DirectX-Specs Raytracing2](https://microsoft.github.io/DirectX-Specs/d3d/Raytracing2.html); [Xenospectrum](https://xenospectrum.com/en/dxr-clustered-geometry-partitioned-tlas-preview/)
  - NVIDIA's vendor equivalent is RTX Mega Geometry, used in Gears of War: E-Day, CONTROL Resonant and Alan Wake 2. NVIDIA plans to bring it to "DXR 2.0". [NV-only via NVAPI today] — [NVIDIA Gamescom 2026](https://www.nvidia.com/en-us/geforce/news/gamescom-2026-dlss-4-5-ray-reconstruction-release-announcements-trailers/)

### Inferences
- For the student's DX12 engine (November 2026): target **Agility SDK 1.619+ / SM 6.9 / DXR 1.2**. Write the main path-tracing loop so SER (`MaybeReorderThread`) is an optional speed-up. It reorders on the RTX PC and is a harmless no-op on the RX 9070 XT.
- Inline RayQuery in compute is the simpler starting point and is AMD-friendly. A raygen-based pipeline gives SER plus hit-group material dispatch, which matters once material counts grow. A middle path is raygen plus HitObject with "uber-shader" material evaluation.
- Treat OMM as optional. It is hardware-accelerated only on RTX 40+, and AMD support is not stated in the table. Keep an any-hit or RayQuery alpha-test fallback.
- Build static BLASes with ALLOW_COMPACTION and compact them. For deforming meshes, build PREFER_FAST_BUILD|ALLOW_UPDATE, refit per frame, and rebuild periodically or on large deformation. Rebuild the TLAS every frame.

### Gaps
- No performance numbers were found comparing inline RayQuery against pipelines on RDNA 4 versus Ada/Blackwell in 2025–2026.
- AMD RX 9070 XT OMM support (hardware vs emulation vs none) is not stated in Microsoft's table.
- The exact HitObject/MaybeReorderThread signature and the RayQuery→HitObject path should be verified in the DirectX-Specs.

---

## 5. Material models in real-time PT, and reconciling raster PBR with path-traced materials

### Takeaway
Shipped real-time path tracers reuse the game's **existing raster PBR material** (metal/roughness GGX plus a diffuse lobe, with extensions) evaluated as a BSDF in hit shaders, rather than adopting OpenPBR or MaterialX. Research path tracers use a "simple, extensible BSDF" (RTXPT) or glTF metallic-roughness (kajiya). Public, detailed sources are thin.

### Cited findings
- RTXPT uses a "simple BSDF model" that is "easy to extend", plus volumes and nested dielectrics with priority. — [RTXPT](https://github.com/NVIDIA-RTX/RTXPT)
- kajiya supports only the glTF 2.0 metallic-roughness workflow. — [kajiya](https://github.com/EmbarkStudios/kajiya)
- Half-Life 2 RTX remade nearly every asset with PBR materials for path tracing; legacy materials were not path-tracing ready. — [NVIDIA GeForce news, HL2 RTX](https://www.nvidia.com/en-eu/geforce/news/rtx-remix-half-life-2-rtx-demo-launching-march-18)
- id: shading is among the highest PT costs, so keep shader count and complexity low. — [NVIDIA dev blog, DOOM](https://developer.nvidia.com/blog/how-id-software-used-neural-rendering-and-path-tracing-in-doom-the-dark-ages)
- [Training knowledge] OpenPBR Surface 1.0 (ASWF/MaterialX, by Autodesk and Adobe) was released in 2024. Its reference is offline-oriented and it is not yet used by shipped real-time PT games. — [OpenPBR spec](https://academysoftwarefoundation.github.io/OpenPBR/)

### Inferences
- Use one shared HLSL material-evaluation library (GGX specular with VNDF sampling, Lambert or Burley diffuse, optional clearcoat and transmission) called from both the raster deferred lighting and the PT hit shading. Then the reference PT validates the raster look. Alpha-tested materials need the any-hit/OMM path; emissives must feed the light-sampling structure (RTXPT's emissive-triangle lights / NEE-AT).

### Gaps
- No primary 2025–2026 source on how Cyberpunk, Alan Wake 2 or idTech reconcile raster and PT materials (for example, energy conservation fixes or roughness clamping for denoising). The GDC talks likely cover this but were not retrievable.
- MaterialX or OpenPBR adoption in real-time engines was not researched beyond the training-knowledge note.

---

## 6. How engines expose PT next to the rasterizer (hybrid vs pure, reference vs real-time)

### Takeaway
Two patterns coexist. (a) **Game PT modes are a lighting-preset toggle inside the hybrid renderer**: raster G-buffer and primary visibility stay, and ray-traced lighting replaces raster/probe lighting. Examples: Cyberpunk "RT Overdrive", Indiana Jones "Full Ray Tracing", DOOM TDA PT on top of its RTGI base, Black Myth "Full Ray Tracing". (b) **Reference modes are separate progressive accumulators** that restart when the camera moves (UE Path Tracer, Unity HDRP, RTXPT reference mode, kajiya reference mode) and apply an offline denoiser (OIDN or OptiX) to the converged frame.

### Cited findings
- Indiana Jones is Forward+ raster with rays traced from fragment shaders (shadows) and raygen passes for the PT lighting, so the PT mode is hybrid. — [NVIDIA Indiana Jones blog](https://developer.nvidia.com/blog/path-tracing-optimizations-in-indiana-jones-opacity-micromaps-and-compaction-of-dynamic-blass)
- DOOM TDA path tracing is layered on a base renderer that already uses RTGI everywhere. — [Windows Central](https://www.windowscentral.com/gaming/pc-gaming/doom-the-dark-ages-path-tracing-june-2025)
- Cyberpunk Overdrive "replaces the game's current ray tracing pipeline with a fully unified path-tracing solution", toggled as a preset. — [Tom's Hardware](https://www.tomshardware.com/news/rtx-4090-16fps-cyberpunk-2077-overdrive)
- CONTROL Resonant: the "Ray Tracing Preset: Ultra" automatically turns on DLSS RR on RTX (third-party write-up). — [tech-insider.org](https://tech-insider.org/control-resonant-path-tracing-5-rtx-features-2026/)
- Pure-PT references: RTXPT has no rasterization and offers reference plus real-time modes. — [RTXPT](https://github.com/NVIDIA-RTX/RTXPT)
- The UE Path Tracer is a separate view mode that accumulates. — [Epic docs](https://dev.epicgames.com/documentation/en-us/unreal-engine/path-tracer-in-unreal-engine)
- Unity HDRP accumulates up to a max spp and resets on camera move. — [HDRP 13.0](https://docs.unity3d.com/Packages/com.unity.render-pipelines.high-definition@13.0/manual/Ray-Tracing-Path-Tracing.html)
- kajiya has a reference path-tracing mode for comparison next to its hybrid real-time GI. — [kajiya](https://github.com/EmbarkStudios/kajiya)
- PICA PICA/Halcyon: hybrid raster, compute and DXR. — [SEED](https://ea.com/seed/news/hybrid-rendering-for-real-time-ray-tracing)

### Inferences
- Recommended layout for a student DX12 engine:
  1. Keep the raster G-buffer (primary visibility, motion vectors, depth and normals for the denoiser).
  2. Add a "PT lighting" mode that starts paths at G-buffer surfaces: ReSTIR DI, then indirect bounces into a cache, then the denoiser.
  3. Add a separate "reference" mode that traces primary rays from the camera and accumulates N frames with no denoiser, used as ground truth to compare against.
  The reference mode is also the quickest first milestone and validates the materials and light sampling.

### Gaps
- I found no primary source confirming whether Cyberpunk or Alan Wake 2 trace primary rays or reuse the raster G-buffer. The hybrid assumption rests on Indiana Jones and on the GDC 2023 description of the pipeline (RTXDI for direct, PT for indirect).
