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
