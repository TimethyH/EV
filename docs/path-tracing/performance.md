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

## A/B changes

| Date | Change | Machine | Shot | Before | After | Quality impact | Kept? |
|---|---|---|---|---|---|---|---|
