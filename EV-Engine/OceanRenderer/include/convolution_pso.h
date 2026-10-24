#pragma once
#include <d3d12.h>
#include <DirectXMath.h>
#include <memory>
#include <string>
#include <wrl/client.h>

#include "DX12/shader.h"
#include "resources/texture.h"

struct CD3DX12_ROOT_PARAMETER1;

namespace EV
{
    class ShaderResourceView;
    class PipelineStateObject;
    class RootSignature;
    class CommandList;
}

using namespace EV;
class ConvolutionCompute
{
public:
    ConvolutionCompute(const std::wstring& computePath, CD3DX12_ROOT_PARAMETER1* rootParams, uint32_t numRootParams);
    // ~OceanCompute();

    void Dispatch(std::shared_ptr<CommandList> commandList, const std::shared_ptr<ShaderResourceView>& envCubemap, const std::shared_ptr<Texture>& irradianceMap, uint32_t cubemapSize, uint32_t sampleCount);
    void DispatchSpecular(std::shared_ptr<CommandList> commandList,
                          const std::shared_ptr<ShaderResourceView>& envCubemap,
                          const std::shared_ptr<Texture>& specularMap, uint32_t cubemapSize, uint32_t sampleCount,
                          uint32_t mip, uint32_t mipLevels, uint32_t face);
    void DispatchBRDFLUT(std::shared_ptr<CommandList> commandList, const std::shared_ptr<Texture>& brdfLUT,
                         uint32_t lutSize);

    // Increases when the shader is recompiled, so what was computed with it can be computed again.
    uint32_t GetShaderVersion() const { return m_computeShader->GetVersion(); }

    enum RootParameters
    {
        ReadTextures,
        WriteTextures,
        Time,
        NumRootParameters
    };

private:
    void CreatePipelineState();
    // Binds the pipeline state and root signature, after creating the pipeline state again if the shader was recompiled.
    void SetPipelineState(CommandList& commandList);

    std::shared_ptr<Shader> m_computeShader;
    PipelineShaders m_pipelineShaders;
    std::shared_ptr<RootSignature> m_rootSignature;
    std::shared_ptr<PipelineStateObject> m_pipelineStateObject;

    // Think about: h0 spectrum, phase update output, 
    // FFT intermediate, final heightfield
};