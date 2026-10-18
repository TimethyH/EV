#include "DX12/dx12_includes.h"

#include "UI/format_utils.h"

#include <DirectXPackedVector.h>
#include <cstring>
#include <string_view>

using namespace EV;
using namespace DirectX::PackedVector;

const char* FormatUtils::GetName(DXGI_FORMAT format)
{
    switch (format)
    {
    case DXGI_FORMAT_R32G32B32A32_TYPELESS: return "R32G32B32A32_TYPELESS";
    case DXGI_FORMAT_R32G32B32A32_FLOAT: return "R32G32B32A32_FLOAT";
    case DXGI_FORMAT_R32G32B32A32_UINT: return "R32G32B32A32_UINT";
    case DXGI_FORMAT_R32G32B32A32_SINT: return "R32G32B32A32_SINT";
    case DXGI_FORMAT_R32G32B32_TYPELESS: return "R32G32B32_TYPELESS";
    case DXGI_FORMAT_R32G32B32_FLOAT: return "R32G32B32_FLOAT";
    case DXGI_FORMAT_R32G32B32_UINT: return "R32G32B32_UINT";
    case DXGI_FORMAT_R32G32B32_SINT: return "R32G32B32_SINT";
    case DXGI_FORMAT_R16G16B16A16_TYPELESS: return "R16G16B16A16_TYPELESS";
    case DXGI_FORMAT_R16G16B16A16_FLOAT: return "R16G16B16A16_FLOAT";
    case DXGI_FORMAT_R16G16B16A16_UNORM: return "R16G16B16A16_UNORM";
    case DXGI_FORMAT_R16G16B16A16_UINT: return "R16G16B16A16_UINT";
    case DXGI_FORMAT_R16G16B16A16_SNORM: return "R16G16B16A16_SNORM";
    case DXGI_FORMAT_R16G16B16A16_SINT: return "R16G16B16A16_SINT";
    case DXGI_FORMAT_R32G32_TYPELESS: return "R32G32_TYPELESS";
    case DXGI_FORMAT_R32G32_FLOAT: return "R32G32_FLOAT";
    case DXGI_FORMAT_R32G32_UINT: return "R32G32_UINT";
    case DXGI_FORMAT_R32G32_SINT: return "R32G32_SINT";
    case DXGI_FORMAT_R32G8X24_TYPELESS: return "R32G8X24_TYPELESS";
    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT: return "D32_FLOAT_S8X24_UINT";
    case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS: return "R32_FLOAT_X8X24_TYPELESS";
    case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT: return "X32_TYPELESS_G8X24_UINT";
    case DXGI_FORMAT_R10G10B10A2_TYPELESS: return "R10G10B10A2_TYPELESS";
    case DXGI_FORMAT_R10G10B10A2_UNORM: return "R10G10B10A2_UNORM";
    case DXGI_FORMAT_R10G10B10A2_UINT: return "R10G10B10A2_UINT";
    case DXGI_FORMAT_R11G11B10_FLOAT: return "R11G11B10_FLOAT";
    case DXGI_FORMAT_R8G8B8A8_TYPELESS: return "R8G8B8A8_TYPELESS";
    case DXGI_FORMAT_R8G8B8A8_UNORM: return "R8G8B8A8_UNORM";
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return "R8G8B8A8_UNORM_SRGB";
    case DXGI_FORMAT_R8G8B8A8_UINT: return "R8G8B8A8_UINT";
    case DXGI_FORMAT_R8G8B8A8_SNORM: return "R8G8B8A8_SNORM";
    case DXGI_FORMAT_R8G8B8A8_SINT: return "R8G8B8A8_SINT";
    case DXGI_FORMAT_R16G16_TYPELESS: return "R16G16_TYPELESS";
    case DXGI_FORMAT_R16G16_FLOAT: return "R16G16_FLOAT";
    case DXGI_FORMAT_R16G16_UNORM: return "R16G16_UNORM";
    case DXGI_FORMAT_R16G16_UINT: return "R16G16_UINT";
    case DXGI_FORMAT_R16G16_SNORM: return "R16G16_SNORM";
    case DXGI_FORMAT_R16G16_SINT: return "R16G16_SINT";
    case DXGI_FORMAT_R32_TYPELESS: return "R32_TYPELESS";
    case DXGI_FORMAT_D32_FLOAT: return "D32_FLOAT";
    case DXGI_FORMAT_R32_FLOAT: return "R32_FLOAT";
    case DXGI_FORMAT_R32_UINT: return "R32_UINT";
    case DXGI_FORMAT_R32_SINT: return "R32_SINT";
    case DXGI_FORMAT_R24G8_TYPELESS: return "R24G8_TYPELESS";
    case DXGI_FORMAT_D24_UNORM_S8_UINT: return "D24_UNORM_S8_UINT";
    case DXGI_FORMAT_R24_UNORM_X8_TYPELESS: return "R24_UNORM_X8_TYPELESS";
    case DXGI_FORMAT_X24_TYPELESS_G8_UINT: return "X24_TYPELESS_G8_UINT";
    case DXGI_FORMAT_R8G8_TYPELESS: return "R8G8_TYPELESS";
    case DXGI_FORMAT_R8G8_UNORM: return "R8G8_UNORM";
    case DXGI_FORMAT_R8G8_UINT: return "R8G8_UINT";
    case DXGI_FORMAT_R8G8_SNORM: return "R8G8_SNORM";
    case DXGI_FORMAT_R8G8_SINT: return "R8G8_SINT";
    case DXGI_FORMAT_R16_TYPELESS: return "R16_TYPELESS";
    case DXGI_FORMAT_R16_FLOAT: return "R16_FLOAT";
    case DXGI_FORMAT_D16_UNORM: return "D16_UNORM";
    case DXGI_FORMAT_R16_UNORM: return "R16_UNORM";
    case DXGI_FORMAT_R16_UINT: return "R16_UINT";
    case DXGI_FORMAT_R16_SNORM: return "R16_SNORM";
    case DXGI_FORMAT_R16_SINT: return "R16_SINT";
    case DXGI_FORMAT_R8_TYPELESS: return "R8_TYPELESS";
    case DXGI_FORMAT_R8_UNORM: return "R8_UNORM";
    case DXGI_FORMAT_R8_UINT: return "R8_UINT";
    case DXGI_FORMAT_R8_SNORM: return "R8_SNORM";
    case DXGI_FORMAT_R8_SINT: return "R8_SINT";
    case DXGI_FORMAT_A8_UNORM: return "A8_UNORM";
    case DXGI_FORMAT_R1_UNORM: return "R1_UNORM";
    case DXGI_FORMAT_R9G9B9E5_SHAREDEXP: return "R9G9B9E5_SHAREDEXP";
    case DXGI_FORMAT_R8G8_B8G8_UNORM: return "R8G8_B8G8_UNORM";
    case DXGI_FORMAT_G8R8_G8B8_UNORM: return "G8R8_G8B8_UNORM";
    case DXGI_FORMAT_BC1_TYPELESS: return "BC1_TYPELESS";
    case DXGI_FORMAT_BC1_UNORM: return "BC1_UNORM";
    case DXGI_FORMAT_BC1_UNORM_SRGB: return "BC1_UNORM_SRGB";
    case DXGI_FORMAT_BC2_TYPELESS: return "BC2_TYPELESS";
    case DXGI_FORMAT_BC2_UNORM: return "BC2_UNORM";
    case DXGI_FORMAT_BC2_UNORM_SRGB: return "BC2_UNORM_SRGB";
    case DXGI_FORMAT_BC3_TYPELESS: return "BC3_TYPELESS";
    case DXGI_FORMAT_BC3_UNORM: return "BC3_UNORM";
    case DXGI_FORMAT_BC3_UNORM_SRGB: return "BC3_UNORM_SRGB";
    case DXGI_FORMAT_BC4_TYPELESS: return "BC4_TYPELESS";
    case DXGI_FORMAT_BC4_UNORM: return "BC4_UNORM";
    case DXGI_FORMAT_BC4_SNORM: return "BC4_SNORM";
    case DXGI_FORMAT_BC5_TYPELESS: return "BC5_TYPELESS";
    case DXGI_FORMAT_BC5_UNORM: return "BC5_UNORM";
    case DXGI_FORMAT_BC5_SNORM: return "BC5_SNORM";
    case DXGI_FORMAT_B5G6R5_UNORM: return "B5G6R5_UNORM";
    case DXGI_FORMAT_B5G5R5A1_UNORM: return "B5G5R5A1_UNORM";
    case DXGI_FORMAT_B8G8R8A8_UNORM: return "B8G8R8A8_UNORM";
    case DXGI_FORMAT_B8G8R8X8_UNORM: return "B8G8R8X8_UNORM";
    case DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM: return "R10G10B10_XR_BIAS_A2_UNORM";
    case DXGI_FORMAT_B8G8R8A8_TYPELESS: return "B8G8R8A8_TYPELESS";
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return "B8G8R8A8_UNORM_SRGB";
    case DXGI_FORMAT_B8G8R8X8_TYPELESS: return "B8G8R8X8_TYPELESS";
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB: return "B8G8R8X8_UNORM_SRGB";
    case DXGI_FORMAT_BC6H_TYPELESS: return "BC6H_TYPELESS";
    case DXGI_FORMAT_BC6H_UF16: return "BC6H_UF16";
    case DXGI_FORMAT_BC6H_SF16: return "BC6H_SF16";
    case DXGI_FORMAT_BC7_TYPELESS: return "BC7_TYPELESS";
    case DXGI_FORMAT_BC7_UNORM: return "BC7_UNORM";
    case DXGI_FORMAT_BC7_UNORM_SRGB: return "BC7_UNORM_SRGB";
    case DXGI_FORMAT_AYUV: return "AYUV";
    case DXGI_FORMAT_Y410: return "Y410";
    case DXGI_FORMAT_Y416: return "Y416";
    case DXGI_FORMAT_NV12: return "NV12";
    case DXGI_FORMAT_P010: return "P010";
    case DXGI_FORMAT_P016: return "P016";
    case DXGI_FORMAT_420_OPAQUE: return "420_OPAQUE";
    case DXGI_FORMAT_YUY2: return "YUY2";
    case DXGI_FORMAT_Y210: return "Y210";
    case DXGI_FORMAT_Y216: return "Y216";
    case DXGI_FORMAT_NV11: return "NV11";
    case DXGI_FORMAT_AI44: return "AI44";
    case DXGI_FORMAT_IA44: return "IA44";
    case DXGI_FORMAT_P8: return "P8";
    case DXGI_FORMAT_A8P8: return "A8P8";
    case DXGI_FORMAT_B4G4R4A4_UNORM: return "B4G4R4A4_UNORM";
    case DXGI_FORMAT_P208: return "P208";
    case DXGI_FORMAT_V208: return "V208";
    case DXGI_FORMAT_V408: return "V408";
    case DXGI_FORMAT_SAMPLER_FEEDBACK_MIN_MIP_OPAQUE: return "SAMPLER_FEEDBACK_MIN_MIP_OPAQUE";
    case DXGI_FORMAT_SAMPLER_FEEDBACK_MIP_REGION_USED_OPAQUE: return "SAMPLER_FEEDBACK_MIP_REGION_USED_OPAQUE";
    case DXGI_FORMAT_A4B4G4R4_UNORM: return "A4B4G4R4_UNORM";
    default: return "UNKNOWN";
    }
}

