# Offline / Production Path Tracing and Spectral Rendering with Hero Wavelength Sampling

Research notes for a student building a spectral path tracer (HLSL/DXR and/or CUDA, NVIDIA RTX, Windows). Written 2026-11-18.

How I verified things: I fetched primary sources where possible (pbr-book.org 4ed chapters, the pbrt-v4 source on GitHub, Mitsuba 3 docs, RGL/EPFL, Moments in Graphics). Two sources could not be read: the Wilkie et al. 2014 PDF was larger than the fetch limit (10 MB) and the ACM page for the Manuka paper returned 403. Anything I recalled from memory but could not confirm is marked **UNVERIFIED** and sits under Inferences or Gaps, never under Cited Findings.

---

## 1. Production renderers and their architectures: which are spectral, which are RGB, and why

### Takeaway
The sources confirm these as spectral: Weta's Manuka (hero wavelength, from Weta's own research), PBRT-v4 (4 wavelengths per sample), Mitsuba 3 (`*_spectral` variants, which are optional), Octane (spectral by default) and Iray (optional spectral mode). Cycles is RGB, apart from a spectral random-walk SSS option. Arnold is described as RGB, but only by weak forum evidence. I found no confirmation either way for RenderMan XPU, V-Ray, Corona, Hyperion, or the Unreal/Unity offline modes.

