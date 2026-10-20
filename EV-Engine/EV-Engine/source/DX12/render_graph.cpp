#include "DX12/dx12_includes.h"

#include "DX12/render_graph.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <stdexcept>

#include "core/application.h"
#include "DX12/command_list.h"
#include "DX12/gpu_timer.h"
#include "resources/buffer.h"
#include "resources/texture.h"
#include "utility/helpers.h"

using namespace EV;

namespace
{
    // Textures and buffers the graph created are released when no frame used them for this many frames.
    constexpr uint64_t g_keepUnusedResourcesFrames = 8;

    // Name of the texture RenderGraph::SetCapture copies into, also how the editor finds it.
    const wchar_t* g_captureTextureName = L"Render Graph Capture";

    D3D12_RESOURCE_STATES GetResourceState(RGAccess access)
    {
        switch (access)
        {
        case RGAccess::ShaderRead: return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        case RGAccess::PixelShaderRead: return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        case RGAccess::NonPixelShaderRead: return D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        case RGAccess::UnorderedAccess: return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        case RGAccess::RenderTarget: return D3D12_RESOURCE_STATE_RENDER_TARGET;
        // The engine's depth stencil views are writable, so a bound depth buffer has to be in the DEPTH_WRITE
        // state even when the pass only tests against it.
        case RGAccess::DepthWrite: return D3D12_RESOURCE_STATE_DEPTH_WRITE;
        case RGAccess::DepthRead: return D3D12_RESOURCE_STATE_DEPTH_WRITE;
        case RGAccess::VertexBuffer:
        case RGAccess::ConstantBuffer: return D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        case RGAccess::IndexBuffer: return D3D12_RESOURCE_STATE_INDEX_BUFFER;
        case RGAccess::IndirectArgument: return D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
        case RGAccess::CopySource: return D3D12_RESOURCE_STATE_COPY_SOURCE;
        case RGAccess::CopyDest: return D3D12_RESOURCE_STATE_COPY_DEST;
        }
        return D3D12_RESOURCE_STATE_COMMON;
    }

    D3D12_RESOURCE_FLAGS GetRequiredFlags(RGAccess access)
    {
        switch (access)
        {
        case RGAccess::UnorderedAccess: return D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        case RGAccess::RenderTarget: return D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        case RGAccess::DepthWrite:
        case RGAccess::DepthRead: return D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        default: return D3D12_RESOURCE_FLAG_NONE;
        }
    }

    RGTextureDesc GetTextureDesc(const Texture& texture)
    {
        const auto desc = texture.GetD3D12ResourceDesc();
        RGTextureDesc result;
        result.width = static_cast<uint32_t>(desc.Width);
        result.height = desc.Height;
        result.depthOrArraySize = desc.DepthOrArraySize;
        result.mipLevels = desc.MipLevels;
        result.format = desc.Format;
        return result;
    }

    bool SameClearValue(const RGClear& a, const RGClear& b)
    {
        return a.depth == b.depth && memcmp(a.color, b.color, sizeof(a.color)) == 0;
    }
}

// ------------------------------------------------------------------------------------------------
// Access helpers

const char* EV::GetAccessName(RGAccess access)
{
    switch (access)
    {
    case RGAccess::ShaderRead: return "Shader read";
    case RGAccess::PixelShaderRead: return "Pixel shader read";
    case RGAccess::NonPixelShaderRead: return "Non-pixel shader read";
    case RGAccess::UnorderedAccess: return "Unordered access";
    case RGAccess::RenderTarget: return "Render target";
    case RGAccess::DepthWrite: return "Depth write";
    case RGAccess::DepthRead: return "Depth test";
    case RGAccess::VertexBuffer: return "Vertex buffer";
    case RGAccess::IndexBuffer: return "Index buffer";
    case RGAccess::ConstantBuffer: return "Constant buffer";
    case RGAccess::IndirectArgument: return "Indirect argument";
    case RGAccess::CopySource: return "Copy source";
    case RGAccess::CopyDest: return "Copy destination";
    }
    return "?";
}

