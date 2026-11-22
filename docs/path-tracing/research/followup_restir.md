# ReSTIR family (RIS → ReSTIR DI → ReSTIR GI → ReSTIR PT/GRIS → 2023–2026 follow-ups) and what EV can take from it (follow-up research, 2026-11-22)

Scope: the ReSTIR family of sampling algorithms (resampled importance sampling with spatio-temporal reuse), deep enough
to implement each rung and to compare them on cost and image quality. It covers the theory as taught in the SIGGRAPH 2023
course "A Gentle Introduction to ReSTIR", the three main papers (ReSTIR DI 2020, ReSTIR GI 2021, GRIS/ReSTIR PT 2022), the
2023–2026 follow-ups, the open-source implementations, the (thin) published work on spectral ReSTIR, and how all of it
maps onto the EV plan (`plan.md` D1–D18, M0–M11; ReSTIR DI is today only an optional item in M11, real-time mode is M8b,
SHaRC M9, own NRC M10). It extends `industry_realtime_path_tracing.md` §2 (which ReSTIR variant ships where) and does not
repeat it. Labels: [T] training knowledge, [I] inference, [E] estimate, [U] unverified. Facts without a label come from the
linked source, fetched 2026-11-22. The course notes, the ReSTIR DI, ReSTIR GI, GRIS and ReSTIR PT Enhanced papers and the
Wētā 2022 spectral course notes were downloaded and their text extracted and read (PDF text extraction loses some maths
symbols; equations below were re-typed from the extracted text and the paper structure).

## 0. Jargon in plain words (read this first)

### Takeaway
ReSTIR is not a renderer and not a denoiser. It is a way to **choose better random samples** (light points, bounce
directions, whole paths) by letting each pixel "borrow" good samples that its neighbours and its own previous frame
already found, while keeping the maths that makes the average come out right. Everything below uses these words:

| Word | Plain meaning |
|---|---|
| pdf (probability density function) | "How likely was it that my random generator produced this sample?" A density, not a probability: it can be > 1. Monte Carlo divides each sample's value by its pdf so rare samples count more. |
| Importance sampling | Generating samples more often where the integrand (the light you are adding up) is large. If the pdf has exactly the integrand's shape, one sample gives the exact answer. |
| Target function p̂ ("p-hat") | The shape you *wish* your samples followed, e.g. "BSDF × light × geometry". It does not have to integrate to 1 (unnormalised). |
| RIS (resampled importance sampling) | Generate M cheap candidates, give each a weight p̂/pdf, pick one at random in proportion to the weights. The picked one is distributed roughly like p̂. |
| Unbiased contribution weight (UCW) W | The number that replaces 1/pdf for a resampled sample, whose true pdf is unknown. On average it equals 1/pdf. Shading is `f(Y) · W`. |
| Reservoir | A tiny fixed-size struct per pixel (selected sample, W, a running weight sum, a "confidence" count). Lets you do RIS over a stream of candidates without storing them. |
| Weighted reservoir sampling (WRS) | The streaming pick: for each new candidate, keep it with probability (its weight / running sum). |
| Confidence weight c (historically "M") | Roughly "how many samples this reservoir stands for". Used to weight reservoirs when merging. Capped ("M-capping") to stop old samples dominating. |
| MIS (multiple importance sampling) | Weights that decide how much each of several sampling strategies counts for a given sample, summing to 1. Needed whenever samples come from different distributions (different pixels!). |
| Canonical sample | The current pixel's own fresh sample, which is guaranteed to be able to produce every useful sample for this pixel. It protects unbiasedness. |
| Bias | The average of many renders differs from the true image. ReSTIR's typical bias is *darkening* (lost energy at shadow and normal discontinuities) or *brightening* (over-counting). |
| Correlation | Neighbouring pixels (and successive frames) end up using the *same* sample, so noise turns from fine grain into blotches that "boil" over time. |
| Domain | The set of things a pixel integrates over: for direct light, "points on lights seen from *this* pixel's surface point". Neighbours have different domains. |
| Shift mapping T | A rule that turns a path that belongs to pixel A into a similar path for pixel B (e.g. "keep the same light point", or "keep the second bounce point"). |
| Jacobian (determinant) | When a shift squeezes or stretches space, densities change. The Jacobian is the stretch factor that must multiply the weight. For "reconnect to the same point" it is a ratio of cosines and squared distances. |
| Reconnection / random replay / hybrid shift | Reconnection: connect the new pixel's surface straight to the old path's vertex. Random replay: re-trace the new pixel's path using the *same random numbers* as the old one. Hybrid: replay through shiny vertices, reconnect at the first rough pair. |
| PSS (primary sample space) | Describing a path by the list of random numbers that generated it, instead of by its 3D vertices. Random replay is trivial there (Jacobian 1). |
| G-buffer | Per-pixel buffer of the first visible surface (depth, normal, material). |
| Motion vectors | Per-pixel "where was this point on screen last frame", needed to find the temporal neighbour. |
| Disocclusion | A surface that was hidden last frame and is visible now: it has no history, so it is noisy. |

