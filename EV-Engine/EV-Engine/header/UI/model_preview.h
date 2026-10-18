#pragma once

/**
 *  @file model_preview.h
 *
 *  @brief Renders a loaded model (Scene) into an RGBA8 image that ImGui can display, with a simple
 *  built-in shader (base color texture, one light, ambient). Used by the editor's model thumbnails
 *  and the Model Preview panel, so it works in every application without app specific shaders.
 */

#include <cstdint>
#include <map>
#include <memory>
#include <vector>

#include <d3d12.h>

namespace EV
{
    class CommandList;
    class PipelineStateObject;
    class RootSignature;
    class Scene;
    class ShaderResourceView;
    class Texture;

    // Orbit camera around the model's bounds.
    struct ModelPreviewView
    {
        float yaw = 0.7f;    // Radians around the vertical axis.
        float pitch = 0.35f; // Radians above the horizon.
        float zoom = 1.0f;   // Camera distance relative to the distance that fits the whole model.
    };

    class ModelPreview
    {
    public:
        ModelPreview();
        ~ModelPreview();

        // An image that Request() can render into.
        static std::shared_ptr<Texture> CreateImage(uint32_t width, uint32_t height);

        /**
         * Queue a render of a scene into an image made with CreateImage. The image can be used with
         * GUI::GetTextureID during this frame; it is rendered when Render() is called (before the GUI is drawn).
         * The scene's GPU resources are kept alive by the command list, so the scene may be released afterwards.
         */
        void Request(const std::shared_ptr<Scene>& scene, const ModelPreviewView& view, const std::shared_ptr<Texture>& image);

        /**
         * Record all queued renders into the command list.
         */
        void Render(CommandList& commandList);

    private:
        struct Pending
        {
            std::shared_ptr<Scene>   scene;
            ModelPreviewView         view;
            std::shared_ptr<Texture> image;
        };

        void RenderScene(CommandList& commandList, const Pending& pending);
        std::shared_ptr<Texture> GetDepthTexture(uint32_t width, uint32_t height);

        std::shared_ptr<RootSignature>       m_rootSignature;
        std::shared_ptr<PipelineStateObject> m_pipelineState;
        std::shared_ptr<ShaderResourceView>  m_nullSRV; // Bound when a material has no base color texture.

        std::map<uint64_t, std::shared_ptr<Texture>> m_depthTextures; // Shared by every image of the same size.
        std::vector<Pending>                         m_pending;
    };
}
