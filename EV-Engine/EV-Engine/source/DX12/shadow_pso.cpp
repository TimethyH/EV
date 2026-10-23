#include "DX12/dx12_includes.h"

#include "DX12/shadow_pso.h"

#include "core/application.h"
#include "DX12/command_list.h"
#include "DX12/root_signature.h"
#include "resources/vertex_types.h"

using namespace EV;
using namespace DirectX;

ShadowPSO::ShadowPSO(const std::wstring& vertexPath, const std::wstring& pixelPath)
{
    // world and projection (the light's view * projection) are used; view is not.
    m_pAlignedMVP = static_cast<MVP*>(_aligned_malloc(sizeof(MVP), 16));
    m_pAlignedMVP->world = XMMatrixIdentity();
    m_pAlignedMVP->view = XMMatrixIdentity();
    m_pAlignedMVP->projection = XMMatrixIdentity();

    LoadShaders(vertexPath, pixelPath);

    const D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS | D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS | D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS;

    CD3DX12_ROOT_PARAMETER1 rootParameters[1];
    rootParameters[0].InitAsConstants(sizeof(XMMATRIX) / 4, 0, 0, D3D12_SHADER_VISIBILITY_VERTEX);

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDescription;
    rootSignatureDescription.Init_1_1(_countof(rootParameters), rootParameters, 0, nullptr, rootSignatureFlags);
    m_rootSignature = Application::Get().CreateRootSignature(rootSignatureDescription.Desc_1_1);

    CreatePipelineState();
}

ShadowPSO::~ShadowPSO()
{
    _aligned_free(m_pAlignedMVP);
}

void ShadowPSO::CreatePipelineState()
{
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

    // Both sides cast shadows (thin geometry such as leaves), with a slope scaled bias against shadow acne.
    CD3DX12_RASTERIZER_DESC rasterizerState(D3D12_DEFAULT);
    rasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    rasterizerState.DepthBias = 64;
    rasterizerState.SlopeScaledDepthBias = 2.0f;
    rasterizerState.DepthBiasClamp = 0.01f;

    D3D12_RT_FORMAT_ARRAY rtvFormats = {};
    rtvFormats.NumRenderTargets = 0;

    pipelineStateStream.pRootSignature = m_rootSignature->GetRootSignature().Get();
    pipelineStateStream.VS = m_vertexShader->GetBytecode();
    pipelineStateStream.PS = m_pixelShader->GetBytecode();
    pipelineStateStream.RasterizerState = rasterizerState;
    pipelineStateStream.InputLayout = VertexPositionNormalTangentBitangentTexture::inputLayout;
    pipelineStateStream.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pipelineStateStream.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    pipelineStateStream.RTVFormats = rtvFormats;
    pipelineStateStream.SampleDesc = DXGI_SAMPLE_DESC{ 1, 0 };

    m_pipelineStateObject = Application::Get().CreatePipelineStateObject(pipelineStateStream);
}

void XM_CALLCONV ShadowPSO::SetLightViewProjection(FXMMATRIX viewProjection)
{
    m_pAlignedMVP->projection = viewProjection;
}

void XM_CALLCONV ShadowPSO::SetWorldMatrix(FXMMATRIX world)
{
    m_pAlignedMVP->world = world;
}

void ShadowPSO::Apply(CommandList& commandList)
{
    UpdatePipelineState();

    commandList.SetPipelineState(m_pipelineStateObject);
    commandList.SetGraphicsRootSignature(m_rootSignature);
    const XMMATRIX worldViewProjection = m_pAlignedMVP->world * m_pAlignedMVP->projection;
    commandList.SetGraphics32BitConstants(0, worldViewProjection);
}
