# EV engine on the `path-tracing` branch: corrections and additions

Companion to `ev_engine_architecture.md`, which was written against `render-graph` (HEAD `e738ba0`).
This file checks that analysis against branch **`path-tracing`** and adds what the new commits mean for a path tracer.
Nothing in the repo was modified except this file. Paths are relative to `T:\Projects\DX12\EV` unless absolute;
line numbers are for `path-tracing`.

Branch facts:
- `path-tracing` HEAD `6cf3e9b`, common base with `origin/render-graph` is `dc62eb4`.
- New on top of the base: `aac3d33` (Mesh remembers model file + index), `5f2ffc2` (nlohmann/json 3.11.3), `20e444b`
  (editor scene files `.evscene`), `a04f33b` (play mode), `3f3b7c6` (OceanRenderer scene files and play mode),
  `95acb22` + `2add933` (docs), `6cf3e9b` (cherry-pick of `e738ba0`, the IBL TODO in the handoff notes).
- `origin/render-graph` has nothing that `path-tracing` lacks (its only extra commit `e738ba0` is the same change as `6cf3e9b`).
- Working tree: clean except the untracked `docs/path-tracing/`.

Short version:
- 16 files changed (`git diff origin/render-graph path-tracing --stat`). Most of the change is in the **editor** (scene files,
  play mode) plus `json.hpp`. The render graph, DX12 core (device, command lists, descriptors, resources), shaders, the
  editor's render graph panel and the viewport toolbar are **unchanged**.
- `OceanRenderer`'s frame (`RecordRenderGraph`) is unchanged. Only its line numbers moved (+9 lines because of the new camera callback).
- New things a path tracer can use: `Mesh::Source` (a stable "where did this mesh come from" key), scene files (a place for
  path tracer settings and reference camera shots, after a small extension), play mode (full-window viewport at window size),
  `SetCameraChangedCallback` and `SetPlayModeCallback`.
- New pitfalls: the scene file stores the camera FOV, but `Ocean::ResizeViewport` sets the FOV back to 45° on every viewport
  resize, and entering play mode is a resize. The ocean spectrum is seeded from `std::random_device`, so the ocean cannot be
  reproduced between runs. Ocean time, ocean/light parameters and tonemap settings are not saved. OceanRenderer does not use
  the play-mode callback.

---

## 1. Line reference check

### 1.1 Still valid (files byte-identical between `origin/render-graph` and `path-tracing`)
`git diff --stat` lists only these files as changed: `EV-Engine.vcxproj`, `EV-Engine.vcxproj.filters`, `header/DX12/scene.h`,
`header/editor/editor.h`, `header/resources/mesh.h`, `source/DX12/scene.cpp`, `source/editor/editor.cpp`,
`source/editor/editor_assets.cpp`, `source/editor/editor_internal.h`, `source/editor/editor_play.cpp` (new),
`source/editor/editor_scene_file.cpp` (new), `thirdparty/nlohmann/*` (new), `OceanRenderer/include/ocean_scene.h`,
`OceanRenderer/source/ocean_scene.cpp`, `docs/editor-handoff.md`.

So **every reference into any other file is unchanged**. That covers all references into:
`source/core/application.cpp` / `header/core/application.h`, `command_list.*`, `command_queue.*`, `resource_state_tracker.*`,
`descriptor_allocator.h`, `dynamic_descriptor_heap.*`, `root_signature.cpp`, `resource.cpp`, `shader_resource_view.cpp`,
`texture.cpp`, `upload_buffer.*`, `shader.cpp`, `shader_library.cpp`, `shader_compiler.cpp`, `render_graph.h/.cpp`,
`swapchain.*`, `camera.cpp`, `effect_pso.cpp`, `sdr_pso.cpp`, `pano_cubemap_pso.cpp`, `scene_visitor.cpp`, `GUI.cpp`,
`model_preview.cpp`, `texture_preview.cpp`, `helpers.h`, `vertex_types.h`, `material.h`, `gpu_timer.h`,
`OceanRenderer/source/main.cpp`, `ocean_pso.cpp`, `compute_pso.cpp`, all `.hlsl` files, `OceanRenderer.vcxproj`,
`ShaderArt.vcxproj`, `EV-Engine.slnx`, `.github/workflows/windows-build.yml`, `docs/render-graph.md`.

Spot checks on `path-tracing` (they match the old text): `application.cpp` `Initialize` 353-439, `D3D12CreateDevice` 504,
break on WARNING 516, `++m_frameCount` commented out 993, `CreateBuffer` 1397, `CreateGUI` 1402; `application.h` Device13 at
66/195/210; `command_list.cpp` `CreateCommandList` 47, `CopyBuffer` 202-264, `LoadTextureFromFile` 303, `CreatePlane` 984,
`SetPipelineState` 1261, `Draw` 1518, `Dispatch` 1542, `TrackResource` 1623, `LoadSceneFromFile` 1718;
`dynamic_descriptor_heap.cpp` assert 69; `resource.cpp` 109; `shader_library.cpp` `g_entryPoint` 18; `swapchain.cpp` 189;
`render_graph.cpp` 22, 529, `Execute` 584, `ExecutePass` 630; `render_graph.h` `RGAccess` 76, `RGPassContext` 191,
`RGPassBuilder` 223, `RenderGraphDebugInfo` 254 (`raster` 266), `Create`/`Import` 301/306; `scene_visitor.cpp` 16-46;
`GUI.cpp` `GetTextureID` 212/222; CI workflow lines 3-7, 14, 33-56, 58-65, 67-128 (`EV_USE_WARP` 73).

