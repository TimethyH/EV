# Path tracing build log

One entry per step: date, milestone, what was built (and by whom), results/measurements, problems, next step.
Newest entry at the bottom. Decisions and roadmap: `plan.md`. Tutor reference: `knowledge.md`.

## 2026-11-18 – Research, decisions, branch setup (before M0)
- **Research**: industry real-time path tracing, offline/spectral rendering (hero wavelength sampling), OptiX denoiser
  and CUDA/D3D12 interop, Neural Radiance Cache, path tracing an FFT ocean, and a full read of EV's architecture.
  Report and notes in `research/`.
- **Follow-up research** on the uncertain points (AMD HIP compute + OIDN on Windows, RTX 3070 / OptiX specifics,
  spectral implementation details, DX12/DXR practicalities, EV architecture on this branch): `research/followup_*.md`.
- **Decisions D1–D18** made (see `plan.md`). Key changes against the report: development mainly on the AMD RX 9070 XT,
  so GPU compute is one CUDA-style source built for HIP and CUDA, OIDN is the first denoiser, the NRC is self-written
  (NVIDIA's SDK only as reference on the RTX 3070).
- **Branch** `path-tracing` created from `origin/scene-save-load`; the handoff note about the IBL bake fix
  (`e738ba0`, from `render-graph`) was cherry-picked so the TODO is not lost.
- **Next**: M0 – capabilities and plumbing (written by the developer).
