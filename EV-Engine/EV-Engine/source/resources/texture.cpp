#include <DX12/dx12_includes.h>

#include <resources/texture.h>

#include <core/application.h>
#include <utility/helpers.h>
#include <DX12/resource_state_tracker.h>

using namespace EV;

/// Claude code to hash the SRV UAV structs --------------------------------

namespace
{
    template<typename T>
    void hash_combine(std::size_t& seed, const T& value)
    {
        seed ^= std::hash<T>{}(value)+0x9e3779b9 + (seed << 6) + (seed >> 2);
    }

    std::size_t HashSRVDesc(const D3D12_SHADER_RESOURCE_VIEW_DESC& desc)
    {
        std::size_t hash = 0;
        hash_combine(hash, static_cast<UINT>(desc.Format));
        hash_combine(hash, static_cast<UINT>(desc.ViewDimension));
        hash_combine(hash, desc.Shader4ComponentMapping);

        // Hash the union as raw bytes
        const char* data = reinterpret_cast<const char*>(&desc.Buffer);
        for (size_t i = 0; i < sizeof(desc.Buffer); ++i)
        {
            hash_combine(hash, static_cast<std::size_t>(data[i]));
        }
        return hash;
    }

    std::size_t HashUAVDesc(const D3D12_UNORDERED_ACCESS_VIEW_DESC& desc)
    {
        std::size_t hash = 0;
        hash_combine(hash, static_cast<UINT>(desc.Format));
        hash_combine(hash, static_cast<UINT>(desc.ViewDimension));

        // Hash the union as raw bytes
        const char* data = reinterpret_cast<const char*>(&desc.Buffer);
        for (size_t i = 0; i < sizeof(desc.Buffer); ++i)
        {
            hash_combine(hash, static_cast<std::size_t>(data[i]));
        }
        return hash;
    }
}

/// --------------------------------

// Texture::Texture(TextureUsage textureUsage, const std::wstring& name)
//     : Resource(name)
//     , m_textureUsage(textureUsage)
// {
// }
//
// Texture::Texture(const D3D12_RESOURCE_DESC& resourceDesc,
//     const D3D12_CLEAR_VALUE* clearValue,
//     TextureUsage textureUsage,
//     const std::wstring& name)
//     : Resource(resourceDesc, clearValue, name)
//     , m_textureUsage(textureUsage)
// {
//     CreateViews();
// }
//
// Texture::Texture(Microsoft::WRL::ComPtr<ID3D12Resource> resource,
//     TextureUsage textureUsage,
//     const std::wstring& name)
//     : Resource(resource, name)
//     , m_textureUsage(textureUsage)
// {
//     CreateViews();
// }
//
// Texture::Texture(Microsoft::WRL::ComPtr<ID3D12Resource> resource,
//     TextureUsage textureUsage)
//     : Resource(resource)
//     , m_textureUsage(textureUsage)
// {
//     CreateViews();
// }



// Texture::Texture(const Texture& copy)
//     : Resource(copy)
// {
//     CreateViews();
// }
//
// Texture::Texture(Texture&& copy)
//     : Resource(copy)
// {
//     CreateViews();
// }

Texture::Texture(const D3D12_RESOURCE_DESC& resourceDesc, const D3D12_CLEAR_VALUE* clearValue)
    : Resource(resourceDesc, clearValue)
{
    CreateViews();
}

Texture::Texture(ComPtr<ID3D12Resource> resource,
    const D3D12_CLEAR_VALUE* clearValue)
    : Resource(resource, clearValue)
{
    CreateViews();
}

// Texture::Texture(const std::wstring& name) : Resource(name)
// {
// }

// Texture& Texture::operator=(const Texture& other)
// {
//     Resource::operator=(other);
//
//     CreateViews();
//
//     return *this;
// }
// Texture& Texture::operator=(Texture&& other)
// {
//     Resource::operator=(other);
//
//     CreateViews();
//
//     return *this;
// }

