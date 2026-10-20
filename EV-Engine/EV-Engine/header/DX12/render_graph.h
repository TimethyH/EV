#pragma once

/**
 *  @file render_graph.h
 *
 *  @brief A render graph: describe a frame as passes that read and write textures and buffers, the graph does the rest.
 *
 *  Modelled on Embark Studios' kajiya (kajiya-rg) and the Frostbite FrameGraph (GDC 2017): the graph is
 *  declared again every frame, passes run in the order they are added, and every texture and buffer a pass
 *  touches is declared with how it is used. From that the graph
 *
 *    - transitions resources to the right state before each pass and adds UAV barriers between passes that
 *      write the same resource,
 *    - creates the textures and buffers that only live during the frame ("transient" resources) with the flags
 *      their uses need, and reuses them in later frames (a resized viewport just asks for a different size),
 *    - binds render targets, sets the viewport and scissor and clears attachments,
 *    - skips passes whose results nobody uses,
 *    - names every pass for PIX/RenderDoc, measures its GPU time, and can copy a texture or buffer as it was
 *      right after a pass for inspection (kajiya's "debug hook"). The editor's Render Graph panel shows all of it.
 *
 *  A frame:
 *
 *      RGTexture color = rg.Create("HDR Color", RGTextureDesc::Tex2D(DXGI_FORMAT_R16G16B16A16_FLOAT, w, h));
 *      RGTexture depth = rg.Create("Depth", RGTextureDesc::Tex2D(DXGI_FORMAT_D32_FLOAT, w, h));
 *      RGTexture output = rg.Import("Viewport", m_viewportTexture);
 *
 *      rg.AddPass("Opaque")
 *          .RenderTarget(color, RGClear::Color(0.0f, 0.0f, 0.0f, 1.0f))
 *          .DepthStencil(depth, RGClear::Depth(1.0f))
 *          .Execute([&](RGPassContext& context) { m_scene->Accept(visitor(context.GetCommandList())); });
 *
 *      rg.AddPass("Tonemap")
 *          .Read(color)
 *          .RenderTarget(output)
 *          .Execute([&](RGPassContext& context) { m_tonemapPSO->SetHDRTexture(context.GetTexture(color)); ... });
 *
 *      rg.Execute(commandList);
 *
 *  Buffers work the same way, with their own handle type:
 *
 *      RGBuffer particles = rg.Create("Particles", RGBufferDesc::Structured(count, sizeof(Particle)));
 *      rg.AddPass("Simulate").Write(particles).Execute([&](RGPassContext& context)
 *      {
 *          context.GetCommandList().SetUnorderedAccessView(0, context.GetBuffer(particles));
 *          ...
 *      });
 *      rg.AddPass("Draw particles").Read(particles, RGAccess::NonPixelShaderRead).RenderTarget(color).Execute(...);
 *
 *  Pass callbacks run during Execute, so lambdas may capture locals by reference. Everything is recorded on
 *  the one command list given to Execute (the direct queue); compute passes are dispatched on it as well.
 */

#include <d3d12.h>

#include <wrl/client.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "DX12/render_target.h"

namespace EV
{
    class Buffer;
    class CommandList;
    class GpuTimer;
    class RenderGraph;
    class Resource;
    class Texture;

    // How a pass uses a texture or buffer. Decides the resource state, the barriers and the flags a transient
    // resource is created with.
    enum class RGAccess
    {
        ShaderRead,         // Sampled/loaded (SRV) in any shader stage.
        PixelShaderRead,    // SRV in the pixel shader only.
        NonPixelShaderRead, // SRV in the vertex or compute shader only.
        UnorderedAccess,    // Read and written as a UAV (compute).
        RenderTarget,       // Textures: color attachment (use RGPassBuilder::RenderTarget).
        DepthWrite,         // Textures: depth attachment, written (use RGPassBuilder::DepthStencil).
        DepthRead,          // Textures: depth attachment, test only (RGPassBuilder::DepthStencil with write = false).
        VertexBuffer,       // Buffers: bound as vertex buffer.
        IndexBuffer,        // Buffers: bound as index buffer.
        ConstantBuffer,     // Buffers: bound as constant buffer (CBV).
        IndirectArgument,   // Buffers: arguments of ExecuteIndirect.
        CopySource,
        CopyDest,
    };

    const char* GetAccessName(RGAccess access);
    bool IsWriteAccess(RGAccess access);
    bool IsTextureAccess(RGAccess access); // Valid for textures.
    bool IsBufferAccess(RGAccess access);  // Valid for buffers.

