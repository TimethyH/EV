# ImGui editor – handoff notes

## Branch order
Each branch was started from the previous one, so every branch contains all the work above it.

| # | Branch | Started from | Dates | Commits | What it added |
|---|--------|--------------|-------|---------|---------------|
| 1 | `imgui-editor` | `main` (4a25900, "base setup, renders uv to a screensize triangle") | 2026-10-18 | 44 | Windows CI build (assimp v6.0.2, WARP smoke test, screenshots on `ci-screenshots`), ImGui docking scene editor (viewport, gizmo, hierarchy, inspector, assets, texture viewer), crash/DRED reports, model thumbnails and preview, HDR drops for the skybox, borderless full screen, app icon |
| 2 | `shader-hot-reload` | `imgui-editor` | 2026-10-19 | 8 | Runtime HLSL compiling with DXC, shaders reload when saved, PSOs rebuilt, Shaders panel |
| 3 | `render-graph` | `shader-hot-reload` | 2026-10-19 – 2026-10-20 | 16 | RenderGraph (passes, textures, buffers, barriers), GpuTimer, Render Graph panel and buffer viewer, OceanRenderer renders through the graph |
| 4 | `scene-save-load` | `render-graph` | 2026-10-23 | 7 | Scene files (.evscene: New/Open/Save), play mode (F5, `--play <file>`), these branch notes |

The `claude/...` branches (`claude/shader-hot-reload-26017u`, `claude/scene-save-load-wigmdt`) were the session branches the work started on; the branches above are the ones to use.

Branch: `imgui-editor`. Goal: make EV look and work like a full scene editor (reference: BredaUniversityGames/2425-Y2C-PR-Kudzu, `src/shell/kudzu/editor/`), usable by every app in the EV-Engine library, with strong texture/data inspection for debugging.

## Done
- **ImGui 1.93 docking + ImGuizmo** (`EV-Engine/thirdparty/imgui`, `thirdparty/imguizmo`).
- **GUI renderer** (`source/UI/GUI.cpp`): per-draw texture binding (`GUI::GetTextureID(texture|srv)`), Segoe UI font, docking enabled.
- **Engine**: depth textures are created typeless so they can be sampled (`Texture::GetDepthStencilFormat/GetShaderResourceFormat`); `Application::GetLiveTextures()` lists every texture made with `CreateTexture` (swap chain buffers and temporary mip/cubemap textures excluded); WARP fallback (`EV_USE_WARP=1`); `ThrowIfFailed` reports HRESULT + file/line; exceptions in the window procedure are reported (message box + `ev_crash.log`); DRED breadcrumbs/page faults are reported on device removal; `CommandList::GetResourceState/RestoreResourceState`.
- **Editor** (`header/editor/editor.h`, `source/editor/*.cpp`, `source/UI/texture_preview.cpp`, `texture_readback.cpp`, `format_utils.cpp`):
  docked layout, Scene viewport (render target sized to the panel, click picking, Move/Rotate/Scale gizmo), Hierarchy (drag to reparent, Delete), Inspector (transform, mesh info, material values, texture slots with thumbnails, drop images on slots), Assets panel (browse `assets/`, drag models into the scene – loaded in the background, placed under the mouse; drop images on objects), Textures panel (all live textures), Texture Viewer (mip/slice/face, channels, range + Auto, depth linearization, texel value readback, zoom/pan), Stats.
- **OceanRenderer** uses the editor: scene renders into `m_viewportTexture`, parameter windows are docked panels, camera = right mouse + WASD/QE when the viewport has focus, renders `Editor::GetSpawnedScenes()`.
- **CI** (`.github/workflows/windows-build.yml`): builds on windows-2025-vs2026, runs OceanRenderer on WARP and publishes a screenshot to the `ci-screenshots` branch.

