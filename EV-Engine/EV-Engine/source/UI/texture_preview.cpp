#include "DX12/dx12_includes.h"

#include "UI/texture_preview.h"

#include <d3dcompiler.h>

#include "core/application.h"
#include "DX12/command_list.h"
#include "DX12/pipeline_state_object.h"
#include "DX12/render_target.h"
#include "DX12/root_signature.h"
#include "DX12/shader_resource_view.h"
#include "resources/texture.h"
#include "UI/format_utils.h"
#include "utility/helpers.h"

using namespace EV;

namespace
{
    // Compiled at runtime so the engine library does not need a shader build step.
    const char* g_previewShader = R"(
cbuffer PreviewParams : register(b0)
{
    float4 ChannelMask;
    float  RangeMin;
    float  RangeMax;
    float  Mip;
    float  Slice;
    float  NearZ;
    float  FarZ;
    uint   Flags;
    float  Padding;
};

#define FLAG_LINEARIZE_DEPTH 1
#define FLAG_ABSOLUTE        2
#define FLAG_SRGB            4
#define FLAG_LINEAR_FILTER   8

#if defined(TEX_3D)
Texture3D<float4> Source : register(t0);
#elif defined(TEX_UINT)
Texture2DArray<uint4> Source : register(t0);
#elif defined(TEX_SINT)
Texture2DArray<int4> Source : register(t0);
#else
Texture2DArray<float4> Source : register(t0);
#endif

SamplerState PointSampler  : register(s0);
SamplerState LinearSampler : register(s1);

struct VSOutput
{
    float4 Position : SV_Position;
    float2 UV       : TEXCOORD0;
};

VSOutput VSMain(uint vertexID : SV_VertexID)
{
    // Fullscreen triangle.
    VSOutput output;
    output.UV = float2((vertexID << 1) & 2, vertexID & 2);
    output.Position = float4(output.UV * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return output;
}

float3 LinearToSRGB(float3 color)
{
    float3 low  = color * 12.92f;
    float3 high = 1.055f * pow(max(color, 0.0f), 1.0f / 2.4f) - 0.055f;
    return lerp(high, low, step(color, 0.0031308f));
}

float4 ReadSource(float2 uv)
{
#if defined(TEX_UINT) || defined(TEX_SINT)
    uint width, height, elements, levels;
    Source.GetDimensions((uint)Mip, width, height, elements, levels);
    int2 texel = min(int2(uv * float2(width, height)), int2(width, height) - 1);
    return float4(Source.Load(int4(texel, (int)Slice, (int)Mip)));
#elif defined(TEX_3D)
    float3 uvw = float3(uv, Slice);
    if (Flags & FLAG_LINEAR_FILTER)
        return Source.SampleLevel(LinearSampler, uvw, Mip);
    return Source.SampleLevel(PointSampler, uvw, Mip);
#else
    float3 uvw = float3(uv, Slice);
    if (Flags & FLAG_LINEAR_FILTER)
        return Source.SampleLevel(LinearSampler, uvw, Mip);
    return Source.SampleLevel(PointSampler, uvw, Mip);
#endif
}

float4 PSMain(VSOutput input) : SV_Target
{
    float4 value = ReadSource(input.UV);

    if (Flags & FLAG_LINEARIZE_DEPTH)
    {
        // Standard (non reversed) perspective depth to view space distance.
        float depth = value.r;
        value.r = NearZ * FarZ / (FarZ - depth * (FarZ - NearZ));
    }
    if (Flags & FLAG_ABSOLUTE)
    {
        value = abs(value);
    }

    value = (value - RangeMin) / max(RangeMax - RangeMin, 1e-20f);

    float channelCount = dot(ChannelMask, 1.0f);
    float3 color;
    if (channelCount == 1.0f)
    {
        // A single channel is shown as grayscale.
        color = dot(value, ChannelMask).xxx;
    }
    else
    {
        color = value.rgb * ChannelMask.rgb;
    }
    color = saturate(color);

    if (Flags & FLAG_SRGB)
    {
        color = LinearToSRGB(color);
    }

    // Show alpha by blending over a checkerboard.
    if (ChannelMask.a > 0.0f && channelCount > 1.0f)
    {
        float2 cell = floor(input.Position.xy / 8.0f);
        float checker = fmod(cell.x + cell.y, 2.0f) > 0.5f ? 0.45f : 0.3f;
        color = lerp(checker.xxx, color, saturate(value.a));
    }

    return float4(color, 1.0f);
}
)";

