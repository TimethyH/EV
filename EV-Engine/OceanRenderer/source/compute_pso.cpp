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
    // Load the compute shader. It is recompiled when its source is saved.
    m_computeShader = Application::Get().LoadShader(computePath);
    m_pipelineShaders = PipelineShaders({ m_computeShader });

    D3D12_ROOT_SIGNATURE_FLAGS computeFlags =
        D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

    
    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDescription;
	rootSignatureDescription.Init_1_1(numRootParams, rootParams, 0, nullptr, computeFlags);

    m_rootSignature = Application::Get().CreateRootSignature(rootSignatureDescription.Desc_1_1);

    CreatePipelineState();
}

void OceanCompute::CreatePipelineState()
{
    struct PipelineStateStream
    {
        CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE        pRootSignature;
        CD3DX12_PIPELINE_STATE_STREAM_CS                    CS;
    } pipelineStateStream;

    pipelineStateStream.pRootSignature = m_rootSignature->GetRootSignature().Get();
    pipelineStateStream.CS = m_computeShader->GetBytecode();

    m_pipelineStateObject = Application::Get().CreatePipelineStateObject(pipelineStateStream);
}

void OceanCompute::SetPipelineState(CommandList& commandList)
{
    // Create the pipeline state again if the shader was recompiled.
    m_pipelineShaders.Update([this]() { CreatePipelineState(); });

    commandList.SetPipelineState(m_pipelineStateObject);
    commandList.SetComputeRootSignature(m_rootSignature);
}

// TODO: make more clear which dispatch belongs to which pass since they are all different anyway
void OceanCompute::Dispatch(std::shared_ptr<CommandList> commandList, const std::shared_ptr<Texture>& inputTexture, std::shared_ptr<Texture> slopeTexture, std::shared_ptr<Texture> displacementTexture, float totalTime, float patchSize, DirectX::XMUINT3 dispatchDimension)
{
    SetPipelineState(*commandList);

    // Bind H0
    commandList->SetShaderResourceView(RootParameters::ReadTextures, 0, inputTexture,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    // Bind Phase UAV
    commandList->SetUnorderedAccessView(RootParameters::WriteTextures, 0, slopeTexture, 0);
    commandList->SetUnorderedAccessView(RootParameters::WriteTextures, 1, displacementTexture, 0);
    // Set Time 
    struct constants
    {
        float time;
        float patchSize;
    }cbv;

    cbv.time = totalTime;
    cbv.patchSize = patchSize;
    commandList->SetCompute32BitConstants(RootParameters::Constants, 2, &cbv);

    commandList->Dispatch(dispatchDimension.x, dispatchDimension.y, dispatchDimension.z);
    
}

void OceanCompute::Dispatch(std::shared_ptr<CommandList> commandList, const std::shared_ptr<Texture>& RWTexture, DirectX::XMUINT3 dispatchDimension, uint32_t columnPhase)
{
    SetPipelineState(*commandList);

    // Bind Phase UAV
    commandList->SetUnorderedAccessView(0, 0, RWTexture, 0);
    // Set Time 
    commandList->SetCompute32BitConstants(1, 1, &columnPhase);

    commandList->Dispatch(dispatchDimension.x, dispatchDimension.y, dispatchDimension.z);

}

void OceanCompute::Dispatch(std::shared_ptr<CommandList> commandList, const std::shared_ptr<ShaderResourceView>& envCubemap, const std::shared_ptr<Texture>& irradianceMap, uint32_t cubemapSize, uint32_t sampleCount)
{
    SetPipelineState(*commandList);

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

void OceanCompute::Dispatch(std::shared_ptr<CommandList> commandList, const std::shared_ptr<Texture>& RWSlopeTexture, const std::shared_ptr<Texture>& RWDisplacementTexture, const std::shared_ptr<Texture>& foamTexture, std::vector<float> foamData, DirectX::XMUINT3 dispatchDimension)
{
    SetPipelineState(*commandList);

    // Bind Phase UAV
    commandList->SetUnorderedAccessView(0, 0, RWDisplacementTexture, 0);
    commandList->SetUnorderedAccessView(0, 1, RWSlopeTexture, 0);
    commandList->SetUnorderedAccessView(0, 2, foamTexture, 0);

    commandList->SetCompute32BitConstants(1, foamData.size(), foamData.data());
    commandList->Dispatch(dispatchDimension.x, dispatchDimension.y, dispatchDimension.z);

}


