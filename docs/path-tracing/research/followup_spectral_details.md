# Follow-up: exact implementation details for a GPU spectral path tracer (HWSS, Jakob–Hanika, spectral water, Peters)

Written 2026-11-18. This continues `offline_and_spectral_rendering.md` and `ocean_path_tracing.md`: it closes the gaps they flagged and does not repeat the parts already verified there.

**How things were verified.** For this pass I downloaded the primary sources and read them directly, rather than relying on summaries:
- pbrt-v4 `master`: `spectrum.h/.cpp`, `color.h/.cpp`, `cmd/rgb2spec_opt.cpp`, `cpu/integrators.cpp`, `media.h`, `CMakeLists.txt`
- `mitsuba-renderer/rgb2spec` `master`: `rgb2spec.h/.c`, `rgb2spec_opt.cpp`
- Mitsuba 3 `master`: `include/mitsuba/core/spectrum.h`, `src/render/srgb.cpp`, plus the docs pages
- Peters' `path_tracer` repo, `spectral` branch
- The OMLC water data files, the CIE dataset CSVs and the Zhang et al. 2009 MATLAB code

Earlier the HWSS paper was "too large". This time I downloaded it in full (23.6 MB) from jo.dreggn.org and extracted its text with a small PDF-stream inflater. The Wyman et al. 2013 paper was read the same way.

Anything I derived myself is under **Inferences**. Anything recalled but not confirmed this session is marked **UNVERIFIED**.

---

## 1. Hero Wavelength Spectral Sampling (Wilkie et al. 2014): estimator, MIS weights, why C = 4

### Takeaway
The paper defines each rotated hero as its own sampling technique and combines the C techniques with the balance heuristic. That gives the estimator ⟨I⟩ = (1/N) Σᵢ Σⱼ f(Xᵢ, λʲᵢ) / Σₖ p(λᵏᵢ) p(Xᵢ | λᵏᵢ) (Eq. 11). When the path pdf does not depend on λ, this reduces to a plain average over the C wavelengths. The weights matter only for wavelength-dependent sampling: media, SSS, dispersion, spectrally importance-sampled lights. C = 4 was an engineering choice for SSE 4-wide vectorisation, and the authors say it "proved to be sufficient in practice".

