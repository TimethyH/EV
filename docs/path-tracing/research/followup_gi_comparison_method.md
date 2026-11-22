# How to compare real-time GI techniques: metrics, protocol and test scenes (follow-up research, 2026-11-22)

Scope: how to compare plain path tracing + denoiser, SHaRC, the student's own NRC and (probably) surfel GI, DDGI
probes and ReSTIR DI/GI/PT in EV rigorously, for both visual fidelity and performance. It covers:
- image-quality metrics and how to build a ground truth;
- temporal metrics (flicker, lag, ghosting);
- how to separate bias from variance;
- performance methodology;
- standard test scenes, with what EV can load today;
- which scenes the GI papers used;
- a concrete comparison protocol and a "GI comparison harness" milestone.

The techniques themselves are covered by other notes. Not repeated here:
- FLIP default settings (67 ppd, ACES, automatic exposure range): `followup_gaps_completion.md` Q49.
- Stable clocks, D3D12 timestamp caveats and profiler tools: `followup_performance_optimization.md` §2–3.

Labels: [T] training knowledge, [I] inference, [E] estimate, [U] unverified. Facts without a label come from the
linked source, fetched 2026-11-22. EV facts come from reading the engine source on branch `path-tracing`. No engine
file was changed.

## 1. Image-quality metrics, ground truth, equal-time vs equal-sample

### Takeaway
Rendering papers report a **relative squared error** (relMSE / MRSE) or a relative absolute error against a
high-spp reference, almost always together with **ꟻLIP**, and compare at **equal time**. Plain MSE and PSNR are
dominated by bright pixels and fireflies (very bright isolated noise pixels). "Relative" means each pixel's error is
divided by that pixel's reference brightness (squared), so dark and bright regions count about equally.

The most useful single plot is **error vs time on log–log axes**:
- an unbiased method is a straight line going down (slope −1 for relMSE);
- a biased cache flattens out at its bias floor;
- where two curves cross tells you which method wins at which budget.

For EV:
- compute every metric on **linear HDR radiance (EXR), before tone mapping**;
- use relMSE as the main number, HDR-FLIP as the perceptual number, and "time to equal error" as the speed-up number;
- render the reference with the **same spectral pipeline** with clamping and denoising off, and check its own noise
  floor with a second independent reference.