    // A texture in the render graph. Only valid for the frame it was created or imported in.
    struct RGTexture
    {
        uint32_t index = UINT32_MAX;

        bool IsValid() const { return index != UINT32_MAX; }
        bool operator==(const RGTexture& other) const { return index == other.index; }
        bool operator!=(const RGTexture& other) const { return index != other.index; }
    };

    // A buffer in the render graph. Only valid for the frame it was created or imported in.
    struct RGBuffer
    {
        uint32_t index = UINT32_MAX;

        bool IsValid() const { return index != UINT32_MAX; }
        bool operator==(const RGBuffer& other) const { return index == other.index; }
        bool operator!=(const RGBuffer& other) const { return index != other.index; }
    };

    // What a transient buffer looks like. As for textures, the UAV flag comes from how passes use it.
    struct RGBufferDesc
    {
        uint64_t size = 0;    // In bytes.
        uint32_t stride = 0;  // Size of one element for structured buffers, 0 for raw data. Used by the editor's viewer.

        static RGBufferDesc Structured(uint64_t elementCount, uint32_t elementSize)
        {
            return { elementCount * elementSize, elementSize };
        }
        static RGBufferDesc Raw(uint64_t sizeInBytes)
        {
            return { sizeInBytes, 0 };
        }

        bool operator==(const RGBufferDesc& other) const { return size == other.size && stride == other.stride; }
    };

    // What a transient texture looks like. Usage flags (render target, depth, UAV) are not part of it: the graph
    // derives them from how the passes use the texture.
    struct RGTextureDesc
    {
        uint32_t    width = 1;
        uint32_t    height = 1;
        uint16_t    depthOrArraySize = 1;
        uint16_t    mipLevels = 1;
        DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;

        static RGTextureDesc Tex2D(DXGI_FORMAT format, uint32_t width, uint32_t height, uint16_t mipLevels = 1)
        {
            RGTextureDesc desc;
            desc.format = format;
            desc.width = width;
            desc.height = height;
            desc.mipLevels = mipLevels;
            return desc;
        }

        bool operator==(const RGTextureDesc& other) const
        {
            return width == other.width && height == other.height && depthOrArraySize == other.depthOrArraySize &&
                mipLevels == other.mipLevels && format == other.format;
        }
    };

    // Clearing an attachment at the start of a pass. RGClear::None() keeps its contents.
    struct RGClear
    {
        bool  enabled = false;
        float color[4] = {};
        float depth = 1.0f;

        static RGClear None() { return {}; }
        static RGClear Color(float r, float g, float b, float a)
        {
            RGClear clear;
            clear.enabled = true;
            clear.color[0] = r;
            clear.color[1] = g;
            clear.color[2] = b;
            clear.color[3] = a;
            return clear;
        }
        static RGClear Depth(float depth)
        {
            RGClear clear;
            clear.enabled = true;
            clear.depth = depth;
            return clear;
        }
    };

    // Passed to a pass callback: the command list and the textures the pass declared.
    class RGPassContext
    {
    public:
        CommandList& GetCommandList() const { return *m_commandList; }
        const std::shared_ptr<CommandList>& GetCommandListPtr() const { return m_commandList; }

        // The texture or buffer behind a handle. Only resources this pass declared should be used.
        std::shared_ptr<Texture> GetTexture(RGTexture texture) const;
        std::shared_ptr<Buffer> GetBuffer(RGBuffer buffer) const;

        // The pass's attachments, already bound with the viewport and scissor covering the first attachment.
        const RenderTarget& GetRenderTarget() const { return m_renderTarget; }
        D3D12_VIEWPORT GetViewport() const { return m_viewport; }

    private:
        friend class RenderGraph;
        RGPassContext(const RenderGraph& graph, const std::shared_ptr<CommandList>& commandList)
            : m_graph(graph), m_commandList(commandList) {}

        const RenderGraph&           m_graph;
        std::shared_ptr<CommandList> m_commandList;
        RenderTarget                 m_renderTarget;
        D3D12_VIEWPORT               m_viewport = {};
    };

    using RGPassCallback = std::function<void(RGPassContext&)>;

    /**
     * Declares what a pass uses. Returned by RenderGraph::AddPass; chain the calls and end with Execute.
     * Declaring the same resource twice in one pass is fine for reads; a resource cannot be read and written by
     * the same pass (use UnorderedAccess, which allows both).
     */
    class RGPassBuilder
    {
    public:
        RGPassBuilder& Read(RGTexture texture, RGAccess access = RGAccess::ShaderRead);
        RGPassBuilder& Write(RGTexture texture, RGAccess access = RGAccess::UnorderedAccess);
        RGPassBuilder& Read(RGBuffer buffer, RGAccess access = RGAccess::ShaderRead);
        RGPassBuilder& Write(RGBuffer buffer, RGAccess access = RGAccess::UnorderedAccess);

