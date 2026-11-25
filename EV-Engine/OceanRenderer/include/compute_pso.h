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
	class Buffer;
}

using namespace EV;
class OceanCompute
{
public:
    OceanCompute(const std::wstring& computePath, CD3DX12_ROOT_PARAMETER1* rootParams, uint32_t numRootParams);
    // ~OceanCompute();

    void Dispatch(std::shared_ptr<CommandList> commandList, const std::shared_ptr<Texture>& inputTexture, std::shared_ptr<Texture> slopeTexture, std::shared_ptr<Texture> displacementTexture, float totalTime, float patchSize, DirectX::XMUINT3 dispatchDimension);
    void Dispatch(std::shared_ptr<CommandList> commandList, const std::shared_ptr<Texture>& RWTexture, DirectX::XMUINT3 dispatchDimension, uint32_t columnPhase);
    void Dispatch(std::shared_ptr<CommandList> commandList, const std::shared_ptr<ShaderResourceView>& envCubemap, const std::shared_ptr<Texture>& irradianceMap, uint32_t cubemapSize, uint32_t sampleCount);
    void Dispatch(std::shared_ptr<CommandList> commandList, const std::shared_ptr<Texture>& RWSlopeTexture, const std::shared_ptr<Texture>& RWDisplacementTexture, const std::shared_ptr<Texture>& foamTexture, std::vector<float> foamData, DirectX::XMUINT3 dispatchDimension);

    // Mips 1-9 of a cascade's 512x512 slope and foam textures in one dispatch. counters: one uint per cascade, 0
    // between dispatches (the shader resets it).
    void DispatchMipChain(CommandList& commandList, const std::shared_ptr<Texture>& slopeTexture,
        const std::shared_ptr<Texture>& foamTexture, const std::shared_ptr<Buffer>& counters, uint32_t cascade);
    static constexpr uint32_t MipChainLevels = 10;

    enum RootParameters
    {
        ReadTextures,
        WriteTextures,
        Constants,
        NumRootParameters
    };

    // Binds the pipeline state and root signature, after creating the pipeline state again if the shader was recompiled.
    // Public so callers with their own bindings (e.g. RcToy's paint pass) can bind the PSO and then set their own
    // UAVs / constants and dispatch, without adding another Dispatch overload here.
    void SetPipelineState(CommandList& commandList);

private:
    void CreatePipelineState();

    std::shared_ptr<Shader> m_computeShader;
    PipelineShaders m_pipelineShaders;
    std::shared_ptr<RootSignature> m_rootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_phasePSO;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_fftPSO;
    std::shared_ptr<PipelineStateObject> m_pipelineStateObject;

    // Think about: h0 spectrum, phase update output, 
    // FFT intermediate, final heightfield
};