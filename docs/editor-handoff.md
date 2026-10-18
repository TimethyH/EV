# ImGui editor – handoff notes

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

## Planned next
- Undo/redo for transforms and drops; snapping; multi-select.
- `.hdr` drop to change the skybox (needs IBL recompute in the app).
- Hook the editor into ShaderArt the same way as OceanRenderer.
- entt-based entities later (user's plan).
