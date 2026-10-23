#include "core/EV.h"

#include "textures.h"

#include <objbase.h>
#include <wincodec.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>

namespace Birthday
{
    namespace
    {
        uint8_t ToByte(float value)
        {
            return static_cast<uint8_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
        }

        // GDI pixels are 0x00RRGGBB.
        uint32_t ToGDIColor(const DirectX::XMFLOAT3& color)
        {
            return (ToByte(color.x) << 16) | (ToByte(color.y) << 8) | ToByte(color.z);
        }

        HFONT CreateCardFont(float size)
        {
            return CreateFontW(-static_cast<int>(size), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS,
                CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        }

        // EXIF orientation (1-8): where the pixel shown at (x, y) is stored in the file's image.
        void SourcePixel(uint32_t orientation, uint32_t x, uint32_t y, uint32_t sourceWidth, uint32_t sourceHeight,
            uint32_t& sourceX, uint32_t& sourceY)
        {
            switch (orientation)
            {
            case 2: sourceX = sourceWidth - 1 - x; sourceY = y; break;                         // Mirrored.
            case 3: sourceX = sourceWidth - 1 - x; sourceY = sourceHeight - 1 - y; break;      // Rotated 180.
            case 4: sourceX = x; sourceY = sourceHeight - 1 - y; break;                        // Mirrored vertically.
            case 5: sourceX = y; sourceY = x; break;                                           // Transposed.
            case 6: sourceX = y; sourceY = sourceHeight - 1 - x; break;                        // Turn 90 clockwise.
            case 7: sourceX = sourceWidth - 1 - y; sourceY = sourceHeight - 1 - x; break;      // Transversed.
            case 8: sourceX = sourceWidth - 1 - y; sourceY = x; break;                         // Turn 90 counterclockwise.
            default: sourceX = x; sourceY = y; break;
            }
        }
    }

    std::wstring ToWide(const std::string& utf8)
    {
        if (utf8.empty())
        {
            return {};
        }
        const int size = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
        std::wstring wide(size, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(), size);
        return wide;
    }

    Image RenderTextCard(const TextCard& card)
    {
        const int width = static_cast<int>(card.width);
        const int height = static_cast<int>(card.height);

        HDC screen = GetDC(nullptr);
        HDC dc = CreateCompatibleDC(screen);
        ReleaseDC(nullptr, screen);

        BITMAPINFO info = {};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height; // Top-down rows.
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!bitmap || !bits)
        {
            DeleteDC(dc);
            throw std::runtime_error("Could not create a bitmap for a text card");
        }
        HGDIOBJ previousBitmap = SelectObject(dc, bitmap);
        uint32_t* pixels = static_cast<uint32_t*>(bits);

        // Background: a vertical gradient and a thin frame.
        for (int y = 0; y < height; ++y)
        {
            const float t = height > 1 ? static_cast<float>(y) / (height - 1) : 0.0f;
            const DirectX::XMFLOAT3 color(
                card.topColor.x + (card.bottomColor.x - card.topColor.x) * t,
                card.topColor.y + (card.bottomColor.y - card.topColor.y) * t,
                card.topColor.z + (card.bottomColor.z - card.topColor.z) * t);
            std::fill(pixels + y * width, pixels + (y + 1) * width, ToGDIColor(color));
        }
        if (card.border)
        {
            const int inset = height / 30;
            const int thickness = std::max(2, height / 180);
            const uint32_t frame = ToGDIColor(DirectX::XMFLOAT3(
                card.textColor.x * 0.5f + card.topColor.x * 0.5f,
                card.textColor.y * 0.5f + card.topColor.y * 0.5f,
                card.textColor.z * 0.5f + card.topColor.z * 0.5f));
            for (int y = inset; y < height - inset; ++y)
            {
                for (int x = inset; x < width - inset; ++x)
                {
                    const bool edge = y < inset + thickness || y >= height - inset - thickness ||
                        x < inset + thickness || x >= width - inset - thickness;
                    if (edge)
                    {
                        pixels[y * width + x] = frame;
                    }
                }
            }
        }
        GdiFlush();

        // The largest font size the text fits at.
        const int margin = static_cast<int>(card.margin * width);
        const RECT area = { margin, margin, width - margin, height - margin };
        const std::wstring& text = card.text;
        const UINT format = DT_CENTER | DT_WORDBREAK | DT_NOPREFIX;

        HFONT font = nullptr;
        RECT measured = area;
        float fontSize = card.maxFontSize;
        for (; fontSize >= 12.0f; fontSize *= 0.92f)
        {
            font = CreateCardFont(fontSize);
            HGDIOBJ previousFont = SelectObject(dc, font);
            measured = area;
            DrawTextW(dc, text.c_str(), -1, &measured, format | DT_CALCRECT);
            SelectObject(dc, previousFont);
            if (measured.right - measured.left <= area.right - area.left && measured.bottom - measured.top <= area.bottom - area.top)
            {
                break;
            }
            DeleteObject(font);
            font = nullptr;
        }
        if (!font)
        {
            fontSize = 12.0f;
            font = CreateCardFont(fontSize);
            HGDIOBJ previousFont = SelectObject(dc, font);
            measured = area;
            DrawTextW(dc, text.c_str(), -1, &measured, format | DT_CALCRECT);
            SelectObject(dc, previousFont);
        }

