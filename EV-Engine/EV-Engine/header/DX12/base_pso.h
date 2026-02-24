#pragma once

#include <DirectXMath.h>
#include <Shlwapi.h>

#include "resources/texture.h"

namespace EV
{
	class RootSignature;
	class PipelineStateObject;
	class Material;
	class CommandList;

    class BasePSO
    {
    public:
        virtual ~BasePSO() = default;

        virtual void SetMaterial(const std::shared_ptr<Material>& material) = 0;
        virtual void XM_CALLCONV SetViewMatrix(DirectX::FXMMATRIX view) = 0;
        virtual void XM_CALLCONV SetProjectionMatrix(DirectX::FXMMATRIX proj) = 0;

        virtual void XM_CALLCONV SetWorldMatrix(DirectX::FXMMATRIX world) = 0;

        virtual void Apply(CommandList& commandList) = 0;

        std::wstring GetModulePath();


    protected:
        
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

    	// Transformation matrices for the vertex shader.
        struct alignas(16) Matrices
        {
            DirectX::XMMATRIX modelMatrix;
            DirectX::XMMATRIX modelViewMatrix;
            DirectX::XMMATRIX inverseTransposeModelViewMatrix;
            DirectX::XMMATRIX modelViewProjectionMatrix;
        };

        struct alignas(16) MVP
        {
            DirectX::XMMATRIX world;
            DirectX::XMMATRIX view;
            DirectX::XMMATRIX projection;
        };

        // Matrices
        MVP* m_pAlignedMVP = nullptr;

        std::shared_ptr<RootSignature>       m_rootSignature;
        std::shared_ptr<PipelineStateObject> m_pipelineStateObject;

        // Which properties need to be bound to the
        uint32_t m_dirtyFlags = DF_All;

        // The material to apply during rendering.
        std::shared_ptr<Material> m_material;

    private:
    };

    inline std::wstring BasePSO::GetModulePath()
    {
            WCHAR buffer[MAX_PATH];
            GetModuleFileNameW(nullptr, buffer, MAX_PATH);
            PathRemoveFileSpecW(buffer);
            return std::wstring(buffer);
    }
}