### Cited Findings
- **PBRT-v4:** `NSpectrumSamples = 4` is a compile-time constant ("pbrt uses 4 wavelength samples by default"; changing it means recompiling). The wavelength range is `Lambda_min = 360` to `Lambda_max = 830` nm. — [pbr-book 4ed, Representing Spectral Distributions](https://pbr-book.org/4ed/Radiometry,_Spectra,_and_Color/Representing_Spectral_Distributions)
- **PBRT-v4 GPU:** the source is marked with `PBRT_CPU_GPU` qualifiers, so the same functions compile for both CPU and GPU (e.g. `SampleVisibleWavelengths`). — [pbrt-v4 src/pbrt/util/sampling.h](https://raw.githubusercontent.com/mmp/pbrt-v4/master/src/pbrt/util/sampling.h)
- **Mitsuba 3 variant names** combine a backend (`scalar`/`llvm`/`cuda`), an optional `_ad` suffix, a colour mode (`mono`/`rgb`/`spectral`), an optional `_polarized` and an optional `_double`, for about 60 variants in total. pip ships `scalar_rgb`, `scalar_spectral`, `scalar_spectral_polarized`, `llvm_ad_{mono,mono_polarized,rgb,spectral,spectral_polarized}` and `cuda_ad_{mono,mono_polarized,rgb,spectral,spectral_polarized}`. — [Mitsuba 3 docs: Variants](https://mitsuba.readthedocs.io/en/stable/src/key_topics/variants.html)
- **Mitsuba 3 spectral mode** treats "the wavelength domain" as "yet another dimension of the space of light paths" over 360–830 nm. For RGB inputs it "determines plausible smooth spectra corresponding to the specified RGB colors". — [Mitsuba 3 docs: Variants](https://mitsuba.readthedocs.io/en/stable/src/key_topics/variants.html)
- **Mitsuba 3 / Dr.Jit architecture:** the `cuda` backend JIT-compiles the computation into CUDA kernels and hands ray tracing to OptiX, which makes Mitsuba a wavefront path tracer. `llvm` JIT-compiles parallel CPU kernels. `_ad` adds automatic differentiation. Polarized variants cost about 1.5–2x render time. `_double` still ray-traces in single precision, because Embree and OptiX do not support doubles. — [Mitsuba 3 docs: Variants](https://mitsuba.readthedocs.io/en/stable/src/key_topics/variants.html)
- **Manuka (Weta) / hero wavelength:** HWSS came from Weta Digital, with co-authors at Charles University and KIT. The hero wavelength is randomly sampled per path, and "all directional sampling is solely based on it". — [Wilkie EGSR'14 project page](https://cgg.mff.cuni.cz/~wilkie/Website/EGSR_14.html); [Eurographics DL record, DOI 10.1111/cgf.12419](https://diglib.eg.org/handle/10.1111/v33i4pp123-131)
- One source calls Manuka the first major production renderer to use spectral MIS, in the form of "Hero Spectral Sampling". — [pixelsham.com summary of Manuka/Gazebo pipeline](https://www.pixelsham.com/?p=3684) (secondary source)
- Weta's own technology page says light transport uses "a continuously sampled wavelength which is traced with each path". It credits this with improving temporal stability for geometry that moves very slowly relative to the pixel raster. — [Weta FX: physically based shading](https://www-ext.wetafx.co.nz/research-and-tech/technology/physically-based-shading)
- Weta's SIGGRAPH 2022 course notes (PDF) are a further primary source on Manuka. I found them but did not read them. — [wetafx.co.nz sig-course-2022.pdf](https://www.wetafx.co.nz/assets/Uploads/PDFs/sig-course-2022.pdf)
- **Octane:** OTOY describes it as an unbiased, "spectrally correct" GPU render engine, with a spectral AI denoiser and spectral SSS. — [cglounge.studio render-engines guide](https://cglounge.studio/journal/render-engines-explained); [ArchiVinci: What is spectral rendering](https://www.archivinci.com/blogs/spectral-rendering) (both secondary)
- **Iray:** spectral only as an option. A "Spectral Rendering" setting switches it to spectral light transport, and it is off by default. — [ArchiVinci](https://www.archivinci.com/blogs/spectral-rendering) (secondary; check against the NVIDIA Iray docs)
- **Cycles:** has no spectral rendering mode, but does offer "Spectral Random Walk" subsurface scattering. — [ArchiVinci](https://www.archivinci.com/blogs/spectral-rendering); [cgdirector](https://www.cgdirector.com/best-renderers-render-engines-for-blender/). A long-running community "Spectral Cycles" fork/effort exists. — [devtalk.blender.org: Thoughts on making Cycles into a spectral renderer](https://devtalk.blender.org/t/thoughts-on-making-cycles-into-a-spectral-renderer/2192?page=90); [BlenderArtists: Spectral Cycles vs Octane](https://blenderartists.org/t/spectral-cycles-vs-octane/1460308?page=9)
- **Arnold:** a forum user calls it "a plain RGB unidirectional path tracer". This is weak and old evidence. — [BlenderArtists thread](https://blenderartists.org/t/spectral-cycles-vs-octane/1460308?page=9)
- **RenderMan XPU** is described as Pixar's CPU+GPU hybrid engine, with near-complete feature parity with CPU-only RIS. Its spectral status was not found. — [cglounge.studio](https://cglounge.studio/journal/render-engines-explained)
- **A 2026 research direction:** "Spectral Rendering Without a Spectral Renderer: Learned Spectral Codes for RGB Pipelines" (arXiv 2602.18741). Title only; not read. — [arXiv 2602.18741](https://arxiv.org/pdf/2602.18741)

### Inferences
- Why most production renderers stay RGB, based on the cost/benefit evidence in section 5. Spectral adds only a small per-path cost on the GPU (under 0.3 ms at 1080p in one test). The real costs sit elsewhere: the asset pipeline needs RGB-to-spectrum conversion, AOVs and compositing are RGB, and denoisers are trained on RGB. Studios with strict colour-fidelity needs (Weta: skin, water, dispersion, matching on-set measured illuminants) adopted spectral. Studios with large RGB pipelines did not.
- Architecture lesson from PBRT-v4 and Mitsuba 3. Both are wavefront GPU path tracers on OptiX in which the spectral sample is a short fixed-size vector (4 floats in pbrt). This maps directly onto `float4` in HLSL/CUDA. A DXR engine can do the same with a `float4 throughput` and a `float4 lambda`.
- **UNVERIFIED (from memory, not confirmed in this session):** the Manuka paper is Fascione et al., "Manuka: A Batch-Shading Architecture for Spectral Path Tracing in Movie Production", ACM TOG 37(3), 2018 (DOI 10.1145/3182161). Its key ideas are "shade-before-hit" batch shading (all shading is evaluated on micropolygon grids before ray tracing, so that path tracing only does lookups) and spectral hero wavelength transport. The ACM page returned 403, so read it via the DOI or the authors' copy.
- **UNVERIFIED:** Disney's Hyperion is RGB, built on sorted-deferred ray batching (Eisenacher et al. 2013). Corona and V-Ray are RGB. Unreal's Path Tracer is RGB. Check each vendor's docs.

### Gaps
- No authoritative (vendor-documentation) statement found on spectral vs RGB for RenderMan XPU, Arnold GPU, V-Ray, Corona, Hyperion, Unity HDRP path tracing, or the Unreal Path Tracer.
- Manuka's paper content (shade-before-hit, wavelength count, upsampling method) could not be fetched (ACM 403). Try [doi.org/10.1145/3182161](https://doi.org/10.1145/3182161) or the Weta SIGGRAPH course PDF.

---

## 2. Hero Wavelength Spectral Sampling (HWSS): the algorithm and its GPU mapping

### Takeaway
Pick one random **hero** wavelength per path and place C−1 more wavelengths at equal offsets of Λ/C around the visible range, wrapping at the ends. All sampling decisions use the hero. All C wavelengths are evaluated together, and their contributions are combined with spectral MIS. At a dispersive (wavelength-dependent specular) event, the secondary wavelengths are terminated and the hero continues alone. With C = 4, which is what pbrt-v4 uses, this maps directly onto a `float4`.

### Cited Findings
- **Paper:** Wilkie, Nawaz, Droske, Weidlich, Hanika, "Hero Wavelength Spectral Sampling", Computer Graphics Forum 33(4) (EGSR 2014), DOI 10.1111/cgf.12419. The authors describe it as a simplified form of spectral MIS that is easier to implement, performs well, and is "actually more powerful than the original method". It was applied to colour fidelity, SSS, dispersion and volumes, and coupled with photon maps and BDPT. — [EG Digital Library](https://diglib.eg.org/handle/10.1111/v33i4pp123-131); [author page with preprint](https://cgg.mff.cuni.cz/~wilkie/Website/EGSR_14.html); [PDF mirror](https://cgg.mff.cuni.cz/wp-content/uploads/2021/05/WNDWH14HWSS.pdf)
- **Core idea:** the hero is randomly sampled per path and all directional sampling uses only the hero. The extra wavelengths are equally spaced from it, "so that all path wavelengths together always evenly cover the visible range". — [Wilkie EGSR'14 page](https://cgg.mff.cuni.cz/~wilkie/Website/EGSR_14.html); [Spectral Gradient Sampling paper (ResearchGate)](https://www.researchgate.net/publication/326510117_Spectral_Gradient_Sampling_for_Path_Tracing)
- **pbrt-v4 `SampledWavelengths::SampleUniform(u, lambda_min, lambda_max)`**, the reference implementation of the rotation:
  - `lambda[0] = Lerp(u, lambda_min, lambda_max)`
  - `delta = (lambda_max - lambda_min) / NSpectrumSamples`
  - `lambda[i] = lambda[i-1] + delta`. If this exceeds `lambda_max`, it wraps to `lambda_min + (lambda[i] - lambda_max)`.
  - `pdf[i] = 1 / (lambda_max - lambda_min)`
  — [pbr-book 4ed 4.5](https://pbr-book.org/4ed/Radiometry,_Spectra,_and_Color/Representing_Spectral_Distributions)
- **pbrt-v4 `SampleVisible(u)`**, the importance-sampled version: for each i, `up = u + i/NSpectrumSamples` (minus 1 if `up > 1`), then `lambda[i] = SampleVisibleWavelengths(up)` and `pdf[i] = VisibleWavelengthsPDF(lambda[i])`. The rotation happens in primary-sample (u) space, so the wavelengths are stratified under the importance density. — [pbr-book 4ed 4.5](https://pbr-book.org/4ed/Radiometry,_Spectra,_and_Color/Representing_Spectral_Distributions)
- **Exact pbrt-v4 visible-wavelength sampling code:**
  ```cpp
  Float SampleVisibleWavelengths(Float u) {
      return 538 - 138.888889f * std::atanh(0.85691062f - 1.82750197f * u);
  }
  Float VisibleWavelengthsPDF(Float lambda) {
      if (lambda < 360 || lambda > 830) return 0;
      return 0.0039398042f / Sqr(std::cosh(0.0072f * (lambda - 538)));
  }
  ```
  — [pbrt-v4 src/pbrt/util/sampling.h](https://raw.githubusercontent.com/mmp/pbrt-v4/master/src/pbrt/util/sampling.h)
- **Termination on dispersion:** `TerminateSecondary()` sets `pdf[i] = 0` for i ≥ 1 and then does `pdf[0] /= NSpectrumSamples`. The surviving wavelength is always `lambda[0]`. `SecondaryTerminated()` returns true when all `pdf[i>=1]` are 0. — [pbr-book 4ed 4.5](https://pbr-book.org/4ed/Radiometry,_Spectra,_and_Color/Representing_Spectral_Distributions)
- Zero pdfs after termination are handled with `SafeDiv` (returns 0 where the denominator is 0) when converting to XYZ. — [pbr-book 4ed 4.6 Color](https://pbr-book.org/4ed/Radiometry,_Spectra,_and_Color/Color)
- pbrt says its `SampledSpectrum`/`SampledWavelengths` approach effectively implements Evans & McCool's stratified wavelength clusters. Walter et al. (1997) and Morley et al. (2006) used one wavelength per path. Radziszewski et al. (2009) showed that secondaries need not all terminate at non-specular dispersive interfaces, and that all wavelengths can be combined with MIS weighting. — [pbr-book 4ed Further Reading](https://pbr-book.org/4ed/Radiometry,_Spectra,_and_Color/Further_Reading)
- pbrt's summary: HWSS uses "equally spaced point samples in the wavelength domain" and also works for photon mapping and participating media. — [pbr-book 4ed Further Reading](https://pbr-book.org/4ed/Radiometry,_Spectra,_and_Color/Further_Reading)
- **Alternative wavelength density** (Peters, Moments in Graphics): p(λ) ∝ (|r̄(λ)| + |ḡ(λ)| + |b̄(λ)|)·i(λ), where i is the scene illuminant. Sample by inverse CDF, with jittered stratification u_k = (u + k)/m and m = 4, which the post says is inspired by HWSS. Reflectances are left out of the density because they are smooth and bounded. — [Moments in Graphics: Spectral rendering part 2](https://momentsingraphics.de/SpectralRendering2Rendering.html)
- **GPU variant:** Ray Tracing Gems II chapter 42, "Efficient Spectral Rendering on the GPU for Predictive Rendering", describes a GPU-friendly scheme that "can be interpreted as a binary version of the Hero Wavelength Spectral Sampling". — [Springer, RTG II ch. 42 PDF (open access)](https://link.springer.com/content/pdf/10.1007/978-1-4842-7185-8_42.pdf)

### Inferences
- **Rotation formula in closed form** (equivalent to the pbrt code): λ_j = λ_min + ((λ_h − λ_min + j·Λ/C) mod Λ), where Λ = λ_max − λ_min and j = 0..C−1. For importance sampling, rotate u instead: u_j = frac(u + j/C), then λ_j = F⁻¹(u_j).
- **Spectral MIS estimator. UNVERIFIED** recollection of Wilkie et al.; check it against the paper PDF. A path x is sampled with directions chosen by the hero λ_h, with path pdf p(x|λ_h). Each wavelength λ_j is treated as if it could have produced the same path, and the balance heuristic is applied across the C wavelengths:
  ⟨I⟩ = Σ_j f(x, λ_j) / Σ_k p(x | λ_k), with the wavelength pdfs folded in.
  In practice, most renderers (pbrt-v4 included) skip the full per-wavelength path pdf whenever the sampling pdf is wavelength-independent. That is true for every non-dispersive BSDF driven by RGB-upsampled albedo, because GGX lobes, roughness and IOR are then the same for all λ. In that case p(x|λ_k) is identical for all k, and the MIS weight reduces to a plain average over the C wavelengths. Wavelength-dependent pdfs only show up with dispersive IOR, spectrally varying media (σ_t(λ) in distance sampling), or spectral SSS. That is where HWSS's MIS weighting (or terminating the secondaries) matters.
- **The pbrt `pdf[0] /= N` trick:** after termination, only one of the N stratified wavelengths survives. The other N−1 estimates are discarded, and the effective estimator becomes "1 of N chosen". Dividing the pdf by N keeps the final average (which still divides by N in `ToXYZ`) unbiased.
- **Mapping to HLSL:** keep `float4 lambda; float4 pdfLambda; float4 beta (throughput); float4 L`. Every spectral texture lookup returns a `float4` (one sigmoid evaluation per lane, see section 3). Dispersion is one branch: `if (ior varies with λ && !terminated) { pdfLambda.yzw = 0; pdfLambda.x /= 4; }`, after which the IOR at `lambda.x` drives refraction.
- HLSL has no `atanh` intrinsic. Use `atanh(x) = 0.5*log((1+x)/(1-x))`. `cosh` exists in HLSL.
- Wavelengths need a sample dimension of their own. pbrt draws `u` for wavelengths from the sampler once per camera ray, so each pixel sample gets a new hero.

### Gaps
- Could not read the HWSS PDF (>10 MB). The exact estimator equation, the reported speedups vs single-wavelength sampling, and the choice of C = 4 are unconfirmed here. Read Sections 3–4 of the [PDF](https://cgg.mff.cuni.cz/wp-content/uploads/2021/05/WNDWH14HWSS.pdf).
- Whether Manuka uses exactly 4 wavelengths was not confirmed by a fetched source.

---

## 3. Spectral data pipeline (RGB→spectrum, illuminants, CMFs, colour spaces, water)

### Takeaway
Use Jakob & Hanika 2019: 3 coefficients per texel, evaluated as S(λ) = s(c₀λ² + c₁λ + c₂) with the sigmoid s(x) = ½ + x/(2√(1+x²)). Coefficients come from a 3×64³ precomputed table. Treat albedos, unbounded values and illuminants differently. Convert to XYZ with a Monte Carlo estimator divided by ∫ȳ = 106.856895, then multiply by the XYZ→RGB matrix of the target space. Use the Wyman 2013 analytic CMF fits if you want to avoid tables on the GPU.

### Cited Findings
- **Jakob & Hanika 2019, "A Low-Dimensional Function Space for Efficient Spectral Upsampling"** (Eurographics 2019): a smooth, energy-conserving parametric reflectance model. Spectral textures need the same storage as RGB textures. Evaluation takes "as few as six floating-point instructions" at any wavelength. The authors claim zero error across the full sRGB gamut, support for wide gamuts, and vectorization across many wavelengths. — [RGL EPFL page](https://rgl.epfl.ch/publications/Jakob2019Spectral); [paper PDF](https://d38rqfq1h7iukm.cloudfront.net/media/papers/Jakob2019Spectral_3.pdf); [supplement ZIP](https://d38rqfq1h7iukm.cloudfront.net/media/papers/Jakob2019Spectral.zip); [code: github.com/mitsuba-renderer/rgb2spec](https://github.com/mitsuba-renderer/rgb2spec)
- **pbrt-v4 implementation details:**
  - The sigmoid is s(x) = 1/2 + x/(2√(1+x²)), and S(λ) = s(c₀λ² + c₁λ + c₂).
  - The table is `res = 64`, stored as `float[3][64][64][64][3]` (3 partitions by max component × 64³ × 3 coefficients).
  - Lookup: `maxc = argmax(r,g,b)`, z = rgb[maxc], x = rgb[(maxc+1)%3]·63/z, y = rgb[(maxc+2)%3]·63/z. z goes through a nonlinear remap via `zNodes` (binary search), followed by trilinear interpolation of the coefficients.
  - Grey input (r = g = b) bypasses the table: c₀ = c₁ = 0, c₂ = (r − 0.5)/√(r(1 − r)).
  - Input is clamped to ≥ 0.
  — [pbr-book 4ed 4.6](https://pbr-book.org/4ed/Radiometry,_Spectra,_and_Color/Color)
- **The three spectrum kinds in pbrt-v4:**
  - `RGBAlbedoSpectrum`: rgb in [0,1], evaluates the sigmoid polynomial directly.
  - `RGBUnboundedSpectrum`: `scale = 2·max(r,g,b)`, coefficients fit to rgb/scale, evaluates `scale·s(λ)`. Normalizing to a maximum of ½ instead of 1 avoids odd spectra for saturated colours.
  - `RGBIlluminantSpectrum`: `scale·s(λ)·illuminant(λ)`, where the illuminant is the colour space's white point (e.g. D65 for sRGB). This is needed because a flat spectrum does not map to neutral white.
  — [pbr-book 4ed 4.6](https://pbr-book.org/4ed/Radiometry,_Spectra,_and_Color/Color)
- **XYZ conversion:** `SampledSpectrum::ToXYZ` is a Monte Carlo estimator. Each X, Y, Z term is (CMF(λᵢ)·L(λᵢ)/pdf(λᵢ)), averaged over the samples, then divided by `CIE_Y_integral = 106.856895`. It uses `SafeDiv` for pdf = 0. Offline `SpectrumToXYZ` uses a 1 nm Riemann sum over 360–830. — [pbr-book 4ed 4.6](https://pbr-book.org/4ed/Radiometry,_Spectra,_and_Color/Color)
- **RGB colour spaces in pbrt-v4:** each is defined by the xy of its primaries plus a white point. `XYZFromRGB = rgb·Diag(C)` with `C = inverse(rgb)·W`, and `RGBFromXYZ` is its inverse. Built-in spaces are sRGB, DCI-P3, Rec2020 and ACES2065-1. — [pbr-book 4ed 4.6](https://pbr-book.org/4ed/Radiometry,_Spectra,_and_Color/Color)
- **Tabulated spectra:** `DenselySampledSpectrum` stores 1 nm samples over 360–830 (471 floats, "nearly 2 kB"), and lookup is `lround(λ) − λ_min`. — [pbr-book 4ed 4.5](https://pbr-book.org/4ed/Radiometry,_Spectra,_and_Color/Representing_Spectral_Distributions)
- **Blackbody in pbrt-v4:** normalized so its peak is 1. Peak wavelength is λ_max = 2.8977721e-3 / T (meters, Wien's law), `normalizationFactor = 1/Blackbody(λ_max·1e9, T)`. — [pbr-book 4ed 4.5](https://pbr-book.org/4ed/Radiometry,_Spectra,_and_Color/Representing_Spectral_Distributions)
- **Other upsampling methods, as characterised by pbrt:**
  - Smits 1999 optimized discrete spectra for the primaries and their mixes.
  - Meng et al. 2015 optimized a highly smooth interpolant over xy chromaticity, so it does not depend on the gamut.
  - Otsu et al. 2018 used PCA on measured spectra, a data-driven interpolant (maximal smoothness is not always natural).
  - Peters et al. 2019 used Fourier coefficients with moment-based reconstruction, which gives smooth, energy-conserving spectra without ringing.
  - Tódová, Wilkie & Fascione 2021 extended Peters et al. so that chosen RGB values reproduce user-specified spectra.
  — [pbr-book 4ed Further Reading](https://pbr-book.org/4ed/Radiometry,_Spectra,_and_Color/Further_Reading)
- **Real-time alternative (Peters):** store sRGB textures, convert them to "Fourier sRGB", and reconstruct with the MAM 2019 / SIGGRAPH 2019 methods. CMF weights and the illuminant are precomputed into a 1D RGBA16F LUT of 1024 entries (8 KiB per illuminant) that holds (r̄,ḡ,b̄)·i(λ)/p(λ) and a phase φ. XYZ→sRGB is folded into the CMFs via a 3×3 matrix, so the per-pixel colour conversion disappears. — [Moments in Graphics part 2](https://momentsingraphics.de/SpectralRendering2Rendering.html); background in [part 1](https://momentsingraphics.de/SpectralRendering1Spectra.html)
- **Analytic CMFs:** Wyman, Sloan, Shirley, "Simple Analytic Approximations to the CIE XYZ Color Matching Functions", JCGT 2(2), 2013. Their simple fits (Eqs. 2–3) and a more accurate multi-lobe fit (Eq. 4) need about 10 lines of code. The fitting error is smaller than the variance among the human subject data aggregated into the CIE standard. — [JCGT 0002/02/01](https://jcgt.org/published/0002/02/01/); [NVIDIA Research page](https://research.nvidia.com/labs/rtr/publication/wyman2013simple/); [PDF mirror](https://www.haralick.org/DV/XYZJCGT.pdf)
- **Water absorption:** Pope & Fry 1997, "Absorption spectrum (380–700 nm) of pure water. II. Integrating cavity measurements", Appl. Opt. 36, 8710–8723. The minimum is 0.0044 ± 0.0006 m⁻¹ at 418 nm, and blue absorption is lower than previously believed. The companion paper, Sogandares & Fry 1997 (340–640 nm, photothermal), gives a minimum of 0.0062 ± 0.0006 m⁻¹ at 420 nm. Tabulated data is on OMLC. — [OMLC pope97](https://omlc.org/spectra/water/abs/pope97.html); [OMLC water absorption index](https://omlc.org/spectra/water/abs/); [OMLC sogandares97](https://omlc.org/spectra/water/abs/sogandares97.html)

### Inferences
- **GPU data sizes:** the rgb2spec table is 3·64³·3 floats = 2,359,296 floats ≈ 9.0 MiB in fp32. On the GPU, upload it as three `Texture3D<float4>` 64³ textures (one per max-component partition, ≈ 4 MiB each in RGBA32F) and use hardware trilinear filtering. The nonlinear z remap (`zNodes`) has to be inverted first, or replaced with a 1D LUT. The better option is to convert textures offline: store per-texel (c₀,c₁,c₂) in an RGB16F/RGBA16F texture, so render time costs one fetch plus about 6 FLOPs per wavelength, and `float4` evaluates 4 wavelengths at once. Filtering coefficients bilinearly is not exactly the same as filtering RGB, but it is the standard practice the paper's "same storage as RGB textures" claim implies.
- **HLSL sigmoid for `float4 lambda`:** `float4 x = (c0*lambda + c1)*lambda + c2; float4 s = 0.5 + x * 0.5 * rsqrt(1 + x*x);`. Infinite coefficients encode pure 0/1 spectra, so they need special-casing.
- **Final colour pipeline:** accumulate XYZ (or fold the matrix into the CMFs, as Peters does, to accumulate linear Rec.709 or Rec.2020 directly). Then apply exposure, then tonemapping (e.g. ACES or AgX) in RGB. The film/accumulation buffer stays RGB/XYZ and never spectral.
- **Choice of illuminant spectrum:** a light given as RGB should be an `RGBIlluminantSpectrum` (multiplied by D65 for sRGB). Otherwise a "white" light renders slightly off-white after the XYZ→sRGB conversion.
- **UNVERIFIED:** the water IOR dispersion could come from a Cauchy fit n(λ) ≈ A + B/λ² (λ in µm, with A ≈ 1.32, B ≈ 0.003 as rough values) or from a Sellmeier/IAPWS formulation, with Hale & Querry 1973 as the classic source for the full complex index of water (0.2–200 µm). I did not fetch or verify the coefficients. Use refractiveindex.info (Hale & Querry dataset) for the numbers.

### Gaps
- The exact Wyman 2013 fit equations and coefficients were not fetched (JCGT page found, not read). Copy them from [jcgt.org/published/0002/02/01](https://jcgt.org/published/0002/02/01/).
- The pbrt-v4 `Blackbody()` (Planck's law) code body and the D65 spectrum source were not fetched.
- Scattering coefficients of water (e.g. Morel's pure-water scattering) and the Hale & Querry tables were not fetched.
- Whether rgb2spec coefficients assume λ in nm or a normalized λ: the pbr-book text implies polynomial evaluation directly in λ, but I did not verify this in the repo.

---

## 4. Core path-tracer theory and the best learning resources

### Takeaway
PBRT-v4 (free online at pbr-book.org) is the main textbook. It covers spectra (chapter 4), sampling, MIS, microfacet BSDFs and a GPU wavefront implementation, and its source matches the book. Within the 15-call budget I did not fetch sources for the rest of this section (Veach thesis, GGX VNDF papers, Sobol/Owen, Ray Tracing in One Weekend, Bikker), so the pointers below are **UNVERIFIED**.

### Cited Findings
- The pbr-book.org 4th edition is online. Chapter 4 ("Radiometry, Spectra, and Color") covers `SampledSpectrum`, `SampledWavelengths`, RGB colour spaces and the RGB-to-spectrum tables. — [pbr-book 4ed 4.5](https://pbr-book.org/4ed/Radiometry,_Spectra,_and_Color/Representing_Spectral_Distributions); [4.6 Color](https://pbr-book.org/4ed/Radiometry,_Spectra,_and_Color/Color)
- The pbrt-v4 sampling utilities include `Hammersley2D/3D`, `Stratified1D/2D/3D` and `RNGGenerator`. Sobol/Owen-scrambling code lives in `lowdiscrepancy.h`, which `sampling.h` includes. — [pbrt-v4 sampling.h](https://raw.githubusercontent.com/mmp/pbrt-v4/master/src/pbrt/util/sampling.h)
- Peters' post gives a concrete real-time spectral path tracer structure: throughput is a vector of m = 4 entries starting at 1, multiplied at each vertex by BRDF × geometry / density, while radiance is accumulated as RGB. — [Moments in Graphics part 2](https://momentsingraphics.de/SpectralRendering2Rendering.html)

### Inferences
- **UNVERIFIED reading list** (well-known; URLs from memory, check before citing):
  - Veach PhD thesis 1997, "Robust Monte Carlo Methods for Light Transport Simulation" (MIS, balance/power heuristics), graphics.stanford.edu/papers/veach_thesis/
  - Kajiya 1986, "The Rendering Equation"
  - Heitz 2018 JCGT, "Sampling the GGX Distribution of Visible Normals" (jcgt.org/published/0007/04/01/)
  - Eto & Tokuyoshi 2023, "Bounded VNDF Sampling for Smith–GGX Reflections" (SIGGRAPH Asia 2023 tech comm.)
  - Burley 2020, "Practical Hash-based Owen Scrambling" (JCGT)
  - Heitz et al. 2019, blue-noise screen-space sampler
  - Shirley's "Ray Tracing in One Weekend" series (raytracing.github.io)
  - Jacco Bikker's blog/lectures (jacco.ompf2.com)
- For Russian roulette with spectral throughput, use a scalar such as `max(beta)` or `average(beta / pdfLambda)`. pbrt's `SampledSpectrum` provides `MaxComponentValue()` and `Average()` for exactly this purpose ([pbr-book 4.5](https://pbr-book.org/4ed/Radiometry,_Spectra,_and_Color/Representing_Spectral_Distributions)). That use is my inference.
- **Dielectric Fresnel is the one place spectral matters for glass/water.** With a constant IOR it is wavelength-independent and no termination is needed. With a dispersive IOR, call `TerminateSecondary()` before sampling refraction.

### Gaps
- Primary sources for the Veach, GGX/VNDF, bounded-VNDF, Owen-scrambling and blue-noise references were not fetched in this session. Another researcher may cover them.

---

## 5. Real-time / GPU spectral rendering: cost, tricks, and denoisers

### Takeaway
On an RTX 5070 Ti at 1080p and 1 spp, spectral path tracing with 4 wavelengths costs under 0.3 ms extra: about 2–7% in Bistro and 19–36% in a Cornell box. Colour noise from well importance-sampled 4-wavelength HWSS is small compared with ordinary MC noise, so RGB denoisers can be fed the RGB-converted output. No source shows a shipped spectral-aware real-time denoiser.

### Cited Findings
- **Timings (RTX 5070 Ti, 1920×1080, 1 spp, locked clocks), RGB vs spectral:**

  | Scene | Path length | RGB | Spectral |
  |---|---|---|---|
  | Cornell box | 2 | 0.26 ms | 0.31 ms |
  | Cornell box | 8 | 0.83 ms | 1.13 ms |
  | Bistro | 2 | 2.71 ms | 2.89 ms |
  | Bistro | 8 | 14.3 ms | 14.6 ms |

  Absolute overhead stays under 0.3 ms. — [Moments in Graphics part 2](https://momentsingraphics.de/SpectralRendering2Rendering.html)
- **Tricks from the same post:**
  - Importance-sample wavelengths by illuminant × |CMF|.
  - Stratify with m = 4.
  - Precompute a 1024-entry RGBA16F LUT (8 KiB) per illuminant.
  - Express BRDF outputs as weights on base colour and white, so only one reflectance spectrum is reconstructed per hit.
  - Fold XYZ→sRGB into the CMFs.
  - For multiple illuminants, the baseline is to merge all lights into one power-weighted spectrum, which increases colour noise. The alternatives are per-vertex storage (quadratic cost) or wavelength guiding (which struggles with narrow peaks).
  — [Moments in Graphics part 2](https://momentsingraphics.de/SpectralRendering2Rendering.html)
- **Source files to read in that project:** `brdfs.glsl`, `spectra.glsl` (Fourier sRGB → spectra), `pathtrace.frag.glsl` (path tracing and wavelength sampling), `tools/fourier_srgb_conversion.py`, `tools/illuminant_spectra.py`. — [Moments in Graphics part 2](https://momentsingraphics.de/SpectralRendering2Rendering.html)
- **Denoising:** "color noise caused by spectral rendering with good importance sampling tends to be quite negligible relative to the noise caused by other Monte Carlo strategies", so a path-tracing denoiser should handle it. The post does not discuss TAA. — [Moments in Graphics part 2](https://momentsingraphics.de/SpectralRendering2Rendering.html)
- **Ray Tracing Gems II ch. 42** (predictive GPU spectral rendering) presents a "binary version of HWSS" and calls denoising "currently the most popular choice" for real-time MC path tracing. — [Springer PDF](https://link.springer.com/content/pdf/10.1007/978-1-4842-7185-8_42.pdf)
- **spectral-restir** (Vulkan): real-time spectral ReSTIR path tracing that resamples wavelengths as part of the path. Its roadmap lists a "Denoiser + temporal accumulation that respects spectral samples" as not yet done. — [github.com/nathencoelhoo/spectral-restir](https://github.com/nathencoelhoo/spectral-restir)
- **NVIDIA NRD** is a spatio-temporal denoiser aimed mainly at 1-path/pixel path tracing, and it is RGB-oriented. **RTXPT** is NVIDIA's real-time path tracing sample. — [github.com/NVIDIA-RTX/NRD](https://github.com/NVIDIA-RTX/NRD); [github.com/NVIDIA-RTX/RTXPT](https://github.com/NVIDIA-RTX/RTXPT)
- **Octane** markets a "spectral AI denoiser". — [cglounge.studio](https://cglounge.studio/journal/render-engines-explained) (secondary; no technical detail)

### Inferences
- **Recommended integration for a DXR engine:** convert each path's spectral estimate to linear RGB (via XYZ) at the camera-ray level. From then on, everything (accumulation buffer, NRD/OIDN/DLSS-RR, TAA history) sees ordinary RGB. Because each frame's hero wavelength is a new random sample, temporal accumulation naturally integrates over wavelength. Spectral rendering therefore adds a small, low-dynamic-range chroma noise that denoisers treat like any other MC noise.
- Hand denoiser albedo guide buffers RGB albedo, i.e. the original RGB texture or the upsampled spectrum converted back, not one wavelength's reflectance. Otherwise the guide flickers with λ.
- If secondaries are terminated (dispersion), colour noise in caustics and through glass rises sharply. That is where temporal accumulation or many spp is needed; it is expected and physically motivated.

### Gaps
- No published, production-proven spectral-aware real-time denoiser was found.
- No source measured how DLSS Ray Reconstruction behaves on spectral input.

---

## 6. Open-source spectral (GPU) renderers to study: repos and files

### Takeaway
Start with pbrt-v4 (C++/CUDA/OptiX, spectral, matches the free book). Then read rgb2spec (the upsampling table generator), Mitsuba 3 (`cuda_ad_spectral`, Dr.Jit/OptiX), and Peters' real-time GLSL code for a game-engine-style implementation. I could not verify file paths for LuxCoreRender, Tungsten and ART in this session.

### Cited Findings
- **pbrt-v4:** [github.com/mmp/pbrt-v4](https://github.com/mmp/pbrt-v4). Read `src/pbrt/util/sampling.h` (visible-wavelength sampling, verified) and the classes the book documents (`SampledSpectrum`, `SampledWavelengths`, `RGBToSpectrumTable`, `RGBAlbedoSpectrum`/`RGBUnboundedSpectrum`/`RGBIlluminantSpectrum`, `BlackbodySpectrum`, `DenselySampledSpectrum`). — [sampling.h raw](https://raw.githubusercontent.com/mmp/pbrt-v4/master/src/pbrt/util/sampling.h); [pbr-book 4.5](https://pbr-book.org/4ed/Radiometry,_Spectra,_and_Color/Representing_Spectral_Distributions); [pbr-book 4.6](https://pbr-book.org/4ed/Radiometry,_Spectra,_and_Color/Color)
- **rgb2spec** (Jakob & Hanika reference code): [github.com/mitsuba-renderer/rgb2spec](https://github.com/mitsuba-renderer/rgb2spec) — [RGL page](https://rgl.epfl.ch/publications/Jakob2019Spectral)
- **Mitsuba 3** (spectral variants, Dr.Jit → CUDA/OptiX or LLVM): [Variants docs](https://mitsuba.readthedocs.io/en/stable/src/key_topics/variants.html)
- **Peters' real-time spectral path tracer** (GLSL: `pathtrace.frag.glsl`, `spectra.glsl`, `brdfs.glsl`), linked from [Moments in Graphics part 2](https://momentsingraphics.de/SpectralRendering2Rendering.html)
- **spectral-restir** (Vulkan, real-time spectral ReSTIR): [github.com/nathencoelhoo/spectral-restir](https://github.com/nathencoelhoo/spectral-restir)
- **NVIDIA RTXPT** (RGB, but a reference DXR/Vulkan real-time path tracer that could be extended spectrally): [github.com/NVIDIA-RTX/RTXPT](https://github.com/NVIDIA-RTX/RTXPT)

### Inferences
- **Suggested study order for a DX12 student:**
  1. pbr-book 4.5/4.6 together with `sampling.h`.
  2. Port `SampledWavelengths` and `RGBSigmoidPolynomial` to HLSL `float4`.
  3. Precompute the rgb2spec coefficients per texture offline.
  4. Accumulate XYZ→RGB and feed the existing denoiser.
  5. Only then add dispersion (`TerminateSecondary`) and spectral media (water σ_a(λ) from Pope & Fry).
- **UNVERIFIED repos** (from memory, verify the URLs):
  - Mitsuba 3 source at github.com/mitsuba-renderer/mitsuba3 (look at `include/mitsuba/core/spectrum.h` and `src/render/spectrum.cpp`)
  - LuxCoreRender at github.com/LuxCoreRender/LuxCore (OpenCL/CUDA GPU path, spectral dispersion)
  - Tungsten at github.com/tunabrain/tungsten (CPU, mostly RGB)
  - ART, the Advanced Rendering Toolkit by Wilkie et al., at Charles University (cgg.mff.cuni.cz), a spectral/polarised research renderer

### Gaps
- File-level guidance for Mitsuba 3, LuxCoreRender, Tungsten and ART was not verified in this session.
- I did not check whether pbrt-v4's GPU path runs on DXR. It uses OptiX/CUDA, so an HLSL port is needed. (The pbrt GPU-uses-OptiX point is background knowledge, consistent with the `PBRT_CPU_GPU` qualifiers but not explicitly confirmed by a fetched page.)
