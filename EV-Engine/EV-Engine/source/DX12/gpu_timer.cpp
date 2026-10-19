#include "DX12/dx12_includes.h"

#include "DX12/gpu_timer.h"

#include "core/application.h"
#include "DX12/command_list.h"
#include "DX12/command_queue.h"
#include "utility/helpers.h"

using namespace EV;

GpuTimer::GpuTimer(uint32_t maxScopesPerFrame)
    : m_maxScopes(maxScopesPerFrame)
{
    auto& app = Application::Get();
    auto device = app.GetDevice();

    // Two timestamps per scope, a range of queries per frame in flight.
    const uint32_t queryCount = FramesInFlight * m_maxScopes * 2;

    D3D12_QUERY_HEAP_DESC heapDesc = {};
    heapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    heapDesc.Count = queryCount;
    ThrowIfFailed(device->CreateQueryHeap(&heapDesc, IID_PPV_ARGS(&m_queryHeap)));
    m_queryHeap->SetName(L"GPU Timer Queries");

    CD3DX12_HEAP_PROPERTIES heapProperties(D3D12_HEAP_TYPE_READBACK);
    auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(queryCount * sizeof(uint64_t));
    ThrowIfFailed(device->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &bufferDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_readbackBuffer)));
    m_readbackBuffer->SetName(L"GPU Timer Readback");

    ThrowIfFailed(app.GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT).GetCommandQueue()->GetTimestampFrequency(&m_frequency));
}

GpuTimer::~GpuTimer() = default;

void GpuTimer::BeginFrame()
{
    ++m_frame;
    const uint32_t slot = static_cast<uint32_t>(m_frame % FramesInFlight);
    Frame& frame = m_frames[slot];

    // This slot was recorded FramesInFlight frames ago; its timestamps have been copied to the readback buffer.
    if (frame.resolved && !frame.names.empty() && m_frequency > 0)
    {
        const size_t first = static_cast<size_t>(slot) * m_maxScopes * 2;
        const size_t count = frame.names.size() * 2;
        const D3D12_RANGE readRange = { first * sizeof(uint64_t), (first + count) * sizeof(uint64_t) };
        void* data = nullptr;
        if (SUCCEEDED(m_readbackBuffer->Map(0, &readRange, &data)))
        {
            const uint64_t* timestamps = static_cast<const uint64_t*>(data) + first;
            m_results.resize(frame.names.size());
            for (size_t i = 0; i < frame.names.size(); ++i)
            {
                const uint64_t begin = timestamps[i * 2];
                const uint64_t end = timestamps[i * 2 + 1];
                m_results[i].name = frame.names[i];
                m_results[i].milliseconds = end > begin ? static_cast<float>(double(end - begin) * 1000.0 / double(m_frequency)) : 0.0f;
            }
            const D3D12_RANGE writtenRange = { 0, 0 };
            m_readbackBuffer->Unmap(0, &writtenRange);
        }
    }

    frame.names.clear();
    frame.resolved = false;
}

uint32_t GpuTimer::Begin(CommandList& commandList, const std::string& name)
{
    Frame& frame = m_frames[m_frame % FramesInFlight];
    if (frame.names.size() >= m_maxScopes)
    {
        return UINT32_MAX;
    }

    const uint32_t scope = static_cast<uint32_t>(frame.names.size());
    frame.names.push_back(name);

    const uint32_t query = static_cast<uint32_t>(m_frame % FramesInFlight) * m_maxScopes * 2 + scope * 2;
    commandList.GetCommandList()->EndQuery(m_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, query);
    return scope;
}

void GpuTimer::End(CommandList& commandList, uint32_t scope)
{
    if (scope == UINT32_MAX)
    {
        return;
    }
    const uint32_t query = static_cast<uint32_t>(m_frame % FramesInFlight) * m_maxScopes * 2 + scope * 2 + 1;
    commandList.GetCommandList()->EndQuery(m_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, query);
}

void GpuTimer::EndFrame(CommandList& commandList)
{
    Frame& frame = m_frames[m_frame % FramesInFlight];
    if (frame.names.empty())
    {
        return;
    }

    const uint32_t first = static_cast<uint32_t>(m_frame % FramesInFlight) * m_maxScopes * 2;
    const uint32_t count = static_cast<uint32_t>(frame.names.size()) * 2;
    commandList.GetCommandList()->ResolveQueryData(m_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, first, count,
        m_readbackBuffer.Get(), first * sizeof(uint64_t));

    // The command list keeps the query heap and buffer alive while it is in flight.
    commandList.TrackResource(m_queryHeap);
    commandList.TrackResource(m_readbackBuffer);
    frame.resolved = true;
}
