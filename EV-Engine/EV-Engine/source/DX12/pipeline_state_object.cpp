#include "DX12/dx12_includes.h"

#include <DX12/pipeline_state_object.h>

#include "core/application.h"
#include "utility/helpers.h"

// #include <dx12lib/Device.h>

using namespace EV;

PipelineStateObject::PipelineStateObject(const D3D12_PIPELINE_STATE_STREAM_DESC& desc)
{
    auto d3d12Device = Application::Get().GetDevice();

    ThrowIfFailed(d3d12Device->CreatePipelineState(&desc, IID_PPV_ARGS(&m_pipelineState)));
}