#pragma once
#include "DX12/base_pso.h"

namespace EV
{
	struct PointLight;
	struct SpotLight;
	struct DirectionalLight;
	class Camera;
	class ShaderResourceView;

	class OceanPSO : public BasePSO
	{
	public:
		OceanPSO(const EV::Camera& cam, const std::wstring& vertexPath, const std::wstring& pixelPath);
        const std::vector<PointLight>& GetPointLights() const
        {
            return m_pointLights;
        }
        void SetPointLights(const std::vector<PointLight>& pointLights)
        {
            m_pointLights = pointLights;
            m_dirtyFlags |= DF_PointLights;
        }

        const std::vector<SpotLight>& GetSpotLights() const
        {
            return m_spotLights;
        }
        void SetSpotLights(const std::vector<SpotLight>& spotLights)
        {
            m_spotLights = spotLights;
            m_dirtyFlags |= DF_SpotLights;
        }

        const std::vector<DirectionalLight>& GetDirectionalLights() const
        {
            return m_directionalLights;
        }
        void SetDirectionalLights(const std::vector<DirectionalLight>& directionalLights)
        {
            m_directionalLights = directionalLights;
            m_dirtyFlags |= DF_DirectionalLights;
        }

        const std::shared_ptr<Material>& GetMaterial() const
        {
            return m_material;
        }
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
        DirectX::XMMATRIX GetProjectionMatrix() const
        {
            return m_pAlignedMVP->projection;
        }
		void Apply(CommandList& commandList) override;



		void SetOceanTextures(std::shared_ptr<Texture> displacement, std::shared_ptr<Texture> slope, std::shared_ptr<Texture> foam);
        // Helper function to bind a texture to the rendering pipeline.
        inline void BindTexture(CommandList& commandList, uint32_t offset,
            const std::shared_ptr<Texture>& texture);


	private:
        enum RootParameters
        {
            // Vertex shader parameter
            MatricesCB,  // ConstantBuffer<Matrices> MatCB : register(b0);

            // Pixel shader parameters
            MaterialCB,         // ConstantBuffer<Material> MaterialCB : register( b0, space1 );
            // LightPropertiesCB,  // ConstantBuffer<LightProperties> LightPropertiesCB : register( b1 );

            PointLights,        // StructuredBuffer<PointLight> PointLights : register( t0 );
            // SpotLights,         // StructuredBuffer<SpotLight> SpotLights : register( t1 );
            DirectionalLights,  // StructuredBuffer<DirectionalLight> DirectionalLights : register( t2 )

            Textures,  // Texture2D AmbientTexture       : register( t3 );
            // Texture2D EmissiveTexture : register( t4 );
            // Texture2D DiffuseTexture : register( t5 );
            // Texture2D SpecularTexture : register( t6 );
            // Texture2D SpecularPowerTexture : register( t7 );
            // Texture2D NormalTexture : register( t8 );
            // Texture2D BumpTexture : register( t9 );
            // Texture2D OpacityTexture : register( t10 );
            Camera, // just its position
            NumRootParameters
        };
        struct alignas(16) CameraData
        {
            DirectX::XMFLOAT3 position;
            float pad;
        };

		std::shared_ptr<Texture> m_displacementTexture;
		std::shared_ptr<Texture> m_slopeTexture;
		std::shared_ptr<Texture> m_foamTexture;
        // An SRV used pad unused texture slots.
        std::shared_ptr<ShaderResourceView> m_defaultSRV;

        const EV::Camera& m_camera;


        std::vector<PointLight>       m_pointLights;
        std::vector<SpotLight>        m_spotLights;
        std::vector<DirectionalLight> m_directionalLights;

        // If the command list changes, all parameters need to be rebound.
        CommandList* m_pPreviousCommandList;
	};
}