        // Centered vertically, with a soft shadow.
        HGDIOBJ previousFont = SelectObject(dc, font);
        SetBkMode(dc, TRANSPARENT);
        const int textHeight = measured.bottom - measured.top;
        RECT target = { area.left, (height - textHeight) / 2, area.right, (height - textHeight) / 2 + textHeight };
        const int shadowOffset = std::max(2, static_cast<int>(fontSize * 0.05f));
        RECT shadow = target;
        OffsetRect(&shadow, shadowOffset, shadowOffset);
        SetTextColor(dc, RGB(0, 0, 0));
        DrawTextW(dc, text.c_str(), -1, &shadow, format);
        SetTextColor(dc, RGB(ToByte(card.textColor.x), ToByte(card.textColor.y), ToByte(card.textColor.z)));
        DrawTextW(dc, text.c_str(), -1, &target, format);
        GdiFlush();

        Image image;
        image.width = card.width;
        image.height = card.height;
        image.pixels.resize(static_cast<size_t>(width) * height);
        for (size_t i = 0; i < image.pixels.size(); ++i)
        {
            // 0x00RRGGBB -> 0xFFBBGGRR.
            const uint32_t value = pixels[i];
            image.pixels[i] = 0xFF000000u | ((value & 0xFFu) << 16) | (value & 0xFF00u) | ((value >> 16) & 0xFFu);
        }

        SelectObject(dc, previousFont);
        SelectObject(dc, previousBitmap);
        DeleteObject(font);
        DeleteObject(bitmap);
        DeleteDC(dc);
        return image;
    }

    ShapeMask RenderLetterMask(wchar_t letter, uint32_t pixelHeight, uint32_t margin)
    {
        // Capitals are about 0.7 of the font size high. The bitmap has room for any letter.
        const int fontSize = static_cast<int>(pixelHeight * 1.4f);
        const int size = fontSize * 2;

        HDC screen = GetDC(nullptr);
        HDC dc = CreateCompatibleDC(screen);
        ReleaseDC(nullptr, screen);
        BITMAPINFO info = {};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = size;
        info.bmiHeader.biHeight = -size; // Top-down rows.
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!bitmap || !bits)
        {
            DeleteDC(dc);
            throw std::runtime_error("Could not create a bitmap for a letter");
        }
        HGDIOBJ previousBitmap = SelectObject(dc, bitmap);
        const uint32_t* pixels = static_cast<const uint32_t*>(bits);
        std::fill(static_cast<uint32_t*>(bits), static_cast<uint32_t*>(bits) + size * size, 0u);

        HFONT font = CreateFontW(-fontSize, 0, 0, 0, FW_BLACK, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS,
            CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        HGDIOBJ previousFont = SelectObject(dc, font);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(255, 255, 255));
        TextOutW(dc, fontSize / 2, fontSize / 4, &letter, 1);
        GdiFlush();

        // Crop to the ink.
        int minX = size, minY = size, maxX = -1, maxY = -1;
        for (int y = 0; y < size; ++y)
        {
            for (int x = 0; x < size; ++x)
            {
                if ((pixels[y * size + x] & 0xFFu) >= 128u)
                {
                    minX = std::min(minX, x);
                    maxX = std::max(maxX, x);
                    minY = std::min(minY, y);
                    maxY = std::max(maxY, y);
                }
            }
        }
        ShapeMask mask;
        if (maxX >= minX)
        {
            mask.width = static_cast<uint32_t>(maxX - minX + 1) + margin * 2;
            mask.height = static_cast<uint32_t>(maxY - minY + 1) + margin * 2;
            mask.inside.assign(static_cast<size_t>(mask.width) * mask.height, 0);
            for (int y = minY; y <= maxY; ++y)
            {
                for (int x = minX; x <= maxX; ++x)
                {
                    mask.inside[(y - minY + margin) * mask.width + (x - minX + margin)] = (pixels[y * size + x] & 0xFFu) >= 128u;
                }
            }
        }

