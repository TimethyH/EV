#include <dx12_includes.h>

#include <command_queue.h>

#include <application.h>
#include <command_list.h>
#include <resource_state_tracker.h>

CommandQueue::CommandQueue(D3D12_COMMAND_LIST_TYPE type)
    : m_fenceValue(0)
    , m_commandListType(type)
    , m_processInFlightCommandLists(true)
{
    auto device = Application::Get().GetDevice();

    D3D12_COMMAND_QUEUE_DESC desc = {};
    desc.Type = type;
    desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    desc.NodeMask = 0;

    ThrowIfFailed(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&m_commandQueue)));
    ThrowIfFailed(device->CreateFence(m_fenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));

    switch (type)
    {
    case D3D12_COMMAND_LIST_TYPE_COPY:
        m_commandQueue->SetName(L"Copy Command Queue");
        break;
    case D3D12_COMMAND_LIST_TYPE_COMPUTE:
        m_commandQueue->SetName(L"Compute Command Queue");
        break;
    case D3D12_COMMAND_LIST_TYPE_DIRECT:
        m_commandQueue->SetName(L"Direct Command Queue");
        break;
    }

    m_processInFlightCommandListsThread = std::thread(&CommandQueue::ProccessInFlightCommandLists, this);
}

CommandQueue::~CommandQueue()
{
    m_processInFlightCommandLists = false;
    m_processInFlightCommandListsThread.join();
}

uint64_t CommandQueue::Signal()
{
    uint64_t fenceValue = ++m_fenceValue;
    m_commandQueue->Signal(m_fence.Get(), fenceValue);
    return fenceValue;
}

bool CommandQueue::IsFenceComplete(uint64_t fenceValue)
{
    return m_fence->GetCompletedValue() >= fenceValue;
}

void CommandQueue::WaitForFenceValue(uint64_t fenceValue)
{
    if (!IsFenceComplete(fenceValue))
    {
        auto event = ::CreateEvent(NULL, FALSE, FALSE, NULL);
        assert(event && "Failed to create fence event handle.");

        // Is this function thread safe?
        m_fence->SetEventOnCompletion(fenceValue, event);
        ::WaitForSingleObject(event, DWORD_MAX);

        ::CloseHandle(event);
    }
}

void CommandQueue::Flush()
{
    std::unique_lock<std::mutex> lock(m_processInFlightCommandListsThreadMutex);
    m_processInFlightCommandListsThreadCV.wait(lock, [this] { return m_inFlightCommandLists.Empty(); });

    // In case the command queue was signaled directly 
    // using the CommandQueue::Signal method then the 
    // fence value of the command queue might be higher than the fence
    // value of any of the executed command lists.
    WaitForFenceValue(m_fenceValue);
}

std::shared_ptr<CommandList> CommandQueue::GetCommandList()
{
    std::shared_ptr<CommandList> commandList;

    // If there is a command list on the queue.
    if (!m_availableCommandLists.Empty())
    {
        m_availableCommandLists.TryPop(commandList);
    }
    else
    {
        // Otherwise create a new command list.
        commandList = std::make_shared<CommandList>(m_commandListType);
    }

    return commandList;
}

// Execute a command list.
// Returns the fence value to wait for for this command list.
uint64_t CommandQueue::ExecuteCommandList(std::shared_ptr<CommandList> commandList)
{
    return ExecuteCommandLists(std::vector<std::shared_ptr<CommandList> >({ commandList }));
}

uint64_t CommandQueue::ExecuteCommandLists(const std::vector<std::shared_ptr<CommandList> >& commandLists)
{
    ResourceStateTracker::Lock();

    // Command lists that need to put back on the command list queue.
    std::vector<std::shared_ptr<CommandList> > toBeQueued;
    toBeQueued.reserve(commandLists.size() * 2);        // 2x since each command list will have a pending command list.

    // Generate mips command lists.
    std::vector<std::shared_ptr<CommandList> > generateMipsCommandLists;
    generateMipsCommandLists.reserve(commandLists.size());

    // Command lists that need to be executed.
    std::vector<ID3D12CommandList*> d3d12CommandLists;
    d3d12CommandLists.reserve(commandLists.size() * 2); // 2x since each command list will have a pending command list.

    for (auto commandList : commandLists)
    {
        auto pendingCommandList = GetCommandList();
        bool hasPendingBarriers = commandList->Close(pendingCommandList);
        pendingCommandList->Close();
        // If there are no pending barriers on the pending command list, there is no reason to 
        // execute an empty command list on the command queue.
        if (hasPendingBarriers)
        {
            d3d12CommandLists.push_back(pendingCommandList->GetCommandList().Get());
        }
        d3d12CommandLists.push_back(commandList->GetCommandList().Get());

        toBeQueued.push_back(pendingCommandList);
        toBeQueued.push_back(commandList);

        auto generateMipsCommandList = commandList->GetGenerateMipsCommandList();
        if (generateMipsCommandList)
        {
            generateMipsCommandLists.push_back(generateMipsCommandList);
        }
    }

    UINT numCommandLists = static_cast<UINT>(d3d12CommandLists.size());
    m_commandQueue->ExecuteCommandLists(numCommandLists, d3d12CommandLists.data());
    uint64_t fenceValue = Signal();

    ResourceStateTracker::Unlock();

    // Queue command lists for reuse.
    for (auto commandList : toBeQueued)
    {
        m_inFlightCommandLists.Push({ fenceValue, commandList });
    }

    // If there are any command lists that generate mips then execute those
    // after the initial resource command lists have finished.
    if (generateMipsCommandLists.size() > 0)
    {
        auto computeQueue = Application::Get().GetCommandQueue(D3D12_COMMAND_LIST_TYPE_COMPUTE);
        computeQueue->Wait(*this);
        computeQueue->ExecuteCommandLists(generateMipsCommandLists);
    }

    return fenceValue;
}

void CommandQueue::Wait(const CommandQueue& other)
{
    m_commandQueue->Wait(other.m_fence.Get(), other.m_fenceValue);
}

Microsoft::WRL::ComPtr<ID3D12CommandQueue> CommandQueue::GetCommandQueue() const
{
    return m_commandQueue;
}

void CommandQueue::ProccessInFlightCommandLists()
{
    std::unique_lock<std::mutex> lock(m_processInFlightCommandListsThreadMutex, std::defer_lock);

    while (m_processInFlightCommandLists)
    {
        CommandListEntry commandListEntry;

        lock.lock();
        while (m_inFlightCommandLists.TryPop(commandListEntry))
        {
            auto fenceValue = std::get<0>(commandListEntry);
            auto commandList = std::get<1>(commandListEntry);

            WaitForFenceValue(fenceValue);

            commandList->Reset();

            m_availableCommandLists.Push(commandList);
        }
        lock.unlock();
        m_processInFlightCommandListsThreadCV.notify_one();

        std::this_thread::yield();
    }
}