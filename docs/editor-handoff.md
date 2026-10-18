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

## Open problem (top priority)
On a real GPU the OceanRenderer editor hangs (`DXGI_ERROR_DEVICE_HUNG`, TDR) about a second after starting. WARP does not reproduce it. Already ruled out / fixed: swap-chain resize references, editor previews leaving textures in different states (now restored), depth partial copies, temporary placed textures. The debug layer reports no errors before the hang.

Next steps:
1. Run Debug, read the `Device removed` / `Unfinished:` lines (Output window or `EV-Engine/ev_crash.log`) – they name the command list and operation that hung.
2. Check whether commit `4a25900` (before the editor) hangs too.
3. Bisect by closing panels (Window menu): Textures, Texture Viewer, Assets; or temporarily skip `m_texturePreview->Render` / `m_textureReadback->Record` in `Editor::EndFrame`.
4. Suspect: the ocean runs compute (FFT) on the compute queue with no GPU-side sync to the direct queue (only CPU waits), so frame N+1 compute can overlap frame N direct work that reads/transitions the same textures.

## Planned next
- Undo/redo for transforms and drops; snapping; multi-select.
- `.hdr` drop to change the skybox (needs IBL recompute in the app).
- Hook the editor into ShaderArt the same way as OceanRenderer.
- entt-based entities later (user's plan).
