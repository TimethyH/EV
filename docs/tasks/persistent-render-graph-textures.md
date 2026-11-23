# Claude task: persistent textures owned by the render graph ("option C")

**Status**: queued 2026-11-23 by the developer. **Owner**: Claude (handed over explicitly – the usual "the developer
writes all code" rule does NOT apply to this task). **When**: only in a session where the developer says they are
*not* developing (RC-0 / path-tracing work touches the same files). Start by checking `git status` on `path-tracing`:
if there are uncommitted changes in files this task touches, stop and ask.

## Why

Creating a persistent texture today takes 4–5 hand-written lines (`CD3DX12_RESOURCE_DESC::Tex2D`, set the flags by
hand, `Application::CreateTexture`, `SetName`, then `rg.Import` every frame). Copying those lines from another texture
silently copies the wrong format or flags (it happened in RC-0 step 1a: the BRDF LUT's 2-channel format came along).

The render graph already solves this for *per-frame* textures: `rg.Create(name, RGTextureDesc::Tex2D(format, w, h))`
and the graph **derives** the flags (UAV / render target / depth) from how passes use the texture
(`RenderGraph::CreateTransientResources`, the access→flag switch at the top of `render_graph.cpp`). What is missing is
the same thing for textures whose contents must **survive between frames**: simulation state, accumulation buffers,
history buffers, a paint canvas. From M3 on the path tracer needs exactly that (accumulation buffer; M8b history for
reprojection), so this is the "one fixed way" to create textures EV will use from now on.

The developer chose this over a small `Application::CreateTexture2D` helper ("option A") on 2026-11-23.

## What to build

1. **API** (in `header/DX12/render_graph.h`, next to `Create` / `Import`; names are a proposal – keep EV's style):
   ```cpp
   // A texture that lives across frames, owned by the graph and found again by its name every frame.
   // Flags are derived from the passes like for Create(). Contents are kept between frames.
   RGTexture CreatePersistent(const std::string& name, const RGTextureDesc& desc);
   // True in the frame the persistent texture was (re)created: its contents are undefined (zero on first creation),
   // so the caller clears / re-initialises it (e.g. resets an accumulation buffer).
   bool WasCreatedThisFrame(RGTexture texture) const;
   // The texture behind a persistent name after Execute, e.g. for ImGui::Image in an editor panel. Null if unknown.
   std::shared_ptr<Texture> GetPersistentTexture(const std::string& name) const;
   ```
   Buffers the same way if it costs little (`RGBufferDesc`), otherwise textures only and note it.
2. **Flags across frames**: a persistent texture's flags are only known after this frame's passes are declared, and
   may differ between frames (frame 1 only paints = UAV, later also read). Keep the *union* of all flags ever seen;
   if a new flag appears, recreate it (contents lost → `WasCreatedThisFrame` true). Also recreate when the desc
   changes (resize). Never recreate otherwise.
3. **Lifetime**: persistent textures are NOT in the transient pool and never reused for something else; they live
   until the graph is destroyed or an explicit `ReleasePersistent(name)` (add it). In-flight command lists keep their
   own references (as for pooled resources), so dropping one is safe.
4. **Descriptors not covered by `RGTextureDesc` today**: cubemaps (SRV as `TEXTURECUBE`) and full mip chains
   (`mipLevels = 0`). Check how `Texture::CreateViews` picks the SRV dimension; extend `RGTextureDesc` (e.g. a
   `cube` bool, `TexCube(...)` factory) only if a migrated call site needs it.
5. **Editor**: the Render Graph panel (`editor_render_graph.cpp`, `RenderGraphDebugInfo`) should show persistent
   textures as their own kind (not "imported", not "transient"), with their memory counted separately from
   `transientMemory`. Created through `Application::CreateTexture` so the texture browser still tracks them.
6. **Docs**: `docs/render-graph.md` gets a section "Persistent textures" (when to use `Create` / `CreatePersistent`
   / `Import`, a 5-line example, the recreate rule). This is also how the developer learns the API – write it as a
   tutorial, not a changelog.

## Migrating the existing call sites

Goal: every texture EV's own code creates for rendering goes through the graph (`Create` or `CreatePersistent`);
`Import` stays only for things the graph can't own. Decide per call site and write the decision in the build log.

| Call site | Today | Expected decision |
|---|---|---|
| `RcToy::Initialize` – "RC-0 Canvas" (`OceanRenderer/source/rc_toy.cpp`) | CreateTexture + flags by hand | `CreatePersistent`; panel uses `GetPersistentTexture` |
| Ocean cascades slope / displacement / foam (`Ocean::GenerateH0`) | CreateTexture + Import per frame | `CreatePersistent` |
| Ocean H0 (`Ocean::GenerateH0`) | CreateTexture + CPU upload | `CreatePersistent` + an upload/initialise pass when `WasCreatedThisFrame`, or keep Import if the CPU upload doesn't fit a pass – explain |
| IBL: skybox cubemap, diffuse + specular irradiance, BRDF LUT (`Ocean::SetEnvironment`, `CreateBRDFLUT`) | CreateTexture; baked once on a command list outside the graph | Preferred: persistent + bake passes that only run when dirty (new environment or a hot-reloaded IBL shader – see `UpdateImageBasedLighting`). If that's too invasive, keep Import and say why |
| Scene viewport (`m_viewportTexture`, LDR) | CreateTexture + Import, resized with the panel | `CreatePersistent` (resize = desc change), editor reads it via `GetPersistentTexture` |
| Engine: file-loaded textures (`command_list.cpp`), swapchain / window back buffers, GUI font, model/texture previews, the graph's own pool | – | Leave as they are (not render-graph resources); list them in the build log as "intentionally not migrated" |
| `EV-Engine/source/demo.cpp`, ShaderArt | – | Skip (unused apps) |

## How to approach it

1. Read first: EV `CLAUDE.md`; `header/DX12/render_graph.h` (top comment = the design), `source/DX12/render_graph.cpp`
   (`Create`, `Import`, `CreateTransientResources`, `ReleaseUnusedResources`, `UpdateDebugInfo`); `docs/render-graph.md`;
   the call sites above.
2. Reference designs (the graph is modelled on kajiya): kajiya's temporal resources – `kajiya-rg` crate, its
   `temporal` module (`TemporalRenderGraph`, `get_or_create_temporal`); Unreal RDG's external/extracted textures
   (`RegisterExternalTexture`, `QueueTextureExtraction`, pooled render targets). Verify names in the sources before
   quoting them.
3. Order: API + implementation → migrate `RcToy` (smallest, proves the editor path) → ocean simulation textures →
   viewport → IBL (biggest) → editor panel → docs.
4. Edit code only with Edit/Write (the developer's ClaudeHighlights hook can't see shell edits). Every changed
   function gets explanatory comments (the developer reads this code to learn the API).
5. Build both configurations of EV-Engine + OceanRenderer with MSBuild (see `CLAUDE.md` "Build facts"; MSBuild path
   via `vswhere`).

## Done when

- Debug + Release build without new warnings; a Debug run reaches the scene with the debug layer silent (it breaks on
  warnings) and the ocean, sky lighting, RC-0 canvas and editor look exactly as before (compare screenshots before /
  after; a manual CI run on WARP stays green).
- Resizing the Scene panel recreates the viewport only; painting on the RC-0 canvas survives frames.
- The Render Graph panel lists persistent textures as such, with their memory.
- `docs/render-graph.md` has the "Persistent textures" tutorial; `docs/path-tracing/knowledge.md` RC-0 entry's
  "Patterns to copy" and `CLAUDE.md`'s code map point at `CreatePersistent`; build-log entry with the per-call-site
  decisions.
- Nothing committed without the developer's OK; the final message lists every changed file and what to look at.