        SelectObject(dc, previousFont);
        SelectObject(dc, previousBitmap);
        DeleteObject(font);
        DeleteObject(bitmap);
        DeleteDC(dc);
        return mask;
    }

    Image LoadPicture(const std::filesystem::path& path, uint32_t maxSize)
    {
        // WIC needs COM on the calling thread (pictures are decoded on worker threads).
        const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

        uint16_t orientation = 1;
        DirectX::TexMetadata metadata;
        DirectX::ScratchImage loaded;
        HRESULT result = DirectX::LoadFromWICFile(path.c_str(), DirectX::WIC_FLAGS_FORCE_RGB | DirectX::WIC_FLAGS_IGNORE_SRGB,
            &metadata, loaded, [&orientation](IWICMetadataQueryReader* reader)
            {
                PROPVARIANT value;
                PropVariantInit(&value);
                if (SUCCEEDED(reader->GetMetadataByName(L"/app1/ifd/{ushort=274}", &value)) && value.vt == VT_UI2)
                {
                    orientation = value.uiVal;
                }
                PropVariantClear(&value);
            });

        DirectX::ScratchImage converted;
        if (SUCCEEDED(result) && metadata.format != DXGI_FORMAT_R8G8B8A8_UNORM)
        {
            result = DirectX::Convert(*loaded.GetImage(0, 0, 0), DXGI_FORMAT_R8G8B8A8_UNORM, DirectX::TEX_FILTER_DEFAULT,
                DirectX::TEX_THRESHOLD_DEFAULT, converted);
        }
        else if (SUCCEEDED(result))
        {
            converted = std::move(loaded);
        }

        DirectX::ScratchImage resized;
        if (SUCCEEDED(result))
        {
            const DirectX::Image& source = *converted.GetImage(0, 0, 0);
            const float scale = std::min(1.0f, static_cast<float>(maxSize) / std::max(source.width, source.height));
            if (scale < 1.0f)
            {
                const size_t width = std::max<size_t>(1, static_cast<size_t>(std::lround(source.width * scale)));
                const size_t height = std::max<size_t>(1, static_cast<size_t>(std::lround(source.height * scale)));
                result = DirectX::Resize(source, width, height, DirectX::TEX_FILTER_DEFAULT, resized);
            }
            else
            {
                resized = std::move(converted);
            }
        }

        if (SUCCEEDED(com))
        {
            CoUninitialize();
        }
        if (FAILED(result))
        {
            throw std::runtime_error("Could not read " + path.string());
        }

        const DirectX::Image& source = *resized.GetImage(0, 0, 0);
        const uint32_t sourceWidth = static_cast<uint32_t>(source.width);
        const uint32_t sourceHeight = static_cast<uint32_t>(source.height);
        const bool swapAxes = orientation >= 5 && orientation <= 8;

        Image image;
        image.width = swapAxes ? sourceHeight : sourceWidth;
        image.height = swapAxes ? sourceWidth : sourceHeight;
        image.pixels.resize(static_cast<size_t>(image.width) * image.height);
        for (uint32_t y = 0; y < image.height; ++y)
        {
            for (uint32_t x = 0; x < image.width; ++x)
            {
                uint32_t sourceX, sourceY;
                SourcePixel(orientation, x, y, sourceWidth, sourceHeight, sourceX, sourceY);
                const uint8_t* pixel = source.pixels + sourceY * source.rowPitch + sourceX * 4;
                image.pixels[static_cast<size_t>(y) * image.width + x] =
                    0xFF000000u | (static_cast<uint32_t>(pixel[2]) << 16) | (static_cast<uint32_t>(pixel[1]) << 8) | pixel[0];
            }
        }
        return image;
    }

    std::shared_ptr<EV::Texture> CreateTexture(EV::CommandList& commandList, const Image& image, const std::wstring& name)
    {
        // Mip levels 0: the whole chain, generated after the first level is copied.
        const auto desc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, image.width, image.height, 1, 0);
        auto texture = EV::Application::Get().CreateTexture(desc);
        texture->SetName(name);

        D3D12_SUBRESOURCE_DATA subresource = {};
        subresource.pData = image.pixels.data();
        subresource.RowPitch = static_cast<LONG_PTR>(image.width) * 4;
        subresource.SlicePitch = subresource.RowPitch * image.height;
        commandList.CopyTextureSubresource(texture, 0, 1, &subresource);
        commandList.GenerateMips(texture);
        return texture;
    }

    bool SaveWindowScreenshot(void* windowHandle, const std::filesystem::path& path)
    {
        HWND window = static_cast<HWND>(windowHandle);
        RECT client;
        if (!GetClientRect(window, &client))
        {
            return false;
        }
        POINT origin = { 0, 0 };
        ClientToScreen(window, &origin);
        const int width = client.right - client.left;
        const int height = client.bottom - client.top;
        if (width <= 0 || height <= 0)
        {
            return false;
        }

        HDC screen = GetDC(nullptr);
        HDC dc = CreateCompatibleDC(screen);
        BITMAPINFO info = {};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = height; // Bottom-up, as BMP files store it.
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 24;
        info.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
        HGDIOBJ previous = SelectObject(dc, bitmap);
        BitBlt(dc, 0, 0, width, height, screen, origin.x, origin.y, SRCCOPY | CAPTUREBLT);
        GdiFlush();

        const DWORD rowSize = ((width * 3 + 3) / 4) * 4;
        const DWORD imageSize = rowSize * height;
        BITMAPFILEHEADER header = {};
        header.bfType = 0x4D42; // "BM"
        header.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
        header.bfSize = header.bfOffBits + imageSize;
        info.bmiHeader.biSizeImage = imageSize;

        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(&header), sizeof(header));
        file.write(reinterpret_cast<const char*>(&info.bmiHeader), sizeof(BITMAPINFOHEADER));
        file.write(static_cast<const char*>(bits), imageSize);
        const bool written = static_cast<bool>(file);

        SelectObject(dc, previous);
        DeleteObject(bitmap);
        DeleteDC(dc);
        ReleaseDC(nullptr, screen);
        return written;
    }
}