### 1.2 Corrections (old ref -> new ref)

**`EV-Engine/EV-Engine/EV-Engine.vcxproj`** (+2 lines after the new `ClCompile` entries at 55-56; some old numbers were already off by 1-2)

| Old ref (section) | New ref | What |
|---|---|---|
| 167, 174, 181, 188 (§1) | 169, 175, 182, 188 | `ConfigurationType StaticLibrary` |
| 213, 218, 224, 230 (§1) | 215, 220, 226, 232 | `OutDir` `lib\debug` / `lib\release` |
| 238/259/278/300 (§1) | 240/261/280/301 | `LanguageStandard stdcpp20` |
| 211-227 (§1) | 213-229 | `IncludePath` (now also contains `thirdparty\nlohmann\include`) |
| 244 (§1) | 246 (Debug Win32), 267, 286 (Debug x64), 307 | `AdditionalDependencies` |
| 247-249 (§1) | 249-251 (also 270-272, 289-291, 310-312) | `<FxCompile><ShaderModel>6.0` |

**`EV-Engine/EV-Engine/source/DX12/scene.cpp`** (+6 after line 17, +8 after ~106, +14 after ~404)

| Old ref | New ref | What |
|---|---|---|
| 453-460 (§5) | 467-474 | `Scene::Accept` |
| 376-393 (§5) | 384-401 | index import (`std::vector<unsigned int>`) |
| 56-109 (§5) | 62-117 | `Scene::LoadSceneFromFile` (now sets `m_sceneFile` + `m_loadID`, 112-113) |
| 89-90 (§5) | 95-96 | assimp preprocess flags |
| 412-417 (§5) | 425-431 | node transform (transposed `XMMatrixSet`) |
| 161-324 (§5) | 169-332 | `Scene::ImportMaterial` |
| 222-223 (§5) | 230-231 | roughness default 1 |
| 312-319 (§5) | 320-327 | normal vs bump by bits per pixel |

**`EV-Engine/EV-Engine/header/editor/editor.h`**

| Old ref | New ref | What |
|---|---|---|
| 3-17 (§7) | 3-20 (example 9-16, scene-file/play note 18-19) | usage block |
| 88-92 (§7) | 90-95 | `GetViewportWidth/Height`, `ConsumeViewportResize` |
| 139 (§7) | 142 | `AddPanel` |
| 142 (§7) | 145 | `AddMenuCallback` |
| 296 (§2) | 384 | `RayCastScenes` |
| – | 111 | `SetCamera(Camera*)`, **no longer `const Camera*`** (the editor now writes the camera) |

**`EV-Engine/EV-Engine/source/editor/editor.cpp`** (+16 after 249, then +74 from `DrawStatusBar` on)

| Old ref | New ref | What |
|---|---|---|
| 725-733 (§7) | 799-807 | Scene panel size -> `m_viewportWidth/Height`, `m_viewportResized` |
| 757-761 (§7) | 831-835 | `drawList->AddImage(m_sceneTexture)` |
| 808-832 (§7) | 882-906 | `DrawViewportToolbar` (still Move/Rotate/Scale/Local-World only) |
| 853-862 (§7) | 927-936 | gizmo -> `SetLocalTransform` (936) |
| 995 (§7) | 1069 | reparent -> `SetLocalTransform` |

**`EV-Engine/EV-Engine/source/editor/editor_assets.cpp`**

| Old ref | New ref | What |
|---|---|---|
| 98-121 (§5) | 129-164 (`EditorInternal::LoadModel`/`LoadImageTexture`), `ExecuteAndWait` 41-51 | editor loads: copy queue + CPU wait (now shared with scene files) |

**`EV-Engine/OceanRenderer/include/ocean_scene.h`** (-1 after 125: `m_showFileOpenDialog` removed)

| Old ref | New ref |
|---|---|
| 205 `m_renderGraph` (§4) | 204 |
| 292 `m_oceanCascadesNumber` (§6) | 291 |
| 13-15 defines | unchanged |

**`EV-Engine/OceanRenderer/source/ocean_scene.cpp`**. Shift rule: lines up to 337 are unchanged; 338-851 old are **+9**;
852-1289 old are **+6** (3 lines removed from `DrawMenu`); after that **+10** (Esc change).

