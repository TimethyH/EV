#pragma once

/**
 *  @file texture_readback.h
 *
 *  @brief Copies texture data back to the CPU for the editor's texture viewer:
 *  the value of the hovered texel and the min/max of a whole subresource.
 *  Results arrive a few frames after the request.
 */

#include <cstdint>
#include <memory>
#include <vector>

#include <d3d12.h>
#include <wrl.h>

namespace EV
{
    class CommandList;
    class CommandQueue;
    class Texture;

    class TextureReadback
    {
    public:
        struct TexelResult
        {
            bool     valid = false;
            uint32_t x = 0, y = 0, mip = 0, slice = 0;
            float    value[4] = {};
        };

        struct RangeResult
        {
            bool     valid = false;
            uint32_t mip = 0, slice = 0;
            float    min[4] = {};
            float    max[4] = {};
        };

        explicit TextureReadback(CommandQueue& commandQueue);

        // Queue a readback. Only the latest request of each kind per frame is kept.
        void RequestTexel(const std::shared_ptr<Texture>& texture, uint32_t mip, uint32_t slice, uint32_t x, uint32_t y);
        void RequestRange(const std::shared_ptr<Texture>& texture, uint32_t mip, uint32_t slice);

        // Record the queued copies. Call once per frame on the command list that renders the GUI.
        void Record(CommandList& commandList);

        // Process copies that finished on the GPU. Call once per frame, before reading results.
        void Update();

        // Results belong to the texture they were requested for; they are cleared when another texture is requested.
        const TexelResult& GetTexel() const { return m_texelResult; }
        const RangeResult& GetRange() const { return m_rangeResult; }
        bool               IsRangePending() const;

        static bool CanReadback(const Texture& texture);

    private:
        enum class Kind
        {
            Texel,
            Range,
        };

        struct Request
        {
            Kind                     kind;
            std::shared_ptr<Texture> texture;
            uint32_t                 mip = 0, slice = 0, x = 0, y = 0;
        };

        struct InFlight
        {
            Request                                request;
            Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
            D3D12_PLACED_SUBRESOURCE_FOOTPRINT     footprint = {};
            uint32_t                               width = 0, height = 0;
            DXGI_FORMAT                            format = DXGI_FORMAT_UNKNOWN;
            uint64_t                               fenceValue = 0; // 0 until the recording list was submitted.
        };

        void Resolve(const InFlight& readback);

        CommandQueue&         m_commandQueue;
        const Texture*        m_currentTexture = nullptr;
        std::vector<Request>  m_requests;
        std::vector<InFlight> m_inFlight;
        TexelResult           m_texelResult;
        RangeResult           m_rangeResult;
    };
}
