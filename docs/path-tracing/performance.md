# Path tracing performance log

Measurements for every milestone and every hot-path change. How and when to measure: `plan.md`, "Profiling and
performance". Newest results at the bottom of each table.

## Machines

| Name | GPU | Driver | CPU | Notes |
|---|---|---|---|---|
| AMD (main) | Radeon RX 9070 XT (16 GB) | Adrenalin 26.11.2 | AMD Ryzen 5 3600 (6 cores) | VRAM budget seen by EV: 15419 MB. **CPU upgrade planned** → add a new machine line ("AMD (main, new CPU)") and re-measure the M0 row after the swap: the raster scene is CPU-bound, so its frame time will change |
| NVIDIA (laptop) | GeForce RTX 3070 Ti Laptop GPU (8 GB) | _fill in_ | _fill in_ | on the charger when measuring |

## Benchmark shots

| Shot | File | What it tests |
|---|---|---|
| _e.g._ ocean-noon | `scenes/bench/ocean_noon.evscene` | Frozen ocean, sun + sky, default camera |
| _e.g._ materials-N | `scenes/bench/materials_N.evscene` | 1 / 4 / 16 / 64 distinct materials (M5c) |

## Milestone baselines

| Date | Milestone | Machine | Shot | Res | spp / bounces | Pass times (ms) | Frame (ms) | Rays/s | VRAM | Quality @ time | Notes |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 2026-11-23 | M0 (raster only, no PT) | AMD (main) | default ocean scene, camera untouched | 1237×712 viewport | – | total 0.35 (Render Graph); largest: ocean surface 0.18, skybox 0.013, tonemap 0.012; ocean spectrum 0.003, FFT displace H/V 0.008, slope H/V 0.008 | avg 0.8, max 1.6 (~1200–1300 FPS); later reading steady 1400–1500 FPS (~0.7 ms) – difference = open panels / longer warm-up (CPU-bound, so UI cost shows) | – | 1351 / 15419 MB | – | Ryzen 5 3600, Release, vsync off, normal clocks, 30 s warm-up, read by eye (jumps a lot at this frame rate). CPU-bound: frame 0.8 ms vs GPU 0.35 ms |
| 2026-11-23 | M0 (raster only, no PT) | NVIDIA (laptop) | default ocean scene, camera untouched | 1212×681 viewport | – | total ≈ 0.9 avg (Render Graph); largest: ocean surface 0.26, skybox 0.030, slope/displacement FFT passes ≈ 0.029 each | avg 1.6, max 3.0 (~620 FPS) | – | 1366 / 7250 MB | – | Release, vsync off, normal clocks, on the charger. Less CPU-bound than the main PC (GPU 0.9 of 1.6 ms). Driver + CPU _fill in_ |

## A/B changes

| Date | Change | Machine | Shot | Before | After | Quality impact | Kept? |
|---|---|---|---|---|---|---|---|
| 2026-11-25 | Ocean slope + foam mip chains: engine `GenerateMips` (2 per cascade, ~3 dispatches each, views + texture wrapper created every call) → `ocean_mips.hlsl`, one dispatch per cascade for both chains (groupshared reduction to mip 6, last group via atomic counter builds mips 7–9) | AMD (main) | default ocean scene, camera untouched, maximised editor | Release (median of 3 runs, 600 warm-up + 3000 frames each): mips 4 passes 0.039 ms GPU (p95 0.055), graph GPU 0.518 ms, frame 1.03 ms. Debug: mips 0.070 ms, frame ~13.2 ms | Release: mips 0.029 ms GPU (p95 0.031), graph GPU 0.458 ms, frame 0.86 ms (−17 %). Debug: mips 0.043 ms, frame 7.5–9.4 ms | None expected (same 2×2 box filter); far-view captures A/B look the same, debug layer silent | yes |
| 2026-11-26 | RC-0 bilinear fix (`MergeWithUpperFixed`: one near segment per upper probe = 4 marches per texel on c0–c4) vs the plain merge (1 march + bilinear far) | AMD (main) | RC-0 canvas, "Clear + scatter lights" seed 1234, 1000 lights r = 2, t0 = 2, Overlap off, Fluence view | Fix off, cascades c0–c5: 0.02 / 0.034 / 0.22 / 0.92 / 1.1 / 0.0 ms = **2.3 ms** (2 lights: 0.02 / 0.03 / 0.24 / 1.17 / 2.9 / 0.0 = 4.4 ms) | Fix on: 0.075 / 0.175 / 0.83 / 3.7 / 4.6 / 0.0 ms = **9.4 ms** (2 lights: 17.7 ms); ≈ 4.1× in both, matches the 4 marches | Ring at the c3/c4 border (~170 px) gone (screenshots `Resources/Screenshots/BilinearOff.png` / `bilinearOn.png`); fix also closes the thin-wall leak without Overlap | yes (RC-0 toy; laptop row later) |