        // Bind as color attachment `slot` (0-7). The first attachment decides the viewport size.
        RGPassBuilder& RenderTarget(RGTexture texture, RGClear clear = RGClear::None(), uint32_t slot = 0);
        // Bind as depth attachment, written or only tested.
        RGPassBuilder& DepthStencil(RGTexture texture, RGClear clear = RGClear::None(), bool write = true);

        // The pass has effects the graph cannot see (e.g. it copies to a readback buffer outside the graph), so it
        // is never skipped. Passes that write an imported resource are never skipped either.
        RGPassBuilder& SideEffects();

        // The work of the pass, called while the graph executes. Optional: a pass that only clears needs none.
        void Execute(RGPassCallback callback);

    private:
        friend class RenderGraph;
        RGPassBuilder(RenderGraph& graph, uint32_t pass) : m_graph(graph), m_pass(pass) {}

        RenderGraph& m_graph;
        uint32_t     m_pass;
    };

    // What the last executed frame looked like, for the editor's Render Graph panel.
    struct RenderGraphDebugInfo
    {
        struct Access
        {
            uint32_t resource = 0;
            RGAccess access = RGAccess::ShaderRead;
        };
        struct Pass
        {
            std::string         name;
            std::vector<Access> accesses;
            bool                culled = false;
            bool                raster = false;            // Has attachments.
            float               gpuMilliseconds = -1.0f;   // -1 until measured (results arrive a few frames late).
        };
        struct Resource
        {
            std::string             name;
            bool                    isBuffer = false;
            RGTextureDesc           desc;        // Textures.
            RGBufferDesc            bufferDesc;  // Buffers.
            bool                    imported = false;
            int                     firstPass = -1;  // First and last pass (index into passes) that uses it.
            int                     lastPass = -1;
            std::weak_ptr<Texture>  texture;     // Null for buffers.
        };

        std::vector<Pass>     passes;
        std::vector<Resource> resources;
        uint64_t              frame = 0;
        float                 gpuMilliseconds = 0.0f;  // Sum of the measured passes.
        uint64_t              transientMemory = 0;     // Bytes of the resources the graph created and keeps.
        bool                  captureFound = false;    // The capture's pass and resource ran this frame.
        bool                  captureIsBuffer = false;
    };

    class RenderGraph
    {
    public:
        RenderGraph();
        ~RenderGraph();

        RenderGraph(const RenderGraph&) = delete;
        RenderGraph& operator=(const RenderGraph&) = delete;

        // A texture or buffer that only lives during this frame. The graph creates it (or reuses one from earlier
        // frames).
        RGTexture Create(const std::string& name, const RGTextureDesc& desc);
        RGBuffer Create(const std::string& name, const RGBufferDesc& desc);

        // A texture or buffer that lives outside the graph (simulation state, the editor's viewport image, IBL
        // maps...). The graph transitions it like any other resource; it is never reused for something else.
        RGTexture Import(const std::string& name, const std::shared_ptr<Texture>& texture);
        // `stride` is the size of one element (e.g. a vertex), only used to show the buffer in the editor.
        RGBuffer Import(const std::string& name, const std::shared_ptr<Buffer>& buffer, uint32_t stride = 0);

        RGPassBuilder AddPass(const std::string& name);

        const RGTextureDesc& GetDesc(RGTexture texture) const;
        const RGBufferDesc& GetDesc(RGBuffer buffer) const;

        /**
         * Records the declared passes on the command list (in the order they were added, skipping unused ones)
         * and starts the next frame: handles of this frame become invalid.
         */
        void Execute(const std::shared_ptr<CommandList>& commandList);

        // ----------------------------------------------------------------------------------------
        // Debugging

        // The last executed frame.
        const RenderGraphDebugInfo& GetDebugInfo() const { return m_debugInfo; }

        /**
         * Copy a texture or buffer right after a pass ran, every frame (kajiya's debug hook). Shows intermediate
         * results that later passes overwrite, e.g. a texture between two FFT passes. Textures are copied into
         * GetCapturedTexture. Buffers are read back to the CPU (the first MaxBufferCaptureSize bytes) and arrive
         * in GetCapturedBufferData a few frames later.
         */
        void SetCapture(const std::string& passName, const std::string& resourceName);
        void ClearCapture();
        const std::string& GetCapturePass() const { return m_capturePass; }
        const std::string& GetCaptureResource() const { return m_captureResource; }
        std::shared_ptr<Texture> GetCapturedTexture() const { return m_capturedTexture; }
        const std::vector<uint8_t>& GetCapturedBufferData() const { return m_capturedBufferData; }
        const RGBufferDesc& GetCapturedBufferDesc() const { return m_capturedBufferDesc; }

        static constexpr uint64_t MaxBufferCaptureSize = 4 * 1024 * 1024;

        // Measure each pass's GPU time with timestamp queries (on by default).
        void SetProfilingEnabled(bool enabled) { m_profilingEnabled = enabled; }
        bool IsProfilingEnabled() const { return m_profilingEnabled; }

    private:
        friend class RGPassBuilder;
        friend class RGPassContext;

        struct ResourceAccess
        {
            uint32_t resource;
            RGAccess access;
        };

        struct Attachment
        {
            RGTexture texture;
            RGClear   clear;
        };

        struct Pass
        {
            std::string                 name;
            std::vector<ResourceAccess> accesses;
            Attachment                  colorAttachments[8];
            Attachment                  depthAttachment;
            bool                        sideEffects = false;
            bool                        culled = false;
            RGPassCallback              callback;
        };

        // A texture or a buffer declared this frame.
        struct Resource
        {
            std::string              name;
            bool                     isBuffer = false;
            RGTextureDesc            desc;        // Textures.
            RGBufferDesc             bufferDesc;  // Buffers.
            bool                     imported = false;
            D3D12_RESOURCE_FLAGS     flags = D3D12_RESOURCE_FLAG_NONE;  // Derived from the accesses.
            bool                     hasClearValue = false;             // The first clear, used as optimized clear value.
            RGClear                  clearValue;
            std::shared_ptr<Texture> texture;
            std::shared_ptr<Buffer>  buffer;

            std::shared_ptr<EV::Resource> GetGpuResource() const;
        };

        // A texture or buffer the graph created, kept for reuse by later frames.
        struct PooledResource
        {
            bool                     isBuffer = false;
            RGTextureDesc            desc;
            uint64_t                 bufferSize = 0;
            D3D12_RESOURCE_FLAGS     flags = D3D12_RESOURCE_FLAG_NONE;
            bool                     hasClearValue = false;
            RGClear                  clearValue;
            std::shared_ptr<Texture> texture;
            std::shared_ptr<Buffer>  buffer;
            uint64_t                 size = 0;
            uint64_t                 lastUsedFrame = 0;
        };

        // A CPU readable copy of a captured buffer, one per frame in flight so the CPU never reads one the GPU
        // may still be writing.
        struct BufferReadback
        {
            Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
            uint64_t                               capacity = 0;
            uint64_t                               size = 0;  // Bytes copied in the frame that used this slot; 0 if none.
            RGBufferDesc                           desc;
        };

        uint32_t AddResource(Resource resource);
        void AddAccess(uint32_t pass, uint32_t resource, RGAccess access);
        void CullPasses();
        void CreateTransientResources();
        void ExecutePass(Pass& pass, const std::shared_ptr<CommandList>& commandList, std::vector<bool>& lastUseWasUAV);
        void CapturePass(const Pass& pass, CommandList& commandList);
        void CaptureBuffer(const Resource& source, CommandList& commandList);
        void ReadCapturedBuffer();
        void UpdateDebugInfo();
        void ReleaseUnusedResources();
        Resource& GetResource(RGTexture texture);
        const Resource& GetResource(RGTexture texture) const;
        Resource& GetResource(RGBuffer buffer);
        const Resource& GetResource(RGBuffer buffer) const;
        Resource& GetResource(uint32_t index);

        std::vector<Pass>           m_passes;
        std::vector<Resource>       m_resources;
        std::vector<PooledResource> m_pool;
        uint64_t                    m_frame = 0;

        std::unique_ptr<GpuTimer> m_gpuTimer;
        bool                      m_profilingEnabled = true;

        std::string              m_capturePass;
        std::string              m_captureResource;
        std::shared_ptr<Texture> m_capturedTexture;
        bool                     m_captureFound = false;
        bool                     m_captureIsBuffer = false;
        static constexpr uint32_t ReadbackFrames = 4; // Like GpuTimer::FramesInFlight.
        BufferReadback           m_bufferReadbacks[ReadbackFrames];
        std::vector<uint8_t>     m_capturedBufferData;
        RGBufferDesc             m_capturedBufferDesc;

        RenderGraphDebugInfo m_debugInfo;
    };
}