Texture::~Texture()
{
}

void Texture::Resize(uint32_t width, uint32_t height, uint32_t depthOrArraySize)
{
    // Resource can't be resized if it was never created in the first place.
    if (m_resource)
    {
        ResourceStateTracker::RemoveGlobalResourceState(m_resource.Get());

        CD3DX12_RESOURCE_DESC resDesc(m_resource->GetDesc());

        resDesc.Width = std::max(width, 1u);
        resDesc.Height = std::max(height, 1u);
        resDesc.DepthOrArraySize = depthOrArraySize;

        auto device = Application::Get().GetDevice();

        CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);

        ThrowIfFailed(device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &resDesc,
            D3D12_RESOURCE_STATE_COMMON,
            m_clearValue.get(),
            IID_PPV_ARGS(&m_resource)
        ));

        // Retain the name of the resource if one was already specified.
        m_resource->SetName(m_resourceName.c_str());

        ResourceStateTracker::AddGlobalResourceState(m_resource.Get(), D3D12_RESOURCE_STATE_COMMON);

        CreateViews();
    }
}

// Get a UAV description that matches the resource description.
D3D12_UNORDERED_ACCESS_VIEW_DESC GetUAVDesc(const D3D12_RESOURCE_DESC& resDesc, UINT mipSlice, UINT arraySlice = 0, UINT planeSlice = 0)
{
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = resDesc.Format;

    switch (resDesc.Dimension)
    {
    case D3D12_RESOURCE_DIMENSION_TEXTURE1D:
        if (resDesc.DepthOrArraySize > 1)
        {
            uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1DARRAY;
            uavDesc.Texture1DArray.ArraySize = resDesc.DepthOrArraySize - arraySlice;
            uavDesc.Texture1DArray.FirstArraySlice = arraySlice;
            uavDesc.Texture1DArray.MipSlice = mipSlice;
        }
        else
        {
            uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1D;
            uavDesc.Texture1D.MipSlice = mipSlice;
        }
        break;
    case D3D12_RESOURCE_DIMENSION_TEXTURE2D:
        if (resDesc.DepthOrArraySize > 1)
        {
            uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
            uavDesc.Texture2DArray.ArraySize = resDesc.DepthOrArraySize - arraySlice;
            uavDesc.Texture2DArray.FirstArraySlice = arraySlice;
            uavDesc.Texture2DArray.PlaneSlice = planeSlice;
            uavDesc.Texture2DArray.MipSlice = mipSlice;
        }
        else
        {
            uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            uavDesc.Texture2D.PlaneSlice = planeSlice;
            uavDesc.Texture2D.MipSlice = mipSlice;
        }
        break;
    case D3D12_RESOURCE_DIMENSION_TEXTURE3D:
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
        uavDesc.Texture3D.WSize = resDesc.DepthOrArraySize - arraySlice;
        uavDesc.Texture3D.FirstWSlice = arraySlice;
        uavDesc.Texture3D.MipSlice = mipSlice;
        break;
    default:
        throw std::exception("Invalid resource dimension.");
    }

    return uavDesc;
}

