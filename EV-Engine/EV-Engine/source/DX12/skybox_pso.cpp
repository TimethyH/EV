#include "DX12/skybox_pso.h"

#include <d3d12.h>
#include <d3dcommon.h>
#include <d3dcompiler.h>
#include <d3dx12.h>
#include <wrl/client.h>


#include "core/application.h"
#include "DX12/command_list.h"
#include "DX12/effect_pso.h"
#include "DX12/root_signature.h"
#include "utility/helpers.h"

using namespace EV;
EV::SkyboxPSO::SkyboxPSO(const std::wstring& vertexpath, const std::wstring& pixelPath)
{
    auto& app = Application::Get();
    m_pAlignedMVP = (MVP*)_aligned_malloc(sizeof(MVP), 16);

    // Get the folder of the running executable.
    std::wstring parentPath = GetModulePath();
    std::wstring vertexShader = parentPath + vertexpath;
    std::wstring pixelShader = parentPath + pixelPath;

    // Setup the root signature
    // Load the vertex shader.
    Microsoft::WRL::ComPtr<ID3DBlob> vertexShaderBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> pixelShaderBlob;
    ThrowIfFailed(D3DReadFileToBlob(vertexShader.c_str(), &vertexShaderBlob));
	ThrowIfFailed(D3DReadFileToBlob(pixelShader.c_str(), &pixelShaderBlob));

    // Setup the input layout for the skybox vertex shader.
    D3D12_INPUT_ELEMENT_DESC inputLayout[1] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    // Allow input layout and deny unnecessary access to certain pipeline stages.
    D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

    CD3DX12_DESCRIPTOR_RANGE1 descriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

    // TODO: cleanup by using enums
    CD3DX12_ROOT_PARAMETER1 rootParameters[2];
    rootParameters[0].InitAsConstants(sizeof(DirectX::XMMATRIX) / 4, 0, 0, D3D12_SHADER_VISIBILITY_VERTEX);
    rootParameters[1].InitAsDescriptorTable(1, &descriptorRange, D3D12_SHADER_VISIBILITY_PIXEL);

    CD3DX12_STATIC_SAMPLER_DESC linearClampSampler(
        0, D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDescription;
    rootSignatureDescription.Init_1_1(2, rootParameters, 1, &linearClampSampler, rootSignatureFlags);

    m_rootSignature = app.CreateRootSignature(rootSignatureDescription.Desc_1_1);

    // Setup the Skybox pipeline state.
    struct SkyboxPipelineState
    {
        CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE        pRootSignature;
        CD3DX12_PIPELINE_STATE_STREAM_INPUT_LAYOUT          InputLayout;
        CD3DX12_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY    PrimitiveTopologyType;
        CD3DX12_PIPELINE_STATE_STREAM_VS                    VS;
        CD3DX12_PIPELINE_STATE_STREAM_PS                    PS;
        CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS RTVFormats;
        CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL_FORMAT  DSVFormat;   
        CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL         DepthStencil; 
    } skyboxPipelineStateStream;

    // TODO: shouldnt DHR also care about depth buffer?
    DXGI_FORMAT HDRFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
    DXGI_FORMAT depthBufferFormat = DXGI_FORMAT_D32_FLOAT;
    D3D12_RT_FORMAT_ARRAY rtvFormats = {};
    rtvFormats.NumRenderTargets = 1;
    rtvFormats.RTFormats[0] = HDRFormat;
    
    CD3DX12_DEPTH_STENCIL_DESC dsDesc(D3D12_DEFAULT);
    dsDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;    // don't write depth
    dsDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL; // pass at far plane

    skyboxPipelineStateStream.pRootSignature = m_rootSignature->GetRootSignature().Get();
    skyboxPipelineStateStream.InputLayout = { inputLayout, 1 };
    skyboxPipelineStateStream.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    skyboxPipelineStateStream.VS = CD3DX12_SHADER_BYTECODE(vertexShaderBlob.Get());
    skyboxPipelineStateStream.PS = CD3DX12_SHADER_BYTECODE(pixelShaderBlob.Get());
    skyboxPipelineStateStream.RTVFormats = rtvFormats;
    skyboxPipelineStateStream.DSVFormat = depthBufferFormat;
    skyboxPipelineStateStream.DepthStencil = dsDesc;

    m_pipelineStateObject = app.CreatePipelineStateObject(skyboxPipelineStateStream);
}

void SkyboxPSO::Apply(CommandList& commandList)
{
    commandList.SetPipelineState(m_pipelineStateObject);
    commandList.SetGraphicsRootSignature(m_rootSignature);

    // Strip translation from view so skybox stays centered on camera
    DirectX::XMMATRIX rotationOnlyView = m_pAlignedMVP->view;
    rotationOnlyView.r[3] = DirectX::XMVectorSet(0.f, 0.f, 0.f, 1.f);

    auto viewProjection = rotationOnlyView * m_pAlignedMVP->projection;
    commandList.SetGraphics32BitConstants(0, viewProjection);

    if (m_skyboxSRV)  // use m_skyboxSRV, not m_skyboxTexture
        commandList.SetShaderResourceView(1, 0, m_skyboxSRV,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

void SkyboxPSO::SetMaterial(const std::shared_ptr<Material>& material)
{
}

void XM_CALLCONV SkyboxPSO::SetProjectionMatrix(DirectX::FXMMATRIX proj)
{
    m_pAlignedMVP->projection = proj;
}

void XM_CALLCONV SkyboxPSO::SetViewMatrix(DirectX::FXMMATRIX view)
{
    m_pAlignedMVP->view = view;
}

void XM_CALLCONV SkyboxPSO::SetWorldMatrix(DirectX::FXMMATRIX world)
{
    m_pAlignedMVP->world = world;
}

void SkyboxPSO::SetSkyboxSRV(std::shared_ptr<ShaderResourceView> skyTexture)
{
    m_skyboxSRV = skyTexture;  // just assign directly
}