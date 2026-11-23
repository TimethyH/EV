# EV – notes for Claude

EV is a DX12 C++ engine (static lib `EV-Engine`) with apps on top (`OceanRenderer` = the main app; `ShaderArt` and
`DadsBirthday` are not used for current work). Active work: the **path tracer** on branch `path-tracing`.

## Path tracer: how we work
- **The developer writes all the code.** Claude tutors: knowledge first, then a skeleton with exactly ONE `TODO(human)`,
  then reviews the developer's change (`git diff`). Exception: tasks the developer explicitly hands to Claude.
- Every step is issued in three parts: (1) what we build + why, (2) the knowledge, with concrete reading pointers
  (section / page / paragraph + what to look for), (3) the `TODO(human)` task.
- After a review passes, Claude removes the `TODO(human)` line and instruction-only comments (explanations stay).
- After every step: `docs/path-tracing/build-log.md` entry, Patroni milestone board (task ticks + a card in the
  "Where I struggled" section under the task list), and `plan.md` / `knowledge.md` if the plan changed.
- Commit + push only when the developer asks; never commit `EV-Engine/imgui.ini`. No co-author line.

## Queued Claude tasks (run only when the developer says they're NOT developing)
- **Persistent render-graph textures ("option C")**: `docs/tasks/persistent-render-graph-textures.md` – the graph
  owns textures that live across frames (`CreatePersistent`), flags derived from passes; migrate EV's own
  `CreateTexture` call sites. Handed to Claude by the developer 2026-11-23.

## Where things are (read these instead of searching)
| What | Where |
|---|---|
| Plan, decisions D1–D18 + G1–G6, roadmap M0–M11 + GI track | `docs/path-tracing/plan.md` |
| What happened per step (newest at the end) | `docs/path-tracing/build-log.md` |
| Tutor reference (~2900 lines – jump by heading, don't read whole) | `docs/path-tracing/knowledge.md` |
| Per-milestone tutor entry (goal, teach first, files, order, verify, pitfalls, read) | `knowledge.md` §10 "### M0 …", "### M1 …" |
| Engine architecture for path tracing / hook points / gap table | `knowledge.md` §2.1, §2.5, §2.2 |
| DX12/DXR practicalities (Agility, DXC, RayQuery, AS, tools, WARP) | `knowledge.md` §6.1–6.10 |
| Theory (MC, NEE, MIS, GGX, spectral, water) | `knowledge.md` §3, §4, §5 |
| Profiling routine + measurements | `knowledge.md` §10.1, `docs/path-tracing/performance.md` |
| All raw research + the original report | `docs/path-tracing/research/` |
| Older editor/IBL notes | `docs/editor-handoff.md`, `docs/render-graph.md` |

GI-track milestones (RC-0, R0/R1, M8c–M8e, M9b/M9r/M9c, M9x) are in `plan.md` but have **no tutor entry in
`knowledge.md` yet** – write one when the milestone comes up.

## Code map (by function – line numbers drift, search for the name)
- Device, adapter, caps, DRED, frame loop: `EV-Engine/EV-Engine/source/core/application.cpp`
  (`Application::Initialize`, `CreateAdapter`, `CreateDevice`, `CheckFeatureSupport` wrapper, `QueryRenderCaps`
  (fills `RenderCaps`, read via `GetRenderCaps()`), `LogLoadedD3D12Core`,
  `GetBreadcrumbOpName`, `GetDeviceRemovedReport`) + `header/core/application.h`
- Agility SDK exports macro: `header/core/agility_sdk.h`, expanded in `OceanRenderer/source/main.cpp`
- Command lists, queues, descriptors, render graph, PSOs: `header/DX12/` + `source/DX12/`
- Ocean app (scene, passes, editor panels via `AddPanel`): `OceanRenderer/source/ocean_scene.cpp`
- Agility headers + DLLs: `EV-Engine/EV-Engine/thirdparty/agility/` (`include/` first on the include path).
  `<d3dx12.h>` resolves to `thirdparty/DirectXTex/d3dx12.h`; the split `thirdparty/d3dx12*.h` are unused.

## Build facts
- Solution `EV-Engine/EV-Engine.slnx`, x64 Debug/Release. CI (`.github/workflows/windows-build.yml`) builds Debug with
  `msbuild <project>.vcxproj /p:Configuration=Debug /p:Platform=x64 /p:SolutionDir=<repo>\EV-Engine\` and runs on WARP.
- Debug builds break on D3D12 debug-layer **warnings** (info queue in `CreateDevice`), so a run that reaches the
  scene means the debug layer stayed silent.
- The Agility runtime must load from `x64\<Config>\D3D12\D3D12Core.dll`; the Output window line
  `Loaded D3D12Core.dll from: …` is the proof.
- Hardware: main dev PC = AMD RX 9070 XT (no CUDA/OptiX); laptop = NVIDIA RTX 3070 Ti Laptop GPU (8 GB, cc 8.6;
  "the 3070" in the docs). Laptop Debug crash at swapchain creation (#989 from `dxgi.dll`) fixed 2026-11-23 by
  denying `D3D12_MESSAGE_ID_DEPRECATED_API` in `CreateDevice` (verified on the laptop, incl. resize).