void Texture::CreateViews()
{
    if (m_resource)
    {
        auto& app = Application::Get();
        auto device = app.GetDevice();

        CD3DX12_RESOURCE_DESC desc(m_resource->GetDesc());

        // D3D12_FEATURE_DATA_FORMAT_SUPPORT formatSupport;
        // formatSupport.Format = desc.Format;
        // ThrowIfFailed(device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &formatSupport, sizeof(D3D12_FEATURE_DATA_FORMAT_SUPPORT)));

        // Create RTV
        if ((desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) != 0 && CheckRTVSupport())
        {
            m_renderTargetView = app.AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
            device->CreateRenderTargetView(m_resource.Get(), nullptr,
                m_renderTargetView.GetDescriptorHandle());
        }
        // Create DSV
        if ((desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) != 0 && CheckDSVSupport())
        {
            m_depthStencilView = app.AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
            device->CreateDepthStencilView(m_resource.Get(), nullptr,
                m_depthStencilView.GetDescriptorHandle());
        }
        // Create SRV
        if ((desc.Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE) == 0 && CheckSRVSupport())
        {
            m_shaderResourceView = app.AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            device->CreateShaderResourceView(m_resource.Get(), nullptr,
                m_shaderResourceView.GetDescriptorHandle());
        }
        // Create UAV for each mip (only supported for 1D and 2D textures).
        if ((desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) != 0 && CheckUAVSupport() &&
            desc.DepthOrArraySize == 1)
        {
            m_unorderedAccessView =
                app.AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, desc.MipLevels);
            for (int i = 0; i < desc.MipLevels; ++i)
            {
                auto uavDesc = GetUAVDesc(desc, i);
                device->CreateUnorderedAccessView(m_resource.Get(), nullptr, &uavDesc,
                    m_unorderedAccessView.GetDescriptorHandle(i));
            }
        }
    }
}

DescriptorAllocation Texture::CreateShaderResourceView(const D3D12_SHADER_RESOURCE_VIEW_DESC* srvDesc) const
{
    auto& app = Application::Get();
    auto device = app.GetDevice();
    auto srv = app.AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    device->CreateShaderResourceView(m_resource.Get(), srvDesc, srv.GetDescriptorHandle());

    return srv;
}

DescriptorAllocation Texture::CreateUnorderedAccessView(const D3D12_UNORDERED_ACCESS_VIEW_DESC* uavDesc) const
{
    auto& app = Application::Get();
    auto device = app.GetDevice();
    auto uav = app.AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    device->CreateUnorderedAccessView(m_resource.Get(), nullptr, uavDesc, uav.GetDescriptorHandle());

    return uav;
}


D3D12_CPU_DESCRIPTOR_HANDLE Texture::GetShaderResourceView() const
{
    return m_shaderResourceView.GetDescriptorHandle();
}

D3D12_CPU_DESCRIPTOR_HANDLE Texture::GetUnorderedAccessView(uint32_t mip) const
{
    return m_unorderedAccessView.GetDescriptorHandle(mip);
}

D3D12_CPU_DESCRIPTOR_HANDLE Texture::GetRenderTargetView() const
{
    return m_renderTargetView.GetDescriptorHandle();
}

D3D12_CPU_DESCRIPTOR_HANDLE Texture::GetDepthStencilView() const
{
    return m_depthStencilView.GetDescriptorHandle();
}

bool Texture::IsUAVCompatibleFormat(DXGI_FORMAT format)
{
    switch (format)
    {
    case DXGI_FORMAT_R32G32B32A32_FLOAT:
    case DXGI_FORMAT_R32G32B32A32_UINT:
    case DXGI_FORMAT_R32G32B32A32_SINT:
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
        //    case DXGI_FORMAT_R16G16B16A16_UNORM:
    case DXGI_FORMAT_R16G16B16A16_UINT:
    case DXGI_FORMAT_R16G16B16A16_SINT:
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UINT:
    case DXGI_FORMAT_R8G8B8A8_SINT:
    case DXGI_FORMAT_R32_FLOAT:
    case DXGI_FORMAT_R32_UINT:
    case DXGI_FORMAT_R32_SINT:
    case DXGI_FORMAT_R16_FLOAT:
    case DXGI_FORMAT_R16_UINT:
    case DXGI_FORMAT_R16_SINT:
    case DXGI_FORMAT_R8_UNORM:
    case DXGI_FORMAT_R8_UINT:
    case DXGI_FORMAT_R8_SINT:
        return true;
    default:
        return false;
    }
}

bool Texture::IsSRGBFormat(DXGI_FORMAT format)
{
    switch (format)
    {
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
        return true;
    default:
        return false;
    }
}

