#include "compute_pso.h"

#include <d3dcompiler.h>
#include <d3dx12.h>
#include <Shlwapi.h>
#include <string>

#include "core/application.h"
#include "DX12/root_signature.h"
#include "utility/helpers.h"

OceanCompute::OceanCompute(const std::wstring& computePath)
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

    CD3DX12_ROOT_PARAMETER1 rootParameters[RootParameters::NumRootParameters];
    rootParameters[RootParameters::ReadTextures].InitAsShaderResourceView(0);
    rootParameters[RootParameters::WriteTextures].InitAsUnorderedAccessView(1);

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDescription;
    rootSignatureDescription.Init_1_1(RootParameters::NumRootParameters, rootParameters, 0, nullptr, computeFlags);


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