bool EV::IsWriteAccess(RGAccess access)
{
    return access == RGAccess::UnorderedAccess || access == RGAccess::RenderTarget || access == RGAccess::DepthWrite ||
        access == RGAccess::CopyDest;
}

bool EV::IsTextureAccess(RGAccess access)
{
    switch (access)
    {
    case RGAccess::ShaderRead:
    case RGAccess::PixelShaderRead:
    case RGAccess::NonPixelShaderRead:
    case RGAccess::UnorderedAccess:
    case RGAccess::RenderTarget:
    case RGAccess::DepthWrite:
    case RGAccess::DepthRead:
    case RGAccess::CopySource:
    case RGAccess::CopyDest:
        return true;
    case RGAccess::VertexBuffer:
    case RGAccess::IndexBuffer:
    case RGAccess::ConstantBuffer:
    case RGAccess::IndirectArgument:
        return false;
    }
    return false;
}

bool EV::IsBufferAccess(RGAccess access)
{
    switch (access)
    {
    case RGAccess::ShaderRead:
    case RGAccess::PixelShaderRead:
    case RGAccess::NonPixelShaderRead:
    case RGAccess::UnorderedAccess:
    case RGAccess::VertexBuffer:
    case RGAccess::IndexBuffer:
    case RGAccess::ConstantBuffer:
    case RGAccess::IndirectArgument:
    case RGAccess::CopySource:
    case RGAccess::CopyDest:
        return true;
    case RGAccess::RenderTarget:
    case RGAccess::DepthWrite:
    case RGAccess::DepthRead:
        return false;
    }
    return false;
}

// ------------------------------------------------------------------------------------------------
// RGPassContext

std::shared_ptr<Texture> RGPassContext::GetTexture(RGTexture texture) const
{
    return m_graph.GetResource(texture).texture;
}

std::shared_ptr<Buffer> RGPassContext::GetBuffer(RGBuffer buffer) const
{
    return m_graph.GetResource(buffer).buffer;
}

// ------------------------------------------------------------------------------------------------
// RGPassBuilder

RGPassBuilder& RGPassBuilder::Declare(uint32_t resource, RGAccess access, bool write)
{
    if (IsWriteAccess(access) != write)
    {
        throw std::logic_error("Render graph pass '" + m_graph.m_passes[m_pass].name + "': " + GetAccessName(access) +
            " is a " + (write ? "read, use Read" : "write, use Write"));
    }
    m_graph.AddAccess(m_pass, resource, access);
    return *this;
}

// The typed GetResource calls check that the handle is a texture or a buffer.
RGPassBuilder& RGPassBuilder::Read(RGTexture texture, RGAccess access)
{
    m_graph.GetResource(texture);
    return Declare(texture.index, access, false);
}

RGPassBuilder& RGPassBuilder::Write(RGTexture texture, RGAccess access)
{
    m_graph.GetResource(texture);
    return Declare(texture.index, access, true);
}

RGPassBuilder& RGPassBuilder::Read(RGBuffer buffer, RGAccess access)
{
    m_graph.GetResource(buffer);
    return Declare(buffer.index, access, false);
}

RGPassBuilder& RGPassBuilder::Write(RGBuffer buffer, RGAccess access)
{
    m_graph.GetResource(buffer);
    return Declare(buffer.index, access, true);
}

RGPassBuilder& RGPassBuilder::RenderTarget(RGTexture texture, RGClear clear, uint32_t slot)
{
    auto& pass = m_graph.m_passes[m_pass];
    if (slot >= _countof(pass.colorAttachments))
    {
        throw std::logic_error("Render graph pass '" + pass.name + "': render target slot " + std::to_string(slot) + " is out of range");
    }
    auto& resource = m_graph.GetResource(texture);
    m_graph.AddAccess(m_pass, texture.index, RGAccess::RenderTarget);
    pass.colorAttachments[slot] = { texture, clear };

    if (clear.enabled && !resource.hasClearValue)
    {
        resource.hasClearValue = true;
        resource.clearValue = clear;
    }
    return *this;
}

