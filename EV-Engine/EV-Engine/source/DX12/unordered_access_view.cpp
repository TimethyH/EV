#include "DX12/dx12_includes.h"

#include <DX12/unordered_access_view.h>

// #include <dx12lib/Device.h>
#include <resources/resource.h>

#include "core/application.h"

using namespace EV;

UnorderedAccessView::UnorderedAccessView(const std::shared_ptr<Resource>& resource,
    const std::shared_ptr<Resource>& counterResource,
    const D3D12_UNORDERED_ACCESS_VIEW_DESC* uav)
	    : m_resource(resource)
    , m_counterResource(counterResource)
{
    assert(m_resource || uav);

    auto d3d12Device = Application::Get().GetDevice();
    auto d3d12Resource = m_resource ? m_resource->GetD3D12Resource() : nullptr;
    auto d3d12CounterResource = m_counterResource ? m_counterResource->GetD3D12Resource() : nullptr;

    if (m_resource)
    {
        auto d3d12ResourceDesc = m_resource->GetD3D12ResourceDesc();

        // Resource must be created with the D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS flag.
        assert((d3d12ResourceDesc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) != 0);
    }

    m_descriptor = Application::Get().AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    d3d12Device->CreateUnorderedAccessView(d3d12Resource.Get(), d3d12CounterResource.Get(), uav,
        m_descriptor.GetDescriptorHandle());
}
