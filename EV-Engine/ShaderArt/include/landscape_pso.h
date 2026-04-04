#pragma once
#include "DX12/base_pso.h"

namespace EV
{
	class LandscapePSO : public BasePSO
	{
	public:
		LandscapePSO(const std::wstring& vertexPath, const std::wstring& pixelPath );

        void SetMaterial(const std::shared_ptr<Material>& material) override
        {
            m_material = material;
            m_dirtyFlags |= DF_Material;
        }

        // Set matrices.
        void XM_CALLCONV SetWorldMatrix(DirectX::FXMMATRIX worldMatrix) override
        {
            m_pAlignedMVP->world = worldMatrix;
            m_dirtyFlags |= DF_Matrices;
        }
        DirectX::XMMATRIX GetWorldMatrix() const
        {
            return m_pAlignedMVP->world;
        }

        void XM_CALLCONV SetViewMatrix(DirectX::FXMMATRIX viewMatrix) override
        {
            m_pAlignedMVP->view = viewMatrix;
            m_dirtyFlags |= DF_Matrices;
        }
        DirectX::XMMATRIX GetViewMatrix() const
        {
            return m_pAlignedMVP->view;
        }

        void XM_CALLCONV SetProjectionMatrix(DirectX::FXMMATRIX projectionMatrix) override
        {
            m_pAlignedMVP->projection = projectionMatrix;
            m_dirtyFlags |= DF_Matrices;
        }

        void Apply(CommandList& commandList) override;

	private:

		enum RootParameters
		{
			MatricesCB,
			NumRootParameters
		};
	};
}