| Old ref | New ref | What |
|---|---|---|
| 25-27, 34, 98-102, 130-143, 151, 176-188, 190-250, 197-235, 326 | unchanged | constants, static camera, start pose, `ResizeViewport`, IBL, swap chain |
| 320-505 | 320-514 | `LoadContent` |
| 334-340 | 334-349 | editor creation; **new** `SetCameraChangedCallback` 340-346; menu 347; panels 348-349 |
| 339-340 (§7) | 348-349 | `AddPanel("Ocean Parameters"/"Light Parameters")` |
| 357 / 359 / 361 | 366 / 368 / 370 | ocean plane / skybox cube / `sky4k.hdr` |
| 364-366 | 373-375 | helmet / chess / boat (`AddScene("Damaged Helmet")` 385, `AddScene("Ocean")` 388) |
| 383-387 | 392-396 | cascade patch sizes |
| 389-392 | 398-401 | `GenerateH0` per cascade |
| 396-433 | 405-442 | compute root parameters |
| 435-444 | 444-453 | PSO creation |
| 454 | 463 | `CreateBRDFLUT` |
| 457-461 | 466-470 | ocean render params |
| 478-484 | 487-493 | foam params |
| 487-490 / 487-491 | 496-499 / 496-500 | viewport texture |
| 497 | 506 | `SetEnvironment(skyPanorama)` (environment callback 507, `SetRenderGraph` 508) |
| 507-624 | 516-633 | `OnUpdate` |
| 536-542 | 545-551 | camera (pitch/yaw rebuild at 550-551) |
| 546-576 | 555-585 | directional lights |
| 599-617 | 608-626 | point light |
| 603 (§9) | 612 | animated point light position |
| 622 (§2) | 631 | `OnUpdate` calls `OnRender` |
| 626-666 | 635-675 | `OnRender` |
| 642-665 (§4) | 651-674 | record graph, clear, GUI, execute |
| 668-826 | 677-835 | `RecordRenderGraph` |
| 690-699 | 699-708 | `Ocean Spectrum i` |
| 701-714 | 710-723 | FFT passes |
| 716-724 | 725-733 | `Ocean Permute i` |
| 728-731 | 737-740 | IBL imports |
| 735-736 | 744-745 | transient `HDR Color` / `Depth Render Target` |
| 738-746 | 747-755 | `Skybox` |
| 749-784 | 758-793 | `Ocean Surface` |
| 786-812 | 795-821 | `Objects` |
| 807-811 | 816-820 | point-light spheres |
| 815-823 | 824-832 | `Tonemap` (`rg.Execute` 834) |
| 850-870 (§7) | 859-876 | `DrawMenu` (File menu now only "Exit"; Options unchanged) |
| 877-883 (§7) | 883-889 | `Slider` lambda |
| 918-944 (§6) | 924-950 | ocean render params editable |
| 961-971 (§9) | 967-977 | `GenerateH0` on the copy queue after edits |
| 1105-1214 (§6) | 1111-1220 | `GenerateH0` |
| 1161-1171 (§6) | 1167-1177 | cascade texture formats |
| 1175-1213 (§9) | 1181-1219 | `GenerateH0` texture replacement |

**`docs/editor-handoff.md`** (+12 for the new "Branch order" table at 3-15, +9 more after the new "Scene files and play mode" section at 81-88)

| Old ref | New ref | What |
|---|---|---|
| 11 (§6) | 23 | camera controls (right mouse + WASD/QE) |
| 56 (§9) | 68 | "ocean runs compute ... no GPU-side sync" |
| 64 (§3) | 76 | root signatures not rebuilt on hot reload |
| 69-115 (§1 branch table) | 90-136 | TODO: IBL bake fix from `Dads-Birthday` |

### 1.3 Statements in the old file whose content changed
- §1 include paths: the engine's `IncludePath` now also has `$(SolutionDir)EV-Engine\thirdparty\nlohmann\include`.
  The app projects do **not** have it. `json.hpp` is included only in `source/editor/editor_scene_file.cpp:11`; `editor.h` does not
  include it, so the apps still build.
- §1 third-party: nlohmann/json 3.11.3 is now on this branch (`EV-Engine/EV-Engine/thirdparty/nlohmann/include/nlohmann/json.hpp`, MIT).
  `Comdlg32.lib`/`Ole32.lib` come in via `#pragma comment` (editor_scene_file.cpp:22-23).