static bool EndsWith(std::string_view str, std::string_view suffix)
{
    return str.size() >= suffix.size() && str.substr(str.size() - suffix.size()) == suffix;
}

FormatClass FormatUtils::GetClass(DXGI_FORMAT format)
{
    const DXGI_FORMAT readable = GetShaderReadableFormat(format);
    const std::string_view name = GetName(readable);

    const bool isDepthView = readable == DXGI_FORMAT_R24_UNORM_X8_TYPELESS || readable == DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
    if (readable == DXGI_FORMAT_UNKNOWN || (EndsWith(name, "TYPELESS") && !isDepthView))
    {
        return FormatClass::Unsupported;
    }
    if (EndsWith(name, "_UINT"))
    {
        return FormatClass::Uint;
    }
    if (EndsWith(name, "_SINT"))
    {
        return FormatClass::Sint;
    }
    if (name.find("UNORM") != std::string_view::npos || name.find("SNORM") != std::string_view::npos ||
        name.find("FLOAT") != std::string_view::npos || name.find("SF16") != std::string_view::npos ||
        name.find("UF16") != std::string_view::npos || name == "R9G9B9E5_SHAREDEXP" || isDepthView)
    {
        return FormatClass::Float;
    }
    return FormatClass::Unsupported;
}

bool FormatUtils::IsBlockCompressed(DXGI_FORMAT format)
{
    return (format >= DXGI_FORMAT_BC1_TYPELESS && format <= DXGI_FORMAT_BC5_SNORM) ||
        (format >= DXGI_FORMAT_BC6H_TYPELESS && format <= DXGI_FORMAT_BC7_UNORM_SRGB);
}

