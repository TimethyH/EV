#pragma once

/**
 *  @file shadow_pso.h
 *
 *  @brief Renders shadow casters into a shadow map (depth only) from the light's point of view.
 */

#include <DirectXMath.h>

#include <memory>
#include <string>

#include "base_pso.h"

namespace EV
{
    class ShadowPSO : public BasePSO
    {
    public:
        // The vertex shader transforms by one matrix (world * light view * light projection, b0); the pixel shader
        // writes nothing.
        ShadowPSO(const std::wstring& vertexPath, const std::wstring& pixelPath);
        ~ShadowPSO() override;

        // What the shadow map covers. SceneVisitor's camera view and projection are ignored.
        void XM_CALLCONV SetLightViewProjection(DirectX::FXMMATRIX viewProjection);

        void SetMaterial(const std::shared_ptr<Material>& material) override {}
        void XM_CALLCONV SetViewMatrix(DirectX::FXMMATRIX view) override {}
        void XM_CALLCONV SetProjectionMatrix(DirectX::FXMMATRIX projection) override {}
        void XM_CALLCONV SetWorldMatrix(DirectX::FXMMATRIX world) override;
        void Apply(CommandList& commandList) override;

    private:
        void CreatePipelineState() override;
    };
}
