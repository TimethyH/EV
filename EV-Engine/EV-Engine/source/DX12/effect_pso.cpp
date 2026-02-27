// #include "DX12/effect_pso.h"
#include "DX12/effect_pso.h"

#include <array>
#include <DX12/command_list.h>
#include <utility/helpers.h>
#include <resources/material.h>
#include <DX12/pipeline_state_object.h>
#include <DX12/root_signature.h>
#include <resources/vertex_types.h>

#include <d3dcompiler.h>
#include <d3dx12.h>
#include <Shlwapi.h>
#include <wrl/client.h>

#include "core/application.h"
#include "core/camera.h"

using namespace Microsoft::WRL;
using namespace EV;

EffectPSO::EffectPSO(EV::Camera& cam, const std::wstring& vertexpath, const std::wstring& pixelPath)
	: m_pPreviousCommandList(nullptr)
	, m_camera(cam)
{
    m_pAlignedMVP = (MVP*)_aligned_malloc(sizeof(MVP), 16);

    // Get the folder of the running executable.
    std::wstring parentPath = GetModulePath();
    std::wstring vertexShader = parentPath + vertexpath;
    std::wstring pixelShader = parentPath + pixelPath;

    // Setup the root signature
    // Load the vertex shader.
    ComPtr<ID3DBlob> vertexShaderBlob;
    ThrowIfFailed(D3DReadFileToBlob(vertexShader.c_str(), &vertexShaderBlob));
    ComPtr<ID3DBlob> pixelShaderBlob;
	ThrowIfFailed(D3DReadFileToBlob(pixelShader.c_str(), &pixelShaderBlob));

    // Create a root signature.
    // Allow input layout and deny unnecessary access to certain pipeline stages.
    D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

    // Descriptor range for the textures.
    CD3DX12_DESCRIPTOR_RANGE1 descriptorRage(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 12, 3 );


    // clang-format off
    CD3DX12_ROOT_PARAMETER1 rootParameters[RootParameters::NumRootParameters];
    rootParameters[RootParameters::MatricesCB].InitAsConstantBufferView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY_VERTEX);
    rootParameters[RootParameters::MaterialCB].InitAsConstantBufferView(0, 1, D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY_PIXEL);
    rootParameters[RootParameters::Camera].InitAsConstantBufferView(1,0, D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY_PIXEL);
    // rootParameters[RootParameters::LightPropertiesCB].InitAsConstants(sizeof(LightProperties) / 4, 1, 0, D3D12_SHADER_VISIBILITY_PIXEL);
    rootParameters[RootParameters::PointLights].InitAsShaderResourceView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY_PIXEL);
    // rootParameters[RootParameters::SpotLights].InitAsShaderResourceView(1, 0, D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY_PIXEL);
    rootParameters[RootParameters::DirectionalLights].InitAsShaderResourceView(2, 0, D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY_PIXEL);
    rootParameters[RootParameters::Textures].InitAsDescriptorTable(1, &descriptorRage, D3D12_SHADER_VISIBILITY_PIXEL);

    CD3DX12_STATIC_SAMPLER_DESC anisotropicSampler(0, D3D12_FILTER_ANISOTROPIC);

    CD3DX12_STATIC_SAMPLER_DESC linearClampSampler(
        1, // s1
        D3D12_FILTER_MIN_MAG_MIP_POINT,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP
    );

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDescription; 
    std::array<CD3DX12_STATIC_SAMPLER_DESC, 2> samplers = { anisotropicSampler, linearClampSampler };

    rootSignatureDescription.Init_1_1(RootParameters::NumRootParameters, rootParameters,samplers.size(), samplers.data(), rootSignatureFlags);

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
    DXGI_FORMAT backBufferFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
    DXGI_FORMAT depthBufferFormat = DXGI_FORMAT_D32_FLOAT;

    // Check the best multisample quality level that can be used for the given back buffer format.
    DXGI_SAMPLE_DESC sampleDesc = {1,0};

    D3D12_RT_FORMAT_ARRAY rtvFormats = {};
    rtvFormats.NumRenderTargets = 1;
    rtvFormats.RTFormats[0] = backBufferFormat;

    CD3DX12_RASTERIZER_DESC rasterizerState(D3D12_DEFAULT);

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

    // Default dubemapSRV
    D3D12_SHADER_RESOURCE_VIEW_DESC defaultCubeSRV = {};
    defaultCubeSRV.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    defaultCubeSRV.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
    defaultCubeSRV.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    defaultCubeSRV.TextureCube.MipLevels = 1;
    defaultCubeSRV.TextureCube.MostDetailedMip = 0;

    m_defaultCubeSRV = Application::Get().CreateShaderResourceView(nullptr, &defaultCubeSRV);
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
            BindTexture(commandList, 8, m_material->GetTexture(TextureType::MetallicRoughness));
            
        }
       
    }

    // bind IBL textures
    commandList.SetShaderResourceView(RootParameters::Textures, 9,
        m_diffuseIBL ? m_diffuseIBL : m_defaultCubeSRV,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    commandList.SetShaderResourceView(RootParameters::Textures, 10,
        m_specularIBL ? m_specularIBL : m_defaultCubeSRV,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    commandList.SetShaderResourceView(RootParameters::Textures, 11,
        m_lutIBL,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    // if (m_dirtyFlags & DF_Camera)
    {
        // // TODO: Move camera data in its own class so we can retrieve it here and get rid of Demo.
        auto position = m_camera.GetTranslation();
        CameraData cameraData;
        cameraData.position = DirectX::XMFLOAT3(position.m128_f32[0], position.m128_f32[1], position.m128_f32[2]);
        cameraData.pad = 0.0f;
        commandList.SetGraphicsDynamicConstantBuffer(RootParameters::Camera, cameraData);
    }
    if (m_dirtyFlags & DF_PointLights)
    {
        commandList.SetGraphicsDynamicStructuredBuffer(RootParameters::PointLights, m_pointLights);
    }
    
    // if (m_dirtyFlags & DF_SpotLights)
    // {
    //     commandList.SetGraphicsDynamicStructuredBuffer(RootParameters::SpotLights, m_spotLights);
    // }
    
    if (m_dirtyFlags & DF_DirectionalLights)
    {
        commandList.SetGraphicsDynamicStructuredBuffer(RootParameters::DirectionalLights, m_directionalLights);
    }
    
    // if (m_dirtyFlags & (DF_PointLights | DF_SpotLights | DF_DirectionalLights))
    // {
    //     LightProperties lightProps;
    //     lightProps.numPointLights = static_cast<uint32_t>(m_pointLights.size());
    //     lightProps.numSpotLights = static_cast<uint32_t>(m_spotLights.size());
    //     lightProps.numDirectionalLights = static_cast<uint32_t>(m_directionalLights.size());
    //
    //     commandList.SetGraphics32BitConstants(RootParameters::LightPropertiesCB, lightProps);
    // }

    // Clear the dirty flags to avoid setting any states the next time the effect is applied.
    m_dirtyFlags = DF_None;
}

void EffectPSO::SetIBLTextures(std::shared_ptr<ShaderResourceView> diffuse, std::shared_ptr<ShaderResourceView> specular, std::shared_ptr<ShaderResourceView> lut)
{
    m_diffuseIBL = diffuse;
    m_specularIBL = specular;
    m_lutIBL = lut;
}