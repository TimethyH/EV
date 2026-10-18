#pragma once

#include <dxgiformat.h>
#include <cstdint>

namespace EV
{
    // How a format is read in a shader.
    enum class FormatClass
    {
        Float,       // UNORM, SNORM, FLOAT and block compressed formats.
        Uint,
        Sint,
        Unsupported, // Video, palette and other formats the texture viewer cannot read.
    };

    namespace FormatUtils
    {
        const char* GetName(DXGI_FORMAT format);
        FormatClass GetClass(DXGI_FORMAT format);
        bool IsBlockCompressed(DXGI_FORMAT format);
        bool IsSRGB(DXGI_FORMAT format);
        uint32_t GetChannelCount(DXGI_FORMAT format);

        // Convert a (possibly typeless or depth) resource format into a format that can be used in an SRV.
        DXGI_FORMAT GetShaderReadableFormat(DXGI_FORMAT format);

        // Bytes per texel for uncompressed formats, 0 for block compressed or unknown formats.
        uint32_t GetTexelSize(DXGI_FORMAT format);

        // Decode a single texel into floats. Integer formats are converted to float as-is.
        // Returns false if the format cannot be decoded on the CPU.
        bool DecodeTexel(DXGI_FORMAT format, const uint8_t* data, float outValue[4]);
    }
}
