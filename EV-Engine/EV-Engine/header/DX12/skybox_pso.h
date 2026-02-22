#pragma once
#include <memory>
#include <string>

#include "base_pso.h"


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

		void SetSkyboxTexture(std::shared_ptr<Texture> skyTexture);
		
	private:
		std::shared_ptr<Texture> m_skyboxTexture = nullptr;
	};
}
