#include <DX12/dx12_includes.h>

#include <resources/resource.h>

#include <core/application.h>
#include <DX12/resource_state_tracker.h>

#include "utility/helpers.h"

// Resource::Resource(const std::wstring& name)
//     : m_resourceName(name)
// {
// }
//
// Resource::Resource(const D3D12_RESOURCE_DESC& resourceDesc, const D3D12_CLEAR_VALUE* clearValue, const std::wstring& name)
// {
//     auto device = Application::Get().GetDevice();
//
//     if (clearValue)
//     {
//         m_clearValue = std::make_unique<D3D12_CLEAR_VALUE>(*clearValue);
//     }
//
//     CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
//
//     ThrowIfFailed(device->CreateCommittedResource(
//         &heapProps,
//         D3D12_HEAP_FLAG_NONE,
//         &resourceDesc,
//         D3D12_RESOURCE_STATE_COMMON,
//         m_clearValue.get(),
//         IID_PPV_ARGS(&m_resource)
//     ));
//
//     ResourceStateTracker::AddGlobalResourceState(m_resource.Get(), D3D12_RESOURCE_STATE_COMMON);
//
//     SetName(name);
// }
//
// Resource::Resource(Microsoft::WRL::ComPtr<ID3D12Resource> resource, const std::wstring& name)
//     : m_resource(resource)
// {
//     SetName(name);
// }
//
// Resource::Resource(const Resource& copy)
//     : m_resource(copy.m_resource)
//     , m_resourceName(copy.m_resourceName)
//     , m_clearValue(std::make_unique<D3D12_CLEAR_VALUE>(*copy.m_clearValue))
// {
//     int i = 3;
// }
//
// Resource::Resource(Resource&& copy)
//     : m_resource(std::move(copy.m_resource))
//     , m_resourceName(std::move(copy.m_resourceName))
//     , m_clearValue(std::move(copy.m_clearValue))
// {
// }
//
// Resource& Resource::operator=(const Resource& other)
// {
//     if (this != &other)
//     {
//         m_resource = other.m_resource;
//         m_resourceName = other.m_resourceName;
//         if (other.m_clearValue)
//         {
//             m_clearValue = std::make_unique<D3D12_CLEAR_VALUE>(*other.m_clearValue);
//         }
//     }
//
//     return *this;
// }
//
// Resource& Resource::operator=(Resource&& other)
// {
//     if (this != &other)
//     {
//         m_resource = other.m_resource;
//         m_resourceName = other.m_resourceName;
//         m_clearValue = std::move(other.m_clearValue);
//
//         other.m_resource.Reset();
//         other.m_resourceName.clear();
//     }
//
//     return *this;
// }

using namespace EV;


Resource::Resource(const D3D12_RESOURCE_DESC& resourceDesc, const D3D12_CLEAR_VALUE* clearValue)
{
    auto d3d12Device = Application::Get().GetDevice();

    if (clearValue)
    {
        m_clearValue = std::make_unique<D3D12_CLEAR_VALUE>(*clearValue);
    }

    CD3DX12_HEAP_PROPERTIES heapProp(D3D12_HEAP_TYPE_DEFAULT);

    ThrowIfFailed(d3d12Device->CreateCommittedResource(
        &heapProp, D3D12_HEAP_FLAG_NONE, &resourceDesc,
        D3D12_RESOURCE_STATE_COMMON, m_clearValue.get(), IID_PPV_ARGS(&m_resource)));

    ResourceStateTracker::AddGlobalResourceState(m_resource.Get(), D3D12_RESOURCE_STATE_COMMON);

    CheckFeatureSupport();
}

Resource::Resource(Microsoft::WRL::ComPtr<ID3D12Resource> resource,
    const D3D12_CLEAR_VALUE* clearValue)
	    : m_resource(resource)
{
    if (clearValue)
    {
        m_clearValue = std::make_unique<D3D12_CLEAR_VALUE>(*clearValue);
    }
    CheckFeatureSupport();
}

Resource::~Resource()
{
}

void Resource::SetD3D12Resource(Microsoft::WRL::ComPtr<ID3D12Resource> d3d12Resource, const D3D12_CLEAR_VALUE* clearValue)
{
    m_resource = d3d12Resource;
    if (m_clearValue)
    {
        m_clearValue = std::make_unique<D3D12_CLEAR_VALUE>(*clearValue);
    }
    else
    {
        m_clearValue.reset();
    }
    SetName(m_resourceName);
}

void Resource::SetName(const std::wstring& name)
{
    m_resourceName = name;
    if (m_resource && !m_resourceName.empty())
    {
        m_resource->SetName(m_resourceName.c_str());
    }
}

void Resource::Reset()
{
    m_resource.Reset();
    m_clearValue.reset();
}

bool Resource::CheckFormatSupport(D3D12_FORMAT_SUPPORT1 formatSupport) const
{
    return (m_formatSupport.Support1 & formatSupport) != 0;
}

bool Resource::CheckFormatSupport(D3D12_FORMAT_SUPPORT2 formatSupport) const
{
    return (m_formatSupport.Support2 & formatSupport) != 0;
}

void Resource::CheckFeatureSupport()
{
    auto d3d12Device = Application::Get().GetDevice();

    auto desc = m_resource->GetDesc();
    m_formatSupport.Format = desc.Format;
    ThrowIfFailed(d3d12Device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &m_formatSupport,
        sizeof(D3D12_FEATURE_DATA_FORMAT_SUPPORT)));
}