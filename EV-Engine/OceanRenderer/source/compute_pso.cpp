#include "compute_pso.h"

#include <d3dcompiler.h>
#include <d3dx12.h>
#include <Shlwapi.h>
#include <string>

#include "core/application.h"
#include "DX12/command_list.h"
#include "DX12/root_signature.h"
#include "utility/helpers.h"

OceanCompute::OceanCompute(const std::wstring& computePath, CD3DX12_ROOT_PARAMETER1* rootParams, uint32_t numRootParams)
{
    // Get the folder of the running executable.
    std::wstring parentPath = ModulePath();
    std::wstring computeShader = parentPath + computePath;

    // Load the compute shader.
    Microsoft::WRL::ComPtr<ID3DBlob> computeShaderBlob;
    ThrowIfFailed(D3DReadFileToBlob(computeShader.c_str(), &computeShaderBlob));

    D3D12_ROOT_SIGNATURE_FLAGS computeFlags =
        D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDescription;
    rootSignatureDescription.Init_1_1(numRootParams, rootParams, 0, nullptr, computeFlags);


    m_rootSignature = Application::Get().CreateRootSignature(rootSignatureDescription.Desc_1_1);

    struct PipelineStateStream
    {
        CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE        pRootSignature;
        CD3DX12_PIPELINE_STATE_STREAM_CS                    CS;
    } pipelineStateStream;

    pipelineStateStream.pRootSignature = m_rootSignature->GetRootSignature().Get();
    pipelineStateStream.CS = CD3DX12_SHADER_BYTECODE(computeShaderBlob.Get());

    m_pipelineStateObject = Application::Get().CreatePipelineStateObject(pipelineStateStream);

}

std::wstring OceanCompute::ModulePath()
{
    WCHAR buffer[MAX_PATH];
    GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    PathRemoveFileSpecW(buffer);
    return std::wstring(buffer);
}

void OceanCompute::Dispatch(std::shared_ptr<CommandList> commandList, const std::shared_ptr<Texture>& inputTexture, std::shared_ptr<Texture> outputTexture, float totalTime, DirectX::XMUINT3 dispatchDimension)
{
    commandList->SetPipelineState(m_pipelineStateObject);
    commandList->SetComputeRootSignature(m_rootSignature);

    // Bind H0
    commandList->SetShaderResourceView(RootParameters::ReadTextures, 0, inputTexture,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    // Bind Phase UAV
    commandList->SetUnorderedAccessView(RootParameters::WriteTextures, 0, outputTexture, 0);
    // Set Time 
    commandList->SetCompute32BitConstants(RootParameters::Time, 1, &totalTime);

    commandList->Dispatch(dispatchDimension.x, dispatchDimension.y, dispatchDimension.z);
    
}

