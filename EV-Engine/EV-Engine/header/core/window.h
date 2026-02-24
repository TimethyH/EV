#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <wrl.h>
#include <d3d12.h>
#include <dxgi1_5.h>

#include <core/events.h>
#include <core/clock.h>

#include "resources/texture.h"

LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

namespace EV
{

	class Game;

	class Window
	{
	public:
		static const UINT BufferCount = 3;
		HWND GetWindowHandle() const;
		void Destroy();

		const std::wstring& GetWindowName() const;

		uint32_t GetScreenWidth() const;
		uint32_t GetScreenHeight() const;

		// VSync Options
		bool IsVSync() const;
		void SetVSync(bool bVSync);
		void ToggleVSync();

		// FullScreen Options
		bool IsFullScreen() const;
		void SetFullScreen(bool bFullScreen);
		void ToggleFullScreen();

		// Hide/Show window
		void Show();
		void Hide();

		UINT GetCurrentBackBufferID() const;
		// Present the current backbuffer to the screen
		UINT Present(const std::shared_ptr<Texture>& texture = nullptr);

		// Get RTV of current backbuffer
		D3D12_CPU_DESCRIPTOR_HANDLE GetCurrentRTV() const;
		Microsoft::WRL::ComPtr<ID3D12Resource> GetCurrentBackBuffer() const;

		UpdateEvent Update;

		/**
		* Invoked when the window is resized.
		*/
		ResizeEvent Resize;

		/**
		* Window close event is fired when the window is about to be closed.
		*/
		WindowCloseEvent Close;

		/**
		* Invoked when the window is minimized.
		*/
		ResizeEvent Minimized;

		/**
		 * Invoked when the window is maximized.
		 */
		ResizeEvent Maximized;

		/**
		 * Invoked when the window is restored.
		 */
		ResizeEvent Restored;

		/**
		 * Invoked when a keyboard key is pressed while the window has focus.
		 */
		KeyboardEvent KeyPressed;

		/**
		 * Invoked when a keyboard key is released while the window has focus.
		 */
		KeyboardEvent KeyReleased;

		/**
		 * Invoked when the mouse is moved over the window.
		 */
		MouseMotionEvent MouseMoved;

		/**
		 * Invoked when the mouse enters the client area.
		 */
		MouseMotionEvent MouseEnter;

		/**
		 * Invoked when the mouse button is pressed over the window.
		 */
		MouseButtonEvent MouseButtonPressed;

		/**
		 * Invoked when the mouse button is released over the window.
		 */
		MouseButtonEvent MouseButtonReleased;

		/**
		 * Invoked when the mouse wheel is scrolled over the window.
		 */
		MouseWheelEvent MouseWheel;

		/**
		 * Invoked when the mouse cursor leaves the client area.
		 */
		Event MouseLeave;

		/**
		 * Invoked when the window gains mouse focus.
		 */
		Event MouseFocus;

		/**
		 * Invoked when the window looses mouse focus.
		 */
		Event MouseBlur;

		/**
		* The DPI scaling of the window has changed.
		*/
		DPIScaleEvent DPIScaleChanged;


		// Register a game to this window
		void RegisterCallbacks(std::shared_ptr<Game> pGame);

	protected:
		Window() = delete;
		Window(HWND hWnd, const std::wstring& windowName, uint32_t windowWidth, uint32_t windowHeight, bool bVSync);
		virtual ~Window();

		// Declare friend function/classes to gain access to private/protected data
		friend LRESULT CALLBACK ::WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
		friend class Game;
		friend class Application;


		// Update and Draw are only called by he application
		virtual void OnUpdate(UpdateEventArgs& e);
		virtual void OnRender(RenderEventArgs& e);

		virtual void OnKeyPress(KeyEventArgs& e);
		virtual void OnKeyRelease(KeyEventArgs& e);

		virtual void OnMouseMove(MouseMotionEventArgs& e);
		virtual void OnMouseButtonPress(MouseButtonEventArgs& e);
		virtual void OnMouseButtonRelease(MouseButtonEventArgs& e);
		virtual void OnMouseWheel(MouseWheelEventArgs& e);
		void OnDPIScaleChanged(DPIScaleEventArgs& e);

		virtual void OnResize(ResizeEventArgs& e);
		void OnClose(WindowCloseEventArgs& e);

		// Swapchain Creation
		Microsoft::WRL::ComPtr<IDXGISwapChain4> CreateSwapChain();

		void UpdateRenderTargetViews();

	private:
		// Prevent windows being copied
		Window(const Window& copy) = delete;
		Window& operator=(const Window& other) = delete;

		HWND m_hWnd = {};

		std::wstring m_windowName = {};

		uint32_t m_screenWidth = {};
		uint32_t m_screenHeight = {};
		bool m_VSync = {};
		bool m_fullScreen = {};

		UINT64 m_fenceValues[BufferCount];
		uint64_t m_frameValues[BufferCount];

		HighResolutionClock m_updateClock = {};
		HighResolutionClock m_renderClock = {};
		uint64_t m_frameCounter = {};

		std::weak_ptr<Game> m_pGame;

		Microsoft::WRL::ComPtr<IDXGISwapChain4> m_dxgiSwapChain;
		std::shared_ptr<Texture> m_backBufferTextures[BufferCount];

		UINT m_RTVDescriptorSize;
		UINT m_currentBackBufferIndex;

		RECT m_windowRect;
		bool m_isTearingSupported;

		int m_previousMouseX;
		int m_previousMouseY;

		float m_DPIScaling;
	};

}