## Model thumbnails and Model Preview
- `UI/model_preview.h/.cpp` (`ModelPreview`): engine-side renderer with an embedded shader (base color texture + one light + ambient), renders a `Scene` into an RGBA8 image. Works in every app, does not match the app's PBR look.
- Assets panel: model files get rendered thumbnails (loaded in the background, max 2 at a time, only for visible cells, rendered once, model released afterwards). Click selects/previews, double click places in front of the camera.
- Model Preview panel (docked with the Texture Viewer): orbit (drag), zoom (wheel), reset (double click), mesh/vertex/triangle/material/texture counts and size, "Place in Scene".
- `LoadModel` also waits for mip generation on the compute queue, so loaded scenes are fully uploaded.
- `EV_EDITOR_MODEL=<path>` previews a model at startup (useful for CI screenshots).
- Assets panel caches the folder listing (refreshed every second) and skips off-screen cells; a 145-file folder went from ~38 to ~150 FPS in Debug.
## Fixed: crash in Resource::CheckFeatureSupport when loading a model again
`CommandList::m_textureCache` stored raw `ID3D12Resource*`. Once every user of a texture released it (a model deleted, or loaded only for a thumbnail) the pointer dangled and the next load of that file wrapped freed memory (access violation in `CheckFeatureSupport`). The cache now holds `std::weak_ptr<Texture>` (expired entries are reloaded) and is keyed by file + sRGB.
Follow-up (Sponza folder, D3D12 ERROR #527 on the mip generation compute list): concurrent loads shared textures through the cache while another load was still uploading them. Editor background loads (`LoadModel`, `LoadImageTexture`) now run one at a time under a mutex and wait for the copy and the compute (mips) work before returning, so the cache only hands out completely uploaded textures.
Crash diagnostics: unhandled SEH exceptions (access violations) write a stack trace to `ev_crash.log`; the previous run's log is kept as `ev_crash.prev.log`.

## Fixed: dragging a model into the Scene; HDR drops change the skybox
- A dragged model is selected once loaded and follows the mouse, so its gizmo was under the cursor; the viewport then replaced its input button (also the drop target) with a Dummy and the drop was lost. While an asset is dragged the gizmo is hidden and never takes the mouse. The drag tooltip shows the model's thumbnail until the model itself appears.
- Dragging from the Assets panel never started: pressing a model cell previews it, and the preview focused its window the next frame; `ImGui::FocusWindow` clears the active item (the pressed cell), and `BeginDragDropSource` needs it. Tabs are now brought forward with `ShowDockedTab` (editor_internal.h), which selects the tab without focusing. The Inspector tab comes forward the same way whenever the selection changes (Scene click, Hierarchy, dropped model), without taking the keyboard from the viewport.
- Dropped models rendered black: OceanRenderer's `pixel.hlsl` read albedo/AO/metallic/roughness/emissive only from textures, so models without a full PBR texture set got AO 0 (no IBL) and roughness 0 (NaN). It now falls back to the material values (AO 1) when a texture is missing and clamps roughness to >= 0.04. The importer uses roughness 1 when a file has none (OBJ).
- Object IBL strength: `EffectPSO::SetIBLIntensity` (sent in the camera constant buffer's former padding float, `Camera.IBLIntensity` in pixel.hlsl), slider "Object IBL" (0-2, default 1) under Ocean Parameters > Lighting.
- `Editor::SetEnvironmentCallback(panorama)`: `.hdr` files dropped into the Scene or double clicked in Assets are loaded in the background and passed to the app. OceanRenderer's `SetEnvironment` (also used at startup) builds the skybox cubemap and the diffuse/specular IBL maps on the compute queue and swaps them in.

## Fixed: crash opening a texture (D3D12 WARNING #1008 in FlushResourceBarriers)
Two readbacks of the same texture in one frame (Texture Viewer: auto range + hovered texel) queued the first readback's restore barrier and the second one's transition in the same batch: two barrier descs for one subresource, and the debug layer breaks on warnings. `ResourceStateTracker::AddTransitionBarrier` now merges queued transitions of the same subresource (and drops ones that end where they started); it never merges across UAV/aliasing barriers.

## Fixed: window resizing / full screen did nothing (panels off screen in full screen)
`Window::OnResize` had been an empty, commented-out body since the swap chain moved into `SwapChain` (January), so the `Resize` event never fired and the swap chain kept its startup size while ImGui laid out for the real window size. It fires again (not for minimize, 0x0). F11/Alt+Enter in OceanRenderer toggle `m_fullscreen` (which `OnRender` applies) instead of the window directly, which was undone the next frame.
The Texture Viewer's startup texture (`EV_EDITOR_TEXTURE`) is looked up before `Begin`, so it also works when the viewer is a hidden tab.

## Fixed: random abort with several background loads
`ResourceStateTracker::Unlock` cleared `m_isLocked` after releasing the mutex, so it could clear the flag of the next thread holding the lock, failing `assert(m_isLocked)` (Debug only). `abort()` (failed asserts, `std::terminate`) now writes a symbolized stack trace to `ev_crash.log`.

## Fixed: GPU hang on startup (DEVICE_HUNG / page fault)
Cause: use-after-free of the resized "Color Render Target". `TexturePreview::GetSourceView` cached an SRV per texture and detected resizes with a raw `ID3D12Resource*`; the SRV only held the `Texture` wrapper, so the old D3D12 resource was freed and a new one could be created at the same address (ABA) – the cached descriptor then pointed at freed GPU memory. It triggered during the first frames while the dock layout resizes the Scene viewport.
Fix: `ShaderResourceView`/`UnorderedAccessView` keep the `ID3D12Resource` their descriptor was created for (`GetD3D12Resource()`), `CommandList::SetShaderResourceView/SetUnorderedAccessView` track it, and the preview cache compares against it.
Diagnostics added: the crash report uses DRED's wide names (command lists, queues and freed allocations now show their names), and in Debug every D3D12 warning/error is written to `ev_crash.log` (the debug layer's break otherwise kills the process silently when no debugger is attached).

## Fixed: viewport input
- Right-drag camera rotation: `WndProc` passed mouse buttons as (left, middle, right) to event constructors that take (left, right, middle), so `rightButton` was really the middle button.
- Zoom out: `Camera::SetFov` only accepted a smaller fov (signed comparison).
- Gizmo: ImGuizmo only activates when no ImGui item is hovered, but the viewport's InvisibleButton always was. While the gizmo is hovered/used the button is replaced by a Dummy and the Scene window gets `NoMove`.

## Fixed: deleting/reparenting a child node crashed
`SceneNode::RemoveChild` called `child->SetParent(nullptr)` before erasing the child, and `SetParent(nullptr)` calls `parent->RemoveChild(me)` again – infinite recursion, stack overflow (0xC000041D from the window procedure). Deleting a whole spawned model (scene root) was unaffected. `RemoveChild` now detaches the child itself.

Still worth checking: the ocean runs compute (FFT) on the compute queue with no GPU-side sync to the direct queue (only CPU waits).

## Shader hot reloading
Branch: `shader-hot-reload`. Save an `.hlsl` file while an app runs and the change shows up right away; no restart or rebuild.
- `DX12/shader.h` (`Shader`, `PipelineShaders`), `DX12/shader_library.h` (`ShaderLibrary`), `DX12/shader_compiler.h` (`ShaderCompiler`). The application owns the library (`Application::LoadShader`, `Application::GetShaderLibrary`) and calls `ShaderLibrary::Update` on every `WM_PAINT`, before the frame is rendered.
- `LoadShader(L"/vertex.cso")` loads the compiled shader next to the executable like before, then looks for `vertex.hlsl` in `<exe name>/shaders` or `shaders` next to or above the executable's folder (`x64/Debug/OceanRenderer.exe` finds `OceanRenderer/shaders`), plus folders added with `ShaderLibrary::AddSourceDirectory`.
- The source and the files it `#include "..."`s are checked 4 times per second. On a change the shader is recompiled with DXC using the profile read from the `.cso` (e.g. `ps_6_0`), entry point `main`, `-Zi -Qembed_debug -Od` in Debug. The `.cso` files are not changed; the next build compiles the edited source as usual.
- DXC is loaded on first use (the engine does not link it): `dxcompiler.dll`/`dxil.dll` next to the executable, otherwise the newest Windows SDK (`Windows Kits\10\bin\<version>\x64`, found through the registry), otherwise the DLL search path. Without it shaders still load, they just cannot be recompiled (the Shaders panel says why).
- PSOs: `BasePSO::LoadShaders` + `CreatePipelineState()` (builds the pipeline state from `m_vertexShader`/`m_pixelShader`) + `UpdatePipelineState()` at the start of `Apply`. The compute PSOs in OceanRenderer do the same with a `PipelineShaders` member. Root signatures are not rebuilt; a changed shader has to keep its bindings.
- Errors never take the app down: a compile error keeps the previous bytecode; a shader that compiles but no longer fits the root signature/input layout keeps the previous pipeline state (the debug layer's break on errors is turned off while that pipeline state is created, and its messages are shown with the error). The command list keeps replaced pipeline states alive until the GPU is done with them.
- Editor: **Shaders** panel (docked with Assets/Textures): every loaded shader with profile, status and last reload, per shader and "Reload All" buttons, "Reload on save" toggle, the selected shader's paths, includes and error output. The status bar reports reloads; a failed reload selects the shader in the panel. ShaderArt (no editor) shows a "Shader Errors" window while a shader has errors.
- Not hot reloaded: the engine's built-in shaders (mip generation and ImGui are embedded byte arrays, `pano_to_cubemap_CS` is loaded per command list) and the editor's embedded preview shaders. OceanRenderer computes the image based lighting again (BRDF LUT + `SetEnvironment` with the current panorama) when `ibl_convolution`, `ibl_specular` or `brdf_lut` is reloaded, because those only run once per environment.

## Scene files and play mode
Branch: `scene-save-load`.
- **Scene files** (`source/editor/editor_scene_file.cpp`): File > New Scene (Ctrl+N), Open Scene... (Ctrl+O), Save Scene (Ctrl+S), Save Scene As... (Ctrl+Shift+S). The dialogs start in `scenes/` in the working directory (`EV-Engine/scenes`, not ignored by git, unlike `assets/`). The status bar shows the current file.
- Format: JSON (`.evscene`, nlohmann/json in `thirdparty/nlohmann`). Stored: the models placed in the editor with their whole node hierarchy (names, local transforms, meshes by model file + mesh index, see `Mesh::Source`), material values and textures (by path; null = slot cleared), the node transforms of the application's own scenes (matched by the name passed to `AddScene`, only while their node count is unchanged, otherwise just the root), the camera (position, rotation, fov) and the environment if it was changed in the editor. Paths are relative to the working directory, so models have to stay where they were. Meshes created in code (cubes, planes) cannot be saved inside placed models.
- Loading runs in the background (`ReadSceneFile` builds the node trees, materials and textures off the main thread) and replaces the placed models when done. `SetCameraChangedCallback` lets the application update its camera controller (OceanRenderer recomputes pitch/yaw).
- File dialogs run on a worker thread: a modal dialog on the main thread runs a message loop that paints (renders) the window from inside the current ImGui frame.
- **Play mode** (`source/editor/editor_play.cpp`): F5 or the Play button in the menu bar. No menus, panels, status bar or gizmos; the scene texture fills the window (background draw list, the application's own ImGui windows stay on top) and `GetViewportWidth/Height` become the window size. The viewport counts as focused/hovered unless an application ImGui window wants the input. Esc or F5 goes back; node transforms and the camera are restored. `SetPlayModeCallback`, `IsPlaying`.
- **Running a game**: `OceanRenderer.exe --play scenes/mygame.evscene` starts in play mode without the editor (Esc quits). `--scene <file>` opens a scene in the editor.

## Planned next
- Undo/redo for transforms and drops; snapping; multi-select.
- `.hdr` drop to change the skybox (needs IBL recompute in the app).
- Hook the editor into ShaderArt the same way as OceanRenderer.
- entt-based entities later (user's plan).
