#pragma once

#include "DX12/base_pso.h"

namespace EV
{
	
	class SDRPSO : public EV::BasePSO
	{
	public:
		SDRPSO(const std::wstring& vertexPath, const std::wstring& pixelPath);

		void XM_CALLCONV SetViewMatrix(DirectX::FXMMATRIX view) override;
		void XM_CALLCONV SetProjectionMatrix(DirectX::FXMMATRIX proj) override;
		void XM_CALLCONV SetWorldMatrix(DirectX::FXMMATRIX world) override;
		void SetMaterial(const std::shared_ptr<Material>& material) override;
		void Apply(CommandList& commandList) override;

		void SetHDRTexture(std::shared_ptr<Texture> inTexture);
	private:
		std::shared_ptr<Texture> m_hdrTexture = nullptr;
	};
}
