#pragma once

/**
 *  @file texture_preview.h
 *
 *  @brief Renders any texture (depth, float data, arrays, cubemaps, integer formats) into an
 *  RGBA8 image that ImGui can display. Used by the editor's texture viewer and thumbnails.
 */

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include <d3d12.h>
#include <wrl.h>

namespace EV
{
    class CommandList;
    class PipelineStateObject;
    class RootSignature;
    class ShaderResourceView;
    class Texture;

    struct TexturePreviewSettings
    {
        uint32_t mip = 0;
        uint32_t slice = 0;                 // Array slice, cube face or depth slice of a 3D texture.
        bool     channels[4] = { true, true, true, false };
        float    rangeMin = 0.0f;           // Values in [rangeMin, rangeMax] are mapped to [0, 1].
        float    rangeMax = 1.0f;
        bool     linearizeDepth = false;    // Convert a non-linear depth value into view space distance.
        float    nearZ = 0.1f;
        float    farZ = 1000.0f;
        bool     absolute = false;          // Show abs(value), useful for signed data such as displacement.
        bool     srgb = false;              // Encode the output for display (use for linear color data).
        bool     linearFilter = false;      // Bilinear filtering instead of point sampling.
    };

    class TexturePreview
    {
    public:
        TexturePreview();
        ~TexturePreview();

        /**
         * Queue a preview of a texture. The returned image can be used with GUI::GetTextureID during
         * this frame, its contents are rendered when Render() is called (before the GUI is drawn).
         * @param key Identifies the preview slot so several previews of the same texture can coexist.
         * @returns nullptr if the texture cannot be previewed.
         */
        std::shared_ptr<Texture> Request(uint64_t key, const std::shared_ptr<Texture>& source,
            const TexturePreviewSettings& settings, uint32_t width, uint32_t height);

        /**
         * Record all queued previews into the command list.
         */
        void Render(CommandList& commandList);

        static bool CanPreview(const Texture& texture);

        // Number of slices that can be selected (array size, 6 for a cube, depth of a 3D texture).
        static uint32_t GetSliceCount(const Texture& texture);

    private:
        enum Variant
        {
            Float2DArray,
            Float3D,
            Uint2DArray,
            Sint2DArray,
            NumVariants
        };

        struct Target
        {
            std::shared_ptr<Texture> image;
            uint64_t                 lastUsedFrame = 0;
        };

        struct SourceView
        {
            std::weak_ptr<Texture>              texture;
            ID3D12Resource*                     resource = nullptr; // Detects resized/recreated textures.
            std::shared_ptr<ShaderResourceView> srv;
            Variant                             variant = Float2DArray;
            uint64_t                            lastUsedFrame = 0;
        };

        struct Pending
        {
            std::shared_ptr<Texture>            source;
            std::shared_ptr<ShaderResourceView> srv;
            Variant                             variant;
            TexturePreviewSettings              settings;
            std::shared_ptr<Texture>            image;
        };

        SourceView* GetSourceView(const std::shared_ptr<Texture>& source);
        void        ReleaseUnused();

        std::shared_ptr<RootSignature>       m_rootSignature;
        std::shared_ptr<PipelineStateObject> m_pipelineStates[NumVariants];

        std::unordered_map<uint64_t, Target>            m_targets;
        std::unordered_map<const Texture*, SourceView>  m_sourceViews;
        std::vector<Pending>                            m_pending;
        uint64_t                                        m_frame = 0;
    };
}