    struct PreviewParams
    {
        float    channelMask[4];
        float    rangeMin;
        float    rangeMax;
        float    mip;
        float    slice;
        float    nearZ;
        float    farZ;
        uint32_t flags;
        float    padding;
    };

    enum PreviewFlags : uint32_t
    {
        FlagLinearizeDepth = 1,
        FlagAbsolute = 2,
        FlagSRGB = 4,
        FlagLinearFilter = 8,
    };

    enum RootParameters
    {
        ParamsCB,
        SourceTexture,
        NumRootParameters
    };

    constexpr DXGI_FORMAT g_previewFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

    // Previews and views that were not used for this many frames are released.
    constexpr uint64_t g_releaseAfterFrames = 120;

    Microsoft::WRL::ComPtr<ID3DBlob> CompileShader(const char* entryPoint, const char* target, const D3D_SHADER_MACRO* defines)
    {
        Microsoft::WRL::ComPtr<ID3DBlob> shader;
        Microsoft::WRL::ComPtr<ID3DBlob> errors;

        HRESULT hr = D3DCompile(g_previewShader, strlen(g_previewShader), "TexturePreview", defines, nullptr, entryPoint,
            target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &shader, &errors);
        if (FAILED(hr))
        {
            if (errors)
            {
                OutputDebugStringA(static_cast<const char*>(errors->GetBufferPointer()));
            }
            ThrowIfFailed(hr);
        }
        return shader;
    }
}

TexturePreview::TexturePreview()
{
    auto& app = Application::Get();

    CD3DX12_DESCRIPTOR_RANGE1 sourceRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0,
        D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE);

    CD3DX12_ROOT_PARAMETER1 rootParameters[NumRootParameters];
    rootParameters[ParamsCB].InitAsConstants(sizeof(PreviewParams) / 4, 0, 0, D3D12_SHADER_VISIBILITY_PIXEL);
    rootParameters[SourceTexture].InitAsDescriptorTable(1, &sourceRange, D3D12_SHADER_VISIBILITY_PIXEL);

    CD3DX12_STATIC_SAMPLER_DESC samplers[2] = {
        CD3DX12_STATIC_SAMPLER_DESC(0, D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
            D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP),
        CD3DX12_STATIC_SAMPLER_DESC(1, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
            D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP),
    };

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDescription;
    rootSignatureDescription.Init_1_1(NumRootParameters, rootParameters, _countof(samplers), samplers,
        D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS | D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS);

    m_rootSignature = app.CreateRootSignature(rootSignatureDescription.Desc_1_1);

    auto vs = CompileShader("VSMain", "vs_5_1", nullptr);

    const D3D_SHADER_MACRO variantDefines[NumVariants][2] = {
        { { nullptr, nullptr }, { nullptr, nullptr } },
        { { "TEX_3D", "1" }, { nullptr, nullptr } },
        { { "TEX_UINT", "1" }, { nullptr, nullptr } },
        { { "TEX_SINT", "1" }, { nullptr, nullptr } },
    };

    struct PreviewPipelineStateStream
    {
        CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE        pRootSignature;
        CD3DX12_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY    PrimitiveTopologyType;
        CD3DX12_PIPELINE_STATE_STREAM_VS                    VS;
        CD3DX12_PIPELINE_STATE_STREAM_PS                    PS;
        CD3DX12_PIPELINE_STATE_STREAM_RASTERIZER            Rasterizer;
        CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL         DepthStencil;
        CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS RTVFormats;
    };

    CD3DX12_RASTERIZER_DESC rasterizerDesc(D3D12_DEFAULT);
    rasterizerDesc.CullMode = D3D12_CULL_MODE_NONE;

    CD3DX12_DEPTH_STENCIL_DESC depthStencilDesc(D3D12_DEFAULT);
    depthStencilDesc.DepthEnable = FALSE;

    D3D12_RT_FORMAT_ARRAY rtvFormats = {};
    rtvFormats.NumRenderTargets = 1;
    rtvFormats.RTFormats[0] = g_previewFormat;

    for (int variant = 0; variant < NumVariants; ++variant)
    {
        auto ps = CompileShader("PSMain", "ps_5_1", variantDefines[variant]);

        PreviewPipelineStateStream stream;
        stream.pRootSignature = m_rootSignature->GetRootSignature().Get();
        stream.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        stream.VS = CD3DX12_SHADER_BYTECODE(vs.Get());
        stream.PS = CD3DX12_SHADER_BYTECODE(ps.Get());
        stream.Rasterizer = rasterizerDesc;
        stream.DepthStencil = depthStencilDesc;
        stream.RTVFormats = rtvFormats;

        m_pipelineStates[variant] = app.CreatePipelineStateObject(stream);
    }
}

