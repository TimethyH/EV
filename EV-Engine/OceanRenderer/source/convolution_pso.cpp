#include "convolution_pso.h"

#include <d3dcompiler.h>
#include <d3dx12.h>
#include <Shlwapi.h>
#include <string>

#include "core/application.h"
#include "DX12/command_list.h"
#include "DX12/root_signature.h"
#include "utility/helpers.h"

// The min/max macros conflict with like-named member functions.
// Only use std::min and std::max defined in <algorithm>.
#if defined(min)
#undef min
#endif

#if defined(max)
#undef max
#endif

ConvolutionCompute::ConvolutionCompute(const std::wstring& computePath, CD3DX12_ROOT_PARAMETER1* rootParams, uint32_t numRootParams)
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


    CD3DX12_STATIC_SAMPLER_DESC linearSampler(
        0,
        D3D12_FILTER_MIN_MAG_MIP_LINEAR,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP
    );
    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDescription;
    rootSignatureDescription.Init_1_1(numRootParams, rootParams, 1, &linearSampler, computeFlags);



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

std::wstring ConvolutionCompute::ModulePath()
{
    WCHAR buffer[MAX_PATH];
    GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    PathRemoveFileSpecW(buffer);
    return std::wstring(buffer);
}

void ConvolutionCompute::Dispatch(std::shared_ptr<CommandList> commandList, const std::shared_ptr<ShaderResourceView>& envCubemap, const std::shared_ptr<Texture>& irradianceMap, uint32_t cubemapSize, uint32_t sampleCount)
{
    commandList->SetPipelineState(m_pipelineStateObject);
    commandList->SetComputeRootSignature(m_rootSignature);

    // Bind constant buffer (b0)
    struct IrradianceCB
    {
        uint32_t cubemapSize;
        uint32_t sampleCount;
        float padding;
        float padding2;
    } cb = { cubemapSize, sampleCount, 0.0f, 0.0f };

    commandList->SetCompute32BitConstants(0, cb);

    // Bind source environment cubemap SRV
    commandList->SetShaderResourceView(1, 0, envCubemap, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    // Bind destination irradiance map UAV
    commandList->SetUnorderedAccessView(2, 0, irradianceMap, 0);

    // 6 faces in Z, groups of BLOCK_SIZE (16) in X and Y
    uint32_t groups = (cubemapSize + 16 - 1) / 16;
    commandList->Dispatch(groups, groups, 6);

}

void ConvolutionCompute::DispatchSpecular(
    std::shared_ptr<CommandList> commandList,
    const std::shared_ptr<ShaderResourceView>& envCubemap,
    const std::shared_ptr<Texture>& specularMap,
    uint32_t cubemapSize,
    uint32_t sampleCount,
    uint32_t mipLevels)
{
    commandList->SetPipelineState(m_pipelineStateObject);
    commandList->SetComputeRootSignature(m_rootSignature);

    for (uint32_t mip = 0; mip < mipLevels; mip++)
    {
        uint32_t mipSize = cubemapSize >> mip;
        if (mipSize < 1) mipSize = 1;

        float roughness = (float)mip / (float)(mipLevels - 1);

        struct SpecularCB
        {
            uint32_t cubemapSize;
            uint32_t sampleCount;
            float roughness;
            float padding;
        } cb = { mipSize, sampleCount, roughness, 0.0f };

        commandList->SetCompute32BitConstants(0, cb);

        // Bind source environment cubemap SRV
        commandList->SetShaderResourceView(1, 0, envCubemap, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        // Bind UAV for this specific mip slice
        commandList->SetUnorderedAccessView(2, 0, specularMap, mip); // <-- mip index here

        uint32_t groups = std::max(1u, (mipSize + 16 - 1) / 16);
        commandList->Dispatch(groups, groups, 6);
    }
}

void ConvolutionCompute::DispatchBRDFLUT(
    std::shared_ptr<CommandList> commandList,
    const std::shared_ptr<Texture>& brdfLUT,
    uint32_t lutSize)
{
    commandList->SetPipelineState(m_pipelineStateObject);
    commandList->SetComputeRootSignature(m_rootSignature);

    // No cbuffer needed, but UAV at root param 0
    commandList->SetUnorderedAccessView(0, 0, brdfLUT, 0);

    uint32_t groups = (lutSize + 16 - 1) / 16;
    commandList->Dispatch(groups, groups, 1); // Z=1, not a cubemap
}