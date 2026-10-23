#pragma once

/**
 *  @file textures.h
 *
 *  @brief Textures made on the CPU: text cards drawn with GDI and pictures loaded (and downscaled) with WIC.
 */

#include <DirectXMath.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace EV
{
    class CommandList;
    class Texture;
}

namespace Birthday
{
    // An RGBA8 image in sRGB, rows top to bottom.
    struct Image
    {
        uint32_t              width = 0;
        uint32_t              height = 0;
        std::vector<uint32_t> pixels; // 0xAABBGGRR
    };

    // A card with centered, word wrapped text on a vertical gradient. The text is made as large as fits.
    struct TextCard
    {
        std::wstring      text;
        uint32_t          width = 1280;
        uint32_t          height = 720;
        float             maxFontSize = 150.0f; // Pixels.
        float             margin = 0.08f;       // Relative to the width.
        DirectX::XMFLOAT3 textColor = { 1.0f, 1.0f, 1.0f };
        DirectX::XMFLOAT3 topColor = { 0.05f, 0.12f, 0.32f };
        DirectX::XMFLOAT3 bottomColor = { 0.02f, 0.04f, 0.12f };
        bool              border = true; // A thin light frame around the card.
    };

    Image RenderTextCard(const TextCard& card);

    // Loads a picture (jpg, png, bmp, gif, tiff...) and makes it at most maxSize pixels wide and high. The rotation
    // stored by phone cameras (EXIF orientation) is applied. Throws std::runtime_error when it cannot be read.
    Image LoadPicture(const std::filesystem::path& path, uint32_t maxSize);

    // Uploads an image as an sRGB texture with mipmaps.
    std::shared_ptr<EV::Texture> CreateTexture(EV::CommandList& commandList, const Image& image, const std::wstring& name);

    std::wstring ToWide(const std::string& utf8);

    // Saves the window's client area (as it is on screen) as a 24 bit BMP file.
    bool SaveWindowScreenshot(void* windowHandle, const std::filesystem::path& path);
}
