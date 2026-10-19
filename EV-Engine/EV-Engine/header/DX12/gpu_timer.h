#pragma once

/**
 *  @file gpu_timer.h
 *
 *  @brief Measures how long named scopes of a command list take on the GPU, with timestamp queries.
 *
 *      timer.BeginFrame();
 *      uint32_t scope = timer.Begin(commandList, "Shadows");
 *      ... record the work ...
 *      timer.End(commandList, scope);
 *      timer.EndFrame(commandList);   // Before the command list is executed.
 *
 *      for (const auto& result : timer.GetResults()) ...   // A frame from a few frames ago.
 *
 *  Results are read back FramesInFlight frames later, without waiting for the GPU. The application must not
 *  get further ahead of the GPU than that (the swap chain's frame latency is lower).
 */

#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>
#include <string>
#include <vector>

namespace EV
{
    class CommandList;

    class GpuTimer
    {
    public:
        static constexpr uint32_t FramesInFlight = 4;

        struct Result
        {
            std::string name;
            float       milliseconds = 0.0f;
        };

        explicit GpuTimer(uint32_t maxScopesPerFrame = 256);
        ~GpuTimer();

        GpuTimer(const GpuTimer&) = delete;
        GpuTimer& operator=(const GpuTimer&) = delete;

        // Start a frame, and read the results of the frame recorded FramesInFlight frames ago.
        void BeginFrame();

        // Timestamps around a scope. Scopes beyond maxScopesPerFrame are not measured (Begin returns UINT32_MAX).
        uint32_t Begin(CommandList& commandList, const std::string& name);
        void End(CommandList& commandList, uint32_t scope);

        // Copy the frame's timestamps to the readback buffer.
        void EndFrame(CommandList& commandList);

        // The scopes of the last frame that was read back, in the order they began.
        const std::vector<Result>& GetResults() const { return m_results; }

    private:
        struct Frame
        {
            std::vector<std::string> names;
            bool                     resolved = false;
        };

        uint32_t m_maxScopes;
        uint64_t m_frequency = 0;
        uint64_t m_frame = 0;

        Microsoft::WRL::ComPtr<ID3D12QueryHeap> m_queryHeap;
        Microsoft::WRL::ComPtr<ID3D12Resource>  m_readbackBuffer;

        Frame               m_frames[FramesInFlight];
        std::vector<Result> m_results;
    };
}
