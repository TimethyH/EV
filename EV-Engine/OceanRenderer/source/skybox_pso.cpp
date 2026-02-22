#include "skybox_pso.h"

#include <d3dcompiler.h>
#include <d3dx12.h>
#include <Shlwapi.h>
#include <wrl/client.h>

#include "core/application.h"
#include "DX12/command_list.h"
#include "utility/helpers.h"

using namespace Microsoft::WRL;
using namespace EV;
using namespace DirectX;

SkyboxPSO::SkyboxPSO()
{
    auto& app = Application::Get();

    WCHAR buffer[MAX_PATH];
    GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    PathRemoveFileSpecW(buffer);
    std::wstring parentPath = buffer;

    ComPtr<ID3DBlob> vs, ps;
    ThrowIfFailed(D3DReadFileToBlob((parentPath + L"/skybox_VS.cso").c_str(), &vs));
    ThrowIfFailed(D3DReadFileToBlob((parentPath + L"/skybox_PS.cso").c_str(), &ps));

    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    CD3DX12_DESCRIPTOR_RANGE1 srvRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
    CD3DX12_ROOT_PARAMETER1 rootParameters[2];
    rootParameters[0].InitAsConstants(sizeof(XMMATRIX) / 4, 0, 0, D3D12_SHADER_VISIBILITY_VERTEX);
    rootParameters[1].InitAsDescriptorTable(1, &srvRange, D3D12_SHADER_VISIBILITY_PIXEL);

    CD3DX12_STATIC_SAMPLER_DESC linearClampSampler(0,
        D3D12_FILTER_MIN_MAG_MIP_LINEAR,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc;
    rootSigDesc.Init_1_1(2, rootParameters, 1, &linearClampSampler,
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS);

    m_rootSignature = app.CreateRootSignature(rootSigDesc.Desc_1_1);

    CD3DX12_DEPTH_STENCIL_DESC dsDesc(D3D12_DEFAULT);
    dsDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    dsDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

    D3D12_RT_FORMAT_ARRAY rtvFormats = {};
    rtvFormats.NumRenderTargets = 1;
    rtvFormats.RTFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;

    struct SkyboxPSO_Stream
    {
        CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE        pRootSignature;
        CD3DX12_PIPELINE_STATE_STREAM_INPUT_LAYOUT          InputLayout;
        CD3DX12_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY    PrimitiveTopologyType;
        CD3DX12_PIPELINE_STATE_STREAM_VS                    VS;
        CD3DX12_PIPELINE_STATE_STREAM_PS                    PS;
        CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS RTVFormats;
        CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL_FORMAT  DSVFormat;
        CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL         DepthStencil;
    } psoStream;

    psoStream.pRootSignature = m_rootSignature->GetRootSignature().Get();
    psoStream.InputLayout = { inputLayout, 1 };
    psoStream.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoStream.VS = CD3DX12_SHADER_BYTECODE(vs.Get());
    psoStream.PS = CD3DX12_SHADER_BYTECODE(ps.Get());
    psoStream.RTVFormats = rtvFormats;
    psoStream.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    psoStream.DepthStencil = dsDesc;

    m_pipelineStateObject = app.CreatePipelineStateObject(psoStream);
}

void SkyboxPSO::SetViewMatrix(FXMMATRIX view)
{
    m_viewMatrix = view;
}

void SkyboxPSO::SetProjectionMatrix(FXMMATRIX proj)
{
    m_projMatrix = proj;
}

void SkyboxPSO::Apply(CommandList& commandList)
{
    commandList.SetPipelineState(m_pipelineStateObject);
    commandList.SetGraphicsRootSignature(m_rootSignature);

    // Strip translation so the skybox stays centered on the camera
    XMMATRIX rotationOnlyView = m_viewMatrix;
    rotationOnlyView.r[3] = XMVectorSet(0.f, 0.f, 0.f, 1.f);

    XMMATRIX viewProj = rotationOnlyView * m_projMatrix;
    commandList.SetGraphics32BitConstants(0, viewProj);

    if (m_cubemap)
        commandList.SetShaderResourceView(1, 0, m_cubemap,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}