# Path tracing performance log

Measurements for every milestone and every hot-path change. How and when to measure: `plan.md`, "Profiling and
performance". Newest results at the bottom of each table.

## Machines

| Name | GPU | Driver | CPU | Notes |
|---|---|---|---|---|
| AMD (main) | Radeon RX 9070 XT | _fill in_ | _fill in_ | |
| NVIDIA | GeForce RTX 3070 (8 GB) | _fill in_ | _fill in_ | |

## Benchmark shots

| Shot | File | What it tests |
|---|---|---|
| _e.g._ ocean-noon | `scenes/bench/ocean_noon.evscene` | Frozen ocean, sun + sky, default camera |
| _e.g._ materials-N | `scenes/bench/materials_N.evscene` | 1 / 4 / 16 / 64 distinct materials (M5c) |

## Milestone baselines

| Date | Milestone | Machine | Shot | Res | spp / bounces | Pass times (ms) | Frame (ms) | Rays/s | VRAM | Quality @ time | Notes |
|---|---|---|---|---|---|---|---|---|---|---|---|

## A/B changes

| Date | Change | Machine | Shot | Before | After | Quality impact | Kept? |
|---|---|---|---|---|---|---|---|