bool FormatUtils::IsSRGB(DXGI_FORMAT format)
{
    return EndsWith(GetName(format), "_SRGB");
}

uint32_t FormatUtils::GetChannelCount(DXGI_FORMAT format)
{
    const std::string_view name = GetName(GetShaderReadableFormat(format));
    if (name.starts_with("BC4") || name.starts_with("R24_") || name.starts_with("R32_FLOAT_X8"))
    {
        return 1;
    }
    if (name.starts_with("BC5"))
    {
        return 2;
    }
    if (name.starts_with("BC1") || name.starts_with("BC6") || name.starts_with("R11G11B10") || name.starts_with("R9G9B9E5") ||
        name.starts_with("B5G6R5") || name.starts_with("B8G8R8X8"))
    {
        return 3;
    }
    if (name.starts_with("BC") || name.starts_with("A8"))
    {
        return name.starts_with("A8") ? 1 : 4;
    }

    // Count the channel letters before the first '_'.
    uint32_t channels = 0;
    for (char c : name.substr(0, name.find('_')))
    {
        if (c == 'R' || c == 'G' || c == 'B' || c == 'A')
        {
            ++channels;
        }
    }
    return channels == 0 ? 4 : channels;
}

DXGI_FORMAT FormatUtils::GetShaderReadableFormat(DXGI_FORMAT format)
{
    switch (format)
    {
    // Depth formats
    case DXGI_FORMAT_D32_FLOAT:
    case DXGI_FORMAT_R32_TYPELESS:
        return DXGI_FORMAT_R32_FLOAT;
    case DXGI_FORMAT_D24_UNORM_S8_UINT:
    case DXGI_FORMAT_R24G8_TYPELESS:
        return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    case DXGI_FORMAT_D16_UNORM:
    case DXGI_FORMAT_R16_TYPELESS:
        return DXGI_FORMAT_R16_UNORM;
    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
    case DXGI_FORMAT_R32G8X24_TYPELESS:
        return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;

    // Color typeless formats
    case DXGI_FORMAT_R32G32B32A32_TYPELESS: return DXGI_FORMAT_R32G32B32A32_FLOAT;
    case DXGI_FORMAT_R32G32B32_TYPELESS: return DXGI_FORMAT_R32G32B32_FLOAT;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case DXGI_FORMAT_R32G32_TYPELESS: return DXGI_FORMAT_R32G32_FLOAT;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS: return DXGI_FORMAT_R10G10B10A2_UNORM;
    case DXGI_FORMAT_R8G8B8A8_TYPELESS: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_R16G16_TYPELESS: return DXGI_FORMAT_R16G16_FLOAT;
    case DXGI_FORMAT_R8G8_TYPELESS: return DXGI_FORMAT_R8G8_UNORM;
    case DXGI_FORMAT_R8_TYPELESS: return DXGI_FORMAT_R8_UNORM;
    case DXGI_FORMAT_BC1_TYPELESS: return DXGI_FORMAT_BC1_UNORM;
    case DXGI_FORMAT_BC2_TYPELESS: return DXGI_FORMAT_BC2_UNORM;
    case DXGI_FORMAT_BC3_TYPELESS: return DXGI_FORMAT_BC3_UNORM;
    case DXGI_FORMAT_BC4_TYPELESS: return DXGI_FORMAT_BC4_UNORM;
    case DXGI_FORMAT_BC5_TYPELESS: return DXGI_FORMAT_BC5_UNORM;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS: return DXGI_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_B8G8R8X8_TYPELESS: return DXGI_FORMAT_B8G8R8X8_UNORM;
    case DXGI_FORMAT_BC6H_TYPELESS: return DXGI_FORMAT_BC6H_UF16;
    case DXGI_FORMAT_BC7_TYPELESS: return DXGI_FORMAT_BC7_UNORM;
    default:
        return format;
    }
}