RGPassBuilder& RGPassBuilder::DepthStencil(RGTexture texture, RGClear clear, bool write)
{
    auto& pass = m_graph.m_passes[m_pass];
    if (clear.enabled && !write)
    {
        throw std::logic_error("Render graph pass '" + pass.name + "': a depth buffer that is cleared is written");
    }
    auto& resource = m_graph.GetResource(texture);
    m_graph.AddAccess(m_pass, texture.index, write ? RGAccess::DepthWrite : RGAccess::DepthRead);
    pass.depthAttachment = { texture, clear };

    if (clear.enabled && !resource.hasClearValue)
    {
        resource.hasClearValue = true;
        resource.clearValue = clear;
    }
    return *this;
}

RGPassBuilder& RGPassBuilder::SideEffects()
{
    m_graph.m_passes[m_pass].sideEffects = true;
    return *this;
}

void RGPassBuilder::Execute(RGPassCallback callback)
{
    m_graph.m_passes[m_pass].callback = std::move(callback);
}

// ------------------------------------------------------------------------------------------------
// RenderGraph: declaring a frame

RenderGraph::RenderGraph() = default;
RenderGraph::~RenderGraph() = default;

std::shared_ptr<EV::Resource> RenderGraph::Resource::GetGpuResource() const
{
    if (isBuffer)
    {
        return buffer;
    }
    return texture;
}

uint32_t RenderGraph::AddResource(Resource resource)
{
    // Importing the same texture or buffer twice gives the same handle, so its uses are ordered as one resource.
    if (resource.imported)
    {
        const auto object = resource.GetGpuResource();
        for (uint32_t i = 0; i < m_resources.size(); ++i)
        {
            if (m_resources[i].imported && m_resources[i].GetGpuResource() == object)
            {
                return i;
            }
        }
    }
    m_resources.push_back(std::move(resource));
    return static_cast<uint32_t>(m_resources.size() - 1);
}

RGTexture RenderGraph::Create(const std::string& name, const RGTextureDesc& desc)
{
    Resource resource;
    resource.name = name;
    resource.desc = desc;
    return { AddResource(std::move(resource)) };
}

RGBuffer RenderGraph::Create(const std::string& name, const RGBufferDesc& desc)
{
    if (desc.size == 0)
    {
        throw std::logic_error("Render graph: buffer '" + name + "' has size 0");
    }
    Resource resource;
    resource.name = name;
    resource.isBuffer = true;
    resource.bufferDesc = desc;
    return { AddResource(std::move(resource)) };
}

RGTexture RenderGraph::Import(const std::string& name, const std::shared_ptr<Texture>& texture)
{
    if (!texture)
    {
        throw std::logic_error("Render graph: imported texture '" + name + "' is null");
    }
    Resource resource;
    resource.name = name;
    resource.desc = GetTextureDesc(*texture);
    resource.imported = true;
    resource.texture = texture;
    return { AddResource(std::move(resource)) };
}

RGBuffer RenderGraph::Import(const std::string& name, const std::shared_ptr<Buffer>& buffer, uint32_t stride)
{
    if (!buffer || !buffer->GetD3D12Resource())
    {
        throw std::logic_error("Render graph: imported buffer '" + name + "' is null");
    }
    // Upload and readback heap buffers stay in one state for their whole life, the graph cannot transition them.
    D3D12_HEAP_PROPERTIES heapProperties = {};
    if (SUCCEEDED(buffer->GetD3D12Resource()->GetHeapProperties(&heapProperties, nullptr)) &&
        (heapProperties.Type == D3D12_HEAP_TYPE_UPLOAD || heapProperties.Type == D3D12_HEAP_TYPE_READBACK))
    {
        throw std::logic_error("Render graph: imported buffer '" + name + "' is in an upload or readback heap; "
            "only default heap buffers can be used in the graph");
    }
    Resource resource;
    resource.name = name;
    resource.isBuffer = true;
    resource.bufferDesc.size = buffer->GetD3D12ResourceDesc().Width;
    resource.bufferDesc.stride = stride;
    resource.imported = true;
    resource.buffer = buffer;
    return { AddResource(std::move(resource)) };
}

RGPassBuilder RenderGraph::AddPass(const std::string& name)
{
    Pass pass;
    pass.name = name;
    m_passes.push_back(std::move(pass));
    return RGPassBuilder(*this, static_cast<uint32_t>(m_passes.size() - 1));
}