TexturePreview::~TexturePreview() = default;

bool TexturePreview::CanPreview(const Texture& texture)
{
    const auto desc = texture.GetD3D12ResourceDesc();
    if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D && desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE3D)
    {
        return false;
    }
    if (desc.SampleDesc.Count > 1 || (desc.Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE) != 0)
    {
        return false;
    }
    return FormatUtils::GetClass(texture.GetShaderResourceFormat()) != FormatClass::Unsupported;
}

uint32_t TexturePreview::GetSliceCount(const Texture& texture)
{
    const auto desc = texture.GetD3D12ResourceDesc();
    return std::max<uint32_t>(1, desc.DepthOrArraySize);
}

TexturePreview::SourceView* TexturePreview::GetSourceView(const std::shared_ptr<Texture>& source)
{
    auto resource = source->GetD3D12Resource().Get();
    auto& view = m_sourceViews[source.get()];

    if (view.srv && view.resource == resource && view.texture.lock() == source)
    {
        view.lastUsedFrame = m_frame;
        return &view;
    }

    const auto desc = source->GetD3D12ResourceDesc();
    const DXGI_FORMAT format = FormatUtils::GetShaderReadableFormat(source->GetShaderResourceFormat());

    // View every mip and slice. Cubemaps are viewed as 2D arrays so each face can be selected.
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = format;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

    Variant variant = Float2DArray;
    if (desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D)
    {
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
        srvDesc.Texture3D.MipLevels = desc.MipLevels;
        variant = Float3D;
    }
    else
    {
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        srvDesc.Texture2DArray.MipLevels = desc.MipLevels;
        srvDesc.Texture2DArray.ArraySize = desc.DepthOrArraySize;

        switch (FormatUtils::GetClass(format))
        {
        case FormatClass::Uint:
            variant = Uint2DArray;
            break;
        case FormatClass::Sint:
            variant = Sint2DArray;
            break;
        default:
            break;
        }
    }

    view.texture = source;
    view.resource = resource;
    view.srv = Application::Get().CreateShaderResourceView(source, &srvDesc);
    view.variant = variant;
    view.lastUsedFrame = m_frame;
    return &view;
}

