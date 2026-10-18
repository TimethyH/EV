#include "DX12/dx12_includes.h"

#include "UI/model_preview.h"

#include <algorithm>
#include <cmath>

#include <d3dcompiler.h>
#include <DirectXCollision.h>

#include "core/application.h"
#include "DX12/command_list.h"
#include "DX12/pipeline_state_object.h"
#include "DX12/render_target.h"
#include "DX12/root_signature.h"
#include "DX12/scene.h"
#include "DX12/scene_node.h"
#include "DX12/shader_resource_view.h"
#include "resources/material.h"
#include "resources/mesh.h"
#include "resources/texture.h"
#include "resources/vertex_types.h"
#include "utility/helpers.h"

using namespace EV;
using namespace DirectX;

namespace
{
    const char* g_modelShader = R"(
cbuffer ObjectCB : register(b0)
{
    row_major float4x4 World;
    row_major float4x4 WorldInverseTranspose;
    row_major float4x4 ViewProjection;
    float4 LightDirection; // World space direction towards the light.
};

cbuffer MaterialCB : register(b1)
{
    float4 BaseColor;
    float4 Emissive;
    uint   HasTexture;
    float3 Padding;
};

Texture2D    BaseColorTexture : register(t0);
SamplerState LinearWrap       : register(s0);

struct VSInput
{
    float3 Position : POSITION;
    float3 Normal   : NORMAL;
    float3 TexCoord : TEXCOORD;
};

struct VSOutput
{
    float4 Position : SV_Position;
    float3 Normal   : NORMAL;
    float2 TexCoord : TEXCOORD0;
};

VSOutput VSMain(VSInput input)
{
    VSOutput output;
    output.Position = mul(mul(float4(input.Position, 1.0f), World), ViewProjection);
    output.Normal = mul(input.Normal, (float3x3)WorldInverseTranspose);
    output.TexCoord = input.TexCoord.xy;
    return output;
}

float3 LinearToSRGB(float3 color)
{
    float3 low  = color * 12.92f;
    float3 high = 1.055f * pow(max(color, 0.0f), 1.0f / 2.4f) - 0.055f;
    return lerp(high, low, step(color, 0.0031308f));
}

float4 PSMain(VSOutput input, bool frontFace : SV_IsFrontFace) : SV_Target
{
    float4 color = BaseColor;
    if (HasTexture)
    {
        color *= BaseColorTexture.Sample(LinearWrap, input.TexCoord);
    }
    if (color.a < 0.1f)
    {
        discard;
    }

    // Models are drawn without culling, so light back faces as if they were facing the camera.
    float3 normal = normalize(input.Normal) * (frontFace ? 1.0f : -1.0f);
    float  diffuse = saturate(dot(normal, LightDirection.xyz));
    float  ambient = lerp(0.15f, 0.35f, normal.y * 0.5f + 0.5f);

    float3 lit = color.rgb * (diffuse * 0.85f + ambient) + Emissive.rgb;
    return float4(LinearToSRGB(saturate(lit)), 1.0f);
}
)";

    struct alignas(16) ObjectConstants
    {
        XMMATRIX world;
        XMMATRIX worldInverseTranspose;
        XMMATRIX viewProjection;
        XMFLOAT4 lightDirection;
    };

    struct MaterialConstants
    {
        XMFLOAT4 baseColor;
        XMFLOAT4 emissive;
        uint32_t hasTexture;
        float    padding[3];
    };

    enum RootParameters
    {
        ObjectCB,
        MaterialCB,
        BaseColorTexture,
        NumRootParameters
    };

    constexpr DXGI_FORMAT g_imageFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    constexpr DXGI_FORMAT g_depthFormat = DXGI_FORMAT_D32_FLOAT;
    constexpr float       g_clearColor[4] = { 0.13f, 0.13f, 0.14f, 1.0f };
    constexpr float       g_fieldOfView = XMConvertToRadians(40.0f);

    Microsoft::WRL::ComPtr<ID3DBlob> CompileShader(const char* entryPoint, const char* target)
    {
        Microsoft::WRL::ComPtr<ID3DBlob> shader;
        Microsoft::WRL::ComPtr<ID3DBlob> errors;

        HRESULT hr = D3DCompile(g_modelShader, strlen(g_modelShader), "ModelPreview", nullptr, nullptr, entryPoint,
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

    template<typename Func>
    void ForEachNode(const std::shared_ptr<SceneNode>& node, Func&& func)
    {
        if (!node)
        {
            return;
        }
        func(*node);
        for (const auto& child : node->GetChildren())
        {
            ForEachNode(child, func);
        }
    }

    // World space bounds of all meshes in the scene.
    bool ComputeBounds(const Scene& scene, BoundingBox& bounds)
    {
        bool any = false;
        ForEachNode(scene.GetRootNode(), [&](SceneNode& node)
        {
            const XMMATRIX world = node.GetWorldTransform();
            for (const auto& mesh : node.GetMeshes())
            {
                if (!mesh)
                {
                    continue;
                }
                BoundingBox meshBounds;
                mesh->GetAABB().Transform(meshBounds, world);
                if (any)
                {
                    BoundingBox::CreateMerged(bounds, bounds, meshBounds);
                }
                else
                {
                    bounds = meshBounds;
                    any = true;
                }
            }
        });
        return any;
    }

    bool IsTriangleTopology(D3D12_PRIMITIVE_TOPOLOGY topology)
    {
        return topology == D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST || topology == D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
    }
}

ModelPreview::ModelPreview()
{
    auto& app = Application::Get();

    CD3DX12_DESCRIPTOR_RANGE1 textureRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0,
        D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE);

    CD3DX12_ROOT_PARAMETER1 rootParameters[NumRootParameters];
    rootParameters[ObjectCB].InitAsConstantBufferView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC);
    rootParameters[MaterialCB].InitAsConstants(sizeof(MaterialConstants) / 4, 1, 0, D3D12_SHADER_VISIBILITY_PIXEL);
    rootParameters[BaseColorTexture].InitAsDescriptorTable(1, &textureRange, D3D12_SHADER_VISIBILITY_PIXEL);

    CD3DX12_STATIC_SAMPLER_DESC linearWrap(0, D3D12_FILTER_ANISOTROPIC);

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDescription;
    rootSignatureDescription.Init_1_1(NumRootParameters, rootParameters, 1, &linearWrap,
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS | D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS);

    m_rootSignature = app.CreateRootSignature(rootSignatureDescription.Desc_1_1);

    auto vs = CompileShader("VSMain", "vs_5_1");
    auto ps = CompileShader("PSMain", "ps_5_1");

    struct ModelPipelineStateStream
    {
        CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE        pRootSignature;
        CD3DX12_PIPELINE_STATE_STREAM_INPUT_LAYOUT          InputLayout;
        CD3DX12_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY    PrimitiveTopologyType;
        CD3DX12_PIPELINE_STATE_STREAM_VS                    VS;
        CD3DX12_PIPELINE_STATE_STREAM_PS                    PS;
        CD3DX12_PIPELINE_STATE_STREAM_RASTERIZER            Rasterizer;
        CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL_FORMAT  DSVFormat;
        CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS RTVFormats;
    } stream;

    CD3DX12_RASTERIZER_DESC rasterizerDesc(D3D12_DEFAULT);
    rasterizerDesc.CullMode = D3D12_CULL_MODE_NONE;

    D3D12_RT_FORMAT_ARRAY rtvFormats = {};
    rtvFormats.NumRenderTargets = 1;
    rtvFormats.RTFormats[0] = g_imageFormat;

    stream.pRootSignature = m_rootSignature->GetRootSignature().Get();
    stream.InputLayout = VertexPositionNormalTangentBitangentTexture::inputLayout;
    stream.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    stream.VS = CD3DX12_SHADER_BYTECODE(vs.Get());
    stream.PS = CD3DX12_SHADER_BYTECODE(ps.Get());
    stream.Rasterizer = rasterizerDesc;
    stream.DSVFormat = g_depthFormat;
    stream.RTVFormats = rtvFormats;

    m_pipelineState = app.CreatePipelineStateObject(stream);

    D3D12_SHADER_RESOURCE_VIEW_DESC nullSRV = {};
    nullSRV.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    nullSRV.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    nullSRV.Texture2D.MipLevels = 1;
    nullSRV.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    m_nullSRV = app.CreateShaderResourceView(nullptr, &nullSRV);
}

ModelPreview::~ModelPreview() = default;

std::shared_ptr<Texture> ModelPreview::CreateImage(uint32_t width, uint32_t height)
{
    auto desc = CD3DX12_RESOURCE_DESC::Tex2D(g_imageFormat, std::max(1u, width), std::max(1u, height), 1, 1, 1, 0,
        D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);

    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format = g_imageFormat;
    std::copy(std::begin(g_clearColor), std::end(g_clearColor), clearValue.Color);

    auto image = Application::Get().CreateTexture(desc, &clearValue);
    image->SetName(L"Editor Model Preview");
    return image;
}

void ModelPreview::Request(const std::shared_ptr<Scene>& scene, const ModelPreviewView& view, const std::shared_ptr<Texture>& image)
{
    if (scene && scene->GetRootNode() && image)
    {
        m_pending.push_back({ scene, view, image });
    }
}

void ModelPreview::Render(CommandList& commandList)
{
    for (const auto& pending : m_pending)
    {
        RenderScene(commandList, pending);
    }
    m_pending.clear();
}

std::shared_ptr<Texture> ModelPreview::GetDepthTexture(uint32_t width, uint32_t height)
{
    auto& depth = m_depthTextures[(static_cast<uint64_t>(width) << 32) | height];
    if (!depth)
    {
        auto desc = CD3DX12_RESOURCE_DESC::Tex2D(g_depthFormat, width, height, 1, 1, 1, 0,
            D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);

        D3D12_CLEAR_VALUE clearValue = {};
        clearValue.Format = g_depthFormat;
        clearValue.DepthStencil = { 1.0f, 0 };

        depth = Application::Get().CreateTexture(desc, &clearValue);
        depth->SetName(L"Editor Model Preview Depth");
    }
    return depth;
}