const RGTextureDesc& RenderGraph::GetDesc(RGTexture texture) const
{
    return GetResource(texture).desc;
}

const RGBufferDesc& RenderGraph::GetDesc(RGBuffer buffer) const
{
    return GetResource(buffer).bufferDesc;
}

RenderGraph::Resource& RenderGraph::GetResource(uint32_t index)
{
    if (index >= m_resources.size())
    {
        throw std::logic_error("Render graph: invalid handle (handles are only valid for the frame they were made in)");
    }
    return m_resources[index];
}

RenderGraph::Resource& RenderGraph::GetResource(RGTexture texture)
{
    auto& resource = GetResource(texture.index);
    if (resource.isBuffer)
    {
        throw std::logic_error("Render graph: '" + resource.name + "' is a buffer, not a texture");
    }
    return resource;
}

const RenderGraph::Resource& RenderGraph::GetResource(RGTexture texture) const
{
    return const_cast<RenderGraph*>(this)->GetResource(texture);
}

RenderGraph::Resource& RenderGraph::GetResource(RGBuffer buffer)
{
    auto& resource = GetResource(buffer.index);
    if (!resource.isBuffer)
    {
        throw std::logic_error("Render graph: '" + resource.name + "' is a texture, not a buffer");
    }
    return resource;
}

const RenderGraph::Resource& RenderGraph::GetResource(RGBuffer buffer) const
{
    return const_cast<RenderGraph*>(this)->GetResource(buffer);
}

void RenderGraph::AddAccess(uint32_t passIndex, uint32_t resourceIndex, RGAccess access)
{
    auto& pass = m_passes[passIndex];
    auto& resource = GetResource(resourceIndex);

    if (resource.isBuffer ? !IsBufferAccess(access) : !IsTextureAccess(access))
    {
        throw std::logic_error("Render graph pass '" + pass.name + "': " + GetAccessName(access) + " cannot be used for " +
            (resource.isBuffer ? "buffer '" : "texture '") + resource.name + "'");
    }

    for (const auto& existing : pass.accesses)
    {
        if (existing.resource == resourceIndex && (IsWriteAccess(existing.access) || IsWriteAccess(access)))
        {
            throw std::logic_error("Render graph pass '" + pass.name + "' uses '" + resource.name + "' as " +
                GetAccessName(existing.access) + " and as " + GetAccessName(access) +
                ". A pass can read a resource several times, but not read and write it (use UnorderedAccess for both).");
        }
    }

    pass.accesses.push_back({ resourceIndex, access });
    resource.flags |= GetRequiredFlags(access);
}

// ------------------------------------------------------------------------------------------------
// RenderGraph: compiling

void RenderGraph::CullPasses()
{
    // Walk backwards, tracking which textures' current contents a later (kept) pass still needs.
    std::vector<bool> contentsNeeded(m_resources.size(), false);
    for (auto it = m_passes.rbegin(); it != m_passes.rend(); ++it)
    {
        Pass& pass = *it;

        // A pass is needed if it writes something that is used later or lives outside the graph. Passes that do
        // not declare any texture cannot be connected to anything, so they are kept as well.
        bool keep = pass.sideEffects || pass.accesses.empty();
        for (const auto& access : pass.accesses)
        {
            if (IsWriteAccess(access.access) && (m_resources[access.resource].imported || contentsNeeded[access.resource]))
            {
                keep = true;
            }
        }
        pass.culled = !keep;
        if (!keep)
        {
            continue;
        }

        // A full overwrite (a cleared attachment, a copy) makes the earlier contents unnecessary; everything
        // else this pass touches depends on what earlier passes left behind.
        for (const auto& access : pass.accesses)
        {
            const bool clearedRenderTarget = access.access == RGAccess::RenderTarget &&
                std::any_of(std::begin(pass.colorAttachments), std::end(pass.colorAttachments),
                    [&](const Attachment& a) { return a.texture.index == access.resource && a.clear.enabled; });
            const bool clearedDepth = access.access == RGAccess::DepthWrite &&
                pass.depthAttachment.texture.index == access.resource && pass.depthAttachment.clear.enabled;
            const bool overwritten = clearedRenderTarget || clearedDepth || access.access == RGAccess::CopyDest;
            contentsNeeded[access.resource] = !overwritten;
        }
    }
}