std::shared_ptr<Texture> TexturePreview::Request(uint64_t key, const std::shared_ptr<Texture>& source,
    const TexturePreviewSettings& settings, uint32_t width, uint32_t height)
{
    if (!source || !source->GetD3D12Resource() || !CanPreview(*source))
    {
        return nullptr;
    }

    width = std::clamp<uint32_t>(width, 1, 4096);
    height = std::clamp<uint32_t>(height, 1, 4096);

    auto& target = m_targets[key];
    if (!target.image)
    {
        auto imageDesc = CD3DX12_RESOURCE_DESC::Tex2D(g_previewFormat, width, height, 1, 1, 1, 0,
            D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
        target.image = Application::Get().CreateTexture(imageDesc);
        target.image->SetName(L"Editor Texture Preview");
    }
    else
    {
        const auto imageDesc = target.image->GetD3D12ResourceDesc();
        if (imageDesc.Width != width || imageDesc.Height != height)
        {
            target.image->Resize(width, height);
        }
    }
    target.lastUsedFrame = m_frame;

    SourceView* view = GetSourceView(source);

    Pending pending;
    pending.source = source;
    pending.srv = view->srv;
    pending.variant = view->variant;
    pending.settings = settings;
    pending.image = target.image;
    m_pending.push_back(std::move(pending));

    return target.image;
}

void TexturePreview::Render(CommandList& commandList)
{
    for (const auto& pending : m_pending)
    {
        const auto sourceDesc = pending.source->GetD3D12ResourceDesc();
        const auto& settings = pending.settings;

        const uint32_t mip = std::min<uint32_t>(settings.mip, sourceDesc.MipLevels - 1);

        PreviewParams params = {};
        for (int i = 0; i < 4; ++i)
        {
            params.channelMask[i] = settings.channels[i] ? 1.0f : 0.0f;
        }
        params.rangeMin = settings.rangeMin;
        params.rangeMax = settings.rangeMax;
        params.mip = static_cast<float>(mip);
        params.nearZ = settings.nearZ;
        params.farZ = settings.farZ;

        if (pending.variant == Float3D)
        {
            const uint32_t depth = std::max<uint32_t>(1, sourceDesc.DepthOrArraySize >> mip);
            params.slice = (std::min(settings.slice, depth - 1) + 0.5f) / depth;
        }
        else
        {
            params.slice = static_cast<float>(std::min<uint32_t>(settings.slice, sourceDesc.DepthOrArraySize - 1));
        }

        params.flags = (settings.linearizeDepth ? FlagLinearizeDepth : 0) | (settings.absolute ? FlagAbsolute : 0) |
            (settings.srgb ? FlagSRGB : 0) | (settings.linearFilter ? FlagLinearFilter : 0);

        RenderTarget renderTarget;
        renderTarget.AttachTexture(AttachmentPoint::Color0, pending.image);

        const auto imageDesc = pending.image->GetD3D12ResourceDesc();
        D3D12_VIEWPORT viewport = CD3DX12_VIEWPORT(0.0f, 0.0f, static_cast<float>(imageDesc.Width),
            static_cast<float>(imageDesc.Height));

        commandList.SetRenderTarget(renderTarget);
        commandList.SetViewport(viewport);
        commandList.SetScissorRect(CD3DX12_RECT(0, 0, LONG_MAX, LONG_MAX));
        commandList.SetPipelineState(m_pipelineStates[pending.variant]);
        commandList.SetGraphicsRootSignature(m_rootSignature);
        commandList.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        commandList.SetGraphics32BitConstants(ParamsCB, params);
        commandList.SetShaderResourceView(SourceTexture, 0, pending.srv, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        commandList.Draw(3);
    }

    // Leave the inspected textures in the common state so any queue can use them next
    // (the ocean's textures are written by the compute queue, which cannot leave pixel shader states).
    for (const auto& pending : m_pending)
    {
        commandList.TransitionBarrier(pending.source, D3D12_RESOURCE_STATE_COMMON);
    }
    m_pending.clear();

    ReleaseUnused();
}

void TexturePreview::ReleaseUnused()
{
    const uint64_t frame = m_frame++;

    std::erase_if(m_targets, [frame](const auto& entry) { return frame - entry.second.lastUsedFrame > g_releaseAfterFrames; });
    // Views keep their texture alive, so drop them as soon as a frame does not use them.
    std::erase_if(m_sourceViews, [frame](const auto& entry)
    {
        return entry.second.texture.expired() || entry.second.lastUsedFrame != frame;
    });
}
