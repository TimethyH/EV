#pragma once
#include "DX12/base_pso.h"
#include <memory>
#include <DirectXMath.h>

namespace EV
{
    class RootSignature;
    class PipelineStateObject;
    class Texture;
    class ShaderResourceView;

    class SkyboxPSO : public EV::BasePSO
    {
    public:
        SkyboxPSO();

        // BasePSO interface
        // NOTE: SetWorldMatrix is a no-op for the skybox — it has no world transform,
        // it just follows the camera rotation.
        void SetWorldMatrix(DirectX::FXMMATRIX world) override {}
        void SetViewMatrix(DirectX::FXMMATRIX view) override;
        void SetProjectionMatrix(DirectX::FXMMATRIX proj) override;
        void Apply(CommandList& commandList) override;

        void SetCubemap(std::shared_ptr<ShaderResourceView> cubemap) { m_cubemap = cubemap; }

    private:
        std::shared_ptr<RootSignature>       m_rootSignature;
        std::shared_ptr<PipelineStateObject> m_pipelineStateObject;
        std::shared_ptr<ShaderResourceView>  m_cubemap;

        // We store view and proj separately because the skybox needs
        // the view matrix WITHOUT translation (strip the last column).
        DirectX::XMMATRIX m_viewMatrix;
        DirectX::XMMATRIX m_projMatrix;
    };
}