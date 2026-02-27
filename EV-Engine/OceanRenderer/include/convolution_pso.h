#pragma once
#include <d3d12.h>
#include <DirectXMath.h>
#include <memory>
#include <string>
#include <wrl/client.h>

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
    std::wstring ModulePath();
    // ~OceanCompute();

    void Dispatch(std::shared_ptr<CommandList> commandList, const std::shared_ptr<ShaderResourceView>& envCubemap, const std::shared_ptr<Texture>& irradianceMap, uint32_t cubemapSize, uint32_t sampleCount);
    void DispatchSpecular(std::shared_ptr<CommandList> commandList,
                          const std::shared_ptr<ShaderResourceView>& envCubemap,
                          const std::shared_ptr<Texture>& specularMap, uint32_t cubemapSize, uint32_t sampleCount,
                          uint32_t mipLevels);
    void DispatchBRDFLUT(std::shared_ptr<CommandList> commandList, const std::shared_ptr<Texture>& brdfLUT,
                         uint32_t lutSize);

    enum RootParameters
    {
        ReadTextures,
        WriteTextures,
        Time,
        NumRootParameters
    };

private:
    std::shared_ptr<RootSignature> m_rootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_phasePSO;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_fftPSO;
    std::shared_ptr<PipelineStateObject> m_pipelineStateObject;

    // Think about: h0 spectrum, phase update output, 
    // FFT intermediate, final heightfield
};