#include <dx12_includes.h>

#include <render_target.h>

#include "application.h"
#include "resource_state_tracker.h"

RenderTarget::RenderTarget()
    : m_textures(AttachmentPoint::NumAttachmentPoints)
{
}

// Attach a texture to the render target.
// The texture will be copied into the texture array.
void RenderTarget::AttachTexture(AttachmentPoint attachmentPoint, std::shared_ptr<Texture> texture)
{
    m_textures[attachmentPoint] = texture;

    // if (texture && texture->GetD3D12Resource())
    // {
    //     auto desc = texture->GetD3D12ResourceDesc();
    //
    //     m_Size.x = static_cast<uint32_t>(desc.Width);
    //     m_Size.y = static_cast<uint32_t>(desc.Height);
    // }
}

std::shared_ptr<Texture> RenderTarget::GetTexture(AttachmentPoint attachmentPoint) const
{
    return m_textures[attachmentPoint];
}

void RenderTarget::Resize(uint32_t width, uint32_t height)
{
    for (auto texture : m_textures) { if (texture) texture->Resize(width, height); }
}

// Resize all of the textures associated with the render target.
// void Texture::Resize(uint32_t width, uint32_t height, uint32_t depthOrArraySize)
// {
//     if (m_resource)
//     {
//         // ResourceStateTracker::RemoveGlobalResourceState( m_d3d12Resource.Get() );
//
//         CD3DX12_RESOURCE_DESC resDesc(m_resource->GetDesc());
//
//         resDesc.Width = std::max(width, 1u);
//         resDesc.Height = std::max(height, 1u);
//         resDesc.DepthOrArraySize = depthOrArraySize;
//         resDesc.MipLevels = resDesc.SampleDesc.Count > 1 ? 1 : 0;
//
//         auto d3d12Device = Application::Get().GetDevice();
//
//         CD3DX12_HEAP_PROPERTIES heapProp(D3D12_HEAP_TYPE_DEFAULT);
//
//         ThrowIfFailed(d3d12Device->CreateCommittedResource(
//             &heapProp, D3D12_HEAP_FLAG_NONE, &resDesc,
//             D3D12_RESOURCE_STATE_COMMON, m_clearValue.get(), IID_PPV_ARGS(&m_resource)));
//
//         // Retain the name of the resource if one was already specified.
//         m_resource->SetName(m_resourceName.c_str());
//
//         ResourceStateTracker::AddGlobalResourceState(m_resource.Get(), D3D12_RESOURCE_STATE_COMMON);
//
//         CreateViews();
//     }
// }

// Get a list of the textures attached to the render target.
// This method is primarily used by the CommandList when binding the
// render target to the output merger stage of the rendering pipeline.
const std::vector<std::shared_ptr<Texture>>& RenderTarget::GetTextures() const
{
    return m_textures;
}

D3D12_RT_FORMAT_ARRAY RenderTarget::GetRenderTargetFormats() const
{
    D3D12_RT_FORMAT_ARRAY rtvFormats = {};

    for (int i = AttachmentPoint::Color0; i <= AttachmentPoint::Color7; ++i)
    {
        auto texture = m_textures[i];
        if (texture)
        {
            rtvFormats.RTFormats[rtvFormats.NumRenderTargets++] = texture->GetD3D12ResourceDesc().Format;
        }
    }

    return rtvFormats;
}

DXGI_FORMAT RenderTarget::GetDepthStencilFormat() const
{
    DXGI_FORMAT    dsvFormat = DXGI_FORMAT_UNKNOWN;
    auto depthStencilTexture = m_textures[AttachmentPoint::DepthStencil];
    if (depthStencilTexture)
    {
        dsvFormat = depthStencilTexture->GetD3D12ResourceDesc().Format;
    }

    return dsvFormat;
}


DXGI_SAMPLE_DESC RenderTarget::GetSampleDesc() const
{
    DXGI_SAMPLE_DESC sampleDesc = { 1, 0 };
    for (int i = AttachmentPoint::Color0; i <= AttachmentPoint::Color7; ++i)
    {
        auto texture = m_textures[i];
        if (texture)
        {
            sampleDesc = texture->GetD3D12ResourceDesc().SampleDesc;
            break;
        }
    }

    return sampleDesc;
}