void RenderGraph::CreateTransientResources()
{
    auto& app = Application::Get();
    std::vector<bool> used(m_resources.size(), false);
    for (const auto& pass : m_passes)
    {
        if (!pass.culled)
        {
            for (const auto& access : pass.accesses)
            {
                used[access.resource] = true;
            }
        }
    }

    for (uint32_t i = 0; i < m_resources.size(); ++i)
    {
        Resource& resource = m_resources[i];
        if (resource.imported || !used[i])
        {
            continue;
        }

        // Reuse a texture or buffer from an earlier frame with the same description, flags and clear value.
        PooledResource* pooled = nullptr;
        for (auto& candidate : m_pool)
        {
            if (candidate.lastUsedFrame == m_frame || candidate.isBuffer != resource.isBuffer || candidate.flags != resource.flags)
            {
                continue;
            }
            const bool sameDesc = resource.isBuffer ? candidate.bufferSize == resource.bufferDesc.size :
                candidate.desc == resource.desc && candidate.hasClearValue == resource.hasClearValue &&
                (!resource.hasClearValue || SameClearValue(candidate.clearValue, resource.clearValue));
            if (sameDesc)
            {
                pooled = &candidate;
                break;
            }
        }

        if (!pooled)
        {
            PooledResource created;
            created.isBuffer = resource.isBuffer;
            created.flags = resource.flags;

            if (resource.isBuffer)
            {
                const auto desc = CD3DX12_RESOURCE_DESC::Buffer(resource.bufferDesc.size, resource.flags);
                created.bufferSize = resource.bufferDesc.size;
                created.buffer = app.CreateBuffer(desc);
                created.size = app.GetDevice()->GetResourceAllocationInfo(0, 1, &desc).SizeInBytes;
            }
            else
            {
                const auto desc = CD3DX12_RESOURCE_DESC::Tex2D(resource.desc.format, resource.desc.width, resource.desc.height,
                    resource.desc.depthOrArraySize, resource.desc.mipLevels, 1, 0, resource.flags);

                // Attachments get an optimized clear value: the first clear of the frame, or black / far depth.
                D3D12_CLEAR_VALUE clearValue = {};
                const D3D12_CLEAR_VALUE* clearValuePointer = nullptr;
                if (resource.flags & (D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL))
                {
                    clearValue.Format = resource.desc.format;
                    if (resource.flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)
                    {
                        clearValue.DepthStencil = { resource.hasClearValue ? resource.clearValue.depth : 1.0f, 0 };
                    }
                    else if (resource.hasClearValue)
                    {
                        memcpy(clearValue.Color, resource.clearValue.color, sizeof(clearValue.Color));
                    }
                    clearValuePointer = &clearValue;
                }

                created.desc = resource.desc;
                created.hasClearValue = resource.hasClearValue;
                created.clearValue = resource.clearValue;
                created.texture = app.CreateTexture(desc, clearValuePointer);
                created.size = app.GetDevice()->GetResourceAllocationInfo(0, 1, &desc).SizeInBytes;
            }
            m_pool.push_back(std::move(created));
            pooled = &m_pool.back();
        }

        pooled->lastUsedFrame = m_frame;
        resource.texture = pooled->texture;
        resource.buffer = pooled->buffer;

        // Pooled resources carry the name of what they are used for (PIX, the editor and DRED show it).
        const std::wstring name = ConvertString(resource.name);
        auto object = resource.GetGpuResource();
        if (object->GetName() != name)
        {
            object->SetName(name);
        }
    }
}

void RenderGraph::ReleaseUnusedResources()
{
    // Command lists still in flight keep their own references to the textures and buffers.
    m_pool.erase(std::remove_if(m_pool.begin(), m_pool.end(),
        [&](const PooledResource& pooled) { return pooled.lastUsedFrame + g_keepUnusedResourcesFrames < m_frame; }),
        m_pool.end());
}

