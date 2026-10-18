#pragma once
#include <string>        // std::wstring
#include <memory>        // std::shared_ptr
#include <d3d12.h>
#include <DirectXMath.h>
#include <Shlwapi.h>     // PathRemoveFileSpecW + GetModulePath
#include "resources/texture.h"
#include "core/camera.h"

struct CD3DX12_ROOT_PARAMETER1;

namespace EV
{
    class ShaderResourceView;
    class PipelineStateObject;
    class RootSignature;
    class CommandList;
}

using namespace EV;

class AtmospherePSO
{
public:
    AtmospherePSO(Camera& cam, const std::wstring& computePath, CD3DX12_ROOT_PARAMETER1* rootParams, uint32_t numRootParams, uint32_t width, uint32_t height);

    void Dispatch(std::shared_ptr<CommandList> commandList, std::shared_ptr<Texture> outputTexture);

    std::wstring ModulePath();

    struct alignas(16) AtmosphereConstants
    {
        DirectX::XMMATRIX InvProjection;   
        DirectX::XMMATRIX InvView;         
        DirectX::XMFLOAT3 CameraPositionWS;
        float             pad;             
        DirectX::XMFLOAT2 Resolution;      
        DirectX::XMFLOAT2 pad2;            
    };


private:
    std::shared_ptr<RootSignature> m_rootSignature;
    std::shared_ptr<PipelineStateObject> m_pipelineStateObject;

    Camera& m_camera;
    uint32_t m_width; 
    uint32_t m_height;
};