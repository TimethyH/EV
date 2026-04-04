#include "landscape_pso.h"

#include <d3dcompiler.h>

#include "core/application.h"
#include "DX12/command_list.h"
#include "DX12/root_signature.h"
#include "resources/vertex_types.h"
#include "utility/helpers.h"

using namespace EV;

LandscapePSO::LandscapePSO(const std::wstring& vertexPath, const std::wstring& pixelPath)
{
    // Get the folder of the running executable.
    std::wstring parentPath = GetModulePath();
    std::wstring vertexShader = parentPath + vertexPath;
    std::wstring pixelShader = parentPath + pixelPath;

    // Setup the root signature
    // Load the vertex shader.
    Microsoft::WRL::ComPtr<ID3DBlob> vertexShaderBlob;
    ThrowIfFailed(D3DReadFileToBlob(vertexShader.c_str(), &vertexShaderBlob));
    Microsoft::WRL::ComPtr<ID3DBlob> pixelShaderBlob;
    ThrowIfFailed(D3DReadFileToBlob(pixelShader.c_str(), &pixelShaderBlob));

    // Create a root signature.
    // Allow input layout and deny unnecessary access to certain pipeline stages.
    D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags = 
        D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

    // Descriptor range for the textures.
    //CD3DX12_DESCRIPTOR_RANGE1 descriptorRage(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 0);


    // clang-format off
    CD3DX12_ROOT_PARAMETER1 rootParameters[RootParameters::NumRootParameters];
    rootParameters[RootParameters::MatricesCB].InitAsConstantBufferView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY_VERTEX);


    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDescription;

    rootSignatureDescription.Init_1_1(RootParameters::NumRootParameters, rootParameters, 0, nullptr, rootSignatureFlags);
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
    DXGI_FORMAT backBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    DXGI_FORMAT depthBufferFormat = DXGI_FORMAT_UNKNOWN;

    // Check the best multisample quality level that can be used for the given back buffer format.
    DXGI_SAMPLE_DESC sampleDesc = { 1,0 };

    D3D12_RT_FORMAT_ARRAY rtvFormats = {};
    rtvFormats.NumRenderTargets = 1;
    rtvFormats.RTFormats[0] = backBufferFormat;

    CD3DX12_RASTERIZER_DESC rasterizerState(D3D12_DEFAULT);
    // Disable backface culling on decal geometry.
    rasterizerState.CullMode = D3D12_CULL_MODE_NONE;

    pipelineStateStream.pRootSignature = m_rootSignature->GetRootSignature().Get();
    pipelineStateStream.VS = CD3DX12_SHADER_BYTECODE(vertexShaderBlob.Get());
    pipelineStateStream.PS = CD3DX12_SHADER_BYTECODE(pixelShaderBlob.Get());
    pipelineStateStream.RasterizerState = rasterizerState;
    pipelineStateStream.InputLayout = { nullptr, 0 };
    pipelineStateStream.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pipelineStateStream.DSVFormat = depthBufferFormat;
    pipelineStateStream.RTVFormats = rtvFormats;
    pipelineStateStream.SampleDesc = sampleDesc;

    m_pipelineStateObject = Application::Get().CreatePipelineStateObject(pipelineStateStream);

    //// Create an SRV that can be used to pad unused texture slots.
    //D3D12_SHADER_RESOURCE_VIEW_DESC defaultSRV;
    //defaultSRV.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    //defaultSRV.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    //defaultSRV.Texture2D.MostDetailedMip = 0;
    //defaultSRV.Texture2D.MipLevels = 1;
    //defaultSRV.Texture2D.PlaneSlice = 0;
    //defaultSRV.Texture2D.ResourceMinLODClamp = 0;
    //defaultSRV.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

    //m_defaultSRV = Application::Get().CreateShaderResourceView(nullptr, &defaultSRV);
}

void LandscapePSO::Apply(CommandList& commandList)
{
    commandList.SetPipelineState(m_pipelineStateObject);
    commandList.SetGraphicsRootSignature(m_rootSignature);
    commandList.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList.Draw(3, 1, 0, 0);
    //if (m_dirtyFlags & DF_Matrices)
    //{
    //    Matrices m;
    //    m.modelMatrix = m_pAlignedMVP->world;
    //    m.modelViewMatrix = m_pAlignedMVP->world * m_pAlignedMVP->view;
    //    m.modelViewProjectionMatrix = m.modelViewMatrix * m_pAlignedMVP->projection;
    //    m.inverseTransposeModelViewMatrix = DirectX::XMMatrixTranspose(DirectX::XMMatrixInverse(nullptr, m.modelViewMatrix));

    //    commandList.SetGraphicsDynamicConstantBuffer(RootParameters::MatricesCB, m);
    //}

    // if (m_dirtyFlags & DF_Camera)
    {
        // // TODO: Move camera data in its own class so we can retrieve it here and get rid of Demo.
        //auto position = m_camera.GetTranslation();
        //CameraData cameraData;
        //cameraData.position = DirectX::XMFLOAT3(position.m128_f32[0], position.m128_f32[1], position.m128_f32[2]);
        //cameraData.pad = 0.0f;
        //commandList.SetGraphicsDynamicConstantBuffer(RootParameters::Camera, cameraData.position);
    }
    // Clear the dirty flags to avoid setting any states the next time the effect is applied.
    m_dirtyFlags = DF_None;
}
