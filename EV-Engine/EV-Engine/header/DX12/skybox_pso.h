#pragma once
#include <memory>
#include <string>

#include "base_pso.h"
#include "DX12/shader_resource_view.h"


namespace EV
{
	
	class SkyboxPSO : public BasePSO
	{
		public:
		
		SkyboxPSO(const std::wstring& vertexpath, const std::wstring& pixelPath);

		void Apply(CommandList& commandList) override;
		void SetMaterial(const std::shared_ptr<Material>& material) override;
		void XM_CALLCONV SetProjectionMatrix(DirectX::FXMMATRIX proj) override;
		void XM_CALLCONV SetViewMatrix(DirectX::FXMMATRIX view) override;
		void XM_CALLCONV SetWorldMatrix(DirectX::FXMMATRIX world) override;
		void SetSkyboxSRV(std::shared_ptr<ShaderResourceView> skyTexture);

		// With distance fog the sky below (and just above) the horizon fades to the fog color, so the far edge of
		// the scene disappears in it. amount 0 = no fog.
		void SetFog(const DirectX::XMFLOAT3& color, float amount)
		{
			m_fog = DirectX::XMFLOAT4(color.x, color.y, color.z, amount);
		}

		
	private:
		void CreatePipelineState() override;

		std::shared_ptr<ShaderResourceView> m_skyboxSRV = nullptr;
		DirectX::XMFLOAT4 m_fog = {}; // Color and amount.
	};
}