- §1 branch table: add `path-tracing` = `origin/scene-save-load` + `6cf3e9b`. "Mesh remembers the source file" (§5) is now on this branch.
- §5 `Mesh`: `Mesh::Source` exists (see 2.1). `Scene` has a new `m_loadID` (scene.h:125). `m_sceneFile` (scene.h:124) is now filled in.
- §7 File menu: the **engine** now owns File > New/Open/Save/Save As (`Editor::DrawFileMenu`, editor_scene_file.cpp:704-731,
  drawn before the app's menu callbacks, editor.cpp:502-503). OceanRenderer's own "Open file..." item and `m_showFileOpenDialog` were removed.
- §7 Esc: Esc no longer always quits. While playing, the editor handles it (ocean_scene.cpp:1294-1300).
- §7 accumulation reset triggers: new sources (scene load, New Scene, play start/stop), see 3.3.

---

## 2. What the new commits change for a path tracer

### 2.1 `Mesh::Source` (`aac3d33`)
- `header/resources/mesh.h:56-61`: `struct Source { std::wstring file; uint64_t loadID; uint32_t index; }`; `SetSource`/`GetSource`
  at 100-101; member at 123.
- Filled in by `Scene::ImportMesh` (scene.cpp:408-412) with the model file, a per-load ID (`g_nextLoadID`, scene.cpp:18-22,
  assigned in `LoadSceneFromFile` 112-113) and the mesh's index in the file (order of `aiScene::mMeshes`).
- Empty `file` for meshes made in code: `CreatePlane` (the **ocean plane**), `CreateCube` (skybox), `CreateSphere` (light spheres),
  and `LoadSceneFromString`.
- For the path tracer:
  - `(file, loadID, index)` is a stable, printable key per loaded mesh, good for logs, debug views ("BLAS of helmet mesh 3") and
    for saving per-mesh path tracer overrides in scene files. `loadID` changes every time a file is loaded again (each scene-file
    open loads its models again), so it is **not** stable across runs. Use `(file, index)` for anything saved.
  - A BLAS cache should still be keyed by `Mesh*` with a `weak_ptr` (lifetime). `Source` does not replace that.
  - One `Mesh` can be referenced by several nodes (scene files add the same mesh to several nodes when the file says so,
    editor_scene_file.cpp:520-544). That is instancing: one BLAS, several TLAS instances.

### 2.2 Scene files (`20e444b`, `5f2ffc2`)
Code: `source/editor/editor_scene_file.cpp` (new, 788 lines). API in `editor.h:161-185`. Docs: `docs/editor-handoff.md:81-88`.

**Format** (JSON, `.evscene`, layout comment at editor_scene_file.cpp:31-49; `"format": "EV Scene"`, `"version": 1`, 53-54):
- `camera`: `position` [3], `rotation` quaternion [4], `fov` (`SaveScene` 275-286). **Not saved:** aspect, near/far, pitch/yaw.
- `environment`: path of the last HDR the *editor* loaded (`m_environmentPath`, set in editor_assets.cpp:1046). The startup
  `assets/sky4k.hdr` loaded by OceanRenderer is not recorded, so the key is only present after an editor environment change.
- `models`: one path per loaded copy of a model file (keyed by `(file, loadID)`, 292-325).
- `materials`: every `MaterialProperties` value (diffuse, specular, emissive, ambient, reflectance, metallic, roughness, opacity,
  specularPower, indexOfRefraction, bumpIntensity) + textures by path per slot, `null` = cleared (`WriteMaterial` 130-165).
- `objects`: the models placed in the editor (spawned scenes): full node tree (name, local transform as 16 floats, meshes as
  `{model, mesh, material}`, children) (`writeNode` 299-355, 366-369).
- `applicationScenes`: for scenes the app added with `AddScene` ("Ocean", "Damaged Helmet"), only the node transforms in
  `ForEachNode` order, matched by name on load (372-376; applied 627-649, only while the node count is unchanged).
- **Not saved at all:** ocean time, JONSWAP/spectrum parameters, foam parameters, ocean render params (colours, SSS, IBL),
  light parameters (directional lights, point light), tonemap settings (exposure/method), vsync, any path tracer setting.

**Loading** (`LoadScene` 411-424 -> `ReadSceneFile` 429-596 on a `std::async` thread -> `UpdateSceneLoad` 598-614 ->
`ApplySceneFile` 616-681 on the main thread, called from `Editor::BeginFrame` editor.cpp:343):
- Models load through `EditorInternal::LoadModel` (copy queue, one at a time, **CPU-waits for upload and mips**,
  editor_assets.cpp:129-146, 41-51). When `ApplySceneFile` runs, all new VB/IB/textures are on the GPU. A BLAS build on the
  direct queue in the next frame is safe.
- `ApplySceneFile` **removes all spawned scenes and pushes the new ones** (619-625), sets app-scene transforms, sets the camera and
  calls the camera-changed callback (651-660), then maybe starts an environment load (677-680, async, ends in
  `m_environmentCallback` -> `Ocean::SetEnvironment` a few frames later).
- Scenes built from a file are `std::make_shared<Scene>()` + `SetRootNode` (560-562). Their private `Scene::m_meshes` list is
  **empty**. Gather geometry by walking nodes (visitor), never through `Scene` internals.
- Unknown JSON keys are ignored on read (`file.value(...)` lookups). `SaveScene` writes a **fresh** object (269-409), so unknown keys
  are **dropped on the next save**. A newer `version` only gives a warning (448-451).
- Command line: `--scene <file>` opens a scene, `--play <file>` plays it without the editor (editor.cpp:251-265).

**Storing path tracer settings** (needs an editor extension; nothing exists yet):
- Smallest clean change: an app-data hook in `Editor`, e.g. `AddSceneData(key, std::function<std::string()> save,
  std::function<void(const std::string&)> load)`. `SaveScene` writes `file["application"][key] = json::parse(save())`.
  `ReadSceneFile` copies the raw sub-object text into `SceneFileContents` (editor.h:275-294, add a `std::map<std::string, std::string>`).
  `ApplySceneFile` calls `load(text)` on the **main thread**. Passing strings keeps `json.hpp` out of `editor.h` and out of the
  app projects (their include path has no nlohmann). An engine `raytracing` module can use nlohmann directly, since it compiles
  inside EV-Engine.vcxproj.
- Content for `"pathTracer"`: enabled/render mode, spp per frame, max bounces, target sample count, RR start bounce, clamp,
  denoiser, debug view, freeze ocean time, seed.

**Reference camera shots** (for raster-vs-path-traced comparisons or convergence tests):
- Suggested: `"pathTracer": { "shots": [ { "name", "position", "rotation", "fov", "width", "height", "oceanTime",
  "spp", "maxBounces", "seed", "exposure", "tonemap" } ] }` + a "Go to shot" / "Render shot to file" button in the panel.
  Move the camera through `Camera::SetTranslation/SetRotation/SetFov`, then call the same logic as the camera-changed
  callback (pitch/yaw from the rotation, ocean_scene.cpp:340-346). Without that, `OnUpdate` overwrites the rotation from the
  old pitch/yaw (550-551).
- Pitfalls found on this branch:
  1. **FOV is reset to 45° on every viewport resize**: `Ocean::ResizeViewport` calls `m_camera.SetProjection(45.0f, ...)`
     (ocean_scene.cpp:136). Resize happens when the Scene panel changes size and **when play mode starts or stops** (viewport =
     window size). A loaded or shot FOV survives only until then. Fix: keep the current fov in `ResizeViewport`
     (`SetProjection(m_camera.GetFov(), ...)`).
  2. **Ocean is not reproducible**: `Ocean::GaussianRandom` seeds `std::mt19937` from `std::random_device` (ocean_scene.cpp:1222-1228),
     so H0, and with it every wave, differs per run. A reference shot of the ocean needs a fixed seed (saved in the shot) plus the
     spectrum parameters and the ocean time.
  3. Ocean time is a function-static accumulating `deltaTime` (ocean_scene.cpp:520, 525, copied to `m_oceanTime` 598). To set or freeze
     it from a shot, move it to a member that can be paused/set.
  4. The point light moves with `e.totalTime` (612), so it changes every frame. Freeze it too, or exclude it from reference shots.
  5. Roll is lost: the rotation is rebuilt from pitch/yaw each frame. Shots saved from this camera never have roll, so this is fine.
  6. The resolution of a shot must be forced explicitly (render-graph transient size = `m_viewportWidth/Height`,
     ocean_scene.cpp:744-745). Rendering a shot at a fixed size, independent of the panel, needs its own size path in
     `RecordRenderGraph` (and `m_viewportTexture` sized to match, or a separate output texture).

### 2.3 Play mode (`a04f33b`)
Code: `source/editor/editor_play.cpp` (new, 218 lines). API: `editor.h:187-204`. Docs: `docs/editor-handoff.md:86-88`.
- Start/stop: F5 or the Play button in the menu bar (`DrawPlayButton` 201-218, drawn at editor.cpp:540); Esc stops. With
  `--play` there is no editor and Esc quits (`StopPlaying` 73-78). Handled in `UpdatePlayMode` (111-164), called from
  `BeginFrame` (editor.cpp:351-356). While playing, `BeginFrame` returns early: **no menu bar, no status bar, no dockspace, no
  Ctrl+N/O/S**.
- `EndFrame` (editor.cpp:395-412): while playing it calls `DrawPlayView` instead of `DrawEditorPanels`. **No editor panels, no app
  panels added with `AddPanel`, no Render Graph/Texture Viewer, no gizmo, no viewport toolbar.** The app's own ImGui windows
  (submitted between `BeginFrame` and `EndFrame`) still show, on top of the scene.
- **Viewport size** = ImGui main viewport size = window client size (editor_play.cpp:141-150). It sets `m_viewportResized`, so the
  app sees `ConsumeViewportResize()` on the **next** `OnRender` (ocean_scene.cpp:646-649) and resizes. That resize also resets the FOV
  (see 2.2). The scene texture is drawn stretched to the window with the background draw list (166-178). For one frame the old-size
  image is stretched.
- **Input**: `m_viewportFocused = true`, `m_viewportHovered = !io.WantCaptureMouse`, `m_viewportDragging` on right/middle click
  (152-163). `IsViewportMouseActive()`/`IsViewportKeyboardActive()` (editor.h:102-105) therefore work as in the editor, and
  OceanRenderer's fly camera (OnKeyPress 1282+, OnMouseMove 1384-1401, wheel FOV 1403-1418) works in play mode. Keys already taken:
  F5, Esc, R (camera reset), V (vsync), F11/Alt+Enter, WASD/QE/arrows, Shift, Del (editor only), Ctrl+N/O/S/Shift+S (editor only).
- **Snapshot**: `Play` stores the local transform of every node in every editor scene, plus camera position/rotation/fov
  (28-64). `StopPlaying` restores them and calls the camera-changed callback (80-99). Ocean time, lights and the environment are
  **not** restored.
- For the path tracer:
  - Entering and leaving play mode changes resolution and (through restore) transforms and camera: reset accumulation on both edges.
  - A path tracer panel added with `AddPanel` is hidden while playing. A play-mode control needs a hotkey in the app or an
    app-owned ImGui window.
  - Play mode is the natural "fullscreen reference view" (no panels, viewport = window size, e.g. 1920x1080).

### 2.4 New editor callbacks
| Callback | Declared | Fired from | Used by OceanRenderer |
|---|---|---|---|
| `SetCameraChangedCallback(std::function<void()>)` | editor.h:185 | `ApplySceneFile` (editor_scene_file.cpp:656-659), `StopPlaying` (editor_play.cpp:95-98) | yes, ocean_scene.cpp:340-346: recomputes `m_pitch`/`m_yaw` from the camera rotation |
| `SetPlayModeCallback(std::function<void(bool)>)` | editor.h:204 | `Play` (editor_play.cpp:60-63, `true`), `StopPlaying` (102-105, `false`) | **no** (grep finds no call); the ocean and lights keep animating in both modes |

Both are single slots (`m_cameraChangedCallback`/`m_playModeCallback`, editor.h:515, 523): setting one again replaces the previous one.
If a `raytracing` module also wants these events, the app has to forward them. Another option is to turn the slots into lists,
like `m_menuCallbacks`. The camera callback does not fire for normal fly-camera movement or the R reset; those happen in the app.

### 2.5 OceanRenderer changes (`3f3b7c6`)
- `LoadContent`: camera-changed callback (ocean_scene.cpp:338-346). Nothing else in startup changed.
- `DrawMenu`: "Open file..." removed (859-866); the engine's scene items come first in the same "File" menu.
- `OnKeyPress`: Esc quits only when not playing (1294-1300).
- `OnRender`/`RecordRenderGraph`/`OnGUI`: **unchanged code** (only shifted by +9). Same passes, same single direct command list.

### 2.6 Untouched by the new commits
Render graph (`render_graph.h/.cpp`, `docs/render-graph.md`), the Render Graph panel (`source/editor/editor_render_graph.cpp`),
the viewport toolbar (`DrawViewportToolbar`, now editor.cpp:882-906, same code), `Application`, command lists/queues,
descriptors, resources, PSOs, shader library/hot reload, all HLSL, CI workflow.

### 2.7 Frame order (matters for change detection)
`Ocean::OnUpdate` (camera, lights, ocean time) -> `OnRender` (ocean_scene.cpp:635-675): `ConsumeViewportResize` -> `RecordRenderGraph`
(graph recorded **and executed** into the direct list) -> clear back buffer -> `OnGUI` -> `Editor::BeginFrame` (file dialog
results, **scene-file apply**, environment load results, play-mode start/stop) -> `EndFrame` (panels: gizmo, inspector,
material, drops, delete, reparent) -> `ExecuteCommandList` -> `Present`.
So everything the editor changes in frame N becomes visible to `RecordRenderGraph` in frame N+1. A path tracer that compares a
scene/version snapshot at the **start of `RecordRenderGraph`** catches all of them, one frame late (no visual problem: accumulation
resets one frame later).

---

## 3. Where a `raytracing` engine module hooks in

Suggested layout (engine conventions: area folders, `snake_case`): `EV-Engine/EV-Engine/header/raytracing/*.h`,
`EV-Engine/EV-Engine/source/raytracing/*.cpp`; add `ClCompile`/`ClInclude` entries to `EV-Engine.vcxproj` (next to the editor
entries at 53-56) and `.vcxproj.filters`. `header/` is already on the include path, so `#include "raytracing/..."` works.
Engine shaders for it: see the shader location problem in old §1 (still true: static lib `FxCompile` output goes to `lib/debug`).

### 3.1 Application init (device / caps)
- `Application::Initialize` (`source/core/application.cpp:353-439`): add a capability query **after** the root-signature feature check
  (428-438, before the closing brace at 439): `D3D12_FEATURE_D3D12_OPTIONS5.RaytracingTier`, `D3D12_FEATURE_SHADER_MODEL`
  (highest SM), `D3D12_FEATURE_D3D12_OPTIONS` (`ResourceBindingTier`), and the adapter LUID (`m_dxgiAdapter->GetDesc1`, adapter from
  `CreateAdapter` 441-482). Store them in a caps struct on `Application` (header `application.h`, members near 210-220), exposed by a
  getter.
- `CreateDevice` (501-562): `D3D12CreateDevice(..., ID3D12Device13)` at 504; type in `application.h:66,195,210`. Recommended (unchanged
  from old §2): create a base device, `As<ID3D12Device5>` optionally, gate DXR on it + tier >= 1_1. The debug info queue breaks on
  warnings (516) and logs them (`OnDebugLayerMessage`).
- WARP (`EV_USE_WARP`, 388-396) is what CI runs: the module must report "unsupported" there and not throw.
- `GetBreadcrumbOpName` (1265-1290): add DXR op names.

### 3.2 Editor: panel, render-mode callback, scene-version counter
- **Panel**: `Editor::AddPanel(name, draw, DockSlot, open)` (editor.h:142; drawn in `DrawEditorPanels`, editor.cpp:434-445). Register
  it in `Ocean::LoadContent` next to the other panels (ocean_scene.cpp:348-349), e.g. `AddPanel("Path Tracing", ..., DockSlot::Right)`.
  The panel can call into the module; the module itself should not depend on the editor.
- **Render-mode toggle in the viewport toolbar**: `DrawViewportToolbar` (editor.cpp:882-906) is engine code with no app hook. Add an
  API in `editor.h` (e.g. next to `SetRenderGraph`, 148): `SetViewportModes(std::vector<std::string> names, std::function<void(int)>
  onChange)` or `AddViewportToolbarCallback(std::function<void()>)`. Draw it after the Local/World button (900-904, with
  `ImGui::SameLine()`). The toolbar-hover test uses the last item's rect (`toolbarHovered`, editor.cpp:856), so items added at the end
  of the toolbar stay covered. The toolbar is not drawn in play mode.
- **Scene-version counter** sources (bump a `uint64_t m_sceneVersion` in `Editor`, expose `GetSceneVersion()`):

| Change | Where (path-tracing) |
|---|---|
| Gizmo move/rotate/scale | `DrawGizmo` editor.cpp:927-937 (`SetLocalTransform` 936) |
| Inspector transform edit | `DrawInspector` editor.cpp:1194 |
| Material property edit / texture cleared | `DrawMaterial` editor.cpp:1257 / 1309 |
| Reparent | `ApplyPendingReparent` editor.cpp:1069 |
| Delete node / remove scene | `DeleteSelectedNode` editor_assets.cpp:1158-1190, `RemoveScene` editor.cpp:668-671 |
| Add scene (app) | `AddScene` editor.cpp:660-666 |
| Dragged model placed / spawned | editor_assets.cpp:926, 969, 975 (`PlaceDraggedModel` 887, `UpdateModelDrag` 930) |
| Texture dropped on a material | `UpdateTextureDrops` editor_assets.cpp:1078 |
| Environment changed | `UpdateEnvironmentLoad` editor_assets.cpp:1045 (-> `Ocean::SetEnvironment` ocean_scene.cpp:190) |
| New scene | `NewScene` editor_scene_file.cpp:259-267 |
| Scene file applied (models, app transforms, camera, environment) | `ApplySceneFile` editor_scene_file.cpp:616-681 |
| Play start / stop (transforms + camera restore) | `Play` editor_play.cpp:28-64, `StopPlaying` 66-106 |
| Viewport resize (also play edges) | `m_viewportResized = true` editor.cpp:806, editor_play.cpp:149; consumed at ocean_scene.cpp:646 |
| Shader hot reload | `ShaderLibrary` reload count (editor tracks it in `m_shaderReloadCount`, editor.h:499) |
| App side, not in the editor | camera fly/rotate (ocean_scene.cpp:545-551), FOV wheel (1403-1418), R reset (1311-1317), ocean params (`paramsChanged` 967-977, foam 953-965, render params 924-950), lights (`DrawLightParameters` 981+), ocean time (598), point light (612) |

  Camera changes are easiest to detect by comparing view/projection matrices each frame instead of counting events.

### 3.3 `Ocean::RecordRenderGraph` (ocean_scene.cpp:677-835)
- After the cascade loop (ends 734): an "Ocean Displace Vertices" compute pass (reads the 4 displacement textures, writes a persistent
  imported position buffer), then "Build BLAS/TLAS" (refit ocean BLAS, build new mesh BLASes, TLAS from the visitor).
- After the IBL imports (737-740) and the transient creation (744-745): the path trace pass (ray-query compute or `DispatchRays`)
  reading TLAS + IBL/skybox, writing an **imported** accumulation texture (the graph has no temporal resources) and an HDR output.
- Mode switch: when path traced, skip `Skybox`/`Ocean Surface`/`Objects` (747-821), or keep them for a hybrid G-buffer, and feed
  the path tracer's HDR output to `Tonemap` (824-832) instead of `hdrColor`. `rg.Execute(commandList)` (834) stays the single submit.
- Gather the instance list from the same scenes the raster passes draw: `m_oceanPlane` (791-792), `m_helmet` (804-807),
  `m_editor->GetSpawnedScenes()` (810-813, `GetSpawnedScenes` editor_assets.cpp:175-186), `m_sphere` per point light (816-820,
  one Scene with its transform changed per light: a visitor must record one instance per light, it cannot just walk `m_sphere` once).

### 3.4 Scene visitor
- Interface: `header/DX12/visitor.h:39-47` (`Visit(Scene&)`, `Visit(SceneNode&)`, `Visit(Mesh&)`). Traversal: `Scene::Accept`
  (scene.cpp:467-474) -> `SceneNode::Accept` (`source/DX12/scene_node.cpp:201-216`: node, then its meshes, then children) ->
  `Mesh::Accept` (`source/resources/mesh.cpp:107-110`).
- Example to copy: `SceneVisitor` (`header/DX12/scene_visitor.h`, `source/DX12/scene_visitor.cpp:16-46`). It keeps the world matrix
  from `Visit(SceneNode&)` (`GetWorldTransform()`, 32) for the following `Visit(Mesh&)`. A `RaytracingSceneVisitor` would do the
  same, emitting `(Mesh*, world, material)` per mesh -> TLAS instance + per-instance record (VB/IB/material indices).
  Mesh data: `GetVertexBuffers()` (used at ocean_scene.cpp:766), `GetIndexBuffer()`, `GetMaterial()`, `GetSource()`.

### 3.5 Agility SDK export block (must be in each `.exe`)
The engine is a static library (`EV-Engine.vcxproj:169`). The linker drops unreferenced objects from a `.lib`, so the
`D3D12SDKVersion`/`D3D12SDKPath` exports have to be defined in a translation unit of each application. App entry files (each defines
`wWinMain` at line 22, includes `core/application.h` at 11):
- `EV-Engine/OceanRenderer/source/main.cpp` (absolute: `T:\Projects\DX12\EV\EV-Engine\OceanRenderer\source\main.cpp`)
- `EV-Engine/ShaderArt/source/main.cpp` (absolute: `T:\Projects\DX12\EV\EV-Engine\ShaderArt\source\main.cpp`)
- (on `Dads-Birthday` only: `EV-Engine/DadsBirthday/...`, not on this branch)

Put it after the includes (after line 11, before `ReportLiveObjects` at 13) in both files:
```cpp
// Agility SDK: tells the D3D12 loader to use the D3D12Core.dll shipped next to the exe (in .\D3D12\).
extern "C" { __declspec(dllexport) extern const UINT D3D12SDKVersion = /* SDK version, e.g. 6xx */; }
extern "C" { __declspec(dllexport) extern const char* D3D12SDKPath = ".\\D3D12\\"; }
```
- `D3D12SDKPath` is relative to the **exe folder**, not the working directory. App exes use the default `OutDir`
  (`$(SolutionDir)$(Platform)\$(Configuration)\` = `EV-Engine/x64/Debug/`, no `OutDir` in OceanRenderer/ShaderArt .vcxproj);
  the debugger working directory is `$(SolutionDir)` = `EV-Engine/` (local `OceanRenderer.vcxproj.user:7`, not in git), and CI runs
  from `EV-Engine/` too (workflow line 70). So `D3D12Core.dll` (+ `d3d12SDKLayers.dll` for the debug layer) must be copied to
  `EV-Engine/x64/<Config>/D3D12/` by a post-build step or the `Microsoft.Direct3D.D3D12` NuGet package's targets.
- The exports should be a shared macro (e.g. a tiny header in `header/core/` that the app `main.cpp` includes and expands) so both
  apps stay in sync; the version number must match the copied DLLs.
- The Agility SDK's `d3d12.h` must be used consistently by the engine and the apps, or both keep the Windows SDK header. Note the
  vendored d3dx12 enhanced-barrier code is guarded by `D3D12_SDK_VERSION >= 608` (old §1).
- CI needs the DLLs in the repo or a download step; WARP may still not support DXR. The capability query decides that.

---

## 4. Build facts on `path-tracing` (single current reference)

| Item | Value | Where |
|---|---|---|
| Solution | `EV-Engine/EV-Engine.slnx`: EV-Engine (lib), OceanRenderer, ShaderArt (both depend on EV-Engine); platforms x64, x86 | slnx:1-13 |
| Engine | `EV-Engine/EV-Engine/EV-Engine.vcxproj`, `StaticLibrary`, out `EV-Engine/EV-Engine/lib/debug` / `lib/release` | 169/175/182/188; 215/220/226/232 |
| Apps | `OceanRenderer/OceanRenderer.vcxproj`, `ShaderArt/ShaderArt.vcxproj`: `Application`, `SubSystem Windows`, default OutDir `EV-Engine/x64/<Config>/` | OceanRenderer 30-51; ShaderArt 30-51 |
| App link | `EV-Engine.lib` (+ `DirectXTexd.lib` in Debug), lib dirs `EV-Engine\lib\debug` or `EV-Engine\lib\release`, plus `EV-Engine\lib` | OceanRenderer 96-97, 116-117, 134-135, 154-155; ShaderArt 96-97, 113-114, 128-129, 145-146 |
| Engine link deps | `d3d12; dxgi; d3dcompiler; dxguid; Shlwapi; assimp-vc145-mt(d); DirectXTex(d); zlibstatic(d)` + pragma `dbghelp` (application.cpp:23), `Comdlg32`/`Ole32` (editor_scene_file.cpp:22-23) | EV-Engine.vcxproj 246/267/286/307 |
| Toolset | `v145` (VS 2026), `WindowsTargetPlatformVersion 10.0` | EV-Engine.vcxproj 165, 171…; apps 32… |
| C++ | `stdcpp20`, Level3, SDLCheck, ConformanceMode | EV-Engine 240/261/280/301; OceanRenderer 91/111/129/149; ShaderArt 91/108/123/140 |
| Engine include path | `header`, `thirdparty/DirectXTex`, `imgui`, `imguizmo`, `assimp/include`, **`nlohmann/include`** | EV-Engine.vcxproj 213-229 |
| Shaders | MSBuild `FxCompile`, **SM 6.0** (DXC), entry `main`, output `$(OutDir)%(Filename).cso` next to the exe. Engine default 249-251; OceanRenderer default 99-101 etc. + per-file `ShaderType`/`ShaderModel` 178-299 (15 shaders); ShaderArt `canvas_PS/VS` 162-181. Engine has no `FxCompile` items (embedded headers + runtime FXC/DXC) | – |
| Runtime compile | `ShaderCompiler` loads `dxcompiler.dll` at runtime, `-E main -T <profile>`, Debug adds `-Zi -Qembed_debug -Od` | shader_compiler.cpp:181-189 |
| Agility SDK | none (no `D3D12SDKVersion` anywhere) | – |
| CI | `.github/workflows/windows-build.yml`: push to `imgui-editor`/`main`, any PR, manual (3-7). **A push to `path-tracing` does not trigger it**, only a PR or a manual run. Runner `windows-2025-vs2026`; assimp v6.0.2 via CMake (33-56); msbuild EV-Engine, OceanRenderer, ShaderArt Debug x64 (58-65); WARP smoke test (67-121) | – |

---

## 5. Short list of new pitfalls / to-dos for the path tracer on this branch
1. `Ocean::ResizeViewport` hardcodes FOV 45° (ocean_scene.cpp:136): breaks saved and shot FOVs at the next resize or play toggle.
2. Ocean RNG seeded from `std::random_device` (ocean_scene.cpp:1223-1224): no reproducible reference images of the ocean.
3. Ocean time is a function-static (ocean_scene.cpp:520); not saved, not restored by play mode, not pausable.
4. Scene files drop unknown keys on save; app/path tracer data needs an explicit editor hook (2.2).
5. Play-mode and camera callbacks are single slots; OceanRenderer uses only the camera one.
6. App panels and the viewport toolbar are hidden in play mode.
7. Editor changes land after `RecordRenderGraph` in the same frame: detect them at the start of the next record.
8. Spawned scenes can be replaced wholesale (`ApplySceneFile`, `NewScene`): the BLAS cache must release by `weak_ptr` and
   rebuild instance lists every frame (or on version change).
9. CI does not build `path-tracing` on push.