bool Texture::IsBGRFormat(DXGI_FORMAT format)
{
    switch (format)
    {
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8X8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
        return true;
    default:
        return false;
    }

}

bool Texture::IsDepthFormat(DXGI_FORMAT format)
{
    switch (format)
    {
    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
    case DXGI_FORMAT_D32_FLOAT:
    case DXGI_FORMAT_D24_UNORM_S8_UINT:
    case DXGI_FORMAT_D16_UNORM:
        return true;
    default:
        return false;
    }
}

DXGI_FORMAT Texture::GetTypelessFormat(DXGI_FORMAT format)
{
    DXGI_FORMAT typelessFormat = format;

    switch (format)
    {
    case DXGI_FORMAT_R32G32B32A32_FLOAT:
    case DXGI_FORMAT_R32G32B32A32_UINT:
    case DXGI_FORMAT_R32G32B32A32_SINT:
        typelessFormat = DXGI_FORMAT_R32G32B32A32_TYPELESS;
        break;
    case DXGI_FORMAT_R32G32B32_FLOAT:
    case DXGI_FORMAT_R32G32B32_UINT:
    case DXGI_FORMAT_R32G32B32_SINT:
        typelessFormat = DXGI_FORMAT_R32G32B32_TYPELESS;
        break;
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
    case DXGI_FORMAT_R16G16B16A16_UNORM:
    case DXGI_FORMAT_R16G16B16A16_UINT:
    case DXGI_FORMAT_R16G16B16A16_SNORM:
    case DXGI_FORMAT_R16G16B16A16_SINT:
        typelessFormat = DXGI_FORMAT_R16G16B16A16_TYPELESS;
        break;
    case DXGI_FORMAT_R32G32_FLOAT:
    case DXGI_FORMAT_R32G32_UINT:
    case DXGI_FORMAT_R32G32_SINT:
        typelessFormat = DXGI_FORMAT_R32G32_TYPELESS;
        break;
    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
        typelessFormat = DXGI_FORMAT_R32G8X24_TYPELESS;
        break;
    case DXGI_FORMAT_R10G10B10A2_UNORM:
    case DXGI_FORMAT_R10G10B10A2_UINT:
        typelessFormat = DXGI_FORMAT_R10G10B10A2_TYPELESS;
        break;
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_R8G8B8A8_UINT:
    case DXGI_FORMAT_R8G8B8A8_SNORM:
    case DXGI_FORMAT_R8G8B8A8_SINT:
        typelessFormat = DXGI_FORMAT_R8G8B8A8_TYPELESS;
        break;
    case DXGI_FORMAT_R16G16_FLOAT:
    case DXGI_FORMAT_R16G16_UNORM:
    case DXGI_FORMAT_R16G16_UINT:
    case DXGI_FORMAT_R16G16_SNORM:
    case DXGI_FORMAT_R16G16_SINT:
        typelessFormat = DXGI_FORMAT_R16G16_TYPELESS;
        break;
    case DXGI_FORMAT_D32_FLOAT:
    case DXGI_FORMAT_R32_FLOAT:
    case DXGI_FORMAT_R32_UINT:
    case DXGI_FORMAT_R32_SINT:
        typelessFormat = DXGI_FORMAT_R32_TYPELESS;
        break;
    case DXGI_FORMAT_R8G8_UNORM:
    case DXGI_FORMAT_R8G8_UINT:
    case DXGI_FORMAT_R8G8_SNORM:
    case DXGI_FORMAT_R8G8_SINT:
        typelessFormat = DXGI_FORMAT_R8G8_TYPELESS;
        break;
    case DXGI_FORMAT_R16_FLOAT:
    case DXGI_FORMAT_D16_UNORM:
    case DXGI_FORMAT_R16_UNORM:
    case DXGI_FORMAT_R16_UINT:
    case DXGI_FORMAT_R16_SNORM:
    case DXGI_FORMAT_R16_SINT:
        typelessFormat = DXGI_FORMAT_R16_TYPELESS;
    case DXGI_FORMAT_R8_UNORM:
    case DXGI_FORMAT_R8_UINT:
    case DXGI_FORMAT_R8_SNORM:
    case DXGI_FORMAT_R8_SINT:
        typelessFormat = DXGI_FORMAT_R8_TYPELESS;
        break;
    case DXGI_FORMAT_BC1_UNORM:
    case DXGI_FORMAT_BC1_UNORM_SRGB:
        typelessFormat = DXGI_FORMAT_BC1_TYPELESS;
        break;
    case DXGI_FORMAT_BC2_UNORM:
    case DXGI_FORMAT_BC2_UNORM_SRGB:
        typelessFormat = DXGI_FORMAT_BC2_TYPELESS;
        break;
    case DXGI_FORMAT_BC3_UNORM:
    case DXGI_FORMAT_BC3_UNORM_SRGB:
        typelessFormat = DXGI_FORMAT_BC3_TYPELESS;
        break;
    case DXGI_FORMAT_BC4_UNORM:
    case DXGI_FORMAT_BC4_SNORM:
        typelessFormat = DXGI_FORMAT_BC4_TYPELESS;
        break;
    case DXGI_FORMAT_BC5_UNORM:
    case DXGI_FORMAT_BC5_SNORM:
        typelessFormat = DXGI_FORMAT_BC5_TYPELESS;
        break;
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        typelessFormat = DXGI_FORMAT_B8G8R8A8_TYPELESS;
        break;
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
        typelessFormat = DXGI_FORMAT_B8G8R8X8_TYPELESS;
        break;
    case DXGI_FORMAT_BC6H_UF16:
    case DXGI_FORMAT_BC6H_SF16:
        typelessFormat = DXGI_FORMAT_BC6H_TYPELESS;
        break;
    case DXGI_FORMAT_BC7_UNORM:
    case DXGI_FORMAT_BC7_UNORM_SRGB:
        typelessFormat = DXGI_FORMAT_BC7_TYPELESS;
        break;
    }

    return typelessFormat;
}