uint32_t FormatUtils::GetTexelSize(DXGI_FORMAT format)
{
    if (IsBlockCompressed(format))
    {
        return 0;
    }
    return static_cast<uint32_t>(DirectX::BitsPerPixel(format) / 8);
}

static float UnormToFloat(uint32_t value, uint32_t bits)
{
    return static_cast<float>(value) / static_cast<float>((1ull << bits) - 1);
}

static float SnormToFloat(int32_t value, uint32_t bits)
{
    const float maxValue = static_cast<float>((1 << (bits - 1)) - 1);
    float f = static_cast<float>(value) / maxValue;
    return f < -1.0f ? -1.0f : f;
}

bool FormatUtils::DecodeTexel(DXGI_FORMAT format, const uint8_t* data, float outValue[4])
{
    outValue[0] = outValue[1] = outValue[2] = 0.0f;
    outValue[3] = 1.0f;

    auto readAs = [data]<typename T>(size_t index) -> T
    {
        T value;
        std::memcpy(&value, data + index * sizeof(T), sizeof(T));
        return value;
    };

    const uint32_t channels = GetChannelCount(format);

    switch (GetShaderReadableFormat(format))
    {
    case DXGI_FORMAT_R32G32B32A32_FLOAT:
    case DXGI_FORMAT_R32G32B32_FLOAT:
    case DXGI_FORMAT_R32G32_FLOAT:
    case DXGI_FORMAT_R32_FLOAT:
    case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
        for (uint32_t i = 0; i < channels; ++i) outValue[i] = readAs.operator()<float>(i);
        return true;
    case DXGI_FORMAT_R32G32B32A32_UINT:
    case DXGI_FORMAT_R32G32B32_UINT:
    case DXGI_FORMAT_R32G32_UINT:
    case DXGI_FORMAT_R32_UINT:
        for (uint32_t i = 0; i < channels; ++i) outValue[i] = static_cast<float>(readAs.operator()<uint32_t>(i));
        return true;
    case DXGI_FORMAT_R32G32B32A32_SINT:
    case DXGI_FORMAT_R32G32B32_SINT:
    case DXGI_FORMAT_R32G32_SINT:
    case DXGI_FORMAT_R32_SINT:
        for (uint32_t i = 0; i < channels; ++i) outValue[i] = static_cast<float>(readAs.operator()<int32_t>(i));
        return true;
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
    case DXGI_FORMAT_R16G16_FLOAT:
    case DXGI_FORMAT_R16_FLOAT:
        for (uint32_t i = 0; i < channels; ++i) outValue[i] = XMConvertHalfToFloat(readAs.operator()<HALF>(i));
        return true;
    case DXGI_FORMAT_R16G16B16A16_UNORM:
    case DXGI_FORMAT_R16G16_UNORM:
    case DXGI_FORMAT_R16_UNORM:
        for (uint32_t i = 0; i < channels; ++i) outValue[i] = UnormToFloat(readAs.operator()<uint16_t>(i), 16);
        return true;
    case DXGI_FORMAT_R16G16B16A16_SNORM:
    case DXGI_FORMAT_R16G16_SNORM:
    case DXGI_FORMAT_R16_SNORM:
        for (uint32_t i = 0; i < channels; ++i) outValue[i] = SnormToFloat(readAs.operator()<int16_t>(i), 16);
        return true;
    case DXGI_FORMAT_R16G16B16A16_UINT:
    case DXGI_FORMAT_R16G16_UINT:
    case DXGI_FORMAT_R16_UINT:
        for (uint32_t i = 0; i < channels; ++i) outValue[i] = static_cast<float>(readAs.operator()<uint16_t>(i));
        return true;
    case DXGI_FORMAT_R16G16B16A16_SINT:
    case DXGI_FORMAT_R16G16_SINT:
    case DXGI_FORMAT_R16_SINT:
        for (uint32_t i = 0; i < channels; ++i) outValue[i] = static_cast<float>(readAs.operator()<int16_t>(i));
        return true;
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_R8G8_UNORM:
    case DXGI_FORMAT_R8_UNORM:
    case DXGI_FORMAT_A8_UNORM:
        for (uint32_t i = 0; i < channels; ++i) outValue[i] = UnormToFloat(data[i], 8);
        return true;
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8X8_UNORM:
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
        outValue[0] = UnormToFloat(data[2], 8);
        outValue[1] = UnormToFloat(data[1], 8);
        outValue[2] = UnormToFloat(data[0], 8);
        outValue[3] = channels == 4 ? UnormToFloat(data[3], 8) : 1.0f;
        return true;
    case DXGI_FORMAT_R8G8B8A8_SNORM:
    case DXGI_FORMAT_R8G8_SNORM:
    case DXGI_FORMAT_R8_SNORM:
        for (uint32_t i = 0; i < channels; ++i) outValue[i] = SnormToFloat(static_cast<int8_t>(data[i]), 8);
        return true;
    case DXGI_FORMAT_R8G8B8A8_UINT:
    case DXGI_FORMAT_R8G8_UINT:
    case DXGI_FORMAT_R8_UINT:
        for (uint32_t i = 0; i < channels; ++i) outValue[i] = static_cast<float>(data[i]);
        return true;
    case DXGI_FORMAT_R8G8B8A8_SINT:
    case DXGI_FORMAT_R8G8_SINT:
    case DXGI_FORMAT_R8_SINT:
        for (uint32_t i = 0; i < channels; ++i) outValue[i] = static_cast<float>(static_cast<int8_t>(data[i]));
        return true;
    case DXGI_FORMAT_R10G10B10A2_UNORM:
    {
        const uint32_t v = readAs.operator()<uint32_t>(0);
        outValue[0] = UnormToFloat(v & 0x3FF, 10);
        outValue[1] = UnormToFloat((v >> 10) & 0x3FF, 10);
        outValue[2] = UnormToFloat((v >> 20) & 0x3FF, 10);
        outValue[3] = UnormToFloat(v >> 30, 2);
        return true;
    }
    case DXGI_FORMAT_R11G11B10_FLOAT:
    {
        XMFLOAT3PK packed(readAs.operator()<uint32_t>(0));
        DirectX::XMFLOAT3 unpacked;
        DirectX::XMStoreFloat3(&unpacked, XMLoadFloat3PK(&packed));
        outValue[0] = unpacked.x;
        outValue[1] = unpacked.y;
        outValue[2] = unpacked.z;
        return true;
    }
    case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
        outValue[0] = UnormToFloat(readAs.operator()<uint32_t>(0) & 0xFFFFFF, 24);
        return true;
    default:
        return false;
    }
}
