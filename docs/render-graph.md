# Render graph

`EV-Engine/EV-Engine/header/DX12/render_graph.h`. OceanRenderer renders its whole frame with it (`Ocean::RecordRenderGraph`), and the editor's **Render Graph** panel shows what it did.

## Using it

Keep one `RenderGraph` in your game (it owns the textures it reuses between frames). Every frame, declare the frame and execute it:

```cpp
void MyGame::OnRender()
{
    auto commandList = Application::Get().GetCommandQueue().GetCommandList();
    RenderGraph& rg = m_renderGraph;

    // 1. Textures. Created ones only live during the frame; imported ones live outside the graph.
    RGTexture color  = rg.Create("HDR Color", RGTextureDesc::Tex2D(DXGI_FORMAT_R16G16B16A16_FLOAT, width, height));
    RGTexture depth  = rg.Create("Depth", RGTextureDesc::Tex2D(DXGI_FORMAT_D32_FLOAT, width, height));
    RGTexture output = rg.Import("Viewport", m_viewportTexture);

    // 2. Passes, in the order they run. Declare every texture a pass touches.
    rg.AddPass("Opaque")
        .RenderTarget(color, RGClear::Color(0.0f, 0.0f, 0.0f, 1.0f))
        .DepthStencil(depth, RGClear::Depth(1.0f))
        .Execute([&](RGPassContext& context)
        {
            SceneVisitor visitor(context.GetCommandList(), m_camera, *m_pso, false);
            m_scene->Accept(visitor);
        });

    rg.AddPass("Tonemap")
        .Read(color, RGAccess::PixelShaderRead)
        .RenderTarget(output)
        .Execute([&](RGPassContext& context)
        {
            m_tonemapPSO->SetHDRTexture(context.GetTexture(color));
            m_tonemapPSO->Apply(context.GetCommandList());
        });

    // 3. Record everything on the command list.
    rg.Execute(commandList);

    commandQueue.ExecuteCommandList(commandList);
}
```

And once, so the editor can show it: `m_editor->SetRenderGraph(&m_renderGraph);`

### Buffers

Buffers work the same way, with their own handle type (`RGBuffer`), so a texture cannot be passed where a buffer is expected:

```cpp
RGBuffer particles = rg.Create("Particles", RGBufferDesc::Structured(count, sizeof(Particle)));  // or RGBufferDesc::Raw(bytes)
RGBuffer vertices  = rg.Import("Mesh Vertices", vertexBuffer, sizeof(Vertex));                   // stride only for the editor

rg.AddPass("Simulate particles")
    .Write(particles)                                   // UAV, the graph creates the buffer with the UAV flag
    .Execute([&](RGPassContext& context)
    {
        context.GetCommandList().SetUnorderedAccessView(0, context.GetBuffer(particles));
        context.GetCommandList().Dispatch(groups);
    });

rg.AddPass("Draw particles")
    .Read(particles, RGAccess::NonPixelShaderRead)      // vertex shader reads it
    .Read(vertices, RGAccess::VertexBuffer)
    .RenderTarget(color)
    .Execute(...);
```

Buffer-only accesses: `RGAccess::VertexBuffer`, `IndexBuffer`, `ConstantBuffer`, `IndirectArgument`. `ShaderRead`, `PixelShaderRead`, `NonPixelShaderRead`, `UnorderedAccess`, `CopySource` and `CopyDest` work for both. Render target and depth accesses are texture-only. `Application::CreateBuffer(CD3DX12_RESOURCE_DESC::Buffer(size, flags))` creates a buffer outside the graph (to import).

### What to declare