### Cited Findings
All findings in this section come from the full paper PDF, [Wilkie, Nawaz, Droske, Weidlich, Hanika, "Hero Wavelength Spectral Sampling", CGF 33(4), EGSR 2014](https://jo.dreggn.org/home/2014_herowavelength.pdf) (23.6 MB; it is also at the [cgg.mff.cuni.cz mirror](https://cgg.mff.cuni.cz/wp-content/uploads/2021/05/WNDWH14HWSS.pdf) and on the [EG DL](https://diglib.eg.org/handle/10.1111/v33i4pp123-131)). Equation numbers are the paper's.

**Background estimators (Sec. 2)**
- **Eq. 1, single-wavelength estimator:** ⟨I⟩ = (1/N) Σᵢ f(Xᵢ, λᵢ) / p(Xᵢ, λᵢ).
- **Eq. 2, Evans & McCool multi-wavelength estimator:** ⟨I⟩ = (1/N)(1/C) Σᵢ Σⱼ f(Xᵢ, λʲᵢ) / p(Xᵢ, λʲᵢ).
- **Eq. 3:** if path generation does not depend on λ, then p(Xᵢ, λʲᵢ) = p(λʲᵢ)·p(Xᵢ). The paper says LuxRender uses this form.
- **Eq. 4, Radziszewski et al. 2009 spectral MIS weight:** w = p(Xᵢ | λˢᵢ) p(λˢᵢ) / Σₖ₌₁..C p(Xᵢ | λᵏᵢ). The paper notes that "it is not apparent which sampling techniques are weighted against each other" in this form.

**Rotation (Eq. 5)**
- rⱼ(λ_h) = (λ_h − λ_min + (j/C)·λ̄) mod λ̄ + λ_min, for j = 1..C, where λ̄ = λ_max − λ_min.
- The extra wavelengths "are not sampled randomly", so no additional random numbers are needed.

**Combined sample density (Eq. 6)**
- p(Xᵢ, λʲᵢ) = Σₖ p(λᵏᵢ) p(Xᵢ | λᵏᵢ) / C. Any of the C wavelengths could have been the hero that generated Xᵢ, and each is chosen as hero with probability 1/C.
- The paper warns that most multi-wavelength renderers instead use p̄ = p(λʲᵢ)·p(Xᵢ | λʲᵢ) (Eq. 7). That is "only correct if the probability to sample the path … is the same for all wavelengths".

**MIS with shifted techniques (Sec. 3.2)**
- Technique s samples λ with p(r_s⁻¹(·)) and samples X with p(· | r_s⁻¹(λ)).
- **Balance heuristic (Eq. 8):** w_s(X, λ) = p_s(X, λ) / Σₖ₌₁..C p_k(X, λ).
- **Estimator (Eq. 9):** ⟨I⟩ = (1/N) Σᵢ Σₛ w_s f/p_s.
- **Rewritten weight (Eq. 10):** w_s = p(λ_hᵢ) p(Xᵢ | λ_hᵢ) / Σₖ p(λᵏᵢ) p(Xᵢ | λᵏᵢ).
- **Final estimator (Eq. 11):** ⟨I⟩ = (1/N) Σᵢ Σⱼ f(Xᵢ, λʲᵢ) / Σₖ p(λᵏᵢ) p(Xᵢ | λᵏᵢ).
- A key ingredient is using "the same (Q)MC sequence for each of these techniques", so that every technique produces the same path Xᵢ.

**Combining with NEE and BDPT (Eq. 12)**
- For a set of path techniques T, the weight is p_t′(Xᵢ, λ_hᵢ) / Σ_{t∈T} Σₖ p(λᵏᵢ) p_t(Xᵢ | λᵏᵢ). In other words, sum over all path techniques × all shifted wavelengths.
- Fig. 2 shows why this joint sum matters: an environment map that is importance-sampled per wavelength. MIS "based on the hero wavelength only" fails there.

**When the weights matter**
- At dispersive Dirac interfaces "our method degenerates to using only the hero wavelength".
- When the integrand "only varies slightly with wavelength, as is the case for most materials … we obtain an almost equal weighting for all bands".
- Fig. 7 compares hero without MIS, balance heuristic and power heuristic on skin SSS.

**Why C = 4 (Sec. 3.4)**
- "For performance reasons, we made the engineering choice to always go with exactly four wavelengths per path … This allows all BSDF and scattering computations … to be natively vectorised."
- SSE costs only "a few clock cycles more" than scalar code.
- The system "can be trivially re-compiled to work with multiples of 4, but … the original choice of 4 proved to be sufficient in practice".

**Implementation pitfall (Sec. 4.1)**
- Typical BSDF code returns f/pdf for the same wavelength. With HWSS "we have to make sure that we divide by the probability which generated the path, i.e. the hero wavelength probability".
- The pdfs of all four wavelengths are needed for Eq. 10.

**Volumes (Sec. 4.4)**
- When σ_t depends only on λ (homogeneous media), "the transmittance can be perfectly importance sampled and the probability directly computed", so the weights are straightforward.
- For heterogeneous media, the paper numerically integrates the transmittance for all channels alongside Woodcock tracking. This "introduces some bias due to integration error".

**Cost and results (Sec. 5)**
- Hero-wavelength overhead was "always in the range of 3%–10%" of wall-clock time at equal sample count. Fig. 1 (skin SSS) took 3.5% longer.
- Photon mapping (Sec. 4.2): matching any of a photon's four wavelengths, then cyclically rotating its channels, let the authors shoot a quarter of the photons with no loss in quality (Fig. 4).

**pbrt-v4 code, verified this session**
- `SampledWavelengths::SampleVisible(u)` rotates in primary-sample space: `up = u + i/NSpectrumSamples; if (up > 1) up -= 1; lambda[i] = SampleVisibleWavelengths(up); pdf[i] = VisibleWavelengthsPDF(lambda[i]);`
- `TerminateSecondary()` sets `pdf[i]=0` for i ≥ 1, then `pdf[0] /= NSpectrumSamples`.
- Source: [pbrt-v4 src/pbrt/util/spectrum.h](https://raw.githubusercontent.com/mmp/pbrt-v4/master/src/pbrt/util/spectrum.h)
- `SampledSpectrum::ToXYZ` returns `XYZ(SafeDiv(X*L, pdf).Average(), SafeDiv(Y*L, pdf).Average(), SafeDiv(Z*L, pdf).Average()) / CIE_Y_integral`. — [pbrt-v4 src/pbrt/util/spectrum.cpp](https://raw.githubusercontent.com/mmp/pbrt-v4/master/src/pbrt/util/spectrum.cpp)

**Mitsuba 3 does the same**
- It uses `math::sample_shifted` (shifted stratification in sample space) and then `sample_rgb_spectrum()`, with the identical density 538 − 138.888… · atanh(0.85691062… − 1.82750197… u).
- The pdf is `0.003939804229326285 / cosh²(0.0072(λ−538))` and the returned weight is `253.82·cosh²(…)`. Mitsuba credits this density to Radziszewski et al.
- Source: [Mitsuba 3 include/mitsuba/core/spectrum.h](https://raw.githubusercontent.com/mitsuba-renderer/mitsuba3/master/include/mitsuba/core/spectrum.h), functions `sample_rgb_spectrum`, `pdf_rgb_spectrum` and `sample_wavelength`.

### Inferences
- **How pbrt maps onto Eq. 11 when wavelengths are importance-sampled.** pbrt rotates u, not λ, so each member λⱼ of the set has marginal density p_vis(λⱼ) under every technique. Technique k's path pdf is p(X | λₖ). The balance-heuristic estimator is therefore f(X, λⱼ) / (p_vis(λⱼ) · Σₖ p(X | λₖ)), summed over j. This is exactly pbrt's split:
  - path-pdf ratios live in `r_u`;
  - the final `/ r_u.Average()` supplies Σₖ p(X|λₖ)/(C·p(X|λ_h));
  - the per-wavelength `1/pdf(λⱼ)` is applied in `ToXYZ`, which averages over j (the 1/C cancels).
  - With uniform λ this is identical to Eq. 11. With SampleVisible, the paper's p(λᵏ) inside the sum should be read as p(λʲ) outside it.
  - I derived this myself. Have the tutor double-check it, but it is consistent with pbrt-v4's code.
- **HLSL state:** `float4 lambda, pdfLambda, beta, r_u, r_l`. Write `beta *= f/pdf_hero` and `r_u *= pdf_k/pdf_hero` per lane at each vertex (pdf_k evaluated at lane k). Contributions are `beta*Le / avg(r_u)`, or `/ avg(r_u + r_l)` when NEE MIS applies. Then `XYZ += CMF(lambda)*L/pdfLambda` averaged over 4 lanes, divided by 106.856895.
- In a scene with no dispersion, no spectral media and no per-λ light sampling, every `pdf_k == pdf_hero`, so `r_u ≡ 1`. You can implement the plumbing first and confirm it is a no-op.

### Gaps
- The paper gives no numeric variance comparison across different C values. "Sufficient in practice" is qualitative.
- Wilkie's author slides were not looked for, since the paper itself was readable.

---

## 2. Chromatic media with hero wavelengths: pbrt-v4 VolPathIntegrator (Miller, Georgiev, Jarosz 2019) and homogeneous water

### Takeaway
pbrt-v4 samples every free-flight distance with the first (hero) wavelength's majorant (`sigma_maj[0]`). It carries two rescaled path-probability vectors:
- `r_u`: unidirectional pdf / pdf of the sampled path;
- `r_l`: light-sampling pdf / pdf of the sampled path.

At an emitter it divides by their average over wavelengths, which is single-sample spectral MIS with the balance heuristic. For homogeneous water you do not need null collisions or ratio tracking:
- Sample t with the hero's σ_t.
- Weight all 4 lanes by σ_s(λ)·e^{−σ_t(λ)t} / (σ_t(λ_h)·e^{−σ_t(λ_h)t}), with the matching `r_u` update.
- Use closed-form e^{−σ_t(λ)d} per lane for shadow rays.

### Cited Findings
**Definitions** — [pbr-book 4ed §14.2, Volume Scattering Integrators](https://pbr-book.org/4ed/Light_Transport_II_Volume_Rendering/Volume_Scattering_Integrators)
- r_{u,λᵢ} = p_{u,λᵢ}/p_path and r_{l,λᵢ} = p_{l,λᵢ}/p_path (Eq. 14.26). "These rescaled path probabilities are all easily incrementally updated during path sampling" and they avoid floating-point range problems.
- Distances are sampled with the first wavelength's pdf, which "may be a poor approach for sampling it at others". The estimator divides f by (1/n)Σᵢ p_{λᵢ}(x), and "The balance heuristic is optimal for single-sample MIS" (Eq. 14.20).
- Fig. 14.7: without spectral MIS "variance is high". With it, "MSE is reduced by a factor of 149".

**Distance sampling** — `SampleT_maj` in [pbrt-v4 src/pbrt/media.h](https://raw.githubusercontent.com/mmp/pbrt-v4/master/src/pbrt/media.h)
- `t = tMin + SampleExponential(u, seg->sigma_maj[0])`, i.e. the hero majorant.
- The callback receives `T_maj *= FastExp(-(t - tMin) * seg->sigma_maj)` as a full spectral vector.
- If the sample passes `tMax`, the function returns `T_maj` for the remainder of the segment.
- If `sigma_maj[0] == 0`, it only accumulates `T_maj *= exp(-dt*sigma_maj)` and moves on to the next segment.

**`HomogeneousMedium`** — [pbrt-v4 media.h](https://raw.githubusercontent.com/mmp/pbrt-v4/master/src/pbrt/media.h)
- The majorant is exactly `sigma_a + sigma_s`, through `HomogeneousMajorantIterator(0, tMax, sigma_a + sigma_s)`, so σ_n = 0.
- Spectra are `DenselySampledSpectrum`, and the phase function is HG only.

**Update rules in `VolPathIntegrator::Li`** — [pbrt-v4 src/pbrt/cpu/integrators.cpp](https://raw.githubusercontent.com/mmp/pbrt-v4/master/src/pbrt/cpu/integrators.cpp), approx. lines 953–1271. Verbatim logic, condensed:
```
state: L=0, beta=1, r_u=1, r_l=1
in medium, for each tentative collision (p, mp, sigma_maj, T_maj):
  if emissive: pdf = sigma_maj[0]*T_maj[0]; betap = beta*T_maj/pdf;
               r_e = r_u*sigma_maj*T_maj/pdf; L += betap*mp.sigma_a*mp.Le / r_e.Average()
  pAbsorb = sigma_a[0]/sigma_maj[0]; pScatter = sigma_s[0]/sigma_maj[0]; pNull = max(0,1-pA-pS)
  ABSORB  -> terminate
  SCATTER -> pdf = T_maj[0]*sigma_s[0]; beta *= T_maj*sigma_s/pdf; r_u *= T_maj*sigma_s/pdf;
             L += SampleLd(intr, beta, r_u);  ps = phase.Sample_p(...);
             beta *= ps.p/ps.pdf; r_l = r_u/ps.pdf; continue with new direction
  NULL    -> sigma_n = ClampZero(sigma_maj - sigma_a - sigma_s); pdf = T_maj[0]*sigma_n[0];
             beta *= T_maj*sigma_n/pdf; r_u *= T_maj*sigma_n/pdf; r_l *= T_maj*sigma_maj/pdf
after segment w/o event: beta *= T_maj/T_maj[0]; r_u *= T_maj/T_maj[0]; r_l *= T_maj/T_maj[0]
hit emitter / env:  depth==0 or specular: L += beta*Le / r_u.Average()
                    else: r_l *= p_light(prev, dir); L += beta*Le / (r_u + r_l).Average()
surface BSDF sample: beta *= f*|cos|/pdf; r_l = r_u/pdf   (r_u unchanged: BSDF pdf is λ-independent)
BSSRDF: r_u *= bssrdfSample.pdf / bssrdfSample.pdf[0]
Russian roulette on rrBeta = beta*etaScale/r_u.Average()  (max component < 1, depth > 1)
```

**`VolPathIntegrator::SampleLd`** (same file, approx. lines 1273–1390)
- Shadow rays use ratio tracking with their own `T_ray = 1, r_l = 1, r_u = 1`. At each tentative collision: `pdf = T_maj[0]*sigma_maj[0]; T_ray *= T_maj*sigma_n/pdf; r_l *= T_maj*sigma_maj/pdf; r_u *= T_maj*sigma_n/pdf`.
- Russian roulette applies when `T_ray/(r_l+r_u).Average() < 0.05` (q = 0.75). At the end of a segment, multiply by `T_maj/T_maj[0]`.
- Final combination: `r_l *= r_p*p_l; r_u *= r_p*scatterPDF`. Delta lights return `beta*f*T_ray*L / r_l.Average()`; other lights return `/ (r_l + r_u).Average()`.

**HWSS on homogeneous media**
- In volumes where extinction depends only on λ, "the transmittance can be perfectly importance sampled and the probability directly computed". — [Wilkie et al. 2014, Sec. 4.4](https://jo.dreggn.org/home/2014_herowavelength.pdf)

### Inferences
**Simplest correct homogeneous-water scheme (HLSL `float4` lanes, lane 0 = hero).** This is pbrt's algorithm specialised to σ_maj = σ_t and σ_n = 0, with the absorb/scatter choice replaced by albedo weighting.

1. **Free-flight sampling.** Draw `t = −ln(1−u)/σt.x`.
   - If `t < tSurface`, scatter:
     - `pdf = σt.x*exp(−σt.x*t)`
     - `beta *= σs*exp(−σt*t)/pdf`
     - `r_u *= σt*exp(−σt*t)/pdf`
   - Otherwise, pass through:
     - `P = exp(−σt.x*tSurface)`
     - `beta *= exp(−σt*tSurface)/P`
     - `r_u *= exp(−σt*tSurface)/P`
     - `r_l *= exp(−σt*tSurface)/P`
2. **Why not pbrt's absorb/scatter choice.** Weighting by the albedo σ_s/σ_t is equivalent to pbrt's choice in expectation but never terminates on absorption.
   - That matters for water, where the red albedo is about 0.003/0.6.
   - If the hero is red, pbrt-style absorption sampling would kill nearly every path, including its blue lanes.
   - Let Russian roulette on `beta/avg(r_u)` handle termination instead.
3. **Phase sampling.** After scattering, sample the phase function: `beta *= p/pdf; r_l = r_u/pdf_phase`. The phase function is λ-independent if you use one Morel shape for all λ.
4. **Shadow / NEE rays.** Use the deterministic `T = exp(−σt*d)` per lane. There is no random decision, so `r_u` and `r_l` are not modified.
   - This is exact and zero-variance. pbrt's generic ratio tracking with σ_n = 0 degenerates to a noisy track-length estimator: any sampled collision sets `T_ray = 0`.
5. **Purely absorbing segments (σ_s = 0).** Skip distance sampling entirely and multiply `beta *= exp(−σa*d)` (Beer–Lambert per lane). No MIS is needed because no λ-dependent pdf is involved.
6. **Why MIS matters here.** In clear water σ_t ranges from about 0.012 m⁻¹ (blue) to about 0.6 m⁻¹ (red, 700 nm).
   - With a red hero, collisions cluster at about 1.6 m. The blue lanes then get huge weights e^{−σ_b t}/(σ_r e^{−σ_r t}) when t is large.
   - Dividing by `avg(r_u)` caps this, because the blue lane's own pdf enters the average. Without it, expect strong colour fireflies. This is the Fig. 14.7 effect.
7. **Heterogeneous media** (turbid patches, foam volumes). Use pbrt's full null-scattering loop with a majorant grid. Wilkie's numeric-integration workaround is then unnecessary.

**UNVERIFIED citation:** Miller, Georgiev, Jarosz, "A Null-Scattering Path Integral Formulation of Light Transport", ACM TOG 38(4), SIGGRAPH 2019. Likely URL https://cs.dartmouth.edu/~wjarosz/publications/miller19null.html (not fetched). pbrt-v4's VolPathIntegrator is built on it, per the pbr-book chapter.

### Gaps
- I did not read the remaining 150 k characters of the pbr-book 14.2 page (Further Reading and the exact text around Eq. 14.20). The code was read in full instead.
- pbrt-v4's GPU wavefront volumetric path (`src/pbrt/wavefront/media.cpp`) was not read. It should mirror the CPU logic.

---

## 3. rgb2spec / Jakob–Hanika in pbrt-v4: units, table layout, lookup, GPU port, where to get the table

### Takeaway
**Units.** pbrt's table coefficients are evaluated with **λ in nanometres**. `rgb2spec_opt` optimises in normalised λ ∈ [0,1] and then rewrites the coefficients into the nm domain before storing them.

**Table.** `float[3][64][64][64][3]`, indexed [max component][z][y][x][coef]. The z axis is non-uniform, with nodes `zNodes[k] = smoothstep(smoothstep(k/63))`. The table is about 9 MiB in fp32.

**Build.** pbrt generates it at build time by running `rgb2spec_opt 64 rgbspectrum_srgb.cpp sRGB`. The standalone `mitsuba-renderer/rgb2spec` tool writes the same data as a binary `SPEC` file.

**GPU port.** Upload it as a structured buffer and do a manual trilinear interpolation (exact). Or use Texture3D with a manually computed z coordinate. Never fp16 nm-domain coefficients.

### Cited Findings
**Optimisation domain → nm rewrite** — `src/pbrt/cmd/rgb2spec_opt.cpp`, `main()` and `eval_residual()`, [pbrt-v4](https://raw.githubusercontent.com/mmp/pbrt-v4/master/src/pbrt/cmd/rgb2spec_opt.cpp)
- The residual uses `/* Scale lambda to 0..1 range */ lambda = (lambda_tbl[i] - CIE_LAMBDA_MIN) / (CIE_LAMBDA_MAX - CIE_LAMBDA_MIN)`.
- After Gauss–Newton, the stored values are, with `c0 = 360.0, c1 = 1.0/(830.0-360.0)`:
  ```
  out[3*idx+0] = A*c1²
  out[3*idx+1] = B*c1 − 2*A*c0*c1²
  out[3*idx+2] = C − B*c0*c1 + A*(c0*c1)²
  ```
  This is A·t² + B·t + C with t = (λ−360)/470, expanded in λ.
- Upstream `rgb2spec_opt.cpp` contains the identical rewrite. Upstream `rgb2spec.c` even states "Interpolated coeffs are wavelength-domain" and converts back to the scaled domain for its Levenberg–Marquardt refinement (`rgb2spec_fetch_opt`). — [mitsuba-renderer/rgb2spec rgb2spec_opt.cpp](https://raw.githubusercontent.com/mitsuba-renderer/rgb2spec/master/rgb2spec_opt.cpp); [rgb2spec.c](https://raw.githubusercontent.com/mitsuba-renderer/rgb2spec/master/rgb2spec.c)

**Evaluation** — `RGBSigmoidPolynomial`, [pbrt-v4 src/pbrt/util/color.h](https://raw.githubusercontent.com/mmp/pbrt-v4/master/src/pbrt/util/color.h)
- `operator()(lambda) = s(EvaluatePolynomial(lambda, c2, c1, c0))`, i.e. s(c0·λ² + c1·λ + c2), with λ in nm.
- `s(x) = IsInf(x) ? (x > 0 ? 1 : 0) : 0.5 + x/(2*sqrt(1+x²))`.
- `MaxValue()` checks 360, 830 and the vertex −c1/(2c0) if it lies inside the range.

**Optimiser details** — `rgb2spec_opt.cpp`
- Inputs are the CIE 1931 2° CMFs at 5 nm (95 samples, 360–830) and the D65, D50, D60 and E illuminants, normalised to unit luminance.
- The integrals are refined to `CIE_FINE_SAMPLES = (95−1)*3+1 = 283` points using Simpson's 3/8 rule.
- The residual is a CIELAB difference. Gauss–Newton runs 15 iterations, with the coefficients rescaled when max > 200.
- For each (x, y), the z loop starts at `k = res/5` and runs up, then down, warm-starting each solve from the previous one.
- `scale[k] = smoothstep(smoothstep(k/(res−1)))`, with `smoothstep(x) = x²(3−2x)`.
- Index: `idx = ((l*res + k)*res + j)*res + i`, where l = max component, k = z, j = y, i = x.
- Output is C++ source: `sRGBToSpectrumTable_Res`, `sRGBToSpectrumTable_Scale[64]` and `sRGBToSpectrumTable_Data[3][64][64][64][3]`, printed with `%.9g`.
- Supported gamuts: sRGB, eRGB, XYZ, ProPhotoRGB, ACES2065_1, REC2020, DCI_P3. The sRGB matrices hard-coded in this file are `xyz_to_srgb = {{3.240479,−1.537150,−0.498535},{−0.969256,1.875991,0.041556},{0.055648,−0.204043,1.057311}}`.
- Source: [pbrt-v4 rgb2spec_opt.cpp](https://raw.githubusercontent.com/mmp/pbrt-v4/master/src/pbrt/cmd/rgb2spec_opt.cpp)

**Lookup algorithm, verbatim logic** — `RGBToSpectrumTable::operator()`, [pbrt-v4 src/pbrt/util/color.cpp](https://raw.githubusercontent.com/mmp/pbrt-v4/master/src/pbrt/util/color.cpp)
1. `DCHECK` that rgb ∈ [0,1].
2. If r == g == b, return `(0, 0, (r−0.5)/sqrt(r(1−r)))`.
3. `maxc = (r>g) ? ((r>b)?0:2) : ((g>b)?1:2)`.
4. `z = rgb[maxc]`, `x = rgb[(maxc+1)%3]*(res−1)/z`, `y = rgb[(maxc+2)%3]*(res−1)/z`.
5. `xi = min((int)x, res−2)`, `yi = min((int)y, res−2)`, `zi = FindInterval(res, zNodes[i] < z)`.
6. `dx = x−xi`, `dy = y−yi`, `dz = (z−zNodes[zi])/(zNodes[zi+1]−zNodes[zi])`.
7. Trilinear interpolation of `coeffs[maxc][zi+dz][yi+dy][xi+dx][i]` for each of the 3 coefficients.

**Edge cases in upstream rgb2spec**
- Mono black/white map to c2 = ∓8192 ("we cannot use infinity as this would produce NaNs").
- Input is clamped to [0,1].
- z ≤ 0 returns `data[0..2]`.
- Source: [rgb2spec.c `rgb2spec_fetch`, `rgb2spec_fetch_mono`](https://raw.githubusercontent.com/mitsuba-renderer/rgb2spec/master/rgb2spec.c)

**GPU-style port that already exists**
- Mitsuba 3 `SRGBModel::fetch_jit` uploads `data` (res³·9 floats) and `scale` (res floats) as flat device arrays.
- It picks the max component with selects ("Ties resolve towards the last component"), guards the division with `max(z, 1e-9)`, and binary-searches `scale` on the GPU (`dr::binary_search`).
- It does manual trilinear interpolation with gathers, and handles mono with the closed form and ±8192.
- Source: [Mitsuba 3 src/render/srgb.cpp](https://raw.githubusercontent.com/mitsuba-renderer/mitsuba3/master/src/render/srgb.cpp)
- Mitsuba loads the table from `data/srgb.coeff` with `rgb2spec_load`. — [same file](https://raw.githubusercontent.com/mitsuba-renderer/mitsuba3/master/src/render/srgb.cpp)

**Where the table comes from**
- pbrt's CMake has `add_custom_command(OUTPUT rgbspectrum_srgb.cpp COMMAND rgb2spec_opt 64 ${CMAKE_CURRENT_BINARY_DIR}/rgbspectrum_srgb.cpp sRGB)`. The same is done for `dci_p3`, `rec2020` and `aces`.
- The option `PBRT_USE_PREGENERATED_RGB_TO_SPECTRUM_TABLES` (default OFF) skips generation.
- `rgb2spec_opt` is a single .cpp file with no dependencies beyond threads.
- Source: [pbrt-v4 CMakeLists.txt](https://raw.githubusercontent.com/mmp/pbrt-v4/master/CMakeLists.txt)

**Upstream binary format** — written by `rgb2spec_opt <res> <out> [gamut]`, read by `rgb2spec_load`
- `"SPEC"` (4 bytes), `uint32 res`, `float scale[res]`, `float data[res³·3·3]`.
- Current master then appends `uint32 nfine` and `float fwd[4·nfine+12]` (λ table, rgb tables, rgb→xyz, whitepoint) for `fetch_opt`.
- The current loader returns NULL if that block is missing, so older .coeff files will not load with the new `rgb2spec.c`.
- Source: [rgb2spec.c `rgb2spec_load`](https://raw.githubusercontent.com/mitsuba-renderer/rgb2spec/master/rgb2spec.c); [rgb2spec_opt.cpp](https://raw.githubusercontent.com/mitsuba-renderer/rgb2spec/master/rgb2spec_opt.cpp)

**pbrt spectrum wrappers** — [spectrum.cpp](https://raw.githubusercontent.com/mmp/pbrt-v4/master/src/pbrt/util/spectrum.cpp)
- `RGBUnboundedSpectrum` uses `scale = 2*max(r,g,b)` and `rsp = cs.ToRGBCoeffs(rgb/scale)`.
- `RGBAlbedoSpectrum` `DCHECK`s that rgb ∈ [0,1].

### Inferences
**Size**
- 3·64³·3 floats = 2,359,296 floats = 9,437,184 B (9.0 MiB), plus 256 B for `zNodes`.
- As one `Texture3D<float4>` of 64×64×192 (RGBA32F, padding the 4th channel) it would be 12.6 MB.

**Recommended GPU layout** (exact and simplest)
- One `StructuredBuffer<float>` (or `ByteAddressBuffer`) holding pbrt's array in pbrt order, plus `zNodes[64]` in a constant buffer.
- In HLSL, port `operator()` line by line: 8 corners × 3 coefficients = 24 loads, or 8 `float3` loads.
- For `FindInterval`, a 6-step binary search over 64 entries is cheap.

**Closed-form z index**
- Because `zNodes[k] = S(S(k/63))` with S(x) = x²(3−2x), and the inverse is S⁻¹(y) = 0.5 − sin(asin(1−2y)/3), you can compute `zi = clamp(floor(63*S⁻¹(S⁻¹(z))), 0, 62)`.
- Keep pbrt's dz = (z−zNodes[zi])/(zNodes[zi+1]−zNodes[zi]), which is linear in z, to match pbrt exactly.
- Guard against floating-point rounding at node boundaries: if `z < zNodes[zi]`, decrement zi.
- I derived and checked the inverse at y = 0, 0.5 and 1.

**Hardware trilinear alternative**
- Use three `Texture3D<float4>` 64³ textures, one per maxc, and sample at `((x+0.5)/64, (y+0.5)/64, (zi+dz+0.5)/64)`.
- The z coordinate must be computed manually as above, because the hardware assumes uniform spacing.
- Caveat (**UNVERIFIED**): GPU filtering weights have limited sub-texel precision (commonly 8 bits). That adds a small coefficient error, and the sigmoid amplifies it in saturated regions. Prefer manual interpolation for reference-quality A/B tests.

**Best practice for textures**
- Convert RGB textures to coefficients offline, or once on load in a compute shader, and store per-texel (c0, c1, c2).
- **Do not store nm-domain coefficients in fp16.** c0 is of order 10⁻⁴ while c0·λ² is of order 10–100, so the cancellation would destroy fp16 precision.
- Either keep fp32, or convert back to the normalised domain, exactly as upstream `rgb2spec.c` does for `fetch_opt`. With nm-domain (a₂, a₁, a₀) and t = (λ−360)/470:
  - A = a₂·470²
  - B = 470·(2·360·a₂ + a₁)
  - C = a₂·360² + a₁·360 + a₀
  - Then evaluate s(A t² + B t + C).
- Note that bilinear filtering of coefficients is not equivalent to filtering RGB.

**HLSL evaluation per path vertex** (4 lanes):
```
float4 x = (c.x*lambda + c.y)*lambda + c.z;
float4 s = 0.5 + 0.5*x*rsqrt(1 + x*x);
```
- Clamp |c2| to 8192 as rgb2spec does, to avoid inf/NaN.
- Remember the three pbrt spectrum kinds:
  - albedo: s(λ);
  - unbounded: 2·max·s(λ) fitted to rgb/(2·max);
  - illuminant: scale·s(λ)·D65(λ).

### Gaps
- I did not download a generated `rgbspectrum_srgb.cpp` or a Mitsuba `srgb.coeff`, so the exact byte size of the generated .cpp text file is unknown (it is plausibly tens of MB of text).
- Where Mitsuba 3 keeps `srgb.coeff` (shipped in its resources vs generated at build) was not verified.

---

## 4. Spectral data: CMFs, Wyman fit, D65, CIE_Y_integral, XYZ→sRGB, Planck

### Takeaway
All data below is verified, with machine-readable URLs:
- CMFs: the official CIE CSV, or CVRL at 1 nm.
- D65: the official CIE CSV, 1 nm, 300–830 nm.
- Wyman multi-lobe coefficients: transcribed exactly from the paper.
- CIE_Y_integral = 106.856895 is pbrt's constant. Summing the CIE CSV myself gives 106.856917. Mitsuba 3 uses 106.7502594 instead.
- Planck as in pbrt.

### Cited Findings
**CIE 1931 2° CMFs, machine-readable**
- Official CIE dataset: `https://files.cie.co.at/Publications-datasets/CIE_xyz_1931_2deg.csv`. 471 rows, 360–830 nm at 1 nm, format `λ,x̄,ȳ,z̄`. DOI 10.25039/CIE.DS.xvudnb9b. Landing page: [CIE datatable](https://cie.co.at/datatable/cie-1931-colour-matching-functions-2-degree-observer). Downloaded and checked this session.
- CVRL copy: `http://cvrl.ucl.ac.uk/database/data/cmfs/ciexyz31_1.csv`, 360–830 at 1 nm; downloaded, first row `360,0.000129900000,0.000003917000,0.000606100000`. The 5 nm page is linked from [cvrl.ucl.ac.uk/cmfs.htm](http://cvrl.ucl.ac.uk/cmfs.htm) as `database/text/cmfs/ciexyz31.htm`. The guessed `ciexyz31_5.csv` returned 404.
- pbrt-v4 embeds the same 1 nm data as `CIE_X/CIE_Y/CIE_Z/CIE_lambda[471]` in `spectrum.cpp`. — [spectrum.cpp](https://raw.githubusercontent.com/mmp/pbrt-v4/master/src/pbrt/util/spectrum.cpp)

**CIE D65, machine-readable**
- `https://files.cie.co.at/Publications-datasets/CIE_std_illum_D65.csv`. 531 rows, 300–830 nm at 1 nm, normalised to 100 at 560 nm (e.g. `360,46.6383`, `560,100`, `830,60.3125`). DOI 10.25039/CIE.DS.hjfjmt59. — [CIE datatable D65](https://cie.co.at/datatable/cie-standard-illuminant-d65). Downloaded this session.
- pbrt embeds `CIE_Illum_D6500[]` in `spectrum.cpp`. Named illuminant spectra are normalised by `spec->Scale(CIE_Y_integral / InnerProduct(spec, &Spectra::Y()))`, giving luminance Y = 1. — [spectrum.cpp](https://raw.githubusercontent.com/mmp/pbrt-v4/master/src/pbrt/util/spectrum.cpp)

**CIE_Y_integral**
- pbrt: `static constexpr Float CIE_Y_integral = 106.856895;` — [spectrum.h](https://raw.githubusercontent.com/mmp/pbrt-v4/master/src/pbrt/util/spectrum.h)
- My own sum of ȳ over 360–830 at 1 nm from the official CIE CSV is **106.856917**. It agrees with pbrt to 2·10⁻⁷ relative; the difference is pbrt's float data and summation. — [CIE CSV](https://files.cie.co.at/Publications-datasets/CIE_xyz_1931_2deg.csv)
- Mitsuba 3 instead uses `MI_CIE_Y_NORMALIZATION = 1/106.7502593994140625` with a 95-sample (5 nm) CIE table, `MI_CIE_SAMPLES 95`. That is about 0.1% lower. — [Mitsuba spectrum.h](https://raw.githubusercontent.com/mitsuba-renderer/mitsuba3/master/include/mitsuba/core/spectrum.h)

**Wyman, Sloan, Shirley 2013 (JCGT 2(2))**
Read from the paper PDF, [jcgt.org/published/0002/02/01/paper.pdf](https://jcgt.org/published/0002/02/01/paper.pdf). Wavelength is in nm.

- **Eq. 2, single-lobe 1931 fits:**
  - x̄ = 1.065·exp(−½((λ−595.8)/33.33)²) + 0.366·exp(−½((λ−446.8)/19.44)²)
  - ȳ = 1.014·exp(−½((ln λ − ln 556.3)/0.075)²)
  - z̄ = 1.839·exp(−½((ln λ − ln 449.8)/0.051)²)
  - RMS error < 0.015 and max error < 0.046 for x̄ and ȳ; z̄ has about 3× that.
- **Eq. 3, 1964 10° fits:**
  - x̄₆₄ = 0.398·exp(−1250·[ln((λ+570.1)/1014)]²) + 1.132·exp(−234·[ln((1338−λ)/743.5)]²)
  - ȳ₆₄ = 1.011·exp(−½((λ−556.1)/46.14)²)
  - z̄₆₄ = 2.060·exp(−32·[ln((λ−265.8)/180.4)]²)
  - The ln expressions were reconstructed from the extracted PDF text. Check signs and parentheses against the PDF.
- **Eq. 4 / Listing 1, multi-lobe piecewise Gaussian (1931).** Each term is α·exp(−½·t²) with t = (λ−β)·(λ<β ? γ : δ). Verbatim C from the paper:
  ```c
  float xFit_1931(float wave){
    float t1=(wave-442.0f)*((wave<442.0f)?0.0624f:0.0374f);
    float t2=(wave-599.8f)*((wave<599.8f)?0.0264f:0.0323f);
    float t3=(wave-501.1f)*((wave<501.1f)?0.0490f:0.0382f);
    return 0.362f*expf(-0.5f*t1*t1)+1.056f*expf(-0.5f*t2*t2)-0.065f*expf(-0.5f*t3*t3);}
  float yFit_1931(float wave){
    float t1=(wave-568.8f)*((wave<568.8f)?0.0213f:0.0247f);
    float t2=(wave-530.9f)*((wave<530.9f)?0.0613f:0.0322f);
    return 0.821f*exp(-0.5f*t1*t1)+0.286f*expf(-0.5f*t2*t2);}
  float zFit_1931(float wave){
    float t1=(wave-437.0f)*((wave<437.0f)?0.0845f:0.0278f);
    float t2=(wave-459.0f)*((wave<459.0f)?0.0385f:0.0725f);
    return 1.217f*exp(-0.5f*t1*t1)+0.681f*expf(-0.5f*t2*t2);}
  ```
  - Table 1 gives the same α/β/γ/δ: x₀..x₂, y₀..y₁, z₀..z₁.
  - Table 2 errors for the multi-lobe fit, as max / mean squared error:
    - x̄: 2.0e-4 / 3.1e-5
    - ȳ: 6.4e-5 / 7.1e-6
    - z̄: 4.9e-4 / 1.6e-5
  - The error is comparable to linearly interpolating 10 nm data, and below the within-subject variance.
  - Table 3 GPU cost per 100k evaluations, multi-lobe vs 1 nm table: 0.9 µs vs 13.9 µs.

**XYZ → linear sRGB (Rec.709), D65**
Verified matrices as found in source:
- pbrt `rgb2spec_opt.cpp`: `{{3.240479,−1.537150,−0.498535},{−0.969256,1.875991,0.041556},{0.055648,−0.204043,1.057311}}`; inverse `srgb_to_xyz` row 0 `{0.412453,0.357580,0.180423}`, row 1 `{0.212671,0.715160,0.072169}`, row 2 `{0.019334,0.119193,0.950227}`. — [rgb2spec_opt.cpp](https://raw.githubusercontent.com/mmp/pbrt-v4/master/src/pbrt/cmd/rgb2spec_opt.cpp)
- Peters' `illuminant_spectra.py`: `[[+3.2406255,−1.5372080,−0.4986286],[−0.9689307,+1.8757561,+0.0415175],[+0.0557101,−0.2040211,+1.0569959]]`. — [path_tracer/tools/illuminant_spectra.py](https://raw.githubusercontent.com/MomentsInGraphics/path_tracer/spectral/tools/illuminant_spectra.py)
- pbrt's runtime `RGBColorSpace` derives its matrix from primaries plus the white point of the D65 spectrum, as noted in the earlier notes.

**Planck** — `Blackbody(lambda, T)` in [pbrt-v4 spectrum.h](https://raw.githubusercontent.com/mmp/pbrt-v4/master/src/pbrt/util/spectrum.h)
- Constants: `c = 299792458`, `h = 6.62606957e-34`, `kb = 1.3806488e-23`, `l = lambda*1e-9`.
- `Le = (2*h*c*c) / (l⁵ * (exp((h*c)/(l*kb*T)) − 1))`, returning 0 if T ≤ 0.
- `BlackbodySpectrum` normalises the peak to 1 using Wien's λ_max = 2.8977721e-3/T (see the earlier notes).

### Inferences
- **For the GPU:** use the Wyman multi-lobe fit in shaders (about 10 ALU ops per lane, no memory traffic) for live CMF evaluation. Use the CIE 1 nm CSV for offline LUT generation and tests.
  - The fit's error versus the table is below the HWSS colour noise. For a bit-exact match against pbrt, use the 1 nm table.
- **Grey round-trip check on the matrix:** D65 white XYZ (0.95047, 1, 1.08883) × the pbrt `xyz_to_srgb` row 0 gives R ≈ 1.00001, so the matrix maps D65 to white as intended. My arithmetic.

### Gaps
- The CIE CSV for the 1931 CMFs has no header row. Check whether `CIE_xyz_1931_2deg.csv_metadata.json` documents rounding or version changes; I did not read it.

---

## 5. Water data: absorption, scattering, phase function, IOR

### Takeaway
- **Absorption.** Pope & Fry 1997 covers 380–727.5 nm in 2.5 nm steps (verified file). Above 727.5 nm, Segelstein 1981 is the convenient OMLC file; Smith & Baker 1981 (10 nm, to 800 nm) and Hale & Querry 1973 (25 nm) are alternatives.
- **Scattering.** Morel's b_w(λ) = 0.00288·(λ/500)^−4.32 m⁻¹ (seawater, δ = 0.09).
- **Phase function.** β(θ) ∝ 1 + ((1−δ)/(1+δ))·cos²θ. With δ = 0.09 that gives the normalised p(θ) = 0.06225·(1 + 0.835 cos²θ). Zhang et al. 2009 prefer δ = 0.039.
- **IOR.** Quan & Fry 1995 has **10 coefficients, n₀..n₉** (not n₀..n₁₀). At 550 nm, 20 °C, S = 35 it gives n ≈ 1.34079 relative to air, or 1.34116 relative to vacuum.

### Cited Findings
**Pope & Fry 1997**
- File: [omlc.org/spectra/water/data/pope97.dat](https://omlc.org/spectra/water/data/pope97.dat). Units 1/cm, so multiply by 100 for m⁻¹.
- Rows run from 380 to **727.5 nm in 2.5 nm steps**: 140 values, verified by reading the whole file.
- Selected values in m⁻¹: 380: 0.01137; 417.5: 0.00442 (the minimum); 500: 0.0204; 550: 0.0565; 600: 0.2224; 650: 0.340; 700: 0.624; 727.5: 1.678.
- The paper title still says "(380–700nm)". This resolves the earlier flagged gap: the file extends to 727.5 nm. The earlier notes recorded that a NASA protocol used Pope & Fry for 400–715 nm.

**Smith & Baker 1981, "Optical properties of the clearest natural waters (200–800nm)"**
- [omlc.org/spectra/water/data/smith81.dat](https://omlc.org/spectra/water/data/smith81.dat): 200–800 nm at 10 nm, 1/cm.
- Values in m⁻¹: 700: 0.65; 710: 0.839; 720: 1.169; 730: 1.799; 740: 2.38; 750: 2.47; 760: 2.55; 770: 2.51; 780: 2.36; 790: 2.16; 800: 2.07.

**Segelstein 1981 (thesis, "The complex refractive index of water")**
- [omlc.org/spectra/water/data/segelstein81.dat](https://omlc.org/spectra/water/data/segelstein81.dat): 10 nm to IR, irregular spacing of about 5 nm in the visible, 1/cm.
- Values in m⁻¹: 729: 1.981; 740: 2.4773; 750: 2.6125; 760: 2.6114; 780: 2.2706; 800: 1.9640; 815: 2.2335; 830: 3.0905.

**Hale & Querry 1973**
- [omlc.org/spectra/water/data/hale73.dat](https://omlc.org/spectra/water/data/hale73.dat): 200 nm to 200 µm, 25 nm steps in the visible, 1/cm.
- Values in m⁻¹: 725: 1.59; 750: 2.60; 775: 2.40; 800: 2.00; 825: 2.80; 830: 2.9069.

**Ocean Optics Web Book "Water" page**
- The commonly used 200–800 nm curve is Smith & Baker plus Pope & Fry. Segelstein is shown for the broad range.
- It links Röttgers et al. 2014 for the T/S dependence of absorption, and the OMLC compendium.
- Source: [oceanopticsbook.info Water](https://www.oceanopticsbook.info/view/optical-constituents-of-the-ocean/water)

**Morel scattering**
- "For δ = 0.09 … T = 20 °C and … S = 35–39‰, Morel found b at 500 nm equal to 0.00288", with the λ^−4.32 power law. — [DRDO review citing Morel 1974](https://publications.drdo.gov.in/ojs/index.php/dsj/article/download/6048/3176/15909) (via search snippet, as in the earlier notes)
- My computed values of b_w(λ) = 0.00288(λ/500)^−4.32 m⁻¹: 400: 0.00755; 500: 0.00288; 600: 0.00131; 700: 0.00067.
- Natural seawater scatters about 30% more than pure water. Use the [1 + 0.3S/37] salinity factor on pure-water values (Buiteveld). — [Biogeosciences 4, 1041 (2007)](https://bg.copernicus.org/articles/4/1041/2007/bg-4-1041-2007.pdf)

**Phase function and the b–β(90) relation**
These come from the Zhang, Hu & He 2009 reference implementation, `betasw_ZHH2009.m`. — [oceanopticsbook.info betasw_ZHH2009.m](https://www.oceanopticsbook.info/packages/iws_l2h/conversion/files/betasw_ZHH2009.m); paper: Zhang, Hu, He, Opt. Express 17(7), 5698–5710 (2009)
- Verbatim: `betasw = beta90sw*(1+((cos(rad)).^2).*(1-delta)/(1+delta))`
- Verbatim: `bsw = 8*pi/3*beta90sw*(2+delta)/(1+delta)`
- Default `delta = 0.039; % Farinato and Roswell (1976)`
- The Ocean Optics Web Book uses δ = 0.039 as "providing the best fit to data". — [oceanopticsbook Water](https://www.oceanopticsbook.info/view/optical-constituents-of-the-ocean/water)
- The normalised pure-water phase function is "β̃w(ψ) = 0.06225(1 + 0.835 cos 2ψ)". It is "symmetric about the ψ = 90 deg" angle, with g = 0 and backscatter fraction 0.5. — [oceanopticsbook Visualizing VSFs](https://www.oceanopticsbook.info/view/inherent-and-apparent-optical-properties/visualizing-vsfs)
  - The "cos 2ψ" is a rendering artefact of cos²ψ. Check: ∫0.06225(1+0.835cos²ψ)dΩ = 0.06225·4π·(1+0.835/3) = 0.99999. With cos 2ψ the integral would be 0.56. My arithmetic.

**Quan & Fry 1995 (Appl. Opt. 34, 3477–3480, DOI 10.1364/AO.34.003477)**
These come from the `rho` R package's `n_water` function. — [AlexCast/rho R/refractive_indexes.R](https://raw.githubusercontent.com/AlexCast/rho/master/R/refractive_indexes.R); docs: [rdrr.io rho::n_water](https://rdrr.io/github/AlexCast/rho/man/n_water.html)
- The formula, with λ in nm, T in °C and S in ‰:
  n(S,T,λ) = n₀ + (n₁ + n₂T + n₃T²)S + n₄T² + (n₅ + n₆S + n₇T)/λ + n₈/λ² + n₉/λ³
- The coefficients:

  | n₀ | n₁ | n₂ | n₃ | n₄ | n₅ | n₆ | n₇ | n₈ | n₉ |
  |---|---|---|---|---|---|---|---|---|---|
  | 1.31405 | 1.779e-4 | −1.05e-6 | 1.6e-8 | −2.02e-6 | 15.868 | 0.01155 | −0.00423 | −4382 | 1.1455e6 |

- Validity: T 0–30 °C, S 0–40‰. Huibers (1997) says it extrapolates to 200/220–1100 nm.
- The result is relative to air. To convert to vacuum, the package multiplies by n_air(λ) = 1 + (5792105/(238.0185 − σ²) + 167917/(57.362 − σ²))·1e-8, with σ = 1/λ[µm] (σ² = λ_µm⁻²).

### Inferences
**Sanity values for Quan & Fry**, computed with awk from the coefficients above:

| λ | n (air) | n (vacuum) |
|---|---|---|
| 400 nm | 1.349938 | — |
| 550 nm | **1.340789** | **1.341161** (n_air(550) = 1.00027784) |
| 700 nm | 1.336480 | — |

These are at 20 °C, S = 35. Pure water (S = 0) at 550 nm, 20 °C gives 1.334338 relative to air. The spread Δn ≈ 0.0135 across 400–700 nm matches the "~0.01" dispersion assumed earlier.

**Recommended HLSL water medium** (σ in m⁻¹, scene units in m):
- σ_a(λ): Pope & Fry 380–727.5 nm, then Segelstein 729–830 nm. Below 380 nm use Pope's 380 value or Segelstein (pbrt's range starts at 360). Bake to a 471-entry 1 nm `Buffer<float>` over 360–830, with linear interpolation.
- σ_s(λ) = 0.00288·(λ/500)^−4.32, times about 1.3 for seawater (or use the Zhang 2009 code to bake).
- The phase function is λ-independent: p(μ) = (1 + f μ²)/(4π(1 + f/3)), with f = 0.835 (Morel, δ = 0.09) or 0.925 (δ = 0.039).

**Exact sampling of that phase function** (my derivation)
- The CDF in μ is F(μ) = [(μ+1) + f(μ³+1)/3] / [2(1+f/3)].
- Solving F = u gives the depressed cubic μ³ + pμ + q = 0, with p = 3/f and q = −(3/f + 1)(2u−1).
- Because p > 0 there is a single real root: μ = cbrt(−q/2 + √(q²/4 + p³/27)) + cbrt(−q/2 − √(q²/4 + p³/27)).
- Check: f = 1 and u = 1 gives μ = 1; u = 0.5 gives μ = 0.
- Alternative: uniform-sphere sampling with weight p/pdf ∈ [0.78, 1.43] is also fine, because the function is so nearly isotropic.

**IOR in the path tracer**
- Use n_water(λ) / n_air(λ) at the interface, or simply the air-relative Quan & Fry value directly, since that is what it measures.
- If IOR varies per lane, call `TerminateSecondary` at every specular refraction. For the ocean surface, one option is constant n at the hero λ for the bulk surface (no termination). Enable dispersion only for spray or droplets.

**Chlorophyll / CDOM.** Optionally add Case-1 terms; see the earlier notes for the Ocean Optics Book links.

### Gaps
- Morel 1974's original table and the exact exponent derivation were not read (the source is a secondary search snippet).
- Segelstein's values above 700 nm are higher than Smith & Baker's at some wavelengths (for example 740 nm: 2.48 vs 2.38 m⁻¹). Both are plausible; pick one consistently.
- The temperature dependence of absorption near 740 nm (Röttgers 2014) was not extracted.

---

## 6. Peters' method: Fourier sRGB / bounded MESE, real-time sampling, LUT, code

### Takeaway
Peters et al. 2019 represent a reflectance by a few Fourier coefficients (trigonometric moments) of the spectrum. The spectrum is mapped to a phase φ ∈ [−π, 0] through a fixed "XYZ warp" of λ. Reconstruction uses the **bounded maximum entropy spectral estimate**, a closed form that stays in [0,1] and matches the moments exactly.

For assets, an sRGB texture is converted offline through a 256³ LUT into "Fourier sRGB": 3 numbers per texel, sRGB-like and BC1-compressible. At runtime the shader:
1. Linearly transforms these into 3 Fourier coefficients.
2. Runs a tiny Levinson solve to get 3 Lagrange multipliers per hit.
3. Evaluates a(φ) = atan(L₀ + 2L₁cos φ + 2L₂cos 2φ)/π + ½ per wavelength.

Wavelengths are sampled by inverse CDF of illuminant × (|r̄|+|ḡ|+|b̄|). A 1024-texel RGBA16F LUT per illuminant stores (CMF_rgb weight, phase). An A/B comparison against Jakob–Hanika can share the wavelength sampler and differ only in the "eval albedo at (λ, φ)" function.

### Cited Findings
**The paper** — [momentsingraphics.de/Siggraph2019.html](https://momentsingraphics.de/Siggraph2019.html); [official version, DOI 10.1145/3306346.3322964](https://doi.org/10.1145/3306346.3322964)
- Peters, Merzbach, Hanika, Dachsbacher, "Using Moments to Represent Bounded Signals for Spectral Rendering", ACM TOG 38(4), SIGGRAPH 2019.
- Abstract: spectra are represented "by an arbitrary number of Fourier coefficients", without a truncated Fourier series, "because its ringing could lead to albedos below zero or above one". The "bounded maximum entropy spectral estimate … uses an efficient closed form to compute a smooth signal between zero and one that matches the given Fourier coefficients exactly".
- Each coefficient fits in 10 bits. There is "a mapping from tristimulus color spaces to three Fourier coefficients". With 3 coefficients it reaches state of the art; with 4–8 it is "superior to all existing representations"; it is "fast enough for real-time rendering".
- Downloads: `Media/Siggraph2019/Peters2019-CompactSpectra.pdf`, `…Supplementary.pdf`, `…Code.zip`, relative to momentsingraphics.de.

**Blog part 1** — [Moments in Graphics, Spectral rendering part 1](https://momentsingraphics.de/SpectralRendering1Spectra.html)
- Fourier sRGB: "uses a 3D lookup table of resolution 256^3 to convert the sRGB color for each pixel". It "describes a color using three numbers" and stays close to sRGB, so it can use the same compression (e.g. BC1).
- At render time the inverse sRGB non-linearity is applied, and then the linear Fourier sRGB values are converted to 3 Lagrange multipliers.
- Evaluation formula: a(λ) = (1/π)·arctan(L₀ + 2L₁cos φ + 2L₂cos 2φ) + ½, with φ = warp(λ) ∈ [−π, 0].
- The run-time cost is "not cheap but not prohibitively expensive either".
- Versus storing Lagrange multipliers directly (as in a related Jakob 2019 approach): those need fp16 (6–8 B per texel, 12–16× BC1) and filter unintuitively.
- Code: [github.com/MomentsInGraphics/path_tracer/tree/spectral](https://github.com/MomentsInGraphics/path_tracer/tree/spectral); Shadertoy [X3cBRl](https://www.shadertoy.com/view/X3cBRl); illuminants from [lspdd.org](https://lspdd.org).

**Blog part 2** — [Moments in Graphics, Spectral rendering part 2](https://momentsingraphics.de/SpectralRendering2Rendering.html)
- p(λ) ∝ (|r̄|+|ḡ|+|b̄|)·i(λ), normalised over 360–830. Sampling is by inverse CDF λ_k = F⁻¹(u_k), with u_k = (u+k)/m and m = 4, "inspired by Hero wavelength sampling".
- The LUT maps u to (r̄,ḡ,b̄)·i(λ)/p(λ) and φ: "These four values nicely fit into a 1D RGBA texture … default resolution of 1024 and 16-bit floats" (8 KiB per illuminant).
- Throughput has m entries; the result is averaged with 1/m and accumulated directly as linear sRGB (the CMFs are pre-multiplied by XYZ→Rec.709).
- Files: `src/shaders/spectra.glsl`, `brdfs.glsl`, `shading_data.glsl`, `pathtrace.frag.glsl`, `tools/fourier_srgb_conversion.py`, `tools/illuminant_spectra.py`.

**Verified shader code** — [path_tracer@spectral src/shaders/spectra.glsl](https://raw.githubusercontent.com/MomentsInGraphics/path_tracer/spectral/src/shaders/spectra.glsl). Functions to port to HLSL:
- `fourier_srgb_to_fourier(v)`: a linear 3×3 with rows `(0.2276800310, 0.4748793271, 0.2993498525)`, `(0.2035160895, 0.0770505049, −0.2808208130)` and `(0.1563903497, −0.3230828819, 0.1668540863)`.
- `prep_reflectance_real_lagrange_biased_3(inout vec3 trig_moments)`:
  - clamps m₀ to [0.0001, 0.9999];
  - calls `trig_to_exp_moments_real_3` (paper Eq. 6–7);
  - calls `levinson_3_biased` (MAM 2019 Alg. 2, bias 0.9999);
  - calls `real_autocorrelation_3` and `imag_correlation_3` (Eq. 10);
  - returns 3 Lagrange multipliers (the "end of Sec. 3.6" algorithm).
- `eval_reflectance_real_lagrange_3(phase, L) = atan(2*(L₁cos φ + L₂cos 2φ + ½L₀))*0.318309886 + 0.5`.

**Verified tools**
- `fourier_srgb_conversion.py`:
  - The LUT file is `../data/srgb_to_fourier_srgb.dat`, or the zipped `.dat.zip`.
  - Header `QQQQffffff`: version, width, height, depth, min xyz, max xyz. Payload: `fff` per cell, quantised to uint8.
  - Indexed as `lut[B, G, R]` on 8-bit sRGB.
  - Source: [fourier_srgb_conversion.py](https://raw.githubusercontent.com/MomentsInGraphics/path_tracer/spectral/tools/fourier_srgb_conversion.py)
- `illuminant_spectra.py`:
  - `wavelength_to_phase` interpolates a **95-entry "XYZ warp" table** over `linspace(360, 830, 95)`, from φ = −π at 360 nm to 0 at 830 nm. All values are in the file and can be copied directly into HLSL.
  - The CMFs come from `../data/ciexyz31.csv`, treated as piecewise constant. They are converted to Rec.709, and the importance is Σ|rgb| normalised.
  - The LUT texel stores `rgb(λ)/rgb_importance(λ)` ("not by the illuminant, because we assume that this cancels with the illuminant in the integrand") together with the phase, as float16 × 4 × 1024.
  - The header holds the name, resolution, `total_rgb` and the integral of the flux.
  - Source: [illuminant_spectra.py](https://raw.githubusercontent.com/MomentsInGraphics/path_tracer/spectral/tools/illuminant_spectra.py)

### Inferences
**Comparing the two methods**

| | Jakob–Hanika | Peters (3-coefficient Fourier sRGB) |
|---|---|---|
| Per-texel data | (c0, c1, c2), fp32 or normalised-domain fp16 | 3 × 8-bit, BC1-able |
| Per-hit cost | none | a ~100-flop prep (Levinson etc.) |
| Per-lane cost | ~6 flops at λ | 1 cos/sin pair + atan at φ |
| Evaluated at | λ | φ |

Both are bounded in [0,1] and smooth.

**A/B behind one HLSL interface** (my proposal):
```hlsl
struct WavelengthSample { float4 lambda; float4 pdf; float4 phase; };   // phase = XYZwarp(lambda), only Peters uses it
WavelengthSample SampleWavelengths(float u);          // shared: pbrt SampleVisible OR Peters illuminant×CMF LUT
struct AlbedoCoeffs { float3 c; };                     // JH: sigmoid coeffs; Peters: 3 Lagrange multipliers
AlbedoCoeffs PrepareAlbedo(float3 texel);              // JH: table lookup (or pre-baked); Peters: fourier_srgb_to_fourier + prep_reflectance_...
float4 EvalAlbedo(AlbedoCoeffs a, WavelengthSample w); // JH: sigmoid(poly(lambda)); Peters: atan series at phase
```
- **Keep the wavelength sampler as a separate switch.** Then you can measure the upsampling difference with sampling held fixed, and the sampling difference with upsampling held fixed.
- **Peters' LUT weight already folds in the CMF and 1/p**, while pbrt-style accumulation divides by pdf(λ) and multiplies the CMF in `ToXYZ`. Make the accumulator interface take a ready `float3 rgbWeight[4]` per lane so both fit.
- **Convert the same source sRGB texture to both representations.** That is pbrt's table for JH, and Peters' 256³ LUT for Fourier sRGB. Peters' LUT ships in his repo's data zip, `spectral_path_tracer_data.zip`.

### Gaps
- I did not read the full 2019 paper text: the exact definition of the "XYZ warp" optimisation, and the error figures versus Jakob–Hanika. The paper PDF URL is above.
- The size and download location of `srgb_to_fourier_srgb.dat(.zip)` and `spectral_path_tracer_data.zip` were not checked.
- Part 2's exact real-time cost of `prep_reflectance` versus the JH lookup was not quantified beyond the overall timings (in the earlier notes).

---

## 7. Sanity tests and the Mitsuba 3 / FLIP comparison workflow

### Takeaway
Validate in this order: analytic round-trips (grey, D65 white, the CIE_Y constant), then the white furnace, then the Macbeth chart under D65 (spectral vs RGB render), then the water slab against closed-form Beer–Lambert, and finally image comparison against Mitsuba 3 `scalar_spectral` using HDR-FLIP (`pip install flip-evaluator`).

Mitsuba's homogeneous medium takes spectral `sigma_t` and `albedo`, and its `tabphase` plugin can reproduce the Morel phase function exactly.

### Cited Findings
**Mitsuba spectrum tags** — [Mitsuba 3 docs, Spectra plugins](https://mitsuba.readthedocs.io/en/stable/src/generated/plugins_spectra.html)
- `<spectrum name=".." value="400:0.1, 700:0.2"/>` and `<spectrum name=".." filename=".."/>` become `regular`/`irregular` spectra in spectral mode.
- `<rgb>` always goes through the `srgb` (rgb2spec) upsampling.
- The `uniform` spectrum defaults to `wavelength_min = 360` and `wavelength_max = 830` nm.
- For unbounded RGB properties (e.g. conductor eta), "it is highly recommended to directly define a spectrum curve".

**Mitsuba `homogeneous` medium** — [Mitsuba 3 docs, Media plugins](https://mitsuba.readthedocs.io/en/stable/src/generated/plugins_media.html)
- Parameters: `albedo` (float/spectrum/volume, default 0.75), `sigma_t` (float or spectrum, "in inverse scene units", default 1), `scale`, `sample_emitters` and a nested `phase` (default isotropic).
- Doc example: "when a world-space distance of 1 unit corresponds to a meter, the extinction coefficient should have units of inverse meters".

**Mitsuba phase plugins** — [Mitsuba 3 docs, Phase plugins](https://mitsuba.readthedocs.io/en/stable/src/generated/plugins_phase.html)
- `isotropic`, `hg`, `sggx`, `tabphase`, `rayleigh` (unpolarised, no parameters) and `blendphase`.
- `tabphase` takes `values` as "a comma-separated list of phase function values parametrized by the cosine of the scattering angle", regularly spaced over [−1, 1] and auto-normalised.

**Mitsuba films** — [Mitsuba 3 docs, Films](https://mitsuba.readthedocs.io/en/stable/src/generated/plugins_films.html)
- `hdrfilm` and `specfilm` (spectral film) exist.

**Mitsuba variants**
- pip wheels include `scalar_spectral`, `llvm_ad_spectral` and `cuda_ad_spectral`, per the earlier notes. — [Mitsuba variants](https://mitsuba.readthedocs.io/en/stable/src/key_topics/variants.html)

**FLIP (NVIDIA)** — [github.com/NVlabs/flip README](https://raw.githubusercontent.com/NVlabs/flip/main/README.md)
- Python: `pip install flip-evaluator`, then `flip -r referenceImage.png -t testImage.png` or `flip --reference reference.{exr|png} --test test.{exr|png}`.
- C++/CUDA: `flip[-cuda].exe --reference … --test …` (`src/cpp/FLIP.sln`; single header `FLIP.h`).
- Papers: [LDR-FLIP](https://research.nvidia.com/publication/2020-07_FLIP), [HDR-FLIP](https://research.nvidia.com/publication/2021-05_HDR-FLIP), and the RTG II chapter.

**Constant mismatch.** Mitsuba normalises Y with 1/106.7502594 against pbrt's 1/106.856895, a 0.1% difference. — [Mitsuba spectrum.h](https://raw.githubusercontent.com/mitsuba-renderer/mitsuba3/master/include/mitsuba/core/spectrum.h); [pbrt spectrum.h](https://raw.githubusercontent.com/mmp/pbrt-v4/master/src/pbrt/util/spectrum.h)

### Inferences
**Test ladder** (each step has an exact expected value):

1. **CMF integral.** Monte Carlo of ∫ȳ with your wavelength sampler, i.e. the average of ȳ(λ)/pdf(λ), should converge to 106.857 (±noise). This tests the sampler, its pdf and the rotation together.
2. **Grey round-trip.**
   - RGB (0.5, 0.5, 0.5) gives c = (0, 0, 0) and s ≡ 0.5.
   - Under an `RGBIlluminantSpectrum` white (D65 with Y normalised to 1), a Lambertian albedo of 0.5 must render exactly sRGB 0.5 (up to noise).
   - Repeat with the table path for near-greys (0.5, 0.5, 0.501) to catch indexing bugs.
3. **D65 → white.** Converting D65 (Y = 1) to XYZ to sRGB should give (1, 1, 1) within 1e-4 (see the matrix check in Q4).
4. **White furnace.** Use albedo 1 (rgb2spec mono gives c2 = +8192, so s = 1) and a uniform environment. The pixel must equal the env radiance for every bounce count. Then use the spectral albedo of a saturated colour and compare against the analytic geometric series per lane.
5. **RGB ↔ spectral consistency.** Under D65 with single-bounce direct lighting, a spectral render of the 24 sRGB Macbeth patch colours should reproduce the input sRGB to within ~1e-3, because Jakob–Hanika round-trips exactly at the table nodes. Multi-bounce interreflection will legitimately differ from an RGB render; that difference is the spectral effect, not a bug.
6. **Water slab.**
   - Absorption only: a collimated beam through d metres of water. The pixel XYZ must equal ∫ CMF·D65·e^{−σ_a(λ)d} dλ / ∫ȳ·D65, computed offline at 1 nm.
   - Then add scattering, render the same scene in Mitsuba `scalar_spectral` (`homogeneous` with a `sigma_t`/`albedo` spectrum from your baked tables, and `tabphase` sampled from (1 + 0.835μ²)), and compare.
7. **Compare against Mitsuba.**
   - Export the identical scene in Mitsuba XML (sphere, rectangle, `diffuse`/`dielectric` with `int_ior` as a `<spectrum>` from Quan & Fry, a `constant` or `envmap` emitter, `hdrfilm` in RGB).
   - Render with many spp, write EXR, then run `flip --reference mitsuba.exr --test mine.exr`.
   - Expect about 0.1% luminance offset from the different CIE_Y constants unless you rescale.
   - **UNVERIFIED:** the Python calls `mi.set_variant('scalar_spectral'); scene = mi.load_file('scene.xml'); img = mi.render(scene, spp=N); mi.util.write_bitmap('ref.exr', img)` are standard Mitsuba 3 API from memory and were not re-read this session.
8. **Dispersion.** A prism or sphere with Quan & Fry n(λ) and `TerminateSecondary`. Check that non-dispersive regions show no new colour noise and that the rainbow fringe converges.

### Gaps
- **Macbeth spectral reflectance data** (for a true spectral chart rather than sRGB-upsampled patches) was not sourced this session. The commonly used set is the BabelColor ColorChecker spectral data (**UNVERIFIED** URL, babelcolor.com).
- No Blender → Mitsuba exporter was verified. The mitsuba-blender add-on exists from memory (**UNVERIFIED**).
- The Mitsuba `.spd` file format for `filename=` spectra (whitespace-separated "λ value" lines) was not confirmed from the docs.