// ------------------------------------------------------------------------------------------------
// RenderGraph: executing

void RenderGraph::Execute(const std::shared_ptr<CommandList>& commandList)
{
    // Whatever happens, the next frame starts with an empty graph.
    struct ResetFrame
    {
        RenderGraph& graph;
        ~ResetFrame()
        {
            graph.m_passes.clear();
            graph.m_resources.clear();
            ++graph.m_frame;
        }
    } resetFrame{ *this };

    CullPasses();
    CreateTransientResources();
    ReadCapturedBuffer();

    if (m_profilingEnabled)
    {
        if (!m_gpuTimer)
        {
            m_gpuTimer = std::make_unique<GpuTimer>();
        }
        m_gpuTimer->BeginFrame();
    }
    m_captureFound = false;

    std::vector<bool> lastUseWasUAV(m_resources.size(), false);
    for (auto& pass : m_passes)
    {
        if (!pass.culled)
        {
            ExecutePass(pass, commandList, lastUseWasUAV);
        }
    }

    if (m_profilingEnabled)
    {
        m_gpuTimer->EndFrame(*commandList);
    }

    UpdateDebugInfo();
    ReleaseUnusedResources();
}

void RenderGraph::ExecutePass(Pass& pass, const std::shared_ptr<CommandList>& commandList, std::vector<bool>& lastUseWasUAV)
{
    CommandList& cl = *commandList;
    auto d3d12CommandList = cl.GetCommandList();

    // Named for PIX, RenderDoc and DRED (metadata 0 is a wide string).
    const std::wstring eventName = ConvertString(pass.name);
    d3d12CommandList->BeginEvent(0, eventName.c_str(), static_cast<UINT>((eventName.size() + 1) * sizeof(wchar_t)));

    // Move every resource to the state this pass needs. Several reads of one resource combine their states.
    std::vector<D3D12_RESOURCE_STATES> states(m_resources.size(), D3D12_RESOURCE_STATES(-1));
    for (const auto& access : pass.accesses)
    {
        auto& state = states[access.resource];
        state = state == D3D12_RESOURCE_STATES(-1) ? GetResourceState(access.access) : state | GetResourceState(access.access);
    }
    for (uint32_t i = 0; i < states.size(); ++i)
    {
        if (states[i] == D3D12_RESOURCE_STATES(-1))
        {
            continue;
        }
        const auto object = m_resources[i].GetGpuResource();
        // UAV to UAV is not a transition, but the previous pass's writes have to finish first.
        const bool uav = states[i] == D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        if (uav && lastUseWasUAV[i])
        {
            cl.UAVBarrier(object);
        }
        cl.TransitionBarrier(object, states[i]);
        lastUseWasUAV[i] = uav;
    }
    cl.FlushResourceBarriers();

    const uint32_t timerScope = m_profilingEnabled ? m_gpuTimer->Begin(cl, pass.name) : UINT32_MAX;

    RGPassContext context(*this, commandList);

    // Attachments: bind, clear, and cover them with the viewport.
    bool hasAttachments = false;
    RGTextureDesc viewportSize;
    for (uint32_t slot = 0; slot < _countof(pass.colorAttachments); ++slot)
    {
        const auto& attachment = pass.colorAttachments[slot];
        if (!attachment.texture.IsValid())
        {
            continue;
        }
        const auto& resource = m_resources[attachment.texture.index];
        context.m_renderTarget.AttachTexture(static_cast<AttachmentPoint>(static_cast<int>(AttachmentPoint::Color0) + slot), resource.texture);
        if (attachment.clear.enabled)
        {
            float color[4] = { attachment.clear.color[0], attachment.clear.color[1], attachment.clear.color[2], attachment.clear.color[3] };
            cl.ClearTexture(resource.texture, color);
        }
        if (!hasAttachments)
        {
            viewportSize = resource.desc;
            hasAttachments = true;
        }
    }
    if (pass.depthAttachment.texture.IsValid())
    {
        const auto& resource = m_resources[pass.depthAttachment.texture.index];
        context.m_renderTarget.AttachTexture(AttachmentPoint::DepthStencil, resource.texture);
        if (pass.depthAttachment.clear.enabled)
        {
            cl.ClearDepthStencilTexture(resource.texture, D3D12_CLEAR_FLAG_DEPTH, pass.depthAttachment.clear.depth);
        }
        if (!hasAttachments)
        {
            viewportSize = resource.desc;
            hasAttachments = true;
        }
    }
    if (hasAttachments)
    {
        context.m_viewport = CD3DX12_VIEWPORT(0.0f, 0.0f, static_cast<float>(viewportSize.width), static_cast<float>(viewportSize.height));
        cl.SetRenderTarget(context.m_renderTarget);
        cl.SetViewport(context.m_viewport);
        cl.SetScissorRect(CD3DX12_RECT(0, 0, static_cast<LONG>(viewportSize.width), static_cast<LONG>(viewportSize.height)));
    }

    if (pass.callback)
    {
        pass.callback(context);
    }

    if (m_profilingEnabled)
    {
        m_gpuTimer->End(cl, timerScope);
    }

    if (!m_capturePass.empty() && pass.name == m_capturePass)
    {
        CapturePass(pass, cl);
        // The capture copy moved the resource to COPY_SOURCE; the next pass that uses it transitions it again.
        for (const auto& access : pass.accesses)
        {
            lastUseWasUAV[access.resource] = false;
        }
    }

    d3d12CommandList->EndEvent();
}

