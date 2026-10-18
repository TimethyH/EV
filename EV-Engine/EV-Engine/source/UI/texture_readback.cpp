#include "DX12/dx12_includes.h"

#include "UI/texture_readback.h"

#include <cmath>
#include <limits>

#include "core/application.h"
#include "DX12/command_list.h"
#include "DX12/command_queue.h"
#include "resources/texture.h"
#include "UI/format_utils.h"
#include "utility/helpers.h"

using namespace EV;

namespace
{
    // Larger subresources are not read back for the auto range (keeps the copy below 256 MB).
    constexpr uint64_t g_maxRangeTexels = 4096ull * 4096ull;
}

TextureReadback::TextureReadback(CommandQueue& commandQueue)
    : m_commandQueue(commandQueue)
{
}

bool TextureReadback::CanReadback(const Texture& texture)
{
    const auto desc = texture.GetD3D12ResourceDesc();
    if (desc.SampleDesc.Count > 1 || (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D &&
        desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE3D))
    {
        return false;
    }
    return FormatUtils::GetTexelSize(FormatUtils::GetShaderReadableFormat(desc.Format)) > 0;
}

void TextureReadback::RequestTexel(const std::shared_ptr<Texture>& texture, uint32_t mip, uint32_t slice, uint32_t x, uint32_t y)
{
    if (!texture || !CanReadback(*texture))
    {
        return;
    }

    if (m_currentTexture != texture.get())
    {
        m_currentTexture = texture.get();
        m_texelResult = {};
        m_rangeResult = {};
    }

    std::erase_if(m_requests, [](const Request& r) { return r.kind == Kind::Texel; });
    m_requests.push_back({ Kind::Texel, texture, mip, slice, x, y });
}

void TextureReadback::RequestRange(const std::shared_ptr<Texture>& texture, uint32_t mip, uint32_t slice)
{
    if (!texture || !CanReadback(*texture))
    {
        return;
    }

    if (m_currentTexture != texture.get())
    {
        m_currentTexture = texture.get();
        m_texelResult = {};
        m_rangeResult = {};
    }

    std::erase_if(m_requests, [](const Request& r) { return r.kind == Kind::Range; });
    m_requests.push_back({ Kind::Range, texture, mip, slice, 0, 0 });
}

bool TextureReadback::IsRangePending() const
{
    for (const auto& request : m_requests)
    {
        if (request.kind == Kind::Range) return true;
    }
    for (const auto& readback : m_inFlight)
    {
        if (readback.request.kind == Kind::Range) return true;
    }
    return false;
}

void TextureReadback::Record(CommandList& commandList)
{
    auto device = Application::Get().GetDevice();

    for (auto& request : m_requests)
    {
        const auto desc = request.texture->GetD3D12ResourceDesc();
        const bool is3D = desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D;

        const uint32_t mip = std::min<uint32_t>(request.mip, desc.MipLevels - 1);
        const uint32_t mipWidth = std::max<uint32_t>(1, static_cast<uint32_t>(desc.Width >> mip));
        const uint32_t mipHeight = std::max<uint32_t>(1, desc.Height >> mip);
        const uint32_t sliceCount = is3D ? std::max<uint32_t>(1, desc.DepthOrArraySize >> mip) : desc.DepthOrArraySize;
        const uint32_t slice = std::min(request.slice, sliceCount - 1);
        const uint32_t subresource = is3D ? mip : D3D12CalcSubresource(mip, slice, 0, desc.MipLevels, desc.DepthOrArraySize);

        InFlight readback;
        readback.request = request;
        readback.request.mip = mip;
        readback.request.slice = slice;

        D3D12_BOX box = {};
        if (request.kind == Kind::Texel)
        {
            if (request.x >= mipWidth || request.y >= mipHeight)
            {
                continue;
            }
            box = { request.x, request.y, is3D ? slice : 0, request.x + 1, request.y + 1, (is3D ? slice : 0) + 1 };
            readback.width = 1;
            readback.height = 1;
        }
        else
        {
            if (static_cast<uint64_t>(mipWidth) * mipHeight > g_maxRangeTexels)
            {
                continue;
            }
            box = { 0, 0, is3D ? slice : 0, mipWidth, mipHeight, (is3D ? slice : 0) + 1 };
            readback.width = mipWidth;
            readback.height = mipHeight;
        }

        // The footprint gives the copyable format (typeless for depth), the row pitch is computed for the region.
        device->GetCopyableFootprints(&desc, subresource, 1, 0, &readback.footprint, nullptr, nullptr, nullptr);
        readback.format = readback.footprint.Footprint.Format;

        const uint32_t texelSize = FormatUtils::GetTexelSize(FormatUtils::GetShaderReadableFormat(readback.format));
        readback.footprint.Offset = 0;
        readback.footprint.Footprint.Width = readback.width;
        readback.footprint.Footprint.Height = readback.height;
        readback.footprint.Footprint.Depth = 1;
        readback.footprint.Footprint.RowPitch = Math::AlignUp(readback.width * texelSize, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);

        const uint64_t bufferSize = static_cast<uint64_t>(readback.footprint.Footprint.RowPitch) * readback.height;
        CD3DX12_HEAP_PROPERTIES heapProperties(D3D12_HEAP_TYPE_READBACK);
        auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize);
        ThrowIfFailed(device->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &bufferDesc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback.buffer)));

        commandList.TransitionBarrier(request.texture, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, true);

        CD3DX12_TEXTURE_COPY_LOCATION dst(readback.buffer.Get(), readback.footprint);
        CD3DX12_TEXTURE_COPY_LOCATION src(request.texture->GetD3D12Resource().Get(), subresource);
        commandList.GetCommandList()->CopyTextureRegion(&dst, 0, 0, 0, &src, &box);

        commandList.TrackResource(readback.buffer);
        commandList.TrackResource(request.texture->GetD3D12Resource());

        // Leave the texture in the common state so any queue can use it next.
        commandList.TransitionBarrier(request.texture, D3D12_RESOURCE_STATE_COMMON);

        m_inFlight.push_back(std::move(readback));
    }
    m_requests.clear();
}