### Cited Findings
- The course notes define these terms in Ch. 2–5 and keep a symbol table (target function p̂, target PDF p̄ = p̂/∫p̂, UCW W_X, resampling weight w_i, MIS weight m_i, shift T, support). Tip 3.1 says W is written W_X, not W(X), because "this is not a function since it cannot be evaluated at arbitrary X. Do not use in MIS weights!" — [course notes PDF](https://intro-to-restir.cwyman.org/presentations/2023ReSTIR_Course_Notes.pdf)
- "The target function p̂ is also sometimes (inaccurately) called the target PDF, but this misnomer should be avoided; p̂ is an unnormalized function, often just the integrand" — same, §3
- Confidence: "For historical reasons related to WRS, the confidence weight in the reservoirs is often stored with variable name M, and confidence capping is called M-capping. We adopt the convention of denoting confidence by c and its cap by c_cap." — same, §4.4 margin note

---

## 1. Foundations: importance sampling → RIS (Talbot 2005) → reservoirs → unbiased contribution weights; the 2023 course

### Takeaway
The 2023 SIGGRAPH course is the right textbook. It builds everything from five ideas:
1. **Monte Carlo with a pdf** is unbiased if the pdf is non-zero wherever the integrand is ("the samples *cover* the integrand").
2. **RIS** turns M candidates into one better-distributed sample, with an **unbiased contribution weight**
   W_Y = (1/p̂(Y)) · Σ w_j, where each candidate's resampling weight is w_i = m_i(X_i) · p̂(X_i) · W_{X_i}. For plain i.i.d.
   candidates with known pdf p this is w_i = p̂(X_i) / (M · p(X_i)) — Talbot's original formula.
3. **Weighted reservoir sampling** does RIS in one streaming pass with O(1) memory, and lets you **merge reservoirs**
   (a neighbour's reservoir is just one more candidate whose "W" is its own UCW).
4. **MIS weights decide correctness.** 1/M weights are only unbiased if all inputs are identically distributed; reuse
   between pixels needs the **generalized balance heuristic** (use the inputs' target functions as stand-ins for their
   unknown pdfs) or its cheaper O(M) relative **pairwise MIS**, plus at least one **canonical** sample.
5. **Confidence weights with a cap** (5–30, start at 20) stop the history from swamping new samples.

Then come reuse between domains (shift mappings + Jacobians, Ch. 5), path reuse (ReSTIR PT, Ch. 6), speed-ups (Ch. 7) and
very concrete advice (Ch. 9). The single most repeated message: **build a slow, unbiased, ground-truth-checked version
first, then optimise one step at a time.**

### Cited Findings
**The course itself**
- "A Gentle Introduction to ReSTIR: Path Reuse in Real-time", ACM SIGGRAPH 2023 Courses, by Chris Wyman, Markus Kettunen, Daqi Lin, Benedikt Bitterli, Cem Yuksel, Wojciech Jarosz, Pawel Kozlowski and Giovanni De Francesco (the last two from the Cyberpunk 2077 integration). DOI 10.1145/3587423.3595511. Notes PDF updated **March 4, 2024**; the page recommends the notes as the starting point. Sections and slides: Preliminaries (Yuksel), RIS (Kettunen), Direct light + spatiotemporal (Bitterli), Reuse between domains (Kettunen), Extending reuse to paths (Lin), two "Making ReSTIR fast" talks (Lin, Wyman), Cyberpunk 2077 integration (Kozlowski, De Francesco). No code repository is linked — [course page](https://intro-to-restir.cwyman.org/)
- Course intent: "seemingly obvious 'optimizations' arising during ReSTIR engine integration can silently introduce conditional probabilities and dependencies that, left ignored, add uncontrollable bias" — [notes, abstract](https://intro-to-restir.cwyman.org/presentations/2023ReSTIR_Course_Notes.pdf)
- One way to see ReSTIR: "a filtering technique for sampling distributions — aggregating multiple samples into one with a better PDF" (denoisers filter colours, ReSTIR filters pdfs). Post-process denoisers "entirely ignore the support issue, which is why they often reduce energy" — notes §1.1

**Monte Carlo, supports, MIS (Ch. 2)**
- MC needs only one condition for unbiasedness: supp(f) ⊆ supp(X) ("X covers f"). "ReSTIR-derived techniques differ from the norm and easily run afoul of this requirement" — notes §2.2
- MIS weights must sum to 1 wherever f ≠ 0 and be 0 where a technique cannot produce the sample; the balance heuristic m_i(x) = p_i(x)/Σ_j p_j(x) is the usual choice (Eq. 2.6–2.7) — notes §2.3
- Unbiased contribution weight (Def. 3.0.1): a random variable W_X with E[f(X)·W_X] = ∫ f. It exists even when the pdf of X is intractable, as for RIS outputs (Eq. 2.8) — notes §2.4

**RIS (Ch. 3)**
- Algorithm (§3.1): (1) take candidates X_1..X_M in a common domain; (2) evaluate resampling MIS weights m_i(X_i); (3) w_i = m_i(X_i) · p̂(X_i) · W_{X_i}; (4) choose Y ∝ w_i; (5) W_Y = (1/p̂(Y)) · Σ_j w_j. Estimator ⟨I⟩ = f(Y) · W_Y. For i.i.d. candidates with known pdf: W_{X_i} = 1/p(X_i), m_i = 1/M, so w_i = p̂(X_i)/(M p(X_i)) (Eq. 3.2) — notes §3.1, Algorithm 1
- "Could we use the chosen sample's contribution weight, W_X = W_{X_s}? That would lead to bias! The sampling process must be respected." — notes, Example 3.1.1 margin
- Null samples: if all w_i = 0, return a null sample with W = 0. "do not replace the null sample by immediately drawing another, as that causes bias" — notes §3.1
- With p̂ = f, the one-sample RIS estimate equals the plain MC average of the same candidates (Tip 3.10). The gain comes from *reusing* the selected sample later, not from the first RIS step itself — notes §3.1–3.2
- Rule of thumb (Tip 3.4): "Use 1/M weights if and only if all inputs are identically distributed." Tip 3.5: "A MIS weight is a function of one x. A weight that mixes PDFs or other functions at different input realizations x_j is most likely wrong." — notes §3.2
- RIS between BSDF and NEE candidates (§3.3): balance heuristic m_i = p_1/(M_1 p_1 + M_2 p_2) with both pdfs converted to the same (area) measure. "Technically, using 1/(M_1+M_2) MIS weights does not result in bias … However, the noise level would be as if evaluating direct illumination with only BSDF sampling—terrible."
- Inputs with unknown pdfs (§3.4): the **generalized balance heuristic** m_i(x) = p̂_i(x)/Σ_j p̂_j(x) (Eq. 3.11) uses each input's target function as a proxy for its unknown pdf. A **canonical** sample (one that covers p̂) must be included when the inputs would not otherwise cover p̂.

**Weighted reservoir sampling and ReSTIR (Ch. 4)**
- Reservoir (Algorithm 2/3): fields `Y, W_Y` (output sample + UCW), `w_sum`, `c` (confidence). `update(X_i, w_i, c_i)`: `w_sum += w_i; c += c_i; if rand() < w_i/w_sum then Y = X_i`. Finish: `W_Y = w_sum / p̂(Y)` (if Y not null) and `c = min(c, c_cap)` — notes §4.1, Algorithms 2–3
- Tip 4.1: "Do not choose which spatial neighbors to reuse based on the random samples stored at these neighbors, as this causes bias!" G-buffer-based neighbour choice is fine — notes §4.1–4.3
- Per-frame order: "initial candidate generation followed by temporal reuse, followed by spatial reuse, followed by integration with the selected sample" — notes §4.2
- ReSTIR DI target (Eq. 4.4): start with p̂ = f = f_s · G · V · L_e (BSDF, geometry, **visibility**, emission). Talbot dropped V for speed, which "worsens the theoretical limit distribution and requires guaranteeing additional conditions". Tip 4.3: "We strongly recommend first including visibility in your target function. Correcting an optimized method is exponentially harder than retaining correctness while optimizing." — notes §4.3
- MIS for reuse: generalized balance heuristic with confidence weights, m_i(x) = c_i p̂_i(x)/Σ_j c_j p̂_j(x) (Eq. 4.7). "Using 1/M weights generally leads to non-convergence to p̂ distribution, and, bias" — notes §4.3–4.4
- Temporal reuse needs last frame's p̂ for correct MIS, "which may require the ability to perform visibility queries in the previous frame's scene (and hence storing the previous frame's ray acceleration structure)" — notes §4.3
- Validation order (Tip 4.4): "implementing and validating integration with only candidate samples before implementing spatial reuse, spatial reuse before temporal reuse, and temporal reuse first without motion. By validation we mean that averaging a large number of still frames converges to the path tracing ground truth."
- Confidence: a new independent sample has c = 1; RIS over M new samples gives c = M; merging sums confidences, which "is a drastic overestimate"; "the sample confidence is capped to a constant that defines the balance between noise and correlation"; "We commonly cap the sample confidence to somewhere between 5–30. Starting with a cap of 20 is usually good." Reset c to 0 for pixels with no temporal predecessor and possibly on (dis)occlusion, but "resets depending on sample details leads to bias" — notes §4.4
- Advanced (§4.5): power-based light candidates; "light tiles" shared by screen-pixel blocks; mixing BSDF and light candidates (convert pdfs to area measure); moving objects need a shift mapping between frames.

**Reuse between domains (Ch. 5)**
- Shift mapping (Def. 5.1.1): a bijection from a subset of domain A to a subset of B. Requirements listed: deterministic; maps to at most one path; invertible (the inverse must map back); not all paths need to be shiftable; a shift may return "undefined" (e.g. occluded reconnection) — notes §5.1.1
- RIS between domains (§5.2): Y_i = T_i(X_i); w_i = m_i(Y_i) · p̂(Y_i) · W_{X_i} · |T_i′(X_i)|; W_Y = (1/p̂(Y)) Σ w_j (Algorithm 4); w_i = 0 if the shift failed.
- Generalized balance heuristic between domains (Eq. 5.9–5.11): p̂_{←i}(y) = p̂_i(T_i⁻¹(y)) · |T_i⁻¹′(y)| if y can be shifted back into domain i, else 0; m_i(y) = c_i p̂_{←i}(y) / Σ_j c_j p̂_{←j}(y). Cost O(M²) — notes §5.3, Algorithm 5
- Tip 5.1: "Implement the reconnection shift first. Always first validate that shifting to the same pixel retains the path and its radiance, and that the Jacobian determinant is then 1 … Report also small discrepancies: path tracers often have bugs only discovered when implementing shift mappings." Tip 5.2: "Slow down to speed up!"

**Making it fast (Ch. 7)**
- Neighbour rejection by G-buffer (normals, depth, material) is unbiased "if they do not look at individual samples or weights"; with 1/M weights it acts like Veach's cutoff heuristic (biased) — notes §7.1–7.1.1
- **Contribution MIS** (§7.1.2, as in the 2020 paper's unbiased mode): keep cheap 1/M resampling weights but multiply the final W by c_s(Y)/m_s(Y) with a proper weight c only for the *selected* sample, Eq. 7.1–7.2. Cost O(M). Works "reasonably well in direct lighting" but adds noise when domains differ a lot (e.g. participating media).
- **Pairwise MIS** (§7.1.3, Bitterli's thesis): pair every neighbour only with the canonical sample. Generalized form (Eq. 7.5): m_i = (1/(M−1)) · p̂_i/(p̂_i + p̂_c/(M−1)) for i ≠ c; m_c = (1/(M−1)) Σ_{j≠c} (p̂_c/(M−1))/(p̂_j + p̂_c/(M−1)). The **defensive** variant (Eq. 7.6/7.8, Algorithm 7) uses 1/M and adds a constant 1 for the canonical sample. O(M) instead of O(M²); "ReSTIR PT … adopts the defensive form as the default choice for spatial resampling".
- Biased MIS on purpose (§7.1.4): replacing the expensive p_j(X_i) (re-evaluating a sample in another pixel/frame, which may need a ray and last frame's BVH) by an approximation: overestimating it darkens, underestimating brightens; assuming p_j(X_i) = 0 "means … we overcount, giving a brightening bias."
- Sample tiling (§7.2.1): the Amusement Park scene (3M+ emissive triangles) spent up to 25 ms just picking random light candidates because of cache thrashing. Fix: each frame build light subsets ("usually … 128 light subsets each containing 1024 light samples"), each 8×8 or 16×16 screen tile draws its candidates from one subset; building costs "under 0.1 ms" and can outweigh its benefit in simple scenes. Many analytic light types: presample them into uniform point-light buffers first (§7.2.2).
- Hybrid-shift speed (§7.2.3): split into a random-replay kernel and a reconnection kernel (≈ 40 % less resampling shader time in Veach Ajar), then stream-compact replay work (another ≈ 40 %).
- Grid/world-space reservoirs (e.g. ReGIR, Boissé 2021) reduce ray count and memory "but in exchange, grid based reuse adds bias that we are only just learning to control" — notes §7.2

**Advice for getting started (Ch. 9), quoted points**
- "Start with a simple ground-truth Monte Carlo path tracer … in the same code, on the same scenes … Compare to ground truth. Frequently."
- "Start simple, with basic RIS … If RIS will not converge to ground truth, neither will your experiments with spatiotemporal reuse."
- "Think about rendering bias … With spatiotemporal sample reuse, bias spreads around the screen extremely quickly."
- "Spatial reuse alone is easier to debug; combining with temporal reuse gives better quality."
- "Don't try to get too clever too fast. If you grab RTXDI [you get] a ton of options. Checkerboarding, sample permutations, boiling suppression, etc. Many were never intended to be unbiased."
- "Basic ReSTIR gives you probability distributions at a point. Generally, a reservoir is not valid over, say, an entire voxel."
- "Reuse visibility very carefully … Visibility reuse also causes many problematic biases … (Arguably, it causes most difficult-to-debug biases.)"
- The notes defer the Cyberpunk 2077 lessons to Kozlowski's and De Francesco's slides on the course page (not read, see Gaps).

**Talbot 2005 and older roots**
- RIS is Talbot, Cline, Egbert, "Importance Resampling for Global Illumination", EGSR 2005; WRS is Chao 1982 ("A General Purpose Unequal Probability Sampling Plan") — [notes bibliography](https://intro-to-restir.cwyman.org/presentations/2023ReSTIR_Course_Notes.pdf)
- The 2020 paper notes RIS is related to ratio estimators and "weighted uniform sampling"/"weighted importance sampling", and that Talbot's multi-pdf variant evaluates all pdfs for each candidate (O(M²)) — [Bitterli et al. 2020](https://benedikt-bitterli.me/restir/bitterli20restir.pdf), §4.3 and §7

**A teacher the student knows**
- Jacco Bikker's Utrecht INFOMAGR slides "lecture 14 – ReSTIR" (title slide "TAA & ReSTIR": reprojection, resampling, ReSTIR, citing Bitterli 2020) exist for 2021/22 and 2022/23; the course notes cite the 2022/23 deck as community material — [2022/23 PDF](https://ics-websites.science.uu.nl/docs/vakken/magr/2022-2023/slides/lecture%2014%20-%20ReSTIR.pdf), [2021/22 PDF](https://ics-websites.science.uu.nl/docs/vakken/magr/2021-2022/files/lecture%2014%20-%20ReSTIR.pdf) (search snippets; PDFs not opened). This closes the `followup_bikker_tinybvh.md` gap "ReSTIR in 'Resampling' [U]".

### Inferences
- [I] The UCW is the one idea to really internalise: once "W replaces 1/pdf" clicks, every later algorithm (temporal, spatial, GI, PT) is "RIS again, with more candidates and the right MIS weight". The tutor should drill Example 3.1.1 (why you may not keep the chosen candidate's own W) on a 1D toy before any GPU code.
- [I] A 1D CPU toy (EV's M3-ref bench is the natural home) can reproduce Fig. 7 of the 2020 paper: sample p̂(x) = 2 − 2x from p₁ = 1 and a step pdf p₂, and plot the average W against 1/p from a histogram. This shows bias from 1/M weights and its fix in one evening, with no GPU.

### Gaps
- The Cyberpunk 2077 integration slides (Kozlowski, De Francesco) and the course videos were not opened.
- Talbot's 2005 paper and Bitterli's 2022 PhD thesis (origin of pairwise MIS) were not read directly; their content is taken from the course notes.

---

## 2. ReSTIR DI (Bitterli et al. 2020): candidates, temporal, spatial, visibility reuse, biased vs unbiased, M-cap, RTXDI

### Takeaway
ReSTIR DI samples **direct light at the primary hit** from very many lights. Per pixel and frame:
1. **Initial candidates**: M = 32 light samples (triangle picked ∝ power, uniform point on it; 25 % from the env map), RIS
   with the cheap unshadowed target p̂ = BSDF · L_e · G.
2. **Visibility reuse**: one shadow ray for the winner; if occluded, its W is set to 0 *before* sharing.
3. **Temporal reuse**: merge with the reservoir of the motion-vector-matched pixel of the previous frame (its confidence
   clamped to 20× the current one).
4. **Spatial reuse**: merge with k random neighbours in a 30-pixel radius (k = 5 biased / 3 unbiased), n = 2 / 1 passes.
5. **Shade**: one shadow ray + BSDF for the final sample, `f(y) · W`; store the reservoir for next frame.

The **biased** mode (1/M weights plus G-buffer neighbour rejection) darkens at shadow and geometry edges but has the
lowest error at interactive times. The **unbiased** mode fixes the MIS weight (count only neighbours whose p̂ — including
visibility, so one extra shadow ray per neighbour — is non-zero), and wins after about 1 s of accumulation. Reported
speed-ups at equal error: 6–60× (unbiased) and 35–65× (biased) versus prior real-time light sampling. The RTXDI SDK
(v3.2.0) is the production version: HLSL callbacks you implement, D3D12 or Vulkan samples, NVIDIA's proprietary RTX SDK
licence. That is fine for reading, but EV should not copy code from it.

### Cited Findings
**Paper and numbers** — [Bitterli, Wyman, Pharr, Shirley, Lefohn, Jarosz, "Spatiotemporal reservoir resampling for real-time ray tracing with dynamic direct lighting", ACM TOG 39(4) (SIGGRAPH 2020)](https://benedikt-bitterli.me/restir/), [PDF](https://benedikt-bitterli.me/restir/bitterli20restir.pdf)
- Abstract: unbiased estimator "equal-error 6×–60× faster than state-of-the-art methods"; biased "35×–65× faster, at the cost of some energy loss"; "up to 3.4 million dynamic, emissive triangles in under 50 ms per frame while tracing at most 8 rays per pixel".
- Fig. 1: Zero Day rendered in **15 ms** and Amusement Park in **50 ms** at 1920×1080; Fig. 2: carousel in 42 ms (no denoising) with 3.4M animated lights.
- Hardware: "GeForce RTX 2080 Ti GPU, except for the Amusement Park scene, which required use of a Titan RTX due to high memory requirements". G-buffer rasterisation "averages 1–2 ms" and is excluded. Prototype in Falcor — §6
- Streaming RIS alone (Fig. 4, Subway, 23,000 emissive triangles): tracing 8 shadow rays costs 6 ms; selecting those samples costs 1.0, 2.5, 10.1, 42 and 168 ms for increasing M. "good results require large values of M … computation remains linear in M". Streaming RIS was "consistently faster (20%–30% speedup) than non-streaming implementations" — §3.1, §6
- Reservoir merge (Alg. 4): feed each neighbour's sample as a candidate with weight p̂_q(r.y) · r.W · r.M; s.M = Σ r.M; s.W = (1/p̂_q(s.y)) · (1/s.M) · s.w_sum. Repeating spatial reuse n times "effectively yields kⁿM candidates" at O(nk + M) cost — §3.2
- Full per-frame algorithm (Alg. 5): initial RIS → visibility test of each pixel's sample (occluded → W = 0) → temporal combine → n spatial passes → shade — §3.2
- Bias source (§4): candidates from other pixels can have zero pdf where the current integrand is non-zero (different normals, discarded occluded samples). Eq. 19: E[W | y] = (1/p(y)) · |Z(y)|/M, where Z(y) is the set of candidates that could produce y, so the inverse pdf "is consistently underestimated … biased to be darker".
- Fix (Eq. 20–22, Alg. 6): replace 1/M by a weight m with Σ m = 1 over the candidates that *can* produce y, e.g. 1/Z with Z = Σ r_i.M over neighbours with p̂_{q_i}(s.y) > 0, or the balance heuristic over candidate pdfs. "if we employ visibility reuse, then p̂_{q_i} includes visibility, and evaluating it requires tracing an additional shadow ray. E.g. in spatial reuse, this means tracing k additional rays" — §4.3–4.4
- Implementation choices (§5): M = 32 candidates; triangles by power, then uniform point (p ∝ L_e); with an env map "25% of candidates are instead generated by importance sampling the environment map"; alias tables. Target "unshadowed path contribution p̂ ∝ ρ · L_e · G"; material "dielectric GGX microfacet layer atop a diffuse Lambertian substrate".
- Neighbours (§5): deterministic box neighbours gave "distracting artifacts"; instead "k = 5 (k = 3 for our unbiased algorithm) random points in a 30-pixel radius … sampled from a low-discrepancy sequence". Biased mode rejects neighbours if depth differs > 10 % or normals > 25°. n = 2 (biased) / 1 (unbiased) spatial passes. N = 4 reservoirs per pixel (biased) vs N = 1 (unbiased).
- Temporal: motion vectors back-project the pixel. Confidence growth: "we simply clamp the previous frame's M to at most 20× of the current frame's reservoir's M" — §5
- Reservoir storage: "The selected sample y, the number of candidates M that contributed to the pixel, and the probabilistic weight W" — §5
- Error over time (Fig. 11): biased spatiotemporal has the lowest error "at interactive render times … as render time increases, the error due to bias dominates, so our unbiased spatiotemporal reuse eventually exhibits lower error (usually at around 1s)". Biased spatial-only reuse (no temporal) is competitive and needs no history — §6
- Limitations (§8.1): quality degrades "near disocclusions, lighting discontinuities, high geometric complexity, fast moving lights"; the image-space data structure limits it to the primary hit.
- Denoising: Fig. 2 shows better input samples make the OptiX denoiser's output much better; the authors suggest denoisers adapted to ReSTIR's output — §1, §8

**RTXDI SDK (the productised ReSTIR DI/GI/PT)** — [RTXDI repo](https://github.com/NVIDIA-RTX/RTXDI)
- Version 3.2.0. Algorithms: ReSTIR DI; ReGIR (world-space grid light presampling); ReSTIR GI (v2.0); ReSTIR PT (v3.0, "allows applications to apply importance resampling to all indirect lighting rendered using path tracing"). Folders `Libraries/Rtxdi/Include/Rtxdi/{DI,GI,PT}`, `Samples/MinimalSample` (DI only, no denoiser) and `Samples/FullSample` (DI, GI, PT + NRD). Samples run on D3D12 (default) or Vulkan via NVRHI/Donut; HLSL compiled with DXC — [README](https://raw.githubusercontent.com/NVIDIA-RTX/RTXDI/main/README.md)
- Integration steps: light buffer (mesh lights split into triangles), optional PDF textures with mip chains, constant buffers from `ReSTIRDI_*Parameters`, optional presampling (`RTXDI_PresampleLocalLights`, `RTXDI_PresampleEnvironmentMap`), optional ReGIR build, then shader calls `RTXDI_SampleLightsForSurface`, `RTXDI_DITemporalResampling`, `RTXDI_DISpatialResampling`, then shading (trace visibility, evaluate BRDF), optional denoiser confidence, denoise. The app implements the "RTXDI-Application Bridge" callbacks (`RAB_*`, e.g. `RAB_LoadLightInfo`, `RAB_TranslateLightIndex`); "the library performs no graphics API calls". Reservoirs are `RTXDI_PackedDIReservoir` in a block-linear layout (`reservoirArrayPitch`). Checkerboard mode halves storage. The sample uses ReGIR as the default initial sample generator — [Integration.md](https://github.com/NVIDIA-RTX/RTXDI/blob/main/Doc/Integration.md)
- No NVIDIA-only extension is listed as required by the library; the guide says integrations need not ship extra closed-source libraries. The samples pull NRD and optional DLSS — same page.
- Licence: "NVIDIA RTX SDKs LICENSE" (Jan 31, 2025), proprietary. You may modify sample code and redistribute only in object form inside an application that adds material functionality, and you may not make it subject to an open-source licence — [LICENSE.txt](https://github.com/NVIDIA-RTX/RTXDI/blob/main/LICENSE.txt)
- RTXPT 1.9.0 uses RTXDI 3.0 for "ReSTIR DI (light importance sampling) and ReSTIR GI (indirect lighting)". It requires DXR 1.1, SM 6.6, Resource Binding Tier 3 (DXR 1.2 / SM 6.9 by default), uses NVAPI and SER, and lists AMD only under known issues ("black or incorrect transparencies/reflection on some AMD systems") — [RTXPT README](https://raw.githubusercontent.com/NVIDIA-RTX/RTXPT/main/README.md)
- [T][U] RTXDI's resampling passes are compute shaders using inline `RayQuery` for visibility. Not verified in the source this session.

### Inferences
- [I] **EV's light set is small** (D17: one sun disk, an HDR sky with importance sampling, a few point lights). ReSTIR DI's headline gains come from scenes with thousands to millions of emitters. For EV's ocean shot, the realistic gains are (a) **sky visibility**: env-map NEE that keeps finding unoccluded sky directions under a hull or in a cove; (b) a better **BSDF × light product** on glossy water (sun glitter); (c) far fewer shadow rays per pixel in real-time mode. To see the many-light behaviour the student should add a many-point-light test scene (10³–10⁵ lights) [I/E].
- [I] Everything in the 2020 algorithm is a full-screen pass that needs a ray only for visibility, so it maps directly onto D2 (compute + inline `RayQuery`, `RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH` for shadow rays). No `DispatchRays` is needed for any ReSTIR DI pass.
- [I] RTXDI is the best *reading* reference for production details (packing, block-linear layout, light presampling tiles, ReGIR, checkerboarding, denoiser confidence). Its licence plus the "student writes everything" rule mean: read it like pbrt, re-derive, write EV's own.

### Gaps
- The exact `RTXDI_PackedDIReservoir` byte size and field list were not read (the shader API doc was not opened).
- Whether RTXDI's sample apps are tested on AMD GPUs is not stated anywhere I read.
- The supplemental document of the 2020 paper (Talbot vs. Bitterli MIS comparison) was not read.

---

## 3. ReSTIR GI (Ouyang et al. 2021): reusing secondary-bounce points, reconnection Jacobian, timings

### Takeaway
ReSTIR GI moves the idea from "points on lights" to **"points the first bounce ray hit"**. Each pixel traces one
BSDF/uniform ray from its visible point x_v, finds a **sample point** x_s, and estimates the light leaving x_s toward x_v
(NEE at x_s, optionally a longer path). It stores (x_v, n_v, x_s, n_s, L_o, random seed) and resamples that sample point
temporally and spatially. Reusing a neighbour's x_s at another visible point is a **reconnection shift**, so it needs
the Jacobian |cos φ_r| / |cos φ_q| · ‖x_q1 − x_q2‖² / ‖x_r1 − x_q2‖². Without it you get visible lighting discontinuities
(Fig. 7). The stored radiance is assumed to be the same in all directions (Lambertian at x_s), which makes the shipped
variant biased on glossy surfaces. Cost: about **8–18 ms at 1080p on an RTX 3090** in UE4, with 9.3×–166× lower MSE
than 1-spp path tracing at equal sample count. Memory: 48 B per reservoir, 475 MB total at 1080p. kajiya shows a
biased, half-resolution variant running in about 2.3 ms on an **AMD RX 6800 XT**.

### Cited Findings
**Paper** — [Ouyang, Liu, Kettunen, Pharr, Pantaleoni, "ReSTIR GI: Path Resampling for Real-Time Path Tracing", CGF 40(8), HPG 2021](https://research.nvidia.com/publication/2021-06_restir-gi-path-resampling-real-time-path-tracing), [PDF](https://d1qx31qr3h6wln.cloudfront.net/publications/ReSTIR%20GI.pdf)
- Results: "at a single sample per pixel every frame, our algorithm achieves MSE improvements ranging from 9.3× to 166×"; UE4 implementation "adds from 8 ms to 18 ms per frame when rendering at 1080p resolution with an NVIDIA 3090 RTX GPU". All results measured on an RTX 3090; reference = 8,192 spp path tracer — §1, §6
- Fig. 1 equal time (RTX 3090, 1080p, no denoising): 1-spp path tracing 8.0 ms vs. ReSTIR GI 8.9 ms, MSE improved 15.1×. Fig. 13 rows: PT 8.0 ms / biased 8.9 ms (15.1×) / unbiased 9.6 ms (11.8×); PT 10.4 / 11.2 (51.7×) / 11.4 (26.3×); PT 2 spp 18.9 / 16.0 (141×) / 18.0 (166×); PT 8.2 / 8.8 (17.3×) / 9.6 (14.7×). "ReSTIR GI provides a reduction in MSE ranging from 14.6× to 141× for the biased variant, and 9.3× to 166× for the unbiased version" (after 32 frames of temporal resampling) — §6, Fig. 13
- Fig. 8 (RTX 3080, 1920×1050): initial sampling 3.2 ms (1 bounce) / 4.2 ms (2 bounces); sample reuse 4.6 ms in both cases; 2-bounce path tracing 6.9 ms. Total frame time including resampling 14 ms — Fig. 8, §6
- Initial sampling (Alg. 2): per pixel, read x_v, n_v from the G-buffer, sample a direction from the source pdf, trace to x_s, estimate L_o at x_s, store `SAMPLE(x_v, n_v, x_s, n_s, L_o)` plus "the random numbers used in next event estimation" — Fig. 4, Alg. 2
- Target: p̂ = L_o(x_s, −ω_i) · f(ω_o, ω_i) · ⟨cos θ_i⟩ (Eq. 9), "though we have also found that the simple target function p̂ = L_o(x_s, −ω_i) (Eq. 10) works well … helpful for spatial resampling in that it preserves samples that may be effective at pixels other than the one that initially generated it." Eq. 10 "gives a more stable result, though with higher variance" — §4.2, §6
- "using uniform hemisphere sampling for initial samples causes lower variance than cosine-weighted sampling, especially for light from grazing angles" — §6
- Multi-bounce: following n path-traced bounces from x_s gives (n+1)-bounce GI; "we only follow multi-bounce paths in 25% of the pixels", using Russian roulette on 64×32-pixel tiles for coherence — §4.1, §5
- Reconnection Jacobian (Eq. 11): |J_{q→r}| = (|cos φ_r2| / |cos φ_q2|) · (‖x_q1 − x_q2‖² / ‖x_r1 − x_q2‖²), with φ the angles at the sample point's normal. "there is no reason not to include this factor, given its minimal computational expense" — §4.2, §4.3
- Spatial resampling (Alg. 4): random neighbour → geometric similarity test (normals within 25°, normalised depths within 0.05) → p̂′ = p̂_q(R_n.z)/|J| → if the sample point is not visible from x_v, p̂′ = 0 → merge → bias correction counts only neighbours whose p̂_{q_n}(R_s.z) > 0. Spatial reservoirs read only *temporal* reservoirs (not other spatial ones) "so that bias does not accumulate", except when sample counts are low — §4.2–4.3, Fig. 10
- Temporal bias in dynamic scenes: stored L_o goes stale and "ReSTIR's characteristic of tending to keep relatively bright samples in the reservoirs for many frames" causes lag. Fix: periodic **sample validation** (re-trace with the *same random numbers*, clear the reservoir if radiance changed beyond a tolerance), by default every 6 frames, folded into the initial-sampling pass. A shadow ray during validation also catches new occluders — §4.3, §5
- Implementation (§5): UE4 (raster G-buffer) and Falcor (ray-traced G-buffer). Temporal reprojection via motion vectors; failed reprojection resets both reservoirs. "our implementation neglects the directional variation in scattered radiance at the sample point … corresponding to Lambertian scattering". Double-buffered reservoirs (one frame of lag).
- Memory/bandwidth: normals compressed to 4 bytes, half-float radiance, "each reservoir then requires 48 bytes"; one initial + 2 temporal + 2 spatial buffers = **475 MB at 1080p**; about 570 MB read and 285 MB written per frame — §5
- M caps: "We clamp the value of M in the temporal reservoirs to 30 and in the spatial reservoirs to 500" — §5
- Spatial parameters: maxIterations 9 when the sample count is below half the spatial max M, else 3. Search radius starts at 10 % of image resolution and halves on failure down to 3 pixels — §5
- Limitations (§6.1): too costly for "less than a 2 ms computational budget for global illumination" on lower-end GPUs; fast camera motion leaves new pixels undersampled; perfectly specular primary surfaces break screen-space reuse; not effective for multi-bounce GI on highly glossy surfaces; correlated output confuses denoisers: "the popular SVGF denoiser … accumulates first and second moments … Correlated input may cause artifacts due to an inexact estimate of variance".
- Course notes on ReSTIR GI: it uses the identity (reconnection) shift in area measure; precomputing L_o along x_2→x_1 "is only true for a very limited set of materials like Lambertian diffuse"; "reusing paths with specular vertices can increase the variance instead of reducing it" — [notes §6.3](https://intro-to-restir.cwyman.org/presentations/2023ReSTIR_Course_Notes.pdf)

**kajiya (Embark, open source)** — [gi-overview.md](https://github.com/EmbarkStudios/kajiya/blob/main/docs/gi-overview.md)
- "the ReSTIR implementation in `kajiya` is the biased flavor". Diffuse rays at half resolution, one in four pixels per frame. Spatial passes with 8 then 5 samples, radii 12–32 and 6–16 px; resolve with 4 reservoirs; M clamped to 10 (temporal test). Reflections: temporal-only reservoirs, M clamped to 8.
- Validation re-traces stored samples every third frame; radiance updates capped at 10× the previous value to limit fireflies.
- A world-space irradiance cache (12 camera-aligned 32³ clip maps) provides multi-bounce light and is refined by its own temporal reservoirs (4 candidates per entry per frame). It is "never queried directly from any screen pixel".
- Timings at 1920×1080 on a **Radeon RX 6800 XT**: total 8.4 ms; indirect diffuse ~2.3 ms; irradiance cache ~0.55 ms; reflections ~2.2 ms; G-buffer ~1.15 ms.

### Inferences
- [I] ReSTIR GI's "sample = (point, normal, radiance)" layout is exactly what EV's M9 SHaRC and M10 NRC produce at a path's end: the radiance L_o stored in the sample can come from "NEE at x_s + a short path ending in SHaRC/NRC". So ReSTIR GI and the radiance caches are complementary, not competing. The cache makes the *sample's value* cheap and less noisy; ReSTIR makes the *sample's direction* good.
- [I] kajiya is the proof that a biased ReSTIR GI runs well on AMD hardware in plain shader code (Vulkan, Rust host). That is relevant for the RX 9070 XT main PC.
- [I] On the ocean, the primary hit is the water surface: glossy or smooth GGX with a refraction lobe. ReSTIR GI's Lambertian-at-x_s assumption and its failure on specular primaries mean it will help most on **rough** receivers (boats, foam, seabed seen through rough water, the Sponza/helmet scenes) and least on the clear-water surface itself. The ocean surface is the case that needs ReSTIR PT's hybrid shift.

### Gaps
- kajiya's API and licence were not stated on the page read (Vulkan + Rust and MIT/Apache are [T]).
- The ReSTIR GI paper's exact `SAMPLE` packing beyond "48 bytes" was not given.

---

## 4. ReSTIR PT / GRIS (Lin et al. 2022) and follow-ups (2023–2026); open-source implementations

### Takeaway
**GRIS** ("generalized RIS") is the theory that makes reuse of *correlated* samples from *different domains* with
*unknown pdfs* provably correct. It gives the shift-mapping rules (bijective, Jacobian), the generalized balance
heuristic and pairwise MIS. **ReSTIR PT** applies it to whole paths, with a **hybrid shift**: random replay (same random
numbers) through glossy/specular vertices, then reconnect at the first vertex pair that is rough (per-lobe GGX roughness
≥ 0.2) and far enough apart (distance ≥ 1–5 % of scene size). Paths are stored as a reconnection vertex + RNG seed in
an 88-byte reservoir, in primary-sample space, so replay has Jacobian 1. It handles glossy reflections, refraction and
some caustics that ReSTIR GI cannot. The original prototype was slow: 40–80 ms per frame on an RTX 3090. **ReSTIR PT
Enhanced** (I3D 2026, best paper) brought it to about **13–16 ms at 1080p on an RTX 5880 Ada**. It got there through
code micro-optimisation, forced NEE reconnection, replay compaction, reciprocal ("paired") neighbours, Russian roulette at
initial sampling only, unified DI+GI reservoirs and 64-byte reservoirs. It also reduces correlation with duplication maps
and reduces colour noise with vector-valued resampling weights. RTXDI 3.x now ships ReSTIR PT (hybrid shift, footprint
reconnection, duplication maps). Research has since moved on to temporal robustness (reservoir splatting, multi-layer
splatting), decorrelation (conditional ReSTIR, MCMC mutations), lens/subpixel reuse (Area ReSTIR), caustics (ReSTIR
BDPT, specular manifold resampling) and path guiding (ReSTIR PG).

### Cited Findings
**GRIS / ReSTIR PT** — [Lin, Kettunen, Bitterli, Pantaleoni, Yuksel, Wyman, "Generalized Resampled Importance Sampling: Foundations of ReSTIR", ACM TOG 41(4) (SIGGRAPH 2022)](https://research.nvidia.com/publication/2022-07_generalized-resampled-importance-sampling-foundations-restir), [PDF](https://d1qx31qr3h6wln.cloudfront.net/publications/sig22_GRIS.pdf), [Utah page with result viewer](https://graphics.cs.utah.edu/research/projects/gris/)
- Abstract: "sample reuse introduces correlation, so ReSTIR-style iterative reuse loses most convergence guarantees that RIS theoretically provides". GRIS extends the theory to correlated samples, unknown pdfs and different domains, giving variance bounds and convergence conditions, and "enables advanced path reuse between pixels via complex shift mappings".
- Fig. 1: equal time **80 ms at 1920×1080**; MAPE path tracing / ReSTIR GI / ReSTIR PT = 1.63 / 0.45 / 0.39 (Carousel) and 1.28 / 0.39 / 0.33 (Paris Opera House).
- Shift building blocks (§7.2): vertex copy (reconnection), half-vector copy, direction copy, random replay, manifold exploration. Pitfalls: high-gloss reflections, reconnecting through occlusion, "drastically changing the reconnection segment length", shifting between objects/materials. "Removing paths from a map's domain may cause noise and waste computation, but neglecting bijectivity introduces significant bias" — §7.1
- Real-time shifts (§7.4): the **reconnection shift** (y₂ = x₂ always; "ReSTIR GI … implicitly uses this choice but trades correctness for performance") and a **hybrid** of random replay and reconnection (after Hua et al. 2019). To save memory, "we precompute the first base path vertex x_i that satisfies the connectability condition … Reconnection must happen at this vertex, or it does not happen".
- Connectability (§7.5–7.6): distance condition ‖x_{i+1} − x_i‖ ≥ d_min (both for base and offset) to avoid singular geometry terms near corners; **lobe-specific** roughness (classify by the roughness of the sampled BSDF lobe, not the whole material). This needs a lobe-index-extended path space (Eq. 49–51).
- Jacobians (§8.1): reconnection in solid angle |∂ω_y/∂ω_x| = (|cos θ_y| / |cos θ_x|) · ‖x_{i+1} − x_i‖² / ‖x_{i+1} − y_i‖² (Eq. 52); random replay Jacobian = ratio of the next-vertex solid-angle pdfs (Eq. 53). In primary-sample space "The Jacobian for random replay … is always 1" — §8.1
- Reservoir: "Our reservoirs … consume 88 bytes per path while supporting our hybrid shift", storing the contribution and confidence weights, "the path's chosen reconnection vertex and a seed for random replay" — §8.2
- Parameters (§8.3): offline: 32 initial path trees per pixel, spatial reuse from the current pixel + 6 neighbours in a 10-pixel radius. Real-time: one path tree, "A single spatial reuse pass selects three random neighbors in a 20-pixel radius"; "We cap M with M_c = 20". GGX roughness threshold 0.2; distance threshold 1–5 % of scene size. MIS: defensive pairwise MIS for spatial; generalized Talbot MIS for temporal (real-time).
- Performance: "All performance numbers were measured on a GeForce RTX 3090 at 1920 × 1080". Glossy Cornell box 1 spp: reconnection shift 5 ms vs. hybrid 12 ms (Fig. 6). Real-time comparisons use ReSTIR DI for direct light; "ReSTIR PT achieves 24%–75% lower MAPE compared to path tracing (both with ReSTIR DI)". Offline: reconnection shift converges "up to 14.4× faster" and hybrid "up to 10× faster" than path tracing; Bekaert-style path reuse up to 2.8× — §9.3
- Code: [DQLin/ReSTIR_PT](https://github.com/DQLin/ReSTIR_PT), Falcor 4.4 render pass `ReSTIRPTPass`, **BSD-3-Clause**, requires NVAPI and lists "RTX 2060 or Higher (NVIDIA graphics card with raytracing support)", D3D12.
- Course notes §6.6: the hybrid shift stores "only a reconnection vertex and a random-number-generating seed in addition to other reservoir data like the unbiased contribution weight". Exact invertibility conditions are Eq. 6.6–6.11; NEE samples are tagged with a lobe/technique index so that "random replay can produce the exact same (sub)path produced by a path tracer". PSS benefits: integrand = sampled throughput f/p (as a path tracer computes it), initial candidate pdf = 1, no float overflow from tracking f and p separately — [notes](https://intro-to-restir.cwyman.org/presentations/2023ReSTIR_Course_Notes.pdf)

**ReSTIR PT Enhanced (2026)** — [Lin, Kettunen, Wyman, "ReSTIR PT Enhanced: Algorithmic Advances for Faster and More Robust ReSTIR Path Tracing", Proc. ACM CGIT 9(1), Article 13 (I3D 2026, Best Paper), DOI 10.1145/3804494](https://research.nvidia.com/labs/rtr/publication/lin2026restirptenhanced/), [PDF](https://research.nvidia.com/labs/rtr/publication/lin2026restirptenhanced/lin2026restirptenhanced.pdf)
- Motivation: the 2022 prototype yielded "sluggish performance even on high-end GPUs (originally 40–80 ms per frame on an RTX 3090)" and suffers "spatiotemporal correlations, variance near disocclusions, and color noise"; hybrid-shift thresholds "often benefit from adjustment per scene" — §1
- Contributions: halving spatial shift cost by **reciprocal neighbour selection**; **ray-footprint reconnection thresholds** that adapt to scene and materials; **sample duplication maps** against correlation; **unified DI + GI** in one reservoir; colour/disocclusion noise reduction — §1
- Table 1 (average over Veach Ajar, Carousel, Opera House, Bathroom; ms): baseline 35.73 total (initial 10.59, temporal 5.30, spatial 14.79, ReSTIR DI and others 5.05) → code micro-optimisation 32.98 → forced NEE reconnect 29.75 → replay compaction 26.81 → paired spatial reuse 25.02 → **Russian roulette 16.52** → unify DI & GI 13.04 → (quality features) new thresholds 14.51 → all improvements 15.53 — Table 1
- Evaluation hardware: "NVIDIA RTX 5880 Ada graphics card", Falcor, 1920×1080, HDR-FLIP error. Spaceship scene: original 37.1 ms (FLIP 0.321) vs. enhanced 12.6 ms (FLIP 0.263). Defaults kept from ReSTIR PT: c_cap = 20 temporal, "3 random spatial neighbors in a 30-pixel radius" — §7, Fig. 1
- Duplication map (§5): after each frame, count how many reservoirs in the 17×17 neighbourhood share the pixel's random seed, divided by 288 → score δ ∈ [0,1]; temporal c_cap = lerp(c_cap_default, c_cap_min, δ^α) with c_cap_min = 1, α = 0.1. It "trades correlation for bias": average absolute relative bias 3.25 % in a hard scene. "Correlation artifacts make denoising ReSTIR challenging, as artifacts get detected as signal".
- Unified DI+GI (§6.1): trace an extra NEE ray from x₁ during path-tree generation and let initial resampling pick a length-2 path too; "it also improves quality as direct lighting now benefits from ReSTIR PT's built-in shift mapping". Many lights: RIS for NEE with light tiles (128 tiles × 1024 lights; each 8×8 screen tile draws one).
- GPU divergence (§6.2): branches → conditional moves; **reservoir 88 → 64 bytes**; stream compaction of pixel–neighbour pairs that need replay; forced reconnection at NEE light vertices; Russian roulette only at initial sampling, never in replay.
- Colour noise (§6.3): "ReSTIR inherently suffers from color noise because resampling operates on a scalar target function p̂, while the integrand F(Y) is RGB-valued. GRIS therefore samples primarily according to luminance, leaving chroma poorly importance-sampled." Fix: accumulate **vector-valued resampling weights** w_i = m_i F(Y_i) W_{X_i} |∂T/∂X_i| and use Σ w_i for shading, while scalar weights drive resampling ("almost no extra cost").
- Disocclusion (§6.4): **dual motion vectors** (Zeng et al. 2021) applied to temporal resampling give "a substantial reduction in disocclusion noise".
- Conclusion: "When a noise-free unbiased image is desired, accumulating it with temporal reuse disabled is typically more efficient" — §7/§8 text
- RTXDI's ReSTIR PT implements the hybrid shift (`RTXDI_ComputeHybridShift`), reconnection modes `FixedThreshold` and `Footprint` (the Enhanced criteria), duplication maps (`RTXDI_PTComputeDuplicationMap`), decorrelation, firefly replacement. Bias in the spatial pass is a known issue — [RestirPT.md](https://github.com/NVIDIA-RTX/RTXDI/blob/main/Doc/RestirPT.md), [README](https://github.com/NVIDIA-RTX/RTXDI)

**Other 2021–2026 work (titles and core idea from the authors' pages; full texts not read unless stated)**
- Volumetric ReSTIR: Lin, Wyman, Yuksel, "Fast Volume Rendering with Spatiotemporal Reservoir Resampling", TOG 40(6), SIGGRAPH Asia 2021. Target with ray-marched transmittance in low-resolution volumes; vertex reuse vs. direction reuse (direction reuse default, because vertex reuse gives fireflies from geometric singularities) — summarised in [course notes §6.7](https://intro-to-restir.cwyman.org/presentations/2023ReSTIR_Course_Notes.pdf)
- Wyman & Panteleev, "Rearchitecting Spatiotemporal Resampling for Production", HPG 2021: the source of light tiles and decoupled shading — notes bibliography
- Conditional ReSTIR: Kettunen, Lin, Ramamoorthi, Bashford-Rogers, Wyman, SIGGRAPH Asia 2023 (DOI 10.1145/3610548.3618245). Extends GRIS to conditional probability spaces; a final-gather variant of ReSTIR PT that postpones reuse by at least one bounce and reduces blotchy correlation, which helps denoisers. Falcor prototype at [NVLabs/conditional-restir-prototype](https://github.com/NVLabs/conditional-restir-prototype) — [project page](https://research.nvidia.com/labs/rtr/publication/kettunen2023conditional/)
- Sawhney et al., "Decorrelating ReSTIR Samplers via MCMC Mutations" (TOG 2024): MCMC mutations of reservoir samples against correlation — [PDF](https://research.nvidia.com/labs/rtr/publication/sawhney2024decorrelating/ReSTIRMCMC.pdf), [arXiv 2211.00166](https://arxiv.org/pdf/2211.00166) (not read)
- Area ReSTIR: Zhang, Lin, Kettunen, Yuksel, Wyman, TOG 43(4) (SIGGRAPH 2024). Reservoirs integrate each pixel's 4D ray space (film area + lens area), with subpixel-tracking temporal reuse and new shifts, for antialiasing and depth of field. Source code at [guiqi134/Area-ReSTIR](https://github.com/guiqi134/Area-ReSTIR) — [project page](https://graphics.cs.utah.edu/research/projects/area-restir/)
- Reservoir Splatting: Liu, Lin, Kettunen, Wyman, Ramamoorthi, SIGGRAPH 2025 (DOI 10.1145/3721238.3730646). Forward-splats reservoirs into the next frame instead of back-projecting, which fixes failures under camera motion and subpixel geometry; adds motion blur; better than Area ReSTIR "at up to 10% lower cost" — [project page](https://research.nvidia.com/labs/rtr/publication/liu2025splatting) (search summary)
- NVIDIA RTR's ReSTIR tag also lists: ReSTIR BDPT (Hedstrom 2025, bidirectional + caustics); ReSTIR PG (Zeng 2025, path guiding from resampled paths); Sample Space Partitioning … for Specular Manifold Sampling (Hong 2025, caustics); and 2026 papers: Multi-Layer Reservoir Splatting for Temporal Reuse under Disocclusion (Hong), Real-Time Level-of-Detail Rendering with ReSTIR (Wang), Gradient-Domain ReSTIR PT (Wang), Stochastic Pairwise MIS for Unbiased Large-Kernel Reuse (Hedstrom), Compatibility-Guided Neighbor Selection for ReSTIR (Junkins) — [tag page](https://research.nvidia.com/labs/rtr/tag/restir/)
- Other labs: ReSTIR FG (TU Clausthal, reservoir-resampled photon final gathering for caustics) — [repo](https://github.com/TU-Clausthal-Rendering/ReSTIR-FG) (not read); Tokuyoshi, "Efficient Spatial Resampling Using the PDF Similarity", PACMCGIT 6(1) 2023 — cited in course notes §6.4; ToF ReSTIR — [arXiv 2605.11536](https://arxiv.org/pdf/2605.11536) (not read)

**Open-source implementations (status for EV)**

| Implementation | What | API / vendor | Licence | Use for EV |
|---|---|---|---|---|
| [RTXDI 3.2.0](https://github.com/NVIDIA-RTX/RTXDI) | ReSTIR DI, ReGIR, ReSTIR GI, ReSTIR PT (hybrid, footprint, duplication maps) | HLSL; samples D3D12/Vulkan (NVRHI) | NVIDIA RTX SDK licence (proprietary) | Read as reference |
| [RTXPT 1.9.0](https://github.com/NVIDIA-RTX/RTXPT) | Reference + real-time PT using RTXDI DI/GI, NRD, DLSS-RR | D3D12 SM 6.6+ (6.9 default), NVAPI, SER; AMD only in known issues | see repo (NVAPI terms) | Read; whole-pipeline example |
| [DQLin/ReSTIR_PT](https://github.com/DQLin/ReSTIR_PT) | Original GRIS/ReSTIR PT (Falcor 4.4) | D3D12, NVAPI, "RTX 2060 or higher" | BSD-3-Clause | Read; may run only on the 3070 |
| [conditional-restir-prototype](https://github.com/NVLabs/conditional-restir-prototype) | Conditional ReSTIR (Falcor) | Falcor/NVIDIA [I] | not checked | Read later |
| [Area-ReSTIR](https://github.com/guiqi134/Area-ReSTIR) | Area ReSTIR | not checked | not checked | Read later |
| [kajiya](https://github.com/EmbarkStudios/kajiya) | Biased ReSTIR GI + irradiance cache, temporal ReSTIR reflections | Vulkan/Rust [T]; timed on RX 6800 XT | not stated on page read | Read; AMD-proven |
| [Capsaicin](https://github.com/GPUOpen-LibrariesAndSDKs/Capsaicin) | AMD research framework: reference PT, GI-1.2 (two-level radiance caching); "(optional) reservoir-based resampling" in its grid light sampler | D3D12 Ultimate | MIT | Read for AMD-side light sampling; not a ReSTIR GI/PT reference |
| [spectral-restir](https://github.com/nathencoelhoo/spectral-restir) | Hobby Vulkan "spectral ReSTIR" (milestones 1–2 done, no results) | Vulkan ray query | MIT | Curiosity, unverified |

### Inferences
- [I] The Enhanced Table 1 is a ready-made **optimisation syllabus for O2**. Each row is one measurable change (micro-ops, forced reconnection, compaction, reciprocal neighbours, Russian roulette, DI+GI unification), in the same "change one thing, re-measure" spirit as `plan.md`'s optimisation phases.
- [I] Scaling the Enhanced timings to EV's GPUs: RTX 5880 Ada ≈ RTX 4090-class [T]. The RTX 3070 has much less raw throughput [T], and the RX 9070 XT's RT throughput lies somewhere in between [U]. So a ReSTIR PT at 1080p is plausibly **25–45 ms on the 3070** and **18–35 ms on the 9070 XT** [E]. That is fine for an "interactive high-quality" mode, but it is not a 60 fps budget without lower internal resolution.
- [I] ReSTIR PT's dependency on **replayable random numbers** is the one design decision that is cheap now and expensive later. If EV's M3 sampler is a counter-based RNG keyed by (pixel, frame, bounce, dimension), random replay is free later. A stateful per-pixel RNG that advances unpredictably would have to be rewritten.

### Gaps
- The full texts of Area ReSTIR, Reservoir Splatting, ReSTIR BDPT, ReSTIR PG, Conditional ReSTIR, MCMC decorrelation and the 2026 papers other than Enhanced were not read; only abstracts or titles.
- The licences of Area-ReSTIR, conditional-restir-prototype and kajiya were not checked.
- No ReSTIR PT timings on AMD GPUs or on an RTX 3070 were found anywhere.

---

## 5. Spectral rendering + ReSTIR (how reservoirs meet EV's 4-wavelength HWSS)

### Takeaway
Published work is **thin**. The only primary source found that works through the maths is Aubry & Vorba (Wētā,
SIGGRAPH 2022 spectral course), a work-in-progress prototype for **direct light only** in the offline Manuka renderer.
Their choices:
- target = L_e · G evaluated **at the pixel's hero wavelength only**;
- the reservoir stores the hero wavelength, position, normal and time of its domain, so neighbours can evaluate p̂ of the
  source domain;
- the Jacobian of the "change of hero wavelength" is set to 1;
- the HWSS per-wavelength MIS weights become **stochastic**: the unknown pdf is approximated through the reservoir's
  weight sum.

Their key warning: with resampling you can **not** use the simple constant 1/C HWSS weights, because a hero-only target
can be zero at the hero wavelength while other lanes are non-zero. There is also a Japanese Waseda thesis (HWSS +
ReSTIR, details not extractable) and an unverified hobby repo. No real-time spectral ReSTIR paper was found.

My analysis [I] for EV (4 lanes in a `float4`, one hero per camera ray, rotation in primary-sample space, `r_u ≡ 1` in
non-dispersive scenes):
- **ReSTIR DI**: keep the wavelengths **per pixel, not part of the sample**. Use a **lane-symmetric target**
  (luminance-weighted sum over all 4 lanes). Store the pixel's wavelength random number u_λ so neighbours and the next
  frame can evaluate p̂ in the source domain. Because the target is symmetric in the lanes, all 4 HWSS techniques have
  the same pdf and EV's HWSS weights stay 1/C. Sharing wavelengths between pixels is **not needed**.
- **ReSTIR GI / PT**: the stored radiance (or the replayed path) is only valid at the wavelengths it was computed at.
  Either (a) **make u_λ part of the sample** (it travels with the reservoir; the cleanest form is ReSTIR PT's
  primary-sample space, where u_λ is just one more replayed random number), or (b) store the radiance as XYZ/RGB and
  accept "RGB at the reuse boundary", the same compromise the plan already makes for SHaRC in M9.

What breaks if you get it wrong:
- reusing per-lane radiance at the wrong wavelengths → wrong colours, i.e. bias;
- a hero-only target → zero support for other lanes, i.e. bias, plus extra colour noise;
- carrying λ with reused samples → spatially and temporally correlated colour blotches;
- wavelength-dependent refraction (water n(λ), dispersion) → shifts must replay the same λ or fail.

### Cited Findings
**Aubry & Vorba, "Sampling and Re-Sampling in Spectral Rendering" (Wētā Digital), Ch. 4 of "Practical Aspects of Spectral Data in Digital Content Production", SIGGRAPH 2022 Courses (organiser Andrea Weidlich)** — [course notes PDF (Wētā)](https://www.wetafx.co.nz/assets/Uploads/PDFs/sig-course-2022.pdf), [ACM DL](https://dl.acm.org/doi/10.1145/3532720.3535632); found via the [Blender spectral-Cycles thread, page 90](https://forum.blender.org/t/thoughts-on-making-cycles-into-a-spectral-renderer/2192?page=90)
- Course summary: "In the second part, we explore application of ReSTIR … for sampling of direct illumination within spectral renderer. This requires extending the state-of-the-art resampling framework to account for sampling over wavelengths so that previous samples are reused correctly." — syllabus
- "To our best knowledge, no previous works [provide] details on application of re-sampling in spectral renderer" (2022) — §4.1.3
- Non-uniform hero sampling (§4.2.1): a "probability-stratified mapping" φ_k(λ) = F⁻¹([F(λ) + k/C]) generalises the uniform rotation; with the "equiprobable mapping" every lane has the same pdf as the hero (Eq. 4.4). The path pdf depends only on the hero wavelength, p_X(X | λ₀) — §4.2.1
- Scope: "We experimented with re-sampling of direct illumination, however, we believe that extension to full path space would be straightforward" — §4.3
- Fresh candidates (Eq. 4.14–4.15): w(x_i) = (1/M) p̂(x_i | z_i, λ₀)/p(x_i) with **p̂(x_i | z_i, λ₀) = L_e(z_i, x_i | λ₀) G(z_i, x_i)**, "evaluated at the hero-wavelength chosen for sampling the current path"; "we do not employ spectral dependent sampling of the candidate samples"; "In practice we generate only one or two candidate samples" — §4.3.2
- Temporal reuse = reuse "over progressions" (offline passes), limited to the shutter interval; generalized Talbot resampling MIS between the current domain (z_i, λ₀ᵢ) and the previous domain (z_j, λ₀ⱼ) (Eq. 4.18–4.20); "we simply set the Jacobian of the shift mapping due to change of the hero-wavelength to 1"; hero wavelengths are "sampled independently across pixels and rendering progressions" — §4.3.2
- Spatial reuse: 3 random neighbour reservoirs merged one by one; "we also need to store all the data related to the current sampling domain such as the hero-wavelength, position, normal, and time so that we can use the reservoir in the subsequent progression" — §4.3.2
- Shading: f is "a vector function giving C contributions at wavelengths λ_k". HWSS MIS weights w_k = p_X(X|λ₀)/Σ_j p_X(X|λ_j) (Eq. 4.21) need the unknown resampled pdf, which they approximate as p(z_nee | z_i, λ_k) ≈ p̂(z_nee | z_i, λ_k)/Σ_q w(x_q) (Eq. 4.22–4.24): "the denominator … is the sum of RIS weights … not dependent on the wavelength" — §4.3.3
- Key warning: "while in hero-wavelength sampling with classical MC estimator without re-sampling, we could make a sub-optimal choice and set the MIS contribution weight … simply to constant C⁻¹ … we cannot do this in general for re-sampled estimator. We need to acknowledge the fact, that target function p̂ can be zero for some wavelengths and thus z_nee could not be sampled with such technique. Otherwise our estimator would converge to an incorrect result." And: in plain HWSS "sampling zero contribution in hero-wavelength still allows non-zero contribution in other wavelength channels. This is obviously not true for re-sampling" — §4.3.3
- Combining with BSDF-hit (unidirectional) emitter samples (Eq. 4.25–4.26): the resampled-NEE pdf estimate is itself random, so "In early progressions … the denominator can suffer from high variance", and they suggest a warm-up without unidirectional splats — §4.3.4
- Limits: "We did not include bsdf in the target function nor transmittance for simplicity … Including transmittance (or at least visibility) will be important for truly unbiased estimates"; "this part of the course is report on our current work in progress … not been thoroughly evaluated on production scenes yet" — §4.3.5

**Other spectral + resampling material**
- A Waseda University thesis (Japanese) whose table of contents covers HWSS, RIS, ReSTIR DI/GI/PT (Veach Door, Zero Day), HWSS subsurface scattering and a ReSTIR-based method, with 1-spp vs. 8-spp ground-truth comparisons. Its body text was not extractable (CID fonts), so title, author, year and method are [U] — [PDF](https://waseda.repo.nii.ac.jp/record/2001348/files/t5122f092.pdf)
- [spectral-restir](https://github.com/nathencoelhoo/spectral-restir) (MIT, Vulkan ray query): claims existing real-time ReSTIR "resample[s] light paths in RGB" and instead resamples "over wavelength as part of the path" with an "80-byte spectral reservoir" and "wavelength-aware shift mappings … the novel part"; ReSTIR DI/PT and benchmarks are still roadmap items, and no results are reported. It lists as related work a GraphiCon 2025 paper on spatiotemporal resampling for wave optics and the "Spectra" Blender add-on (ReSTIR BDPT + spectral materials). All of this is unverified.
- ReSTIR PT Enhanced on scalar targets: GRIS "samples primarily according to luminance, leaving chroma poorly importance-sampled"; vector-valued resampling weights fix part of this at almost no cost — [Lin et al. 2026 §6.3](https://research.nvidia.com/labs/rtr/publication/lin2026restirptenhanced/lin2026restirptenhanced.pdf)
- Area ReSTIR shows that **camera-side random dimensions** (subpixel position, lens position) can become part of the resampled sample — [Zhang et al. 2024](https://graphics.cs.utah.edu/research/projects/area-restir/)
- EV's HWSS (from `followup_spectral_details.md`): Eq. 11 estimator; pbrt rotates u, not λ, in primary-sample space; "In a scene with no dispersion, no spectral media and no per-λ light sampling, every pdf_k == pdf_hero, so r_u ≡ 1"; `TerminateSecondary()` at dispersive interfaces; hero-majorant distance sampling in water.

### Inferences (labelled [I] throughout; no paper covers real-time spectral ReSTIR)

**1. What is "the sample", and must wavelengths be shared?**
- Treat the pixel's wavelength set Λ = {λ₀..λ₃} like the camera ray (x₀, x₁): a per-pixel, per-frame random choice that
  is **fixed before resampling**. The course notes already treat lens/pixel randomisation as fixed context (§4.3). The
  resampled sample is then wavelength-free: a light point (DI), or a path (GI/PT).
- Reuse from a neighbour with a different Λ′ is then ordinary reuse between pixels with different target functions,
  which ReSTIR handles anyway: neighbours already have different BSDFs and normals. **Wavelengths do not have to be
  shared**, and sharing them (one Λ for the whole frame) would turn HWSS's per-pixel colour noise into whole-frame colour
  flicker [I].
- What *must* be available is the source domain's Λ′, for the MIS weights (p̂_{←j} is evaluated with the neighbour's Λ′).
  In EV that is one stored number per pixel: the hero's primary sample u_λ (16-bit is enough [E]), or the pixel's RNG
  key if u_λ can be regenerated from (pixel, frame). The previous frame's u_λ must be kept for temporal reuse.
- Cheap biased option: evaluate the neighbour's p̂ with the *current* Λ. For smooth reflectance and emission spectra the
  error is small [E]. Narrow-band emitters (sodium lamps, lasers, spectral sky peaks) are where it shows. This is the
  spectral analogue of RTXDI's "use the current frame's BVH for the previous frame" shortcut.

**2. Target function: luminance? per-wavelength? hero-only?**
- **Hero-only** (Aubry & Vorba): cheapest. But supp(p̂) can miss lanes (a red light seen with a blue hero), which forces
  their stochastic per-lane MIS (Eq. 4.22–4.24) to stay unbiased, and it importance-samples only one wavelength, which
  adds colour noise.
- **Lane-symmetric scalar** (recommended for EV): p̂ = Σ_k ȳ(λ_k) · f(x, λ_k)/p_vis(λ_k), i.e. the luminance (Y) of the
  pixel's own 4-lane estimate, or a sum such as X+Y+Z if blue/red chroma is poorly sampled. Properties:
  (i) it is > 0 wherever *any* lane of f is > 0, which is the support condition, as long as the weighting function is
  > 0 over the sampled range; ȳ is tiny near 360/830 nm, which argues for X+Y+Z or a small floor [I/E];
  (ii) **it is the same for every choice of which lane is "hero"**, because the rotated set Λ is identical for all four
  choices, so the C HWSS techniques produce the sample with the same pdf → the HWSS MIS weights stay exactly 1/C →
  EV's `r_u` plumbing stays a no-op for the resampled vertex;
  (iii) costs 4 lane evaluations of BSDF × L_e × G per candidate, which is cheap because EV's spectral maths is already
  `float4` [I].
- **Vector shading weights** (Enhanced §6.3) carry over directly: accumulate the 4-lane (or XYZ) value of each
  candidate's resampling weight and shade with the sum. Scalar weights drive the selection, vector weights reduce
  chroma noise.
- Per-wavelength separate reservoirs (4 reservoirs per pixel) are possible but quadruple memory and rays and lose HWSS's
  "one path, four wavelengths" efficiency [I]. Not recommended.

**3. GI and PT: stored radiance is wavelength-bound**
- ReSTIR GI stores L_o at the sample point, computed at the *source* pixel's Λ′. Reusing it in a pixel with Λ is wrong
  unless (a) Λ travels with the sample, or (b) L_o is stored in a wavelength-independent form.
  - **(a) Λ in the sample** (joint domain "path × wavelength"): store u_λ in the reservoir; the receiving pixel evaluates
    its primary BSDF and transmittance at the sample's Λ (cheap). λ's pdf is the same in every pixel (SampleVisible), so
    the shift of u_λ is the identity with Jacobian 1. This is unbiased and keeps full spectral fidelity.
    Cost: +2–4 B per reservoir, and **colour correlation**: a good sample's wavelengths get copied to neighbours and
    frames, so chroma noise becomes blotchy. M-capping and duplication maps limit it [I].
  - **(b) XYZ/RGB at the reuse boundary**: store L_o as XYZ (half3), re-uplift to the receiving pixel's Λ with
    Jakob–Hanika. This is biased (metamerism loses spectral detail along the reused segment), but it matches
    `plan.md` M9 ("spectral → RGB at the cache boundary") [I].
- ReSTIR PT in **primary-sample space** gives the cleanest formalism: the path is a list of random numbers; u_λ is
  simply the first one. Random replay copies it (same wavelengths, Jacobian 1); the integrand is the path tracer's own
  HWSS throughput f/p (4-lane, converted to luminance for p̂); the reconnection Jacobian uses the hero-pdf solid-angle
  densities exactly as the path tracer did. This is the spectral analogue of Area ReSTIR resampling lens and subpixel
  coordinates [I].

**4. What breaks with dispersion and spectral media**
- Water with n(λ): if the bulk surface uses n at the hero λ (one option in `followup_spectral_details.md`), refraction
  directions depend on λ₀. Then (i) reusing a path through the water surface with a *different* λ₀ gives a different
  path, so the shift must replay the same u_λ (option a) or return "undefined"; (ii) the HWSS techniques no longer have
  equal pdfs (r_u ≠ 1), so a lane-symmetric target is no longer automatically 1/C-consistent; resampling must then use
  the full HWSS MIS throughput inside the PSS integrand [I].
- `TerminateSecondary()` (dispersion → hero only) makes the lane count vary along a path. A reused sample with 1 live
  lane landing in a pixel with 4 live lanes is fine in the PSS view (the integrand is whatever HWSS computes). It is a
  bias trap in a naive "store per-lane radiance" design [I].
- Hero-majorant distance sampling in water (EV's M5 scheme) makes the path pdf λ-dependent. Volumetric reuse needs a
  volumetric shift (Volumetric ReSTIR) and is far beyond the first rungs. Keep ReSTIR to surface vertices first [I].

**5. Practical recommendation for EV**
- Build every rung in **RGB mode first** (`plan.md` M4 keeps an RGB/spectral toggle), validate against ground truth, then
  switch to spectral with:
  - DI: per-pixel Λ, lane-symmetric target, u_λ stored per pixel;
  - GI: start with option (b) (XYZ at the reuse boundary), then try option (a) as an experiment;
  - PT: PSS with u_λ replayed.
- The A/B experiment is itself novel and publishable-sized for a student project: no real-time spectral ReSTIR paper
  exists [I].

### Gaps
- No peer-reviewed real-time spectral ReSTIR paper was found; Aubry & Vorba is a 2022 work-in-progress for DI only.
- The Waseda thesis content and the spectral-restir repo's `docs/DESIGN.md` were not read.
- The claim that a lane-symmetric target keeps HWSS weights at 1/C is my derivation (it follows from the rotation
  symmetry of the wavelength set). The tutor should check it with a CPU test: render a narrow-band emitter both ways and
  compare to a non-resampled HWSS reference.

---

## 6. Practicalities for EV: memory, pass structure, dependencies, denoisers, caches, artefacts, timings, difficulty

### Takeaway
All ReSTIR rungs fit EV's D2 design: **full-screen compute passes with inline `RayQuery`** for shadow and visibility rays
(GI/PT also trace a few bounce rays). No `DispatchRays` is required. Memory at 1080p is moderate (DI ≈ 70–130 MB, GI ≈
0.5 GB as published, PT ≈ 0.3–0.5 GB [E]), affordable even on the 8 GB RTX 3070. Temporal reuse depends on M8b (G-buffer
+ motion vectors + the previous frame's G-buffer). Spatial-only reuse and RIS work earlier, in reference mode, using the
M2 primary-hit buffers. The main artefacts are correlation and boiling, darkening from biased MIS, disocclusion noise,
lag after light changes, and fireflies from short reconnections. Denoisers need care because ReSTIR noise is
correlated: NRD's ReLAX was designed for RTXDI; SVGF's variance estimate is fooled. Radiance caches (SHaRC/NRC) slot in
as the **path terminator** that produces the reused sample's radiance.

### Cited Findings
- Reservoir sizes: ReSTIR DI stores y, M, W (2020 §5); ReSTIR GI 48 B per reservoir and 475 MB at 1080p for 5 buffers (2021 §5); ReSTIR PT 88 B (2022 §8.2) → 64 B (2026 §6.2.1). RTXDI checkerboarding halves storage — sources in §2–§4 above.
- Unbiased temporal MIS needs last frame's target function, possibly with visibility in last frame's scene (previous BVH) — [course notes §4.3, §7.1.4](https://intro-to-restir.cwyman.org/presentations/2023ReSTIR_Course_Notes.pdf)
- Disocclusions: no temporal predecessor → reset confidence; dual motion vectors reduce disocclusion noise (Enhanced §6.4); reservoir splatting fixes back-projection failures (2025); multi-layer splatting for surfaces that reappear (2026) — §4 above
- Denoisers:
  - NRD: RELAX "has been designed for RTXDI" and "works well with signals produced by RTXDI or very clean high RPP signals";
  - "when using advanced sampling techniques (like RIS, MIS, RESTIR) hitT of a chosen sample cannot be simply passed to NRD";
  - "always ignore 0 hitT produced by RESTIR in disocclusions";
  - history-confidence inputs (`IN_DIFF_CONFIDENCE`, `IN_SPEC_CONFIDENCE`) "are essential to preserve the responsiveness";
  - REBLUR/RELAX diffuse+specular cost 2.55/3.25 ms at 1440p on an RTX 4080.

  — [NRD README](https://github.com/NVIDIA-RTX/NRD)
- SVGF and correlation: "Correlated input may cause artifacts due to an inexact estimate of variance" — [ReSTIR GI §6.1](https://d1qx31qr3h6wln.cloudfront.net/publications/ReSTIR%20GI.pdf); "Correlation artifacts make denoising ReSTIR challenging, as artifacts get detected as signal" — [Enhanced §5](https://research.nvidia.com/labs/rtr/publication/lin2026restirptenhanced/lin2026restirptenhanced.pdf)
- RTXDI integration step 9: compute a denoiser confidence channel — [Integration.md](https://github.com/NVIDIA-RTX/RTXDI/blob/main/Doc/Integration.md)
- Caches with ReSTIR in practice: kajiya's irradiance cache supplies multi-bounce radiance to ReSTIR GI samples — [kajiya](https://github.com/EmbarkStudios/kajiya/blob/main/docs/gi-overview.md); NRC paper: ReSTIR + NRC reduces MRSE by 1–2 orders of magnitude (cited in `industry_realtime_path_tracing.md` §1); shipped recipe "RTXDI → 1–2 bounces (sometimes ReSTIR GI) → SHaRC/NRC" (same note, §2).
- Offline/reference accumulation: GRIS guarantees convergence offline by "rendering multiple independent frames" (GRIS §8.3); "When a noise-free unbiased image is desired, accumulating it with temporal reuse disabled is typically more efficient" (Enhanced).

**Published timings (GPU named)**

| Method | GPU, resolution | Cost | Quality gain | Source |
|---|---|---|---|---|
| ReSTIR DI biased/unbiased | RTX 2080 Ti (Titan RTX for 3.4M lights), 1080p | 15 ms (Zero Day), 42–50 ms (Amusement Park), ≤ 8 rays/px, excl. 1–2 ms G-buffer | 35–65× / 6–60× faster at equal error | [Bitterli 2020](https://benedikt-bitterli.me/restir/bitterli20restir.pdf) |
| ReSTIR GI (UE4) | RTX 3090, 1080p | +8–18 ms/frame; e.g. 8.9 ms (biased) vs 8.0 ms 1-spp PT | MSE 9.3–166× lower (unbiased) | [Ouyang 2021](https://d1qx31qr3h6wln.cloudfront.net/publications/ReSTIR%20GI.pdf) |
| ReSTIR GI passes | RTX 3080, 1920×1050 | initial 3.2 / 4.2 ms (1/2 bounces) + reuse 4.6 ms | — | same, Fig. 8 |
| kajiya biased ReSTIR GI | RX 6800 XT, 1080p (half-res tracing) | indirect diffuse ~2.3 ms; whole frame 8.4 ms | — | [kajiya](https://github.com/EmbarkStudios/kajiya/blob/main/docs/gi-overview.md) |
| ReSTIR PT (2022) | RTX 3090, 1080p | 40–80 ms/frame; Cornell glossy 5 ms (reconnection) / 12 ms (hybrid) | MAPE 24–75 % lower than PT; offline up to 14.4× / 10× faster convergence | [GRIS](https://d1qx31qr3h6wln.cloudfront.net/publications/sig22_GRIS.pdf), [Enhanced §1](https://research.nvidia.com/labs/rtr/publication/lin2026restirptenhanced/lin2026restirptenhanced.pdf) |
| ReSTIR PT Enhanced | RTX 5880 Ada, 1080p | 35.7 → 13.0 ms (optimisations) → 15.5 ms (all quality features), 4-scene average | lower FLIP (e.g. 0.321 → 0.263) | [Enhanced](https://research.nvidia.com/labs/rtr/publication/lin2026restirptenhanced/lin2026restirptenhanced.pdf) |

### Inferences
**Pass structure in EV (compute + `RayQuery`, render-graph passes)** [I]
- DI (real-time mode, after M8b):
  1. optional light presampling / light tiles;
  2. **Initial candidates** CS: M light candidates per pixel (alias tables: sun disk, sky texels, point lights), streaming RIS on the unshadowed target, one shadow `RayQuery` for the winner;
  3. **Temporal** CS: motion vector → previous pixel; G-buffer similarity test; merge with c_cap 20; MIS needs the previous G-buffer (normal, depth, material) to evaluate last frame's p̂;
  4. **Spatial** CS: 3–5 neighbours in a 30 px disk, pairwise MIS; unbiased mode adds one shadow ray per neighbour;
  5. **Shade** CS: shadow ray + BSDF, write diffuse/specular radiance (+ hit distance, confidence) for the denoiser; ping-pong the reservoir buffers.
- GI: an initial-sample CS (bounce ray + NEE at x_s + optional short path → SHaRC/NRC), then temporal, spatial and shade CS, with validation every N frames.
- PT: initial path-tree CS (RNG seed + reconnection vertex), temporal and spatial CS with hybrid shift (split replay and reconnection kernels plus compaction later), shade.
- Everything is a few `Dispatch` calls with UAV barriers between passes, which the render graph already handles. No hit-group shader tables.

**Memory at 1920×1080 (2,073,600 pixels)** [E]

| Rung | Bytes/reservoir | Buffers | Total |
|---|---|---|---|
| ReSTIR DI | 32 (EV design: light ID/uv 8, W 4, c 2, p̂ cache 4, u_λ 2, padding) | 2 (ping-pong) | ≈ 133 MB; 66 MB with 16 B packing |
| ReSTIR GI | 48 (paper) + 4 (u_λ and 4th radiance lane in spectral) | 5 (paper) | ≈ 500–540 MB; 3 buffers ≈ 320 MB |
| ReSTIR PT | 64–88 | 2–3 | ≈ 270–550 MB |
| Previous G-buffer (for temporal MIS) | ≈ 16–24 | 1 | ≈ 33–50 MB |
| Previous TLAS (unbiased temporal visibility) | — | 1 | scene-dependent; the ocean BLAS is the big one (D6) |

All fit next to the path tracer on the 8 GB 3070, but GI/PT buffers belong in the `performance.md` VRAM column [E].

**Mode interplay with D7/D8** [I]
- Reference mode (accumulation): use **RIS and spatial-only reuse per frame** (independent chains), so accumulation
  converges to ground truth. Temporal reuse makes frames correlated, and accumulating correlated frames converges slowly
  or to a biased answer. The D7 scene-version counter must also clear reservoirs.
- Real-time mode: full spatiotemporal reuse; scene changes are handled by temporal MIS, M-caps and validation, not by
  resetting.
- Running ocean time (M8b) means the previous frame's surface differs. That calls for ReSTIR GI-style sample validation,
  and the temporal MIS needs the previous G-buffer of the displaced grid.

**Denoisers** [I]
- OIDN (M7) is trained on independent MC noise [T]. Feeding it temporally reused ReSTIR frames may produce blotches.
  Prefer spatial-only ReSTIR when OIDN is used in reference mode, and test both.
- NRD ReLAX is the cross-vendor real-time choice for ReSTIR DI output.
- An own SVGF needs either decorrelated input (low c_cap, duplication maps) or a variance estimate that does not assume
  independence.

**Caches** [I]
- SHaRC/NRC as path terminator: (i) the GI sample's L_o = NEE at x_s + short path that ends in the cache; (ii) ReSTIR PT's
  initial path trees end in the cache. Comparing "PT + SHaRC", "ReSTIR GI + SHaRC" and "ReSTIR GI + NRC" at equal time is
  exactly the M9/M10 measurement the plan wants.

**Known artefacts and their standard fixes** [I, from the sources above]

| Artefact | Cause | Fix |
|---|---|---|
| Blotchy, boiling noise (correlation) | the same sample reused by many pixels and frames | lower c_cap; duplication maps (Enhanced); fewer spatial passes; decorrelation (Conditional ReSTIR, MCMC); denoiser tuning |
| Darkening near shadow and normal edges | biased 1/M MIS, visibility reuse | unbiased MIS (extra shadow rays) or pairwise MIS; neighbour rejection by G-buffer only |
| Brightening | MIS shortcuts that assume p_j = 0, firefly replacement | correct p̂_{←j}; bias-reduction clamps |
| Disocclusion noise | no history | reset c; more spatial candidates there; dual motion vectors; reservoir splatting |
| Lag/ghosting after light or geometry changes | stale stored radiance or samples | sample validation every N frames (GI: 6); temporal MIS with last frame's data |
| Fireflies at corners | short reconnection segments (singular geometry term) | distance / footprint threshold; Jacobian clamp (biased) |
| Colour noise | scalar luminance target | vector-valued shading weights; better target (X+Y+Z) |

**Difficulty estimates (for this student, with tutor support)** [E]
- RIS-only DI: 2–4 days.
- Spatial reuse + unbiased MIS: 1 week.
- Temporal + motion vectors + previous-frame MIS: 1–2 weeks (after M8b).
- ReSTIR GI (reconnection + Jacobian + validation): 2–4 weeks.
- ReSTIR PT (PSS RNG, hybrid shift, lobe tags, pairwise MIS): 2–3 months.
- Spectral variants: +20–40 % each, mostly validation work.

### Gaps
- No ReSTIR timings exist for the RX 9070 XT or the RTX 3070. All EV numbers above are estimates to replace with
  measurements.
- Whether OIDN degrades on correlated ReSTIR input was not tested in any source found ([I]).
- RTXDI's internal pass/ray-type choice (RayQuery vs. TraceRay) was not verified.

---

## 7. Options for EV

### Takeaway
A ladder of six rungs, each a valid, measurable stopping point. The first two need nothing beyond M3/M4 and can live in
reference mode. Temporal reuse waits for M8b. ReSTIR GI pairs naturally with M9/M10 (caches as path terminators). ReSTIR PT
is the stretch goal after O2. Spectral is a toggle on every rung (RGB first, then spectral), not a separate rung.

### Inferences (proposed ladder; all [I] / [E])

| Rung | What | Where in the plan | Needs | Measure (both GPUs, `performance.md`) |
|---|---|---|---|---|
| **R0 RIS-only DI** | Streaming RIS over M light candidates (sun, sky texels, point lights), one shadow ray; no reuse | Right after M3 (NEE + MIS exist) or as part of O1; reference mode | M3; alias tables (M3 env-map sampling) | Converges to the same image as NEE+MIS (unbiasedness check); **equal-time FLIP/MSE vs ~16k-spp reference** at 1/4/16/64 spp; cost per candidate (ms vs M = 1..64) |
| **R1 Spatial reuse** | Reservoirs + spatial merge (3–5 neighbours, 30 px), 1/M → generalized balance → pairwise MIS; biased vs unbiased toggle | After M4 (so it is born with the RGB/spectral toggle), before or with M5; reference mode, independent chains per frame | M2 primary-hit buffers (normal, depth, material ID) | Equal-time error vs R0 and vs plain NEE; **bias map** (mean of 1k frames − reference) for 1/M vs pairwise; correlation (spatial autocorrelation of the error); cost per pass |
| **R2 ReSTIR DI (temporal)** | Temporal reuse with motion vectors, c_cap, previous-frame MIS; visibility reuse; NRD ReLAX or own SVGF | **With M8b** (real-time mode); replaces "ReSTIR DI" in M11 | M8b G-buffer + motion vectors (incl. ocean) + previous G-buffer; optional previous TLAS | Frame-time budget; equal-time error vs R1 *during camera motion* (20-frame fly-through, as in the 2020 paper); **temporal stability** (frame-to-frame FLIP of a static view, flicker metric); disocclusion recovery frames; many-light test scene scaling (10³–10⁵ lights) |
| **R3 ReSTIR GI** | Bounce sample points, reconnection Jacobian, validation every N frames, Lambertian-L_o (biased) → full re-evaluation (unbiased) | **Between M8b and M9**; then re-run with the GI sample's radiance from SHaRC (M9) and NRC (M10) | R2 infrastructure; short path tracer; later SHaRC/NRC | Equal-time error vs "1–2 bounce PT" and vs "PT + SHaRC"; temporal lag after a sun jump (frames to converge); memory; cost of initial / reuse / validation passes |
| **R4 ReSTIR PT (reconnection shift)** | Whole-path reservoirs in PSS, reconnection at x₂, pairwise MIS | After M10/O2 (stretch) | Replayable counter-based RNG (decide in M3!), lobe tags, previous-frame data | Glossy/refractive scenes (ocean surface, helmet) vs R3; MAPE/FLIP equal time; reservoir bandwidth |
| **R5 ReSTIR PT (hybrid shift) + Enhanced tricks** | Random replay through glossy vertices, footprint thresholds, paired neighbours, duplication maps, vector weights, unified DI+GI | M11 / O2-style optimisation track | R4 | Replay the Enhanced Table 1 ablation on EV's scenes: per-step ms and FLIP, on the RX 9070 XT and the RTX 3070 |
| Spectral toggle on R0–R5 | DI: per-pixel Λ + lane-symmetric target + stored u_λ; GI: XYZ-boundary (b) then Λ-in-sample (a); PT: u_λ replayed in PSS | From R1 on (M4 exists) | M4 | RGB vs spectral **cost overhead**; **chroma noise** (FLIP colour error, ΔE of a grey/Macbeth chart); bias vs a non-resampled HWSS reference with a narrow-band light; target choice A/B (hero-only vs lane-sum vs X+Y+Z) |

**Comparison of the rungs** [E unless cited]

| | R0 RIS | R1 Spatial | R2 ReSTIR DI | R3 ReSTIR GI | R4/R5 ReSTIR PT |
|---|---|---|---|---|---|
| Improves | direct light | direct light | direct light | 1–2 bounce indirect (diffuse/rough) | all bounces incl. glossy, refraction |
| Extra rays/px/frame | 1 shadow | 1 (+k unbiased) | 1–2 (+k) | 1 bounce + 1 NEE (+ validation) | path tree + shifts |
| Memory @1080p | ~0 | ~70–130 MB | ~130 MB + prev G-buffer | ~0.3–0.5 GB | ~0.3–0.5 GB |
| Expected gain vs plain PT at equal time | small (few lights in EV) | medium | large for sky visibility / many lights; 6–65× in the 2020 paper's many-light scenes | 9–166× MSE in the paper's scenes | 24–75 % lower MAPE vs PT+ReSTIR DI (2022) |
| Main artefacts | none (unbiased) | darkening (biased) | + boiling, disocclusion | + lag, Lambertian bias, glossy failure | + correlation, colour noise, cost |
| Works in reference mode | yes | yes (independent chains) | spatial part only | spatial part only | spatial part only |
| Depends on | M3 | M2/M3/M4 | M8b | M8b (+M9/M10) | M8b + replayable RNG |
| Difficulty | low | low–medium | medium | medium–high | high |
| AMD 9070 XT + RTX 3070, plain HLSL | yes | yes | yes | yes (kajiya proves AMD) | yes in principle; no public AMD data |

**Recommendation** [I]
- Adopt R0 + R1 as an **optional side track next to M3/M4**. They are cheap, they teach the UCW/MIS maths that
  everything else needs, and they give a spatial-only ReSTIR that is safe for reference accumulation.
- Make **R2 part of M8b** instead of M11, because real-time mode without good direct-light sampling is the weakest part
  of the plan.
- Put **R3 between M8b and M9** so SHaRC and NRC can be measured as ReSTIR GI terminators.
- Keep **R4/R5 optional (M11)**.
- Decide **now (M3)** to use a counter-based, replayable RNG.

### Integration notes for knowledge.md, plan.md and README.md (proposals only; nothing has been edited)
- `README.md` index: add a row for `followup_restir.md` (date 2026-11-22). Topic: ReSTIR theory (course notes read),
  DI/GI/PT papers with numbers, 2023–2026 follow-ups, implementations and licences, spectral ReSTIR analysis,
  ladder R0–R5. It **updates** `industry_realtime_path_tracing.md` §2: RTXDI 3.x now ships ReSTIR PT, and ReSTIR PT
  Enhanced (I3D 2026) has published per-pass timings. It **closes** the [U] on the course link in `knowledge.md`.
- `knowledge.md` §10 M8b/M11: replace "[Gentle Introduction to ReSTIR] [U]" with the notes PDF link; add the validation
  order from Tip 4.4, the c_cap 20 default, pairwise MIS and the "reference mode = spatial-only chains" rule.
- `knowledge.md` §3.9 (samplers): add "RNG must be replayable (counter-based, keyed by pixel/frame/bounce/dimension)
  for ReSTIR PT random replay".
- `plan.md`: optional rungs R0/R1 next to M3/M4; move "ReSTIR DI" from M11 into M8b as R2; R3 between M8b and M9;
  ReSTIR PT stays in M11.
- `knowledge.md` §12 open questions:
  - (a) Does the lane-symmetric target keep HWSS weights at 1/C? (CPU test);
  - (b) Does OIDN handle temporally correlated ReSTIR input?
  - (c) ReSTIR timings on RX 9070 XT / RTX 3070.