### Cited Findings
**What the GI papers actually report**
- NRC (Müller et al. 2021):
  - "For each image, we report the mean relative squared error (MRSE) [Rousselle et al. 2011] or its decomposition
    into relative bias (rBias) and variance (rVar) … When applicable, we also list the perceptually based FLIP metric
    … that is more robust to outliers ('fireflies')."
  - All images at 1920 × 1080 on an i9 9900k + RTX 3090.
  - "Our reference images were created using ReSTIR-enabled path tracing to ensure that we only measure the bias
    caused by radiance caching and not that of ReSTIR."
  - Table 2, "Time to converge to equal MRSE": the baseline is run until it reaches the MRSE of one frame of the new
    method. Average speed-up 13.6×, e.g. Classroom 145 frames / 2625 ms vs 1 frame / 15.2 ms.
  - [NRC paper PDF](https://d1qx31qr3h6wln.cloudfront.net/publications/mueller21realtime.pdf)
- ReSTIR GI (Ouyang et al. 2021):
  - Reports MSE: "MSE improvements ranging from 9.3× to 166×" at 1 spp.
  - "Reference images were computed using that path tracer with 8,192 samples per pixel."
  - Figure 13 is a "comparison of roughly equal time renderings"; Figure 1 is equal-time at 8.0 ms vs 8.9 ms.
  - RTX 3090, 1080p.
  - [NVIDIA page](https://research.nvidia.com/publication/2021-06_restir-gi-path-resampling-real-time-path-tracing)
    (paper PDF read from the Eurographics digital library copy)
- ReSTIR DI (Bitterli et al. 2020): "Errors are reported as Relative Mean Absolute Error (RMAE), which we found less
  sensitive to isolated outliers than mean squared error (MSE)." Error-vs-render-time curves are in Fig. 11. Run on an
  RTX 2080 Ti (Titan RTX for Amusement Park) — [paper](https://research.nvidia.com/publication/2020-07_spatiotemporal-reservoir-resampling-real-time-ray-tracing-dynamic-direct)
- ReSTIR PT / GRIS (Lin et al. 2022):
  - Reports **MAPE**, with the footnote: "We use MAPE(I, I_gt) = mean(|I − I_gt| / (0.01·mean(Ĩ_gt) + Ĩ_gt)), for
    Ĩ_gt a grayscale ground-truth."
  - Equal-time comparisons at 25–80 ms, 1920 × 1080, RTX 3090.
  - Caveat in the paper: "Equal-time comparisons are impacted by GPU thread divergence … Comparing convergence at equal
    sample counts (Figure 18) rather than equal time, our hybrid shift is generally better."
  - For equal time, cheaper methods get more samples or "multiple independent ReSTIR chains".
  - Images "captured during camera motion, to prevent ReSTIR PT from overly relying on temporal reuse".
  - [paper PDF](https://graphics.cs.utah.edu/research/projects/gris/sig22_GRIS.pdf),
    [project](https://graphics.cs.utah.edu/research/projects/gris/)
- A-SVGF (Schied et al. 2018) reports RMSE and SSIM per scene, and SSIM per frame over an animated sequence against a
  1024 spp reference — [paper PDF](https://cg.ivd.kit.edu/publications/2018/adaptive_temporal_filtering/adaptive_temporal_filtering.pdf)
- GI-1.0 (Boissé et al. 2023, AMD) reports **no** image-error metric, only per-pass timings on RX 6900 XT / RTX 3080
  and visual comparisons — [arXiv HTML](https://arxiv.org/html/2310.19855)

**Metric definitions in real code**
- pbrt-v4 `Image::MRSE`: `Sqr(v − vref) / Sqr(vref + 0.01)`, averaged over pixels, per channel. `Image::MSE` and
  `Image::MAE` have no epsilon — [image.cpp](https://raw.githubusercontent.com/mmp/pbrt-v4/master/src/pbrt/util/image.cpp).
  `imgtool diff --metric` offers "MAE", "MSE", "MRSE", "FLIP". Its FLIP clamps both images to [0,1], which makes it
  LDR-FLIP — [imgtool.cpp](https://raw.githubusercontent.com/mmp/pbrt-v4/master/src/pbrt/cmd/imgtool.cpp)
- Rousselle et al. 2011 (the MRSE citation in NRC) measures "the relative squared distance to the reference image".
  Their own error estimate is (Â − B̂)² / (ε + Â²) with ε = 10⁻³ — [paper PDF](https://visualcomp.ucsd.edu/classes/cse274/fa21/papers/a195-rousselle.pdf)
- Other papers use (I − R)² / (R² + 0.01) and ε = 10⁻⁴ depending on the dataset (secondary source:
  [arXiv 2106.12802](https://arxiv.org/pdf/2106.12802)). So **ε differs between papers**, and numbers are only
  comparable within one paper or one protocol.
- AMD Capsaicin's GPU metrics: MSE, RMSE, PSNR, RMAE, SMAPE and SSIM (k1 = 0.01, k2 = 0.03). Multichannel input is
  reduced to luminance. **HDR input is PQ-encoded before the metric**, so PSNR/SSIM get a bounded range —
  [gpu_image_metrics.h](https://raw.githubusercontent.com/GPUOpen-LibrariesAndSDKs/Capsaicin/master/src/core/src/utilities/gpu_image_metrics.h),
  [gpu_image_metrics.comp](https://raw.githubusercontent.com/GPUOpen-LibrariesAndSDKs/Capsaicin/master/src/core/src/utilities/gpu_image_metrics.comp).
  Capsaicin's SMAPE is `|ref − src| / ((|ref| + |src|)/2)` with a 100× factor.

**ꟻLIP**
- NVlabs/flip v1.7, BSD-3. It "holds implementations of the LDR-FLIP and HDR-FLIP image error metrics".
  `pip install flip-evaluator` (Python ≥ 3.10). CLI: `flip --reference ref.exr --test test.exr` —
  [repo](https://github.com/NVlabs/flip)
- The tool prints mean, weighted median, 1st/3rd weighted quartiles, min and max. It writes weighted histograms as
  Python scripts that produce a PDF — [Python README](https://raw.githubusercontent.com/NVlabs/flip/main/src/python/README.md)
- Python API: `flipErrorMap, meanFLIPError, parameters = flip.evaluate(ref, test, "HDR")`, where ref/test can be paths
  or NumPy arrays from `flip.load("….exr")` — [api_example.py](https://raw.githubusercontent.com/NVlabs/flip/main/src/python/api_example.py)
- HDR-FLIP (Ebelin, Nilsson, Shirley, Akenine-Möller, EG short papers 2021) computes "a composite visualization over a
  number of low dynamic range error maps" of exposure-compensated, tone-mapped image pairs —
  [NVIDIA page](https://research.nvidia.com/publication/2021-05_visualizing-errors-rendered-high-dynamic-range-images)

**Efficiency**
- pbrt defines an estimator's efficiency from its variance V[F] and run time T[F] (ε = 1/(V·T) [T]):
  "an estimator F1 is more efficient than F2 if it takes less time to produce the same variance" —
  [pbrt 3ed §13.7](https://pbr-book.org/3ed-2018/Monte_Carlo_Integration/Russian_Roulette_and_Splitting)
- Worked example in pbrt-v4: 1.47× slower but MSE 4.42× lower, so "a 3.05 times improvement in Monte Carlo
  efficiency" — [pbrt-v4 §16.1](https://pbr-book.org/4ed/Retrospective_and_the_Future/pbrt_over_the_Years)

### Inferences
- [I] **EV metric set (decide once, write it at the top of `performance.md`):**
  1. **relMSE** = mean over pixels and RGB of (I − R)² / (R² + ε), with ε = 0.01, on linear HDR.
     - Also log the pbrt-v4 MRSE variant ((R + 0.01)² in the denominator) so numbers can be checked with
       `imgtool diff`.
     - Optional: a "trimmed" relMSE that ignores the 0.01 % largest per-pixel errors, so one firefly does not decide
       a comparison. Report it next to the untrimmed value, never instead of it [T/U].
  2. **HDR-FLIP** mean and weighted median, with a **fixed exposure range per shot**, as Q49 says.
  3. **MAPE/SMAPE** as an outlier-robust check (and needed for the temporal metric in §2).
  4. **SSIM/PSNR only after PQ encoding**, as Capsaicin does, and only to compare with papers that report them.
     They are not decision metrics for HDR [I].
- [I] **Equal-time vs equal-sample.** Use each for a different question:
  - Equal-sample (same spp / same rays) asks "which estimator is better per sample?"
  - Equal-time asks "which is better on this GPU?". That is the one that matters for a real-time engine, but it mixes
    in divergence and implementation quality (GRIS warns about this).
  - Report both for the main table. The equal-time number is the headline.
- [I] **"Time to equal error"** (NRC Table 2) is the most intuitive speed-up number for a student write-up. Example:
  "SHaRC reaches PT+denoiser's relMSE 6× faster."
- [I] **Ground-truth recipe for EV:**
  1. The same `.evscene`, the same frozen time and camera, the same RGB→spectrum (Jakob–Hanika), CMFs, D65 and film,
     the same max-bounce rule. Russian roulette is fine, because it is unbiased.
  2. **No firefly clamp, no denoiser, no caches, no ReSTIR reuse.** The NRC paper's exception: when one technique's
     own direct lighting (ReSTIR DI) is shared by all compared variants, the reference may use it too, so only the GI
     part is measured.
  3. Mip selection: use mip 0 or the same LOD rule as the real-time path. Otherwise texture filtering differences
     show up as "GI error".
  4. Spp: 16k on the M7 plan's shots (ReSTIR GI used 8,192, A-SVGF 1024).
  5. Check the reference: render **two references with different seeds** at N/2 spp each. relMSE(A, B) ≈ 2 × the
     reference's own noise. Keep it at least 10× below the smallest error you want to compare [I].
  6. Spectral note: hero-wavelength noise is colour noise, so spectral references converge more slowly than RGB ones
     at equal spp. Check the two-seed noise floor per shot rather than trusting a fixed spp [I].
- [I] For an absolute correctness check, not for technique ranking, Mitsuba 3 `scalar_spectral` or pbrt-v4 (both
  spectral) renders of the same scene remain the independent reference (M3/M4). Bitterli's scenes ship in both formats
  (§5).

### Gaps
- The exact relMSE ε that NRC used is not written in the paper; it only cites Rousselle 2011.
- The FLIP Python module name in `import …` was not seen in the example (only the `flip.` prefix) [U].
- No primary source was found for the "discard the 0.01 % brightest pixels" convention [U].

## 2. Temporal metrics: flicker, lag, ghosting

### Takeaway
There is **no standard temporal metric** in rendering papers. Each paper builds its own from three simple
ingredients:
1. **frame-to-frame difference on a static scene** (flicker / temporal instability). NRC uses SMAPE between
   consecutive frames; A-SVGF uses SSIM between consecutive frames.
2. **per-frame error against a per-frame reference along an animated sequence**. A-SVGF plots SSIM per frame.
3. **event tests**: switch a light on or off, move the sun, move an object, then count frames until the image is right
   again. A-SVGF uses a light that turns on and off; ReSTIR GI a sudden sunlight change ("visible bias still remains
   after 12 frames").

For a perceptual video score there is **ColorVideoVDP** (Mantiuk et al., SIGGRAPH 2024, MIT, `pip install cvvdp`). It
models flicker and spatio-temporal sensitivity, takes absolute linear EXR, and reports JOD (10 = no visible
difference). FovVideoVDP (2021) is its foveated predecessor. No "temporal FLIP" tool was found; only an NVIDIA patent
on a spatio-temporal metric.

### Cited Findings
- **NRC Fig. 3:** "The false color images depict temporal stability across 100 frames, measured as the symmetric mean
  absolute percentage error (SMAPE) averaged over all consecutive pairs of frames … To be more meaningful in print,
  the scene and the camera have been fixed."
  - Weight averaging (EMA α = 0.99) is NRC's flicker fix. Without it there are "temporal artifacts like flickering and
    oscillations … even when the scene and camera are static".
  - [NRC PDF](https://d1qx31qr3h6wln.cloudfront.net/publications/mueller21realtime.pdf)
- **A-SVGF §5.2:** "Training (recurrent) autoencoders to be temporally stable is challenging due to the lack of an
  appropriate error metric … we assess the improvement in temporal stability using a static scene configuration …
  We measure the perceptual difference between consecutive frames using SSIM."
  - Fig. 4: per-frame SSIM over an animated sequence "at 1280 × 720 at 30 Hz compared to a 1024 spp reference".
  - Scenes: Pillars (shadows), Sponza, GlossySponza, Dungeon (multiple lights, flythrough at 1080p/60 Hz on a Titan Xp).
  - The teaser has a flickering blue area light; the light switches in frames 406/412, and "SVGF leaves a trail" on
    glossy highlights after the light turns off.
  - [paper PDF](https://cg.ivd.kit.edu/publications/2018/adaptive_temporal_filtering/adaptive_temporal_filtering.pdf),
    [project](https://cg.ivd.kit.edu/atf.php)
- **ReSTIR GI Fig. 11:** "without sample validation, visible bias still remains after 12 frames of sudden change of
  sunlight … with sample validation, the result rapidly adapts to changes in lighting" (PDF, §6).
- **GRIS / ReSTIR PT Fig. 9–10:** with temporal reuse and no M-cap, accumulation converges "to a wrong result due to
  temporal correlations". This is a temporal *bias* test: accumulate many frames of a real-time method and compare to
  the reference — [GRIS PDF](https://graphics.cs.utah.edu/research/projects/gris/sig22_GRIS.pdf)
- **NRD** (NVIDIA's denoisers):
  - The validation overlay (`OUT_VALIDATION`) shows "number of accumulated frames" and "amount of virtual history",
    with red checkerboarding on history reset.
  - The README advises judging temporal stability in the sample's TESTS section, and says the REFERENCE denoiser "can
    be used to estimate temporal signal quality".
  - It gives no comparison methodology — [NRD](https://github.com/NVIDIA-RTX/NRD)
- **ColorVideoVDP:**
  - "a full-reference visual quality metric that predicts the perceptual difference between pairs of images or
    videos". Units are JOD, "The highest quality (no difference) is reported as 10".
  - `pip install cvvdp` (`cvvdp[cuda]`), PyTorch. It runs on CPU, but "processing times will be much longer,
    especially for video".
  - Inputs: PNG frame sequences or EXR, which "MUST contain absolute linear color values".
  - `--display standard_fhd` / `standard_4k` / `standard_hdr_linear`. MIT license. SIGGRAPH 2024, DOI 10.1145/3658144.
  - [repo](https://github.com/gfxdisp/ColorVideoVDP)
- **FovVideoVDP:** `pip install pyfvvdp`, v1.2.2 (2025-02-24), TOG 40(4) 2021. It "can predict temporal artifacts like
  flicker and judder", and handles HDR (`standard_hdr_linear`) since v1.2.0 — [repo](https://github.com/gfxdisp/FovVideoVDP)
- No temporal/video FLIP was found. The closest is NVIDIA patent US10810455B2, "Spatio-temporal image metric for
  rendered animations" (inventors Nilsson, Akenine-Möller) — [patent](https://patents.google.com/patent/US10810455)

### Inferences
- [I] **EV temporal metric set:**
  1. **tSMAPE (flicker)** on a static shot: mean SMAPE between consecutive frames over 100 frames. Show it as a false
     colour map (as NRC does) plus one number. Run it on the **converged** state (after warm-up), with only the RNG
     seed changing per frame.
  2. **Per-frame relMSE/FLIP curve** along a fixed camera path against per-frame references. References are costly,
     so use keyframes (every 10th frame) [E]: 30 references per 300-frame path.
  3. **Lag** after an event, using a region of interest (ROI) next to the change (wall lit by the switched light, floor
     under the moved object). Let L(t) be the ROI mean luminance and L_old/L_new the converged values before/after.
     - lag90 = frames until |L(t) − L_new| ≤ 10 % · |L_old − L_new|; lag99 likewise.
     - Report it in frames **and** ms, because a 120 fps method with 8 frames of lag beats a 30 fps one with 4 frames.
  4. **Ghosting** after a moved object: the error in the ROI it left (old position) for frames e … e+30, against the
     reference of the new state.
  5. **cvvdp JOD** on the 300-frame path against reference frames (3070 PC with CUDA; the AMD PC runs it on CPU,
     slowly) [I].
- [I] Event tests to script (all three appear in the papers above):
  - (a) light switched off/on (point light in Bistro interior, a candle group in Intel Sponza);
  - (b) sun direction jump of 30° (ReSTIR GI style);
  - (c) object moved by 1 m or a door opened (light floods a closed room: DDGI/surfel worst case);
  - (d) camera cut/teleport (all screen-space histories invalid: ReSTIR, denoiser);
  - (e) fast camera turn (90° in 0.5 s).
- [I] Caches with world-space state (SHaRC, NRC, DDGI, surfels) and screen-space reuse (ReSTIR, denoisers) fail in
  different events: caches lag on light changes, screen-space methods break on disocclusion and cuts. The event set
  above covers both, which is why it should be fixed before any technique is built.

### Gaps
- No public SVGF/A-SVGF/DLSS-RR numeric "temporal stability score" exists beyond the frame-to-frame SSIM plots. The
  DLSS-RR material was not checked for a method.
- Whether cvvdp accepts EXR **sequences** as video (it documents PNG sequences and EXR images) is [U].

## 3. Bias vs variance, energy loss, light leaks

### Takeaway
Bias is separated from variance by **averaging many independent runs**:
- run K seeds (or accumulate the method's own output for many frames with temporal reuse that does not correlate);
- the mean image minus the reference is the bias;
- the spread across runs is the variance;
- MSE ≈ bias² + variance.

NRC reports exactly this split (rBias², rVar), and Fig. 13 compares DDGI vs NRC at 1 spp **and** 1024 spp ("converged")
to show the bias–variance trade-off. Caches (SHaRC, NRC, DDGI, surfels) are biased by design, so EV should always show
the **converged** image of each cache next to its real-time one. Energy loss / darkening is tested with a furnace test
and a ratio of mean luminance. Light leaks are tested with thin walls between a bright and a dark room (DDGI's main
selling point, NRC's Attic tarp, RTXGI's "two-rooms" scene).

### Cited Findings
- NRC Fig. 13:
  - "For each method, we show results at 1spp … and 1024spp (converged …) to allow for the visual inspection of the
    bias-variance trade-off. We also report relative bias (rBias) and variance (rVar)."
  - Example Pink Room: DDGI rVar 0.394 / rBias² 3.836 vs NRC rVar 1.087 / rBias² 0.002.
  - "Both methods use ReSTIR for direct illumination."
  - [NRC PDF](https://d1qx31qr3h6wln.cloudfront.net/publications/mueller21realtime.pdf)
- NRC on DDGI: it "terminates paths into the irradiance probes at their first diffuse interaction … very performant …
  little noise, but … lacks ambient occlusion and can expose visible bias". The Attic scene "is an interesting test case
  for light leakage as it features … thin elements such as the tarp" (same PDF).
- DDGI (Majercik et al. 2019):
  - Classic probes show "light leaking, lightmap seams, visibility/occlusion undersampling" (Fig. 2).
  - Low probe density "can lead to subtle light leaking artifacts".
  - Timings on the Greek Temple scene (876,127 primitives), RTX 2080 Ti.
  - [JCGT paper](https://jcgt.org/published/0008/02/01/) [link from training knowledge; the PDF was read]
- GRIS: "convergence is realized in the offline case by rendering multiple independent frames". Temporal correlations
  can make accumulation converge to a wrong image (Fig. 10) — [GRIS PDF](https://graphics.cs.utah.edu/research/projects/gris/sig22_GRIS.pdf)
- ReSTIR GI Fig. 9 shows "Bias caused by spatial reuse" with a "10× difference" image (PDF).
- RTXGI-DDGI's test harness ships glTF scenes `cornell`, `furnace`, `multi-cornell`, `sponza`, `tunnel`, `two-rooms`.
  `two-rooms.ini` has cameras "Open Room", "Closed Room", "Outside", one sun, a 32×6×32 probe volume and path-tracer
  settings (`pt.numBounces=20`) for comparison —
  [config folder](https://github.com/NVIDIAGameWorks/RTXGI-DDGI/tree/main/samples/test-harness/config),
  [two-rooms.ini](https://raw.githubusercontent.com/NVIDIAGameWorks/RTXGI-DDGI/main/samples/test-harness/config/two-rooms.ini)

### Inferences
- [I] **Bias protocol for EV** (per technique, per static shot):
  1. Run K = 32 independent seeds of the *converged real-time state*: warm up 300 frames, then save one frame per seed.
     For temporal methods, restart from scratch per seed so the runs are independent.
  2. Mean image M = average of the K frames; per-pixel variance S² over the K frames.
  3. rBias² = mean((M − R)² / (R² + ε)); rVar = mean(S² / (R² + ε)). Check that relMSE ≈ rBias² + rVar.
     - The variance of M itself is S²/K. Subtract it from the bias term when K is small [T].
  4. Show M next to R with a difference/FLIP map. That is the "what does this cache get wrong" picture.
- [I] **Energy loss:** relative mean luminance ΔL = mean(I)/mean(R) − 1 over the whole image and over indirect-only
  regions. Caches that drop bounces, or probes that clamp irradiance, show up as a negative ΔL.
  - Add a **GI furnace test**: a closed room with all albedo 0.8 under a uniform emitter. The analytic infinite-bounce
    result is known for a closed uniform enclosure [T: radiance = Le/(1 − ρ) for ρ = albedo]. Every technique should
    reach it, and the number of bounces it effectively captures can be read off directly.
- [I] **Light-leak test:** a thin (1–2 cm) wall between a sunlit room and a dark room, with the camera in the dark room
  (RTXGI's "Closed Room" camera). Leak metric = mean luminance in the dark room relative to the reference (which is
  ~0). Repeat with the wall at several thicknesses relative to probe/surfel/hash-cell size.
- [I] **Indirect-only images:** render AOVs with direct lighting removed (path contribution from bounce ≥ 2) for both
  reference and technique. Then the GI error is not diluted by a perfect direct-lighting term, which makes rBias/rVar
  much more sensitive.

## 4. Performance methodology for comparisons

### Takeaway
Compare **GI cost**, not frame cost: the sum of the passes a technique adds (trace for GI, cache update/train, query,
GI denoise), measured with per-pass GPU timestamps after warm-up, with mean, median, **p95 and p99** over ≥ 300 frames.
Also record:
- **memory footprint**: the technique's own buffers plus process VRAM;
- **scaling curves**: resolution, number of lights, scene size, amount of dynamic content.

The fair visual comparison is **"same ms budget, which looks best"**: tune each technique's knobs to hit e.g. 4 ms of GI
time on the main GPU, then compare relMSE/FLIP/temporal metrics. The NRC paper's Table 3 is a good template: per
scene, path tracing & shading ms vs query ms vs training ms.

### Cited Findings
- NRC Table 3 breaks cost into "(i) path tracing & shading, (ii) querying the cache/DDGI, and (iii) training the
  cache/DDGI".
  - Averages: PT+ReSTIR 12.29 ms. PT+ReSTIR+DDGI 9.46 + 0.58 query + 1.37 update. PT+ReSTIR+NRC 9.16 + 1.59 + 1.11.
  - DDGI's 16×16×16 grid traces "16³ · 256 = 1048576 update rays per frame", vs NRC's 2¹⁶ = 65536 training records.
  - Design goal: "stable runtime overhead and memory footprint, both of which should be independent of scene
    complexity. The rendering cost must scale at worst linearly with the number of pixels."
  - [NRC PDF](https://d1qx31qr3h6wln.cloudfront.net/publications/mueller21realtime.pdf)
- GI-1.0 Table 1 splits time into "Raytracing / Caching & sampling / Interpolate & denoise" per GPU (RX 6900 XT, RTX
  3080), over four scenes from 99k to 9.0M triangles (Gas station, Flying world, Kitchen #1, Kitchen #2). Totals are
  1.9–3.4 ms at 1080p. That is a scene-size scaling table — [arXiv HTML](https://arxiv.org/html/2310.19855)
- ReSTIR GI timing is "the time spent for the ReSTIR GI algorithm; other passes occupy less than 30% of the total time
  and G-buffer generation uses less than 1 ms" (PDF §6). This is the "GI cost, not frame cost" convention.
- ReSTIR DI Fig. 4 shows cost growing with the candidate count M (1.0 → 168 ms) in a scene with 23,000 emissive
  triangles. That is a number-of-lights / settings scaling sweep — [paper](https://research.nvidia.com/publication/2020-07_spatiotemporal-reservoir-resampling-real-time-ray-tracing-dynamic-direct)
- GIBS (Frostbite) initially cost "a total average gpu time for GIBS of 6-7ms", which was too much for 60 fps titles —
  [Advances 2024 slides](https://advances.realtimerendering.com/s2024/content/EA-GIBS2/Apers_Advances-s2024_Shipping-Dynamic-GI.pdf),
  [Advances 2021](https://www.advances.realtimerendering.com/s2021/SIGGRAPH%20Advances%202021%20-%20Surfel%20GI.pdf)
- AMD Capsaicin `--benchmark-mode` "blocks user input and runs a set number of frames at fixed frame rate, then saves
  the final image and profiling data and exits". Related flags: `--benchmark-frames`, `--benchmark-first-frame`
  (saves a sequence), `--benchmark-suffix`, `--start-scene-index`, `--start-renderer-index`, `--start-camera-index`,
  `--render-options "k=v …"`. Default output is HDR images — [scene_viewer_usage.md](https://raw.githubusercontent.com/GPUOpen-LibrariesAndSDKs/Capsaicin/master/docs/usage/scene_viewer_usage.md)
- RTXPT (NVIDIA's real-time path tracer):
  - SHaRC is "part of the real-time path tracer and on by default", and ReSTIR DI/GI come via RTXDI.
  - `--scene`, `--nonInteractive`, `--captureSimple` (screenshot after warm-up), `--capturePath` —
    [RTXPT](https://github.com/NVIDIA-RTX/RTXPT)
- PresentMon per-frame CSV columns include `MsBetweenPresents`, `GPUTime`, `GPUBusy`, `GPUWait`, `MsAnimationError`,
  `GPUMemorySizeUsed`, `GPUFrequency`, `GPUTemperature` —
  [PresentMon capture app README](https://github.com/GameTechDev/PresentMon/blob/main/README-CaptureApplication.md)

### Inferences
- [I] **Per-pass statistics:** mean, median, p95, p99, max over N ≥ 300 frames after W ≥ 120 warm-up frames, with stable
  clocks (see `followup_performance_optimization.md` §2).
  - p99 matters for caches with periodic work: DDGI probe relocation, surfel spawning, NRC training bursts, BLAS
    rebuild every N frames.
  - Show a **frame-time trace plot** (ms vs frame index), not only the averages. Periodic spikes are obvious there and
    invisible in a mean.
- [I] **Memory:**
  - sum EV's own technique resources (hash table, probe atlases, surfel buffers, reservoirs, MLP weights and training
    buffers, denoiser history);
  - also log `IDXGIAdapter3::QueryVideoMemoryInfo` (CurrentUsage, Budget) [T] and the BLAS/TLAS sizes.
  - The 3070's 8 GB makes "fits / does not fit" a real result: report it.
- [I] **Fair budgets:**
  - "Same settings on both GPUs": report both GPUs' ms. This shows the vendor/architecture difference.
  - "Same budget per GPU": retune knobs per GPU to the same GI ms. This shows the quality each GPU can afford.
  - Budgets to use: 2, 4 and 8 ms of GI at 1080p (60 fps leaves ~16.6 ms total; GIBS shipped below 6–7 ms) [E].
- [I] **Scaling sweeps** (one variable at a time, all others at the default shot):
  - resolution 720p / 1080p / 1440p / 2160p. Screen-space reuse and denoisers scale with pixels; probe/surfel/hash
    caches partly with scene/probe count;
  - number of emissive lights: Intel Sponza candles 0 / 10 % / 100 %, Zero Day;
  - scene size: Cornell → Sponza → Bistro exterior → San Miguel / Emerald Square;
  - dynamic content: static / one moving object / 10 % of instances moving / animated lights.
- [I] Shared passes (G-buffer, direct lighting, tonemap) should be identical for all techniques: one ReSTIR DI or NEE
  path for direct light, as NRC did. Otherwise a GI comparison becomes a direct-lighting comparison.

## 5. Standard test scenes

### Takeaway
A small, well-known set covers every GI failure mode, and most of it can be loaded by EV's Assimp importer (glTF, OBJ,
FBX) after modest loader work:

| Tier | Scene | What it tests | Size | Format | License | EV can load? |
|---|---|---|---|---|---|---|
| 0 unit | Cornell box variants (McGuire OBJ; RTXGI `cornell`/`multi-cornell` glb; Bitterli CC0 in Mitsuba 3/pbrt-v4) | colour bleeding, sanity, convergence | tiny | OBJ / glb / Mitsuba / pbrt | CC0 (Bitterli); others per file [U] | OBJ yes; glb after unpacking textures; Mitsuba/pbrt no (rebuild by hand) |
| 0 unit | RTXGI `furnace`, `two-rooms`, `tunnel` | energy (furnace), light leaks (two-rooms "Closed Room"), long indirect paths (tunnel) [I] | small | glb | NVIDIA SDK license [U] | after unpacking embedded textures [I] |
| 1 | Crytek Sponza (McGuire version, **already in EV**: `assets/sponza/`) | sun through an open roof → indirect-dominant atrium; alpha-masked plants and chains; GI's "hello world" | ~262k tris [T] | OBJ+MTL | Crytek donation, CC BY 3.0 per third parties [U] | **yes, today** (alpha masks in the diffuse alpha need alpha testing) |
| 1 | Intel Sponza (2022, PBR, 4k textures) + add-ons Emissive Candles, Ivy, Curtains, Trees | PBR materials; **many emissive lights** (candles); foliage/alpha (ivy, trees); thin cloth (curtains) | base 3.71 GB, candles 2.97 GB, ivy 1.29 GB | glTF / FBX / USD | "Creative Commons Attribution" | glTF yes; **4k PNGs uncompressed will not fit 8 GB** → convert to BC7 DDS [E/I] |
| 2 | Amazon Lumberyard Bistro (interior, interior+wine, exterior) | interior: indirect from many lamps + glass; exterior: large city street, ~20k emissive tris + sky, fine shadows at distance | 1.05 M / 1.29 M / 2.83 M tris | FBX (+DDS) on ORCA; **glTF + DDS in RTXPT-Assets**; pbrt-v4 version | CC-BY 4.0 | yes (FBX or the glTF), lights must be recreated |
| 2 | Country Kitchen (Jay-Artist) | glossy interior lit through windows; used by ReSTIR PT and RTXPT (GI-1.0's "Kitchen #1/#2" may be other models [U]) | ~1–2 M tris [U] | glTF + DDS (RTXPT-Assets); Mitsuba/pbrt (Bitterli) | CC-BY / CC BY 3.0 | glTF yes |
| 2 | Zero Day (Beeple), Measure One / Seven | **thousands of emissive triangles** (10,103 / 10,989), glossy, high albedo; the ReSTIR DI/NRC scene | 1.37 M / 1.29 M tris | FBX + DDS (ORCA); pbrt-v4 frames | CC-BY 4.0 (ORCA) | FBX yes; emissive intensities must be checked |
| 2 | Bitterli Living Room / Breakfast Room / Veach Ajar / Bathroom / Classroom | small interiors with hard indirect light (Veach Ajar: light through a door gap) | small | Tungsten, Mitsuba 3, pbrt-v4 | CC0 / CC BY 3.0 per scene | not directly; meshes may be OBJ [U]; the Mitsuba version is a free independent spectral reference |
| 3 stress | San Miguel | huge courtyard, foliage, many materials (ReSTIR PT, A-SVGF) | large (~10 M tris [U]), 293 MB of textures | OBJ (McGuire), pbrt-v4 | not stated [U] | OBJ yes; VRAM risk on the 3070 [E] |
| 3 stress | NVIDIA Emerald Square | city with SpeedTree foliage, 10,046,405 tris | very large | FBX + Falcor file | CC BY-NC-SA 3.0 | FBX yes; likely over budget on 8 GB [E] |
| 3 | UE4 Sun Temple | PBR temple interior, 1,641,711 vertices | medium | FBX + Falcor file | CC BY-NC-SA 4.0 | FBX yes |
| — | Moana Island | — | 45 GB download / 93 GB unpacked, > 15 billion instanced primitives | pbrt / USD / Disney | research/benchmark use only | **no**: far beyond 8 GB and EV's importer |

### Cited Findings
- **Bistro (ORCA):**
  - "Interior: 1,046,609", "Interior with wine: 1,293,691", "Exterior: 2,832,120" triangles.
  - FBX and a Falcor scene file, "Creative Commons CC-BY 4.0".
  - [ORCA Bistro](https://developer.nvidia.com/orca/amazon-lumberyard-bistro)
- ReSTIR DI: "Bistro has 20,638 emissive triangles and an environment map". Other scenes: "Burger Restaurant has 7,517
  textured emissive triangles", "Subway has 23,452", "Zero Day animation has 10,973 dynamic emissive triangles",
  Amusement Park "3.4 million dynamic emissive triangles" (paper PDF).
- **RTXPT-Assets** has `Models/Bistro/bistro.gltf` (+ `bistro.bin`, DDS textures such as `lumberyard_splash_2560x2560.dds`),
  `Models/Kitchen/kitchen.gltf` (DDS textures), `living_room`, `transparent-machines`, `ConvergenceTest` and scene
  files such as `bistro_with_mirrors-rtxdi.scene.json` and `convergence-test.scene.json`. The files are Git LFS —
  [RTXPT-Assets](https://github.com/NVIDIA-RTX/RTXPT-Assets)
- RTXPT-Assets `ArtLicenses.txt`: Kitchen is by Jay-Artist (blendswap 5156), modified by Kate Anderson, "License:
  CC-BY". `living_room` is "adapted from casual-effects.com/data/", "CC BY 3.0" —
  [ArtLicenses.txt](https://raw.githubusercontent.com/NVIDIA-RTX/RTXPT-Assets/main/ArtLicenses.txt)
- **Zero Day (ORCA):**
  - Measure One 1,372,670 tris / 10,103 emissive; Measure Seven 1,294,866 / 10,989.
  - "FBX and DDS", "lit by thousands of animated emissive triangles", CC-BY 4.0.
  - [ORCA Zero Day](https://developer.nvidia.com/orca/beeple-zero-day)
- **Emerald Square:** 10,046,405 triangles, CC BY-NC-SA 3.0, trees and plants are SpeedTree ORCA assets —
  [ORCA Emerald Square](https://developer.nvidia.com/orca/nvidia-emerald-square)
- **Sun Temple:** "1,641,711 vertices", FBX + Falcor scene file, CC BY-NC-SA 4.0 —
  [ORCA Sun Temple](https://developer.nvidia.com/ue4-sun-temple)
- The ORCA index lists Bistro, Emerald Square, SpeedTree Trees and Plants, UE4 Sun Temple and Zero-Day. Each comes as
  FBX with a Falcor scene file — [ORCA](https://developer.nvidia.com/orca)
- **Intel samples:**
  - "Sponza Base Scene": 3ds Max 2022 source, USD, glTF, FBX, 3.71 GB, "Remade for a modern-day, physically-based
    rendering (PBR) pipeline … photogrammetry-derived, high-resolution geometry with 4k textures".
  - "Emissive Candles": "Thousands of floating candles … examples of an emissive material for the flames and point
    lights", 2.97 GB.
  - Ivy 1.29 GB, Colorful Curtains 750 MB, Trees 907 MB.
  - All "under the Creative Commons Attribution license" (version not stated).
  - [Intel graphics research samples](https://www.intel.com/content/www/us/en/developer/topic-technology/graphics-research/samples.html)
- **Bitterli Rendering Resources:**
  - 32 scenes in the intro (35 entries listed), in Tungsten, Mitsuba 3 and pbrt-v4.
  - "conversions to Mitsuba and pbrt-v4 are approximate".
  - Licenses: CC0 for Veach Ajar, Bathroom, Bedroom, Cornell Box, Veach MIS, Glass of Water, Spaceship; CC BY 3.0 for
    Living Room, Kitchen, Staircase, Breakfast Room, Classroom, Coffee Maker.
  - [resources](https://benedikt-bitterli.me/resources/)
- **pbrt-v4-scenes:**
  - Bistro (CC-BY 4.0), San Miguel ("Complex courtyard", license not stated), Zero Day ("Converted from Cinema4D files
    … original materials and lighting are better"), Barcelona Pavilion (day and night, CC-BY 2.0), Contemporary
    Bathroom (CC0), Kroken, Watercolor, Villa and others —
    [pbrt-v4-scenes](https://github.com/mmp/pbrt-v4-scenes)
  - San Miguel was modelled by Guillermo M. Leal Llaguno and has "354 texture maps, representing 293 MB" —
    [pbrt v2 scenes](https://pbrt.org/scenes-v2)
- **McGuire Computer Graphics Archive:** meshes are zips of "OBJ and PNG". Cite as "Morgan McGuire, Computer Graphics
  Archive, July 2017 (https://casual-effects.com/data)". Licences vary per model —
  [archive](https://casual-effects.com/data), [citation page](https://casual-effects.com/g3d/data10/index.html)
- EV's `assets/sponza/copyright.txt`: McGuire's 2011 fix of Crytek's OBJ (Frank Meinl's 2010 model, originally Marko
  Dabrovic 2002). The "mask" textures were moved "into the alpha channel of the associated diffuse texture", and the
  banner was split into `banner.obj`. EV's demo loads `assets/sponza/sponza_nobanner.obj` (`demo.cpp:754`).
- **Moana Island:** Base 45 GB download / 93 GB unpacked, PBRT 6 GB / 41 GB, "over 15 billion primitives when fully
  instantiated". The License.txt allows research or software development (including benchmarking) use —
  [Disney](https://disneyanimation.com/resources/moana-island-scene/),
  [License.txt](https://media.disneyanimation.com/uploads/production/data_set_asset/4/asset/License.txt)

### Inferences
- [I] **Recommended EV set (in this order):**
  1. **Cornell + furnace + two-rooms**: unit tests for energy, bleeding and leaks.
  2. **Crytek Sponza**: already loads; atrium indirect light.
  3. **Bistro interior + exterior** from the RTXPT-Assets glTF. It is the most-used GI scene: NRC, ReSTIR DI, ReSTIR
     PT, Bikker's "BVH Quality" post. Many lights.
  4. **Kitchen** (RTXPT glTF): glossy interior, used by ReSTIR PT (and a "Kitchen" in GI-1.0, model unverified).
  5. **Intel Sponza + Emissive Candles**: many lights, modern PBR.
  6. **Zero Day Measure One**: emissive-heavy, used by ReSTIR DI and NRC.
  7. **San Miguel** as the scale stress test on the 9070 XT (and a "does it fit" test on the 3070).

  This set lets EV results sit next to published ones (§6) while covering indirect-dominant interiors, many emissive
  lights, foliage/alpha, glossy surfaces and leaks.
- [E] VRAM on the 3070 for Bistro exterior:
  - EV's vertex is 60 B (five `float3`: position, normal, tangent, bitangent, texcoord; `vertex_types.h`), with 32-bit
    indices;
  - geometry ≈ 2.8 M tris → roughly 1.5–3 M vertices ≈ 100–200 MB + 34 MB indices, BLAS ≈ 0.2–0.5 GB;
  - the DDS textures are the unknown (BC-compressed, probably 1–2 GB) [U];
  - should fit in 8 GB with room for GI buffers. Emerald Square (10 M tris) and San Miguel are the ones to watch.
- [E/I] Intel Sponza's 4k textures loaded through WIC become uncompressed RGBA8: 64 MB per 4k texture without mips.
  Dozens of them will exceed 8 GB. Pre-convert with DirectXTex `texconv` to BC7 (colour) / BC5 (normals) DDS, which EV
  already loads via `LoadFromDDSFile`.

### Gaps
- No primary license statement was found for McGuire's Crytek Sponza or San Miguel, nor exact triangle counts for
  San Miguel, Kitchen or Intel Sponza [U].
- The RTXPT-Assets glTF files are LFS pointers, so their `extensionsUsed` (e.g. `KHR_materials_emissive_strength`,
  `KHR_texture_transform`, `MSFT_texture_dds`) were not inspected [U].
- The RTXGI-DDGI scene license and whether its `.glb` files embed their textures were not checked [U].
- Whether Bitterli's Mitsuba 3 downloads contain OBJ meshes (loadable by Assimp) was not checked [U].

## 6. Which scenes the GI papers used (for rough comparison with published numbers)

### Takeaway
Bistro and Zero Day are the shared ground between NVIDIA's NRC, ReSTIR DI and ReSTIR PT. Kitchen, Veach Ajar, San
Miguel, Living Room and Classroom recur too. DDGI, GIBS, ReSTIR GI and GI-1.0 mostly used non-public or commercial
content. Their numbers can only be compared in *shape* (ms per pass, error ratios), not shot for shot. All published
timings are on RTX 2080 Ti / 3080 / 3090 or RX 6900 XT at 1080p. The RTX 3070 is a smaller Ampere sibling of the
3080/3090, so expect its ms to be clearly higher [E].

| Paper | Scenes | GPU / res | Metrics |
|---|---|---|---|
| DDGI 2019 (Majercik et al.) | Greek Temple (876,127 prims) and others [U]; vs path-traced ground truth (6 ms vs 1 min/frame) | RTX 2080 Ti, 1920×1080 | visual + timings, Gigarays/s |
| DDGI 2021 (scaling for production) | Greek Villa | — | visual |
| RTXGI-DDGI SDK harness | cornell, furnace, multi-cornell, sponza, tunnel, two-rooms | — | path tracer side by side |
| GIBS 2021 / 2024 (EA SEED, Frostbite) | PICA PICA; shipped game content | consoles; 6–7 ms (2021) | visual + ms |
| ReSTIR DI 2020 | Bistro, Burger Restaurant, Subway, Zero Day, Amusement Park | RTX 2080 Ti / Titan RTX, 1920×1080 | RMAE, error vs time |
| ReSTIR GI 2021 | UE4 Marketplace scenes (Evermotion, Quixel Megascans, Matima); one Falcor scene | RTX 3090 (and 3080), 1080p | MSE vs 8,192 spp PT |
| NRC 2021 | Zero Day, Attic, Bistro, Classroom, Living Room, Pink Room | RTX 3090, 1920×1080 | MRSE, rBias²/rVar, FLIP, SMAPE (temporal), time-to-equal-MRSE |
| ReSTIR PT / GRIS 2022 | BistroInterior, Kitchen, VeachAjar, ZeroDay, SanMiguel, Carousel, OperaHouse, Cornell variants | RTX 3090, 1920×1080 | MAPE, equal time |
| A-SVGF 2018 | Pillars, Sponza, GlossySponza, Dungeon (+ San Miguel, Classroom credited) | Titan Xp; 1280×720 and 1080p | RMSE, SSIM, per-frame SSIM, consecutive-frame SSIM |
| GI-1.0 2023 (AMD) | Gas station, Flying world, Kitchen #1/#2, Sponza, UE5 Archviz | RX 6900 XT, RTX 3080, 1080p | per-pass ms only |
| SHaRC (NVIDIA) | no paper; ships on in RTXPT (Bistro, Kitchen assets) | — | — |

### Cited Findings
- NRC Fig. 10 numbers at 1 spp, 1920 × 1080, RTX 3090:
  - Bistro: PT 11.71 MRSE / 105 fps; PT+NRC 3.43 / 98.8 fps; PT+ReSTIR 10.99 / 73.3 fps; PT+ReSTIR+NRC 1.47 / 69.6 fps;
    FLIP 0.63 / 0.52 / 0.39 / 0.28.
  - Zero Day: 20.52 / 96.6, 5.07 / 126, 17.79 / 70.8, 3.55 / 87.7.
  - NRC per-frame query 1.38 ms + training 1.11 ms in Bistro; DDGI 0.65 + 1.68 ms (Table 3).
  - [NRC PDF](https://d1qx31qr3h6wln.cloudfront.net/publications/mueller21realtime.pdf)
- ReSTIR PT scenes: "BistroInterior, Kitchen, VeachAjar, Zeroday, SanMiguel, Carousel, and OperaHouse" —
  [project](https://graphics.cs.utah.edu/research/projects/gris/). Cornell-box variants are in Fig. 6 of the
  [PDF](https://graphics.cs.utah.edu/research/projects/gris/sig22_GRIS.pdf). Carousel MAPE: path tracing 1.63,
  ReSTIR GI 0.45, ReSTIR PT 0.39.
- ReSTIR GI acknowledgements: UE4 scenes "from Quixel Megascans … Evermotion … Matima Studio. All of them can be found
  from Unreal Marketplace. The scene in Figure 9 is from [ASTUFF Burger restaurant], rendered by NVIDIA Falcor". The
  Figure 13 scenes range "from 1.8 million to 8.3 million triangles" (PDF).
- GI-1.0: Table 1 scenes and GPUs as above, "Results will vary" — [arXiv HTML](https://arxiv.org/html/2310.19855).
  Capsaicin (MIT) ships GI-1.x and a reference path tracer — [Capsaicin](https://github.com/GPUOpen-LibrariesAndSDKs/Capsaicin)
- DDGI 2019: "6 ms/frame, versus 1 min/frame in this scene (on GeForce RTX 2080 Ti at 1920×1080)". Table 1 is on the
  "Greek Temple Scene (876127 primitives)"; DDGI 2021 figures show the "Greek Villa scene" (PDFs read; JCGT).
- GIBS 2024: "we used content from a game that had already been released"; one title "fully relies on GIBS for all
  indirect diffuse lighting" — [slides](https://advances.realtimerendering.com/s2024/content/EA-GIBS2/Apers_Advances-s2024_Shipping-Dynamic-GI.pdf)

### Inferences
- [I] Bistro interior/exterior, Kitchen, Zero Day and a Cornell box give EV direct counterparts for NRC, ReSTIR DI and
  ReSTIR PT figures. The materials will differ (Falcor's Frostbite-style BSDF vs EV's GGX+Lambert; spectral vs RGB), so
  compare **ratios** (e.g. "NRC cut relMSE by 8× vs PT at similar fps; NRC paper: 8× in Bistro"), not absolute errors.
- [I] For DDGI, surfel GI and GI-1.0 there is nothing to compare shot for shot. Compare the per-pass ms breakdown shape
  (update vs query vs denoise) and the failure cases the papers name (leaks, missing AO, lag).
- [I] Running RTXPT (SHaRC + ReSTIR) on the RTX 3070 with the same Bistro/Kitchen glTF is a useful sanity check for
  EV's SHaRC timings and look. Whether RTXPT runs on the RX 9070 XT is [U] (it includes NVAPI).

## 7. What EV can load today (checked in the source)

### Takeaway
EV imports models through **Assimp**:
- `Scene::LoadSceneFromFile` caches a `.assbin` next to the model;
- the editor treats `.gltf .glb .obj .fbx .dae .3ds .ply .stl .blend` as models;
- textures load through DirectXTex (DDS, HDR, TGA, else WIC).

So glTF, OBJ and FBX scenes load. Five gaps matter for GI test scenes:
1. no lights from files;
2. no alpha mode/cutoff;
3. no emissive strength;
4. `.glb`-embedded textures fail;
5. no EXR loading.

### Cited Findings (EV source)
- `source/DX12/scene.cpp:62–117`: `Assimp::Importer::ReadFile` with
  `aiProcessPreset_TargetRealtime_MaxQuality | aiProcess_OptimizeGraph | aiProcess_ConvertToLeftHanded | aiProcess_GenBoundingBoxes`,
  then an `assbin` export as a cache.
- `ImportScene` (`scene.cpp:142–167`) imports materials, meshes and nodes only. **`aiScene::mLights` and cameras are
  not read.**
- `ImportMaterial` (`scene.cpp:169–332`):
  - reads colours, opacity, IOR, metallic and roughness factors (default roughness 1.0 for OBJ);
  - reads textures: emissive, diffuse, specular, shininess, `aiTextureType_GLTF_METALLIC_ROUGHNESS`, lightmap (AO),
    opacity, normal / bump (bump is treated as a normal map when ≥ 24 bpp);
  - does **not** read glTF alpha mode / alpha cutoff or an emissive intensity key.
- Textures are looked up as `parentPath / aiTexturePath`, and `LoadTextureFromFile` throws "File not found." when the
  path does not exist (`command_list.cpp:303–312`). Embedded glb textures (Assimp path `*0`) therefore fail [I].
- `LoadTextureFromFile` handles `.dds` (`LoadFromDDSFile`), `.hdr`, `.tga` and everything else through WIC
  (`command_list.cpp:329–344`). WIC has no EXR codec [T], so an `.exr` environment map cannot be loaded, although
  the asset browser lists `.exr` (`editor_assets.cpp:89`).
- Meshes: 60-byte vertices, 32-bit indices, only triangles kept (`scene.cpp:338–403`).
- `.evscene` (JSON) stores camera position/rotation/FOV, environment, models, materials and object nodes
  (`editor_scene_file.cpp:31–49`). It stores **no lights, no camera path, no events**.

### Inferences
- [I] Loader work needed before the GI comparison (each is small):
  1. **lights in `.evscene`**: point, spot, sun and an emissive scale per material. Scene lights from Falcor/glTF files
     are recreated by hand once and saved.
  2. **alpha test**: read `AI_MATKEY_GLTF_ALPHAMODE` / `AI_MATKEY_GLTF_ALPHACUTOFF` [T] and use the diffuse alpha for
     OBJ Sponza; non-opaque geometry in the RayQuery candidate loop.
  3. **emissive strength**: Assimp's emissive intensity key for `KHR_materials_emissive_strength` [T/U]. Without it,
     Bistro/Intel candle lamps may come out far too dim [I].
  4. **unpack `.glb` → `.gltf` + image files** offline (Blender, or a Python glTF library; no Node on this machine).
     Alternatively, support Assimp's embedded `aiTexture`.
  5. **EXR read/write**: needed anyway for frame dumps. tinyexr (single header, BSD-3 [T]) or DirectXTex's EXR
     helper (needs OpenEXR [T]).

## 8. A concrete comparison protocol for EV and a "GI comparison harness" milestone

### Takeaway
Fix the **shots, events, metrics and recording format before building SHaRC**. Then every technique (M7 denoisers,
M8b real-time, M9 SHaRC, M10 NRC, later DDGI/surfels/ReSTIR) is measured the same way with one command, and the final
write-up is tables + log–log convergence plots + FLIP-mapped crops.

The harness has three parts, modelled on Capsaicin's benchmark mode and NRC's evaluation:
1. **EV `--benchmark` mode**: fixed dt, fixed seed, camera paths, scripted events, per-pass CSV, EXR dumps, run
   manifest.
2. **reference mode**: high spp per keyframe, two seeds.
3. **Python scripts**: relMSE/FLIP/SMAPE/bias/lag, plots, Markdown tables.

### The test matrix
| Axis | Values |
|---|---|
| Scenes (shots) | Cornell, furnace room, two-rooms (leak), Crytek Sponza (sun), Bistro interior (lamps), Bistro exterior (sun + emissive), Kitchen, Intel Sponza + candles, Zero Day M1; stress: San Miguel |
| Per shot | 1 static camera, 1 camera path (300 frames, slow pan + one fast turn), 1–2 events (light switch, sun jump, object move, cut) |
| Techniques | Reference PT (offline); PT 1 spp + OIDN (M7, offline-ish); PT 1–2 spp + real-time denoiser (M8b); SHaRC (M9); own NRC (M10); later DDGI, surfel GI, ReSTIR DI (+GI/PT); combinations as in NRC (ReSTIR DI + cache) |
| Budgets | equal-sample (1 spp, same bounce count); equal-time GI budgets 2 / 4 / 8 ms at 1080p; "time to equal relMSE" |
| Resolutions | 1080p default; sweep 720p–2160p on 2 shots |
| GPUs | RX 9070 XT (all cells), RTX 3070 (all cells that fit 8 GB; "does not fit" is a result) |

### Numbers to record per run (one row per run + one CSV per frame)
- **Manifest (JSON):**
  - date, git commit, Debug/Release, driver version, GPU + LUID, stable-clock mode;
  - shot file + its hash, technique and all its knobs, resolution, spp, bounces, seed;
  - warm-up W, frames N.
- **Per frame (CSV):** frame index, CPU frame ms, GPU ms per render-graph pass (EV `GpuTimer`), GI-pass sum, rays
  traced, VRAM CurrentUsage, technique counters (cache entries used, probes updated, training records, reservoir
  M-cap hits).
- **Per run (summary):**
  - GI ms mean / median / p95 / p99 / max; frame ms mean / p95 / p99;
  - technique-owned memory (MB); BLAS/TLAS MB;
  - relMSE, MRSE (pbrt form), HDR-FLIP mean + weighted median, MAPE, ΔL (energy);
  - static shots: tSMAPE (flicker), rBias², rVar (K = 32);
  - event shots: lag90 / lag99 in frames and ms, ghosting error;
  - paths: relMSE/FLIP per keyframe and cvvdp JOD.

### How to automate it
- **Scene files v2 (`.evscene`)**: add `lights`, `cameraPath` (keyframes with time, position, rotation and FOV; a fixed
  frame count; a fixed dt), `events` (`{ "frame": 150, "type": "lightEnabled", "light": 3, "value": false }`,
  `"nodeTransform"`, `"sunDirection"`, `"cameraCut"`), `seed` and `frozenTime`.
- **CLI**:
  `EV.exe --benchmark scenes/bench/bistro_int.evscene --technique sharc --set sharc.updateFraction=0.04 --frames 600 --warmup 120 --dump 120:600:10 --out runs/2027-xx-xx/…`
  - Time advances by the fixed dt, not the wall clock, so frame k shows the same scene state on both GPUs and for all
    techniques [I].
  - VSync off, fixed window size, no UI, exit when done (Capsaicin's `--benchmark-mode` pattern).
- **Dumps**: linear HDR EXR of the final radiance before tone mapping, plus optional AOVs: albedo, normal,
  indirect-only, and the technique's debug view (cache heat map).
- **Reference mode**: `--reference --spp 16384 --keyframes 0:600:20 --seed 1|2` renders each keyframe of the path with
  frozen scene state, accumulation only and clamps off. The second seed gives the noise-floor check (§1).
- **Python** (fine on both PCs; no Node), in a `tools/gi_compare/` folder:
  - `run_matrix.py` launches EV via `subprocess` for every matrix cell and writes the run folders;
  - `metrics.py` uses NumPy for relMSE/MAPE/SMAPE/ΔL and `flip_evaluator` (`flip.load` reads EXR into NumPy, so no
    extra EXR library is needed) for HDR-FLIP with fixed exposures;
  - `temporal.py` computes tSMAPE maps, lag90/99 from ROI curves and the ghosting error;
  - `bias.py` computes the K-run mean/variance → rBias², rVar plus mean-image FLIP maps;
  - `report.py` uses matplotlib for the log–log error-vs-time plots, frame-time traces, scaling curves and a Pareto
    plot (GI ms vs relMSE). It also writes a crop grid (same crops for every technique, with FLIP map underneath) and
    Markdown table rows to paste into `performance.md`.
  - Optional: `cvvdp` on the 3070 PC (CUDA).

### How to present it
- **Per scene, one table**: technique rows × (GI ms p50/p99, MB, relMSE, FLIP, tSMAPE, lag90, rBias²/rVar), at equal
  time (4 ms), with the equal-sample row underneath.
- **Convergence plot** per static shot: relMSE vs time (log–log), one line per technique. Unbiased lines keep falling;
  caches flatten at their bias floor. The crossing points answer "which technique at which budget".
- **Pareto plot**: GI ms (x) vs relMSE or FLIP (y), each technique a curve over its knob settings. Methods on the lower
  left frontier are the ones worth keeping.
- **Crops**: 3–4 fixed crops per scene (contact shadow, glossy, thin wall/leak, emissive cluster) × techniques +
  reference, with FLIP maps.
- **Temporal**: tSMAPE false-colour maps (NRC Fig. 3 style), lag curves L(t) after each event, and a short side-by-side
  video.
- Always state GPU, resolution, driver and the fact that the reference is EV's own spectral path tracer.

### Proposed milestone: **M8c GI comparison harness** (after M8b, before M9)
Some of it is useful earlier: M7 already needs "FLIP/MSE vs ~16k-spp ground truth". So steps 1–3 could be done during
M7 and the rest right after M8b.
1. EXR writer (tinyexr) + `--dump` of linear radiance and AOVs.
2. Reference mode with the two-seed noise-floor check.
3. `metrics.py` (relMSE, MRSE, MAPE, HDR-FLIP) + `report.py` table output.
4. `.evscene` v2: lights, camera path with fixed dt, events, seed.
5. `--benchmark` CLI with warm-up, per-pass CSV (p50/p95/p99), VRAM, run manifest.
6. Temporal + bias scripts (tSMAPE, lag90/99, ghosting, K-run rBias²/rVar).
7. Test scenes:
   - Cornell + furnace + two-rooms;
   - Crytek Sponza (existing);
   - Bistro interior/exterior and Kitchen (RTXPT glTF, BC textures);
   - Intel Sponza + candles (textures converted to BC7);
   - Zero Day Measure One.

   Loader work: lights in scene files, alpha test, emissive strength, glb unpacking (§7).
8. Run the matrix once with the techniques that exist at that point: PT + OIDN, PT + real-time denoiser. That is the
   baseline every later technique is compared against.

**Verify:**
- two runs of the same cell give the same metrics within noise (and identical images with the same seed on the same
  GPU [I]);
- the reference noise floor is ≥ 10× below the smallest relMSE in the table;
- relMSE of PT at 1, 4, 16, 64 spp falls ∝ 1/spp (slope −1 on log–log), which tests the whole chain;
- the leak scene shows ~0 luminance in the dark room for the reference;
- one complete report (tables + plots + crops) generated by a single `run_matrix.py` call on each PC.

### Inferences
- [I] Doing this before M9 means SHaRC (M9), NRC (M10) and later DDGI/surfels/ReSTIR each get a "results" section
  that is directly comparable, instead of ad hoc screenshots.
- [I] The scripted-event and fixed-dt work also fixes reproducibility problems already listed in the plan (ocean time
  static, `std::random_device` seed, FOV reset) for any shot.
- [E] Cost of a full matrix run is dominated by references:
  - ~10 shots × (1 static + 30 keyframes) × 16k spp at 1080p;
  - at an assumed 10–30 ms per spp in Bistro on the 9070 XT that is ~3–8 min per reference frame, so ~15–40 GPU-hours
    once;
  - references are reused for every technique, and only change when the shared renderer changes.

## Integration notes for knowledge.md, plan.md and performance.md

Proposals only, for the student to choose. Nothing has been edited.

### plan.md
- Add **M8c GI comparison harness** (§8) to the roadmap. Optionally move its steps 1–3 into M7 ("FLIP/MSE vs ~16k-spp
  ground truth" needs them anyway).
- "Profiling and performance": add "GI cost = sum of GI passes; report p50/p95/p99; equal-time GI budgets 2/4/8 ms at
  1080p; time-to-equal-relMSE; bias split rBias²/rVar; temporal tSMAPE + lag90".
- M9 / M10 "Verify": replace "bias vs reference" / "equal-time error vs SHaRC and plain path tracing" with "M8c matrix
  row set (static, path, event shots) on both GPUs".

### performance.md
- Add a "Metric definitions" header: relMSE ε = 0.01 (linear HDR, per channel), MRSE (pbrt form), HDR-FLIP with fixed
  exposures per shot, tSMAPE, lag90, rBias²/rVar with K = 32.
- Add a "GI comparison" table per shot: `Technique | Budget | GI ms p50/p99 | MB | relMSE | FLIP | tSMAPE | lag90 | rBias² | rVar | Notes`.
- Fill the "Benchmark shots" table with the §8 shot list.

### knowledge.md
- A short "How to compare GI techniques" section pointing to this note, with the five metric families (relMSE/FLIP,
  error-vs-time, bias split, temporal, per-pass cost) and the NRC paper as the worked example.
- The EV loader gaps of §7 as pitfalls for test-scene work.
