#include "dx12_includes.h"

#include <pipeline_state_object.h>

#include "application.h"

// #include <dx12lib/Device.h>

// using namespace dx12lib;

PipelineStateObject::PipelineStateObject(const D3D12_PIPELINE_STATE_STREAM_DESC& desc)
{
    auto d3d12Device = Application::Get().GetDevice();

    ThrowIfFailed(d3d12Device->CreatePipelineState(&desc, IID_PPV_ARGS(&m_pipelineState)));
}