void TextureReadback::Update()
{
    // The command list recorded last frame has been submitted by now; fence it.
    for (auto& readback : m_inFlight)
    {
        if (readback.fenceValue == 0)
        {
            readback.fenceValue = m_commandQueue.Signal();
        }
    }

    auto it = m_inFlight.begin();
    while (it != m_inFlight.end())
    {
        if (m_commandQueue.IsFenceComplete(it->fenceValue))
        {
            if (it->request.texture.get() == m_currentTexture)
            {
                Resolve(*it);
            }
            it = m_inFlight.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void TextureReadback::Resolve(const InFlight& readback)
{
    const DXGI_FORMAT format = FormatUtils::GetShaderReadableFormat(readback.format);
    const uint32_t texelSize = FormatUtils::GetTexelSize(format);
    const uint32_t channels = std::min<uint32_t>(4, FormatUtils::GetChannelCount(format));
    const uint64_t bufferSize = static_cast<uint64_t>(readback.footprint.Footprint.RowPitch) * readback.height;

    void* mapped = nullptr;
    D3D12_RANGE readRange = { 0, static_cast<SIZE_T>(bufferSize) };
    if (FAILED(readback.buffer->Map(0, &readRange, &mapped)))
    {
        return;
    }
    const auto* bytes = static_cast<const uint8_t*>(mapped);

    if (readback.request.kind == Kind::Texel)
    {
        TexelResult result;
        result.valid = FormatUtils::DecodeTexel(format, bytes, result.value);
        result.x = readback.request.x;
        result.y = readback.request.y;
        result.mip = readback.request.mip;
        result.slice = readback.request.slice;
        m_texelResult = result;
    }
    else
    {
        RangeResult result;
        result.mip = readback.request.mip;
        result.slice = readback.request.slice;
        for (uint32_t c = 0; c < 4; ++c)
        {
            result.min[c] = std::numeric_limits<float>::max();
            result.max[c] = std::numeric_limits<float>::lowest();
        }

        float value[4];
        for (uint32_t y = 0; y < readback.height; ++y)
        {
            const uint8_t* row = bytes + static_cast<uint64_t>(y) * readback.footprint.Footprint.RowPitch;
            for (uint32_t x = 0; x < readback.width; ++x)
            {
                if (!FormatUtils::DecodeTexel(format, row + static_cast<uint64_t>(x) * texelSize, value))
                {
                    break;
                }
                for (uint32_t c = 0; c < channels; ++c)
                {
                    // Skip NaN and infinity so a few bad texels do not hide the useful range.
                    if (std::isfinite(value[c]))
                    {
                        result.min[c] = std::min(result.min[c], value[c]);
                        result.max[c] = std::max(result.max[c], value[c]);
                        result.valid = true;
                    }
                }
            }
        }
        for (uint32_t c = channels; c < 4; ++c)
        {
            result.min[c] = result.max[c] = 1.0f;
        }
        m_rangeResult = result;
    }

    D3D12_RANGE writeRange = { 0, 0 };
    readback.buffer->Unmap(0, &writeRange);
}
