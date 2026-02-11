#pragma once
#include <d3d12.h>
#include <memory>
#include <string>
#include <wrl/client.h>

namespace EV
{
	class PipelineStateObject;
	class RootSignature;
	class CommandList;
}

using namespace EV;
class OceanCompute
{
public:
    OceanCompute(const std::wstring& computePath);
    std::wstring ModulePath();
    ~OceanCompute();

    void Dispatch(CommandList& commandList, float totalTime);

    enum RootParameters
    {
        ReadTextures,
        WriteTextures,
        NumRootParameters
    };

private:
    std::shared_ptr<RootSignature> m_rootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_phasePSO;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_fftPSO;
    std::shared_ptr<PipelineStateObject> m_pipelineStateObject;

    // TODO: what textures do you need here?
    // Think about: h0 spectrum, phase update output, 
    // FFT intermediate, final heightfield
};