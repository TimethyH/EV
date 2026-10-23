#pragma once

/*
 *  Copyright(c) 2018 Jeremiah van Oosten
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files(the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions :
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 *  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 *  IN THE SOFTWARE.
 */

 /**
  *  @file GUI.h
  *  @date October 24, 2018
  *  @author Jeremiah van Oosten
  *
  *  @brief Wrapper for ImGui. This class is used internally by the Window class.
  */

#include "imgui.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>  // For HWND

#include <memory>  // For std::shared_ptr
#include <vector>  // For std::vector

#include <d3d12.h> // For D3D12_RESOURCE_STATES

namespace EV
{

    class CommandList;
    // class Device;
    class PipelineStateObject;
    class RenderTarget;
    class RootSignature;
    class ShaderResourceView;
    class Texture;

    class GUI
    {
    public:
        /**
         * Window message handler. This needs to be called by the application to allow ImGui to handle input messages.
         */
        LRESULT WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

        /**
         * Begin a new ImGui frame. Do this before calling any ImGui functions that modifies ImGui's render context.
         */
        void NewFrame();

        /**
         * Render ImgGui to the given render target.
         */
        void Render(const std::shared_ptr<CommandList>& commandList, const RenderTarget& renderTarget);

        /**
         * Destroy ImGui context.
         */
        void Destroy();

        /**
         * Set the font scaling for ImGui (this should be called when the window's DPI scaling changes.
         */
        void SetScaling(float scale);

        /**
         * Get an ImTextureID that can be passed to ImGui::Image() during the current frame.
         * The texture is kept alive until the GUI is rendered and is transitioned to a
         * pixel shader readable state before it is drawn.
         * Only plain 2D textures with a sampleable format display correctly; use the
         * editor's texture viewer for depth, array, cube and integer textures.
         */
        ImTextureID GetTextureID(const std::shared_ptr<Texture>& texture);
        ImTextureID GetTextureID(const std::shared_ptr<ShaderResourceView>& srv);

        /**
         * A large bold font for game HUDs and menus (Segoe UI Bold, 56 px at 96 DPI), or nullptr when Windows has no
         * Segoe UI Bold. Use it with ImGui::PushFont(font, font->LegacySize): the atlas is baked once, so text is
         * sharp at the size it was loaded at and smaller (larger sizes are scaled up).
         */
        ImFont* GetLargeFont() const { return m_largeFont; }

    protected:
        GUI(HWND hWnd, const RenderTarget& renderTarget);
        virtual ~GUI();

    private:
        // Device& m_Device;
        HWND                                 m_hWnd;
        ImGuiContext* m_pImGuiCtx;
        std::shared_ptr<Texture>             m_fontTexture;
        std::shared_ptr<ShaderResourceView>  m_fontSRV;
        std::shared_ptr<RootSignature>       m_rootSignature;
        std::shared_ptr<PipelineStateObject> m_pipelineState;

        // Textures referenced by ImGui::Image() this frame. ImTextureID = index + FirstFrameTextureID.
        struct FrameTexture
        {
            std::shared_ptr<Texture>            texture;
            std::shared_ptr<ShaderResourceView> srv;
        };
        std::vector<FrameTexture> m_frameTextures;

        static constexpr ImTextureID FontTextureID = 1;
        static constexpr ImTextureID FirstFrameTextureID = 2;

        // DPI scale the font atlas was rasterized at.
        float m_fontDPIScale = 1.0f;
        ImFont* m_largeFont = nullptr;
    };
}  // namespace EV