// ------------------------------------------------------------------------------------------------
// Debugging

void RenderGraph::SetCapture(const std::string& passName, const std::string& resourceName)
{
    if (passName != m_capturePass || resourceName != m_captureResource)
    {
        // Results of the previous capture must not be shown as this one.
        ClearCapture();
        m_capturePass = passName;
        m_captureResource = resourceName;
    }
}

void RenderGraph::ClearCapture()
{
    m_capturePass.clear();
    m_captureResource.clear();
    m_capturedTexture.reset();
    m_capturedBufferData.clear();
    m_capturedBufferDesc = {};
    for (auto& readback : m_bufferReadbacks)
    {
        readback.size = 0;
    }
    m_captureFound = false;
    m_captureIsBuffer = false;
}

void RenderGraph::CapturePass(const Pass& pass, CommandList& commandList)
{
    const Resource* source = nullptr;
    for (const auto& access : pass.accesses)
    {
        if (m_resources[access.resource].name == m_captureResource)
        {
            source = &m_resources[access.resource];
            break;
        }
    }
    if (!source || !source->GetGpuResource() || !source->GetGpuResource()->GetD3D12Resource())
    {
        return;
    }
    m_captureIsBuffer = source->isBuffer;
    if (source->isBuffer)
    {
        CaptureBuffer(*source, commandList);
        return;
    }

    // CopyResource needs a texture with the same description (also its flags, for depth buffers).
    const auto sourceDesc = source->texture->GetD3D12ResourceDesc();
    const auto captureDesc = m_capturedTexture ? m_capturedTexture->GetD3D12ResourceDesc() : D3D12_RESOURCE_DESC{};
    if (!m_capturedTexture || memcmp(&captureDesc, &sourceDesc, sizeof(sourceDesc)) != 0)
    {
        m_capturedTexture = Application::Get().CreateTexture(sourceDesc);
        m_capturedTexture->SetName(g_captureTextureName);
    }
    commandList.CopyResource(m_capturedTexture, source->texture);
    m_captureFound = true;
}

void RenderGraph::CaptureBuffer(const Resource& source, CommandList& commandList)
{
    // Each frame in flight copies into its own readback buffer; ReadCapturedBuffer reads it ReadbackFrames
    // frames later, when the GPU is done with it.
    BufferReadback& readback = m_bufferReadbacks[m_frame % ReadbackFrames];
    const uint64_t size = std::min<uint64_t>(source.bufferDesc.size, MaxBufferCaptureSize);
    if (!readback.buffer || readback.capacity < size)
    {
        CD3DX12_HEAP_PROPERTIES heapProperties(D3D12_HEAP_TYPE_READBACK);
        const auto desc = CD3DX12_RESOURCE_DESC::Buffer(size);
        ThrowIfFailed(Application::Get().GetDevice()->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback.buffer)));
        readback.buffer->SetName(L"Render Graph Buffer Capture");
        readback.capacity = size;
    }

    commandList.TransitionBarrier(source.buffer, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, true);
    commandList.GetCommandList()->CopyBufferRegion(readback.buffer.Get(), 0, source.buffer->GetD3D12Resource().Get(), 0, size);
    commandList.TrackResource(readback.buffer);
    commandList.TrackResource(source.buffer->GetD3D12Resource());

    readback.size = size;
    readback.desc = source.bufferDesc;
    m_captureFound = true;
}

void RenderGraph::ReadCapturedBuffer()
{
    // This slot was filled ReadbackFrames frames ago.
    BufferReadback& readback = m_bufferReadbacks[m_frame % ReadbackFrames];
    if (readback.size == 0)
    {
        return;
    }

    const D3D12_RANGE readRange = { 0, static_cast<SIZE_T>(readback.size) };
    void* data = nullptr;
    if (SUCCEEDED(readback.buffer->Map(0, &readRange, &data)))
    {
        const auto* bytes = static_cast<const uint8_t*>(data);
        m_capturedBufferData.assign(bytes, bytes + readback.size);
        m_capturedBufferDesc = readback.desc;
        const D3D12_RANGE writtenRange = { 0, 0 };
        readback.buffer->Unmap(0, &writtenRange);
    }
    readback.size = 0;
}

void RenderGraph::UpdateDebugInfo()
{
    // Timings are a few frames old; match them to passes by name (and occurrence, for repeated names).
    std::map<std::pair<std::string, int>, float> timeByPass;
    if (m_profilingEnabled && m_gpuTimer)
    {
        const auto& passTimes = m_gpuTimer->GetResults();
        std::map<std::string, int> occurrences;
        for (const auto& result : passTimes)
        {
            timeByPass[{ result.name, occurrences[result.name]++ }] = result.milliseconds;
        }
    }

    RenderGraphDebugInfo info;
    info.frame = m_frame;

    std::map<std::string, int> occurrences;
    for (const auto& pass : m_passes)
    {
        RenderGraphDebugInfo::Pass passInfo;
        passInfo.name = pass.name;
        passInfo.culled = pass.culled;
        passInfo.raster = pass.depthAttachment.texture.IsValid() ||
            std::any_of(std::begin(pass.colorAttachments), std::end(pass.colorAttachments), [](const Attachment& a) { return a.texture.IsValid(); });
        for (const auto& access : pass.accesses)
        {
            passInfo.accesses.push_back({ access.resource, access.access });
        }
        if (!pass.culled)
        {
            auto time = timeByPass.find({ pass.name, occurrences[pass.name]++ });
            if (time != timeByPass.end())
            {
                passInfo.gpuMilliseconds = time->second;
                info.gpuMilliseconds += time->second;
            }
        }
        info.passes.push_back(std::move(passInfo));
    }

    for (const auto& resource : m_resources)
    {
        RenderGraphDebugInfo::Resource resourceInfo;
        resourceInfo.name = resource.name;
        resourceInfo.isBuffer = resource.isBuffer;
        resourceInfo.desc = resource.desc;
        resourceInfo.bufferDesc = resource.bufferDesc;
        resourceInfo.imported = resource.imported;
        resourceInfo.texture = resource.texture;
        info.resources.push_back(std::move(resourceInfo));
    }
    for (int passIndex = 0; passIndex < static_cast<int>(m_passes.size()); ++passIndex)
    {
        if (m_passes[passIndex].culled)
        {
            continue;
        }
        for (const auto& access : m_passes[passIndex].accesses)
        {
            auto& resourceInfo = info.resources[access.resource];
            if (resourceInfo.firstPass < 0)
            {
                resourceInfo.firstPass = passIndex;
            }
            resourceInfo.lastPass = passIndex;
        }
    }

    for (const auto& pooled : m_pool)
    {
        info.transientMemory += pooled.size;
    }
    info.captureFound = m_captureFound;
    info.captureIsBuffer = m_captureIsBuffer;

    m_debugInfo = std::move(info);
}
