#include "effect_pso.h"

#include <effect_pso.h>

#include <command_list.h>
#include <helpers.h>
#include <material.h>
#include <pipeline_state_object.h>
#include <root_signature.h>
#include <vertex_types.h>

#include <d3dcompiler.h>
#include <d3dx12.h>
#include <Shlwapi.h>
#include <wrl/client.h>

#include "application.h"
#include "demo.h"

using namespace Microsoft::WRL;

EffectPSO::EffectPSO(bool enableLighting, bool enableDecal)
	: m_dirtyFlags(DF_All)
    , m_pPreviousCommandList(nullptr)
    , m_enableLighting(enableLighting)
    , m_enableDecal(enableDecal)
{
    m_pAlignedMVP = (MVP*)_aligned_malloc(sizeof(MVP), 16);

    // Get the folder of the running executable.
    std::wstring parentPath = GetModulePath();
    std::wstring vertexShader = parentPath + L"/vertex.cso";
    std::wstring pixelShader = parentPath + L"/pixel.cso";

    // Setup the root signature
    // Load the vertex shader.
    ComPtr<ID3DBlob> vertexShaderBlob;
    ThrowIfFailed(D3DReadFileToBlob(vertexShader.c_str(), &vertexShaderBlob));

    // Load the pixel shader.
    ComPtr<ID3DBlob> pixelShaderBlob;
    if (enableLighting) {
        if (enableDecal) {
            // ThrowIfFailed(D3DReadFileToBlob(L"data/shaders/05-Models/Decal_PS.cso", &pixelShaderBlob));
        }
        else
        {
            // ThrowIfFailed(D3DReadFileToBlob(L"data/shaders/05-Models/Lighting_PS.cso", &pixelShaderBlob));
        }
    }
    else
    {
        ThrowIfFailed(D3DReadFileToBlob(pixelShader.c_str(), &pixelShaderBlob));
    }

    // Create a root signature.
    // Allow input layout and deny unnecessary access to certain pipeline stages.
    D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

    // Descriptor range for the textures.
    CD3DX12_DESCRIPTOR_RANGE1 descriptorRage(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 8, 3);

    // clang-format off
    CD3DX12_ROOT_PARAMETER1 rootParameters[RootParameters::NumRootParameters];
    rootParameters[RootParameters::MatricesCB].InitAsConstantBufferView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY_VERTEX);
    rootParameters[RootParameters::MaterialCB].InitAsConstantBufferView(0, 1, D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY_PIXEL);
    rootParameters[RootParameters::Camera].InitAsConstantBufferView(1,0, D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY_PIXEL);
    // rootParameters[RootParameters::LightPropertiesCB].InitAsConstants(sizeof(LightProperties) / 4, 1, 0, D3D12_SHADER_VISIBILITY_PIXEL);
    // rootParameters[RootParameters::PointLights].InitAsShaderResourceView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY_PIXEL);
    // rootParameters[RootParameters::SpotLights].InitAsShaderResourceView(1, 0, D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY_PIXEL);
    // rootParameters[RootParameters::DirectionalLights].InitAsShaderResourceView(2, 0, D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY_PIXEL);
    rootParameters[RootParameters::Textures].InitAsDescriptorTable(1, &descriptorRage, D3D12_SHADER_VISIBILITY_PIXEL);

    CD3DX12_STATIC_SAMPLER_DESC anisotropicSampler(0, D3D12_FILTER_ANISOTROPIC);

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDescription;
    rootSignatureDescription.Init_1_1(RootParameters::NumRootParameters, rootParameters, 1, &anisotropicSampler, rootSignatureFlags);
    // clang-format on

    m_rootSignature = Application::Get().CreateRootSignature(rootSignatureDescription.Desc_1_1);

    // Setup the pipeline state.
    struct PipelineStateStream
    {
        CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE        pRootSignature;
        CD3DX12_PIPELINE_STATE_STREAM_VS                    VS;
        CD3DX12_PIPELINE_STATE_STREAM_PS                    PS;
        CD3DX12_PIPELINE_STATE_STREAM_RASTERIZER            RasterizerState;
        CD3DX12_PIPELINE_STATE_STREAM_INPUT_LAYOUT          InputLayout;
        CD3DX12_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY    PrimitiveTopologyType;
        CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL_FORMAT  DSVFormat;
        CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS RTVFormats;
        CD3DX12_PIPELINE_STATE_STREAM_SAMPLE_DESC           SampleDesc;
    } pipelineStateStream;

    // Create a color buffer with sRGB for gamma correction.
    DXGI_FORMAT backBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    DXGI_FORMAT depthBufferFormat = DXGI_FORMAT_D32_FLOAT;

    // Check the best multisample quality level that can be used for the given back buffer format.
    DXGI_SAMPLE_DESC sampleDesc = Application::Get().GetMultisampleQualityLevels(backBufferFormat);

    D3D12_RT_FORMAT_ARRAY rtvFormats = {};
    rtvFormats.NumRenderTargets = 1;
    rtvFormats.RTFormats[0] = backBufferFormat;

    CD3DX12_RASTERIZER_DESC rasterizerState(D3D12_DEFAULT);
    if (m_enableDecal) {
        // Disable backface culling on decal geometry.
        rasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    }

    pipelineStateStream.pRootSignature = m_rootSignature->GetRootSignature().Get();
    pipelineStateStream.VS = CD3DX12_SHADER_BYTECODE(vertexShaderBlob.Get());
    pipelineStateStream.PS = CD3DX12_SHADER_BYTECODE(pixelShaderBlob.Get());
    pipelineStateStream.RasterizerState = rasterizerState;
    pipelineStateStream.InputLayout = VertexPositionNormalTangentBitangentTexture::inputLayout;
    pipelineStateStream.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pipelineStateStream.DSVFormat = depthBufferFormat;
    pipelineStateStream.RTVFormats = rtvFormats;
    pipelineStateStream.SampleDesc = sampleDesc;

    m_pipelineStateObject = Application::Get().CreatePipelineStateObject(pipelineStateStream);

    // Create an SRV that can be used to pad unused texture slots.
    D3D12_SHADER_RESOURCE_VIEW_DESC defaultSRV;
    defaultSRV.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    defaultSRV.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    defaultSRV.Texture2D.MostDetailedMip = 0;
    defaultSRV.Texture2D.MipLevels = 1;
    defaultSRV.Texture2D.PlaneSlice = 0;
    defaultSRV.Texture2D.ResourceMinLODClamp = 0;
    defaultSRV.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

    m_defaultSRV = Application::Get().CreateShaderResourceView(nullptr, &defaultSRV);
}

