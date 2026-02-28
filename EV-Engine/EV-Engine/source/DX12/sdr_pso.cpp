#include "DX12/sdr_pso.h"

#include <d3dcompiler.h>

#include "core/application.h"
#include "DX12/command_list.h"
#include "DX12/root_signature.h"
#include "utility/helpers.h"


enum TonemapMethod : uint32_t
{
    TM_Linear,
    TM_Reinhard,
    TM_ReinhardSq,
    TM_ACESFilmic,
};

struct TonemapParameters
{
    TonemapParameters()
        : TonemapMethod(TM_ACESFilmic)
        , Exposure(0.0f)
        , MaxLuminance(1.0f)
        , K(1.0f)
        , A(0.22f)
        , B(0.3f)
        , C(0.1f)
        , D(0.2f)
        , E(0.01f)
        , F(0.3f)
        , LinearWhite(11.2f)
        , Gamma(2.2f)
    {
    }

    // The method to use to perform tonemapping.
    TonemapMethod TonemapMethod;
    // Exposure should be expressed as a relative exposure value (-2, -1, 0, +1, +2 )
    float Exposure;

    // The maximum luminance to use for linear tonemapping.
    float MaxLuminance;

    // Reinhard constant. Generally this is 1.0.
    float K;

    // ACES Filmic parameters
    // See: https://www.slideshare.net/ozlael/hable-john-uncharted2-hdr-lighting/142
    float A;  // Shoulder strength
    float B;  // Linear strength
    float C;  // Linear angle
    float D;  // Toe strength
    float E;  // Toe Numerator
    float F;  // Toe denominator
    // Note E/F = Toe angle.
    float LinearWhite;
    float Gamma;
};

TonemapParameters g_TonemapParameters;

using namespace EV;

EV::SDRPSO::SDRPSO(const std::wstring& vertexPath, const std::wstring& pixelPath)
{
    auto& app = Application::Get();
    std::wstring parentPath = GetModulePath();

    CD3DX12_DESCRIPTOR_RANGE1 descriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

    CD3DX12_ROOT_PARAMETER1 rootParameters[2];
    rootParameters[0].InitAsConstants(sizeof(TonemapParameters) / 4, 0, 0, D3D12_SHADER_VISIBILITY_PIXEL);
    rootParameters[1].InitAsDescriptorTable(1, &descriptorRange, D3D12_SHADER_VISIBILITY_PIXEL);

    CD3DX12_STATIC_SAMPLER_DESC linearClampsSampler(
        0, D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDescription;
    rootSignatureDescription.Init_1_1(2, rootParameters, 1, &linearClampsSampler);

    m_rootSignature = app.CreateRootSignature(rootSignatureDescription.Desc_1_1);

    // Create the SDR PSO
    Microsoft::WRL::ComPtr<ID3DBlob> vs;
    Microsoft::WRL::ComPtr<ID3DBlob> ps;

    std::wstring vsPath = parentPath + vertexPath;
    std::wstring psPath = parentPath + pixelPath;

    ThrowIfFailed(D3DReadFileToBlob(vsPath.c_str(), &vs));
    ThrowIfFailed(D3DReadFileToBlob(psPath.c_str(), &ps));

    CD3DX12_RASTERIZER_DESC rasterizerDesc(D3D12_DEFAULT);
    rasterizerDesc.CullMode = D3D12_CULL_MODE_NONE;

    struct SDRPipelineStateStream
    {
        CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE        pRootSignature;
        CD3DX12_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY    PrimitiveTopologyType;
        CD3DX12_PIPELINE_STATE_STREAM_VS                    VS;
        CD3DX12_PIPELINE_STATE_STREAM_PS                    PS;
        CD3DX12_PIPELINE_STATE_STREAM_RASTERIZER            Rasterizer;
        CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS RTVFormats;
    } sdrPipelineStateStream;

    DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;
    D3D12_RT_FORMAT_ARRAY rtv = {};
    rtv.NumRenderTargets = 1;
    rtv.RTFormats[0] = format; 

    sdrPipelineStateStream.pRootSignature = m_rootSignature->GetRootSignature().Get();
    sdrPipelineStateStream.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    sdrPipelineStateStream.VS = CD3DX12_SHADER_BYTECODE(vs.Get());
    sdrPipelineStateStream.PS = CD3DX12_SHADER_BYTECODE(ps.Get());
    sdrPipelineStateStream.Rasterizer = rasterizerDesc;
    sdrPipelineStateStream.RTVFormats = rtv;

    m_pipelineStateObject = app.CreatePipelineStateObject(sdrPipelineStateStream);
}


void SDRPSO::SetMaterial(const std::shared_ptr<Material>& material)
{
}

void SDRPSO::Apply(CommandList& commandList)
{
    // commandList.SetRenderTarget(m_swapChain->GetRenderTarget());
    // commandList.SetViewport(m_viewport);
    commandList.SetPipelineState(m_pipelineStateObject);
    commandList.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList.SetGraphicsRootSignature(m_rootSignature);
    commandList.SetGraphics32BitConstants(0, g_TonemapParameters);
    commandList.SetShaderResourceView(1, 0, m_hdrTexture, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
               
    commandList.Draw(3);
}

void SDRPSO::SetHDRTexture(std::shared_ptr<Texture> inTexture)
{
    m_hdrTexture = inTexture;
}

void XM_CALLCONV SDRPSO::SetProjectionMatrix(DirectX::FXMMATRIX proj)
{
    m_pAlignedMVP->projection = proj;
}

void XM_CALLCONV SDRPSO::SetViewMatrix(DirectX::FXMMATRIX view)
{
    m_pAlignedMVP->view = view;
}

void XM_CALLCONV SDRPSO::SetWorldMatrix(DirectX::FXMMATRIX world)
{
    m_pAlignedMVP->world = world;
}