DXGI_FORMAT Texture::GetUAVCompatableFormat(DXGI_FORMAT format)
{
    DXGI_FORMAT uavFormat = format;

    switch (format)
    {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8X8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8X8_TYPELESS:
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
        uavFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
        break;
    case DXGI_FORMAT_R32_TYPELESS:
    case DXGI_FORMAT_D32_FLOAT:
        uavFormat = DXGI_FORMAT_R32_FLOAT;
        break;
    }

    return uavFormat;
}

DXGI_FORMAT Texture::GetSRGBFormat(DXGI_FORMAT format)
{
    DXGI_FORMAT srgbFormat = format;
    switch (format)
    {
    case DXGI_FORMAT_R8G8B8A8_UNORM:
        srgbFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        break;
    case DXGI_FORMAT_BC1_UNORM:
        srgbFormat = DXGI_FORMAT_BC1_UNORM_SRGB;
        break;
    case DXGI_FORMAT_BC2_UNORM:
        srgbFormat = DXGI_FORMAT_BC2_UNORM_SRGB;
        break;
    case DXGI_FORMAT_BC3_UNORM:
        srgbFormat = DXGI_FORMAT_BC3_UNORM_SRGB;
        break;
    case DXGI_FORMAT_B8G8R8A8_UNORM:
        srgbFormat = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        break;
    case DXGI_FORMAT_B8G8R8X8_UNORM:
        srgbFormat = DXGI_FORMAT_B8G8R8X8_UNORM_SRGB;
        break;
    case DXGI_FORMAT_BC7_UNORM:
        srgbFormat = DXGI_FORMAT_BC7_UNORM_SRGB;
        break;
    }

    return srgbFormat;
}

size_t Texture::BitsPerPixel() const
{
    auto format = GetD3D12ResourceDesc().Format;
    return DirectX::BitsPerPixel(format);
}