| Builder call | Use it for | Resource state |
|---|---|---|
| `.Read(t)` / `.Read(t, RGAccess::PixelShaderRead / NonPixelShaderRead)` | sampling or loading a texture (SRV) | (pixel / non-pixel) shader resource |
| `.Write(t)` (= `RGAccess::UnorderedAccess`) | compute shaders writing (and reading) a UAV | unordered access |
| `.RenderTarget(t, clear, slot)` | color attachment `slot` | render target |
| `.DepthStencil(t, clear, write)` | depth attachment, written or only tested | depth write |
| `.Write(t, RGAccess::CopyDest)` / `.Read(t, RGAccess::CopySource)` | copies | copy dest / source |
| `.Read(b, RGAccess::VertexBuffer / IndexBuffer / ConstantBuffer / IndirectArgument)` | buffers bound as vertex/index/constant buffer or ExecuteIndirect arguments | vertex and constant buffer / index buffer / indirect argument |
| `.SideEffects()` | a pass whose result the graph cannot see (e.g. a copy to a readback buffer outside the graph) | – |

The graph then:
- **transitions** every texture and buffer to the state its use needs before the pass, and adds a **UAV barrier** when consecutive passes use the same resource as a UAV (so a chain of compute passes over one texture or buffer is safe without hand-written barriers);
- **creates** created textures and buffers with the flags their uses need (render target, depth, UAV: you only give size and format), with the first clear as optimized clear value, and **reuses** them in later frames. Ask for a new size and you get a new resource; the old one is released after 8 unused frames;
- **binds** the attachments, sets the viewport and scissor to the first attachment's size, and **clears** the attachments that have an `RGClear`;
- **skips** passes whose writes nobody uses. A pass is kept when it writes an imported texture, writes something a later kept pass reads, uses `.SideEffects()`, or declares no textures at all;
- **names** each pass for PIX/RenderDoc (`BeginEvent`) and **times** it on the GPU.

Mistakes fail loudly at the line that declares them: reading and writing one resource in the same pass, `Read` with a write access, a buffer access on a texture (or the other way around), using a handle from another frame.

### Rules of thumb
- Pass callbacks run inside `rg.Execute`, so capturing locals by reference is fine. Use `context.GetTexture(handle)` to get the texture behind a handle.
- Everything runs on the command list you give `Execute` (the direct queue); compute passes dispatch on it too.
- Import textures that must survive the frame (simulation state, history buffers, the image the editor shows) or that something outside the graph made (IBL maps). Create everything else.
- Importing the same texture twice in a frame returns the same handle.
- Keep pass names unique per frame where you can: captures and GPU timings find passes by name (repeated names are matched in order).
- Existing PSOs keep working inside a pass: their own `SetShaderResourceView` transitions become no-ops because the graph already moved the texture.

## Debugging in the editor

**Window > Render Graph** (docked next to Assets/Textures/Shaders):

- **Passes**: every pass in execution order with its type (Raster/Compute), GPU time (a bar relative to the slowest pass) and whether it was skipped. Select a pass to see which textures and buffers it reads and writes, how, their size and format (stride for buffers), and a **Capture** button per resource.
- **Timeline**: one row per texture or buffer, one column per pass (the Frostbite FrameGraph view). Blue = read, orange = written, faint band = the resource's lifetime, dimmed = skipped pass. Hover for details; **click a cell to capture that resource as it was right after that pass**.
- **Resources**: every texture and buffer with size, format, transient/imported, which passes use it, and **View** (a texture at the end of the frame in the Texture Viewer; a buffer is captured after the last pass that uses it).
- **Buffer** (appears while a buffer is captured): the captured bytes as float, int, uint or hex. Structured buffers show one element per row (from the stride); otherwise choose the values per row. The first 4 MB are read back, and the data arrives a few frames after the copy (the CPU never waits for the GPU). In OceanRenderer, capture "Ocean Plane Vertices" in the Ocean Surface pass to see the plane's vertices.
- The summary line shows pass/texture counts, transient memory and the frame's total GPU time; **GPU timings** turns the timestamp queries off.

A **capture** of a texture copies it right after the chosen pass every frame into "Render Graph Capture" and opens it in the Texture Viewer (with all its tools: channels, mips, range, texel values). That shows results later passes overwrite, e.g. the ocean displacement between the horizontal and vertical FFT. Code can do the same with `rg.SetCapture(pass, texture)` / `GetCapturedTexture()`.

## Why it is built this way

**Sources.** kajiya's render graph (Embark Studios, `kajiya-rg`), the Frostbite FrameGraph talk ("FrameGraph: Extensible Rendering Architecture in Frostbite", Yuriy O'Donnell, GDC 2017) and Kudzu's Graphite render graph.

- **Rebuilt every frame, executed in declaration order** (kajiya). Frame setup is ordinary code: `if`s, loops and helper functions (OceanRenderer adds 4 cascades × 6 passes in a loop). Order is what you wrote, so it is predictable when debugging. Kudzu sorts passes into dependency "waves" and Frostbite reorders, which pays off with several queues; with one direct queue (the choice for now) it only makes the order harder to follow. The declared reads/writes already give what a reorder or async compute would need, so it can be added later without changing pass code.
- **Declared accesses instead of hand-written barriers** (all three). The engine's `ResourceStateTracker` already resolves "before" states; the graph decides the "after" states and the UAV barriers from the declarations, which is where hand-written code went wrong (`UAVBarrier(true)` everywhere, a manual transition back to UAV at the end of the frame for the compute queue).
- **Typed, frame-local handles** (kajiya's `Handle<Image>`/`Handle<Buffer>`, Kudzu's `vh32`). `RGTexture` and `RGBuffer` are 4 byte indices, separate types so they cannot be mixed up; the graph owns the mapping to textures, so it can pick, pool and swap textures without the game holding pointers. C++ cannot borrow-check like Rust, so misuse is checked at declaration time instead.
- **Usage flags derived from accesses** (kajiya `calculate_resource_info`). You state what a texture is (size, format) and how passes use it; the flags follow and cannot get out of sync.
- **Transient pool instead of memory aliasing**. kajiya keeps a transient resource cache and leaves aliasing as a TODO; Frostbite aliases memory between textures whose lifetimes do not overlap. The pool already removes the per-frame creation cost and makes resizing automatic; aliasing needs placed resources and aliasing barriers that this engine does not have yet, and the lifetimes it would need are already computed (and shown in the Timeline).
- **Culling** (Frostbite). Debug or optional passes can stay in the code and cost nothing when their output is unused. The rules are conservative so a pass is only skipped when its writes are certainly unused, and passes that declare nothing are always kept.
- **Debug hook** (kajiya's `RenderDebugHook`). kajiya copies the first image a chosen pass writes into a debug image every frame; here you pick any texture of any pass from the editor, and the copy opens in the existing Texture Viewer instead of a separate view.
- **Per-pass GPU timings** (Kudzu's `get_pass_durations`, kajiya's GPU profiler scopes). `GpuTimer` is a separate small class so other code can time scopes too.
- **Lambdas for pass work** (kajiya's `pass.render(|api| ...)`, Frostbite's execute lambda). The pass's code sits next to its declaration, and the existing PSOs and `SceneVisitor` are used unchanged inside the lambda.

## What changed in OceanRenderer

- The ocean simulation (spectrum, 4 FFTs, permute/foam per cascade) moved from the compute queue in `OnUpdate`, where the CPU waited for it every frame, to compute passes on the direct queue in the graph. The CPU no longer stalls on it, and the passes and their textures show up in the panel.
- The HDR color and depth buffers are transient graph textures sized to the Scene panel; `ResizeViewport` no longer resizes them (the graph creates the new size). `m_renderTarget`, `m_HDRTexture`, `m_viewport`, `m_scissorRect` and `m_viewportRenderTarget` are gone (the graph binds attachments and sets the viewport).
- The Ocean Surface pass declares the plane's vertex and index buffers, so they show up in the panel and can be captured.
- The frame is: `Ocean Spectrum n`, `FFT Displacement Horizontal/Vertical n`, `FFT Slope Horizontal/Vertical n`, `Ocean Permute n` (n = 0..3), `Skybox`, `Ocean Surface`, `Objects`, `Tonemap`.

## Possible next steps
- Async compute: mark passes for the compute queue; the declared accesses tell where fences are needed.
- Memory aliasing of transient textures using the computed lifetimes.
- Temporal resources (kajiya's `get_or_create_temporal`): a named texture kept across frames for history effects.
- Move ShaderArt onto the graph.
