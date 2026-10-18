#include "graphics/atmosphere_pso.h"
#include <DX12/dx12_includes.h>
#include <d3dcompiler.h>
#include <d3dx12.h>
#include "core/application.h"
#include "DX12/command_list.h"
#include "DX12/root_signature.h"
#include "utility/helpers.h"

std::wstring AtmospherePSO::ModulePath()
{
    WCHAR buffer[MAX_PATH];
    GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    PathRemoveFileSpecW(buffer);
    return std::wstring(buffer);
}

AtmospherePSO::AtmospherePSO(Camera& cam, const std::wstring& computePath, CD3DX12_ROOT_PARAMETER1* rootParams, uint32_t numRootParams, uint32_t width, uint32_t height)
    :m_camera(cam)
    ,m_width(width)
    ,m_height(height)
{

    // Get the folder of the running executable.
    std::wstring parentPath = ModulePath();
    std::wstring computeShader = parentPath + computePath;

    // Load the shader
    Microsoft::WRL::ComPtr<ID3DBlob> computeShaderBob;
    ThrowIfFailed(D3DReadFileToBlob(computeShader.c_str(), &computeShaderBob));

    D3D12_ROOT_SIGNATURE_FLAGS computeFlags = 
        D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
    rootSignatureDesc.Init_1_1(numRootParams, rootParams, 0U, nullptr, computeFlags);

    m_rootSignature = Application::Get().CreateRootSignature(rootSignatureDesc.Desc_1_1);

    struct PipelineStateStream
    {
        CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE pRootSignature;
        CD3DX12_PIPELINE_STATE_STREAM_CS CS;
    } pipelineStateStream;

    pipelineStateStream.pRootSignature = m_rootSignature->GetRootSignature().Get();
    pipelineStateStream.CS = CD3DX12_SHADER_BYTECODE(computeShaderBob.Get());

    m_pipelineStateObject = Application::Get().CreatePipelineStateObject(pipelineStateStream);
}

void AtmospherePSO::Dispatch(std::shared_ptr<CommandList> commandList, std::shared_ptr<Texture> outputTexture)
{
    commandList->SetPipelineState(m_pipelineStateObject);
    commandList->SetComputeRootSignature(m_rootSignature);

    // pass data
    AtmosphereConstants cb;
    DirectX::XMMATRIX invProj = m_camera.GetInverseProjectionMatrix();
    DirectX::XMMATRIX invView = m_camera.GetInverseViewMatrix();
    DirectX::XMVECTOR camPos = m_camera.GetTranslation();
    invView.r[3] = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f); // remove translation from view mat

    cb.InvProjection = XMMatrixTranspose(invProj);
    cb.InvView = XMMatrixTranspose(invView);
    XMStoreFloat3(&cb.CameraPositionWS, camPos);
    cb.pad = 0.0f;
    cb.Resolution = DirectX::XMFLOAT2((float)m_width, (float)m_height);
    cb.pad2 = DirectX::XMFLOAT2(0.0f, 0.0f);

    commandList->SetCompute32BitConstants(0, cb);

    commandList->SetUnorderedAccessView(1, 0, outputTexture, 0);

    UINT groupsX = (m_width + 7) / 8;
    UINT groupsY = (m_height+ 7) / 8;
    commandList->Dispatch(groupsX, groupsY, 1);
}
