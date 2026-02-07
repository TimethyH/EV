#include "DX12/dx12_includes.h"

#include <DX12/shader_resource_view.h>

// #include <dx12lib/Device.h>
#include <resources/resource.h>

#include "core/application.h"

using namespace EV;

ShaderResourceView::ShaderResourceView(const std::shared_ptr<Resource>& resource,
    const D3D12_SHADER_RESOURCE_VIEW_DESC* srv)
	    : m_resource(resource)
{
    assert(resource || srv);

    auto d3d12Resource = m_resource ? m_resource->GetD3D12Resource() : nullptr;
    auto d3d12Device = Application::Get().GetDevice();

    m_descriptor = Application::Get().AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    d3d12Device->CreateShaderResourceView(d3d12Resource.Get(), srv, m_descriptor.GetDescriptorHandle());
}
