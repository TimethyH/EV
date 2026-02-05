#pragma once

/*
 *  Copyright(c) 2020 Jeremiah van Oosten
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files(the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions :
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 *  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 *  IN THE SOFTWARE.
 */

 /**
  *  @file EffectPSO.h
  *  @date November 12, 2020
  *  @author Jeremiah van Oosten
  *
  *  @brief Basic lighting effect.
  */

#include "light.h"

#include <DirectXMath.h>

#include <memory>
#include <vector>
#include <xstring>

// namespace dx12lib
// {
    class CommandList;
    class Material;
    class RootSignature;
    class PipelineStateObject;
    class ShaderResourceView;
    class Texture;
// }  // namespace dx12lib

class EffectPSO
{
public:
    // Light properties for the pixel shader.
    struct LightProperties
    {
        uint32_t numPointLights;
        uint32_t numSpotLights;
        uint32_t numDirectionalLights;
    };

    // Transformation matrices for the vertex shader.
    struct alignas(16) Matrices
    {
        DirectX::XMMATRIX modelMatrix;
        DirectX::XMMATRIX modelViewMatrix;
        DirectX::XMMATRIX inverseTransposeModelViewMatrix;
        DirectX::XMMATRIX modelViewProjectionMatrix;
    };
    struct alignas(16) CameraData
    {
        DirectX::XMFLOAT3 position;
        float pad;
    };

    // An enum for root signature parameters.
    // I'm not using scoped enums to avoid the explicit cast that would be required
    // to use these as root indices in the root signature.
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

    EffectPSO(bool enableLigting, bool enableDecal);
    virtual ~EffectPSO();

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
    void SetMaterial(const std::shared_ptr<Material>& material)
    {
        m_material = material;
        m_dirtyFlags |= DF_Material;
    }

    // Set matrices.
    void XM_CALLCONV SetWorldMatrix(DirectX::FXMMATRIX worldMatrix)
    {
        m_pAlignedMVP->world = worldMatrix;
        m_dirtyFlags |= DF_Matrices;
    }
    DirectX::XMMATRIX GetWorldMatrix() const
    {
        return m_pAlignedMVP->world;
    }

    void XM_CALLCONV SetViewMatrix(DirectX::FXMMATRIX viewMatrix)
    {
        m_pAlignedMVP->view = viewMatrix;
        m_dirtyFlags |= DF_Matrices;
    }
    DirectX::XMMATRIX GetViewMatrix() const
    {
        return m_pAlignedMVP->view;
    }

    void XM_CALLCONV SetProjectionMatrix(DirectX::FXMMATRIX projectionMatrix)
    {
        m_pAlignedMVP->projection = projectionMatrix;
        m_dirtyFlags |= DF_Matrices;
    }
    DirectX::XMMATRIX GetProjectionMatrix() const
    {
        return m_pAlignedMVP->projection;
    }

    // Apply this effect to the rendering pipeline.
    void Apply(CommandList& commandList);
    std::wstring GetModulePath();

private:
    enum DirtyFlags
    {
        DF_None = 0,
        DF_PointLights = (1 << 0),
        DF_SpotLights = (1 << 1),
        DF_DirectionalLights = (1 << 2),
        DF_Material = (1 << 3),
        DF_Matrices = (1 << 4),
        DF_Camera = (1 << 5),
        DF_All = DF_PointLights | DF_SpotLights | DF_DirectionalLights | DF_Material | DF_Matrices
    };

    struct alignas(16) MVP
    {
        DirectX::XMMATRIX world;
        DirectX::XMMATRIX view;
        DirectX::XMMATRIX projection;
    };

    // Helper function to bind a texture to the rendering pipeline.
    inline void BindTexture(CommandList& commandList, uint32_t offset,
        const std::shared_ptr<Texture>& texture);

    std::shared_ptr<RootSignature>       m_rootSignature;
    std::shared_ptr<PipelineStateObject> m_pipelineStateObject;

    std::vector<PointLight>       m_pointLights;
    std::vector<SpotLight>        m_spotLights;
    std::vector<DirectionalLight> m_directionalLights;

    // The material to apply during rendering.
    std::shared_ptr<Material> m_material;

    // An SRV used pad unused texture slots.
    std::shared_ptr<ShaderResourceView> m_defaultSRV;

    // Matrices
    MVP* m_pAlignedMVP;
    // If the command list changes, all parameters need to be rebound.
    CommandList* m_pPreviousCommandList;

    // Which properties need to be bound to the
    uint32_t m_dirtyFlags;

    bool m_enableLighting;
    bool m_enableDecal;
};