EffectPSO::~EffectPSO()
{
    _aligned_free(m_pAlignedMVP);
}

inline void EffectPSO::BindTexture(CommandList& commandList, uint32_t offset, const std::shared_ptr<Texture>& texture)
{
    if (texture)
    {
        commandList.SetShaderResourceView(RootParameters::Textures, offset, texture,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }
    else
    {
        commandList.SetShaderResourceView(RootParameters::Textures, offset, m_defaultSRV,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }
}

void EffectPSO::Apply(CommandList& commandList)
{
    commandList.SetPipelineState(m_pipelineStateObject);
    commandList.SetGraphicsRootSignature(m_rootSignature);

    if (m_dirtyFlags & DF_Matrices)
    {
        Matrices m;
        m.modelMatrix = m_pAlignedMVP->world;
        m.modelViewMatrix = m_pAlignedMVP->world * m_pAlignedMVP->view;
        m.modelViewProjectionMatrix = m.modelViewMatrix * m_pAlignedMVP->projection;
        m.inverseTransposeModelViewMatrix = XMMatrixTranspose(XMMatrixInverse(nullptr, m.modelViewMatrix));

        commandList.SetGraphicsDynamicConstantBuffer(RootParameters::MatricesCB, m);
    }

    if (m_dirtyFlags & DF_Material)
    {
        if (m_material)
        {
            const auto& materialProps = m_material->GetMaterialProperties();

            commandList.SetGraphicsDynamicConstantBuffer(RootParameters::MaterialCB, materialProps);

            using TextureType = Material::TextureType;

            BindTexture(commandList, 0, m_material->GetTexture(TextureType::Ambient));
            BindTexture(commandList, 1, m_material->GetTexture(TextureType::Emissive));
            BindTexture(commandList, 2, m_material->GetTexture(TextureType::Diffuse));
            BindTexture(commandList, 3, m_material->GetTexture(TextureType::Specular));
            BindTexture(commandList, 4, m_material->GetTexture(TextureType::SpecularPower));
            BindTexture(commandList, 5, m_material->GetTexture(TextureType::Normal));
            BindTexture(commandList, 6, m_material->GetTexture(TextureType::Bump));
            BindTexture(commandList, 7, m_material->GetTexture(TextureType::Opacity));
        }
    }
    if (m_dirtyFlags & DF_Camera)
    {
        auto position = Demo::GetCameraPosition();
        commandList.SetGraphicsDynamicConstantBuffer(RootParameters::Camera, position);
    }
    // if (m_dirtyFlags & DF_PointLights)
    // {
    //     commandList.SetGraphicsDynamicStructuredBuffer(RootParameters::PointLights, m_pointLights);
    // }
    //
    // if (m_dirtyFlags & DF_SpotLights)
    // {
    //     commandList.SetGraphicsDynamicStructuredBuffer(RootParameters::SpotLights, m_SpotLights);
    // }
    //
    // if (m_dirtyFlags & DF_DirectionalLights)
    // {
    //     commandList.SetGraphicsDynamicStructuredBuffer(RootParameters::DirectionalLights, m_DirectionalLights);
    // }
    //
    // if (m_dirtyFlags & (DF_PointLights | DF_SpotLights | DF_DirectionalLights))
    // {
    //     LightProperties lightProps;
    //     lightProps.numPointLights = static_cast<uint32_t>(m_pointLights.size());
    //     lightProps.NumSpotLights = static_cast<uint32_t>(m_SpotLights.size());
    //     lightProps.NumDirectionalLights = static_cast<uint32_t>(m_DirectionalLights.size());
    //
    //     commandList.SetGraphics32BitConstants(RootParameters::LightPropertiesCB, lightProps);
    // }

    // Clear the dirty flags to avoid setting any states the next time the effect is applied.
    m_dirtyFlags = DF_None;
}

std::wstring EffectPSO::GetModulePath()
{
    WCHAR buffer[MAX_PATH];
    GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    PathRemoveFileSpecW(buffer);
    return std::wstring(buffer);
}