void ModelPreview::RenderScene(CommandList& commandList, const Pending& pending)
{
    const auto imageDesc = pending.image->GetD3D12ResourceDesc();
    const uint32_t width = static_cast<uint32_t>(imageDesc.Width);
    const uint32_t height = imageDesc.Height;

    RenderTarget renderTarget;
    renderTarget.AttachTexture(AttachmentPoint::Color0, pending.image);
    renderTarget.AttachTexture(AttachmentPoint::DepthStencil, GetDepthTexture(width, height));

    float clearColor[4] = { g_clearColor[0], g_clearColor[1], g_clearColor[2], g_clearColor[3] };
    commandList.ClearTexture(pending.image, clearColor);
    commandList.ClearDepthStencilTexture(renderTarget.GetTexture(AttachmentPoint::DepthStencil), D3D12_CLEAR_FLAG_DEPTH);

    BoundingBox bounds;
    if (!ComputeBounds(*pending.scene, bounds))
    {
        return;
    }

    // Orbit around the bounds, at the distance where the bounding sphere fills the view.
    const float radius = std::max(XMVectorGetX(XMVector3Length(XMLoadFloat3(&bounds.Extents))), 1e-4f);
    const float aspect = static_cast<float>(width) / static_cast<float>(height);
    const float fitFov = aspect < 1.0f ? 2.0f * std::atan(std::tan(g_fieldOfView * 0.5f) * aspect) : g_fieldOfView;
    const float distance = radius / std::sin(fitFov * 0.5f) * std::max(pending.view.zoom, 0.05f);

    const XMVECTOR center = XMLoadFloat3(&bounds.Center);
    const float cosPitch = std::cos(pending.view.pitch);
    const XMVECTOR offset = XMVectorSet(cosPitch * std::sin(pending.view.yaw), std::sin(pending.view.pitch),
        -cosPitch * std::cos(pending.view.yaw), 0.0f);
    const XMVECTOR eye = center + offset * distance;
    const XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

    const XMMATRIX view = XMMatrixLookAtLH(eye, center, up);
    const float nearZ = std::max(distance - radius * 1.5f, distance * 0.01f);
    const XMMATRIX projection = XMMatrixPerspectiveFovLH(g_fieldOfView, aspect, nearZ, distance + radius * 1.5f);

    // Light from above, slightly to the left of the camera.
    const XMVECTOR forward = XMVector3Normalize(center - eye);
    const XMVECTOR right = XMVector3Normalize(XMVector3Cross(up, forward));
    XMFLOAT4 lightDirection;
    XMStoreFloat4(&lightDirection, XMVector3Normalize(up * 0.8f - forward * 0.5f - right * 0.4f));

    // The scene's textures may be shared with scenes the application renders (the texture cache),
    // so put them back in the state they were in.
    std::vector<CommandList::ResourceStateSnapshot> textureStates;

    commandList.SetRenderTarget(renderTarget);
    commandList.SetViewport(CD3DX12_VIEWPORT(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)));
    commandList.SetScissorRect(CD3DX12_RECT(0, 0, LONG_MAX, LONG_MAX));
    commandList.SetPipelineState(m_pipelineState);
    commandList.SetGraphicsRootSignature(m_rootSignature);

    ForEachNode(pending.scene->GetRootNode(), [&](SceneNode& node)
    {
        if (node.GetMeshes().empty())
        {
            return;
        }

        ObjectConstants object;
        object.world = node.GetWorldTransform();
        object.worldInverseTranspose = XMMatrixTranspose(XMMatrixInverse(nullptr, object.world));
        object.viewProjection = view * projection;
        object.lightDirection = lightDirection;
        commandList.SetGraphicsDynamicConstantBuffer(ObjectCB, object);

        for (const auto& mesh : node.GetMeshes())
        {
            if (!mesh || !IsTriangleTopology(mesh->GetPrimitiveTopology()) || !mesh->GetVertexBuffer(0))
            {
                continue;
            }

            MaterialConstants material = {};
            material.baseColor = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
            std::shared_ptr<Texture> baseColorTexture;
            if (auto meshMaterial = mesh->GetMaterial())
            {
                material.baseColor = meshMaterial->GetDiffuseColor();
                // Assimp reports an emissive factor of 1 alongside emissive textures, which would wash out
                // the whole model without the texture, so only use emissive colors that stand on their own.
                if (!meshMaterial->GetTexture(Material::TextureType::Emissive))
                {
                    material.emissive = meshMaterial->GetEmissiveColor();
                }
                baseColorTexture = meshMaterial->GetTexture(Material::TextureType::Diffuse);
            }

            if (baseColorTexture && baseColorTexture->GetD3D12Resource())
            {
                const bool seen = std::any_of(textureStates.begin(), textureStates.end(),
                    [&](const auto& snapshot) { return snapshot.resource == baseColorTexture; });
                if (!seen)
                {
                    textureStates.push_back(commandList.GetResourceState(baseColorTexture));
                }
                material.hasTexture = 1;
                commandList.SetShaderResourceView(BaseColorTexture, 0, baseColorTexture, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            }
            else
            {
                commandList.SetShaderResourceView(BaseColorTexture, 0, m_nullSRV, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            }

            commandList.SetGraphics32BitConstants(MaterialCB, material);
            mesh->Draw(commandList);
        }
    });

    for (const auto& snapshot : textureStates)
    {
        commandList.RestoreResourceState(snapshot);
    }
}
