#include "core/window.h"
#include "core/game.h"
#include "DX12/dx12_includes.h"
#include "core/application.h"
#include "DX12/command_list.h"
#include "DX12/command_queue.h"
#include "DX12/resource_state_tracker.h"
#include "resources/texture.h"
#include "utility/helpers.h"


using namespace EV;

Window::Window(HWND hWnd, const std::wstring& windowName, uint32_t windowWidth, uint32_t windowHeight, bool bVSync)
	:m_hWnd(hWnd)
	,m_windowName(windowName)
	,m_screenWidth(windowWidth)
	,m_screenHeight(windowHeight)
	,m_VSync(bVSync)
{
}

Window::~Window()
{
	// Window should be destroyed with Application::DestroyWindow before
	// the window goes out of scope.
	assert(!m_hWnd && "Use Application::DestroyWindow before destruction.");
}

HWND Window::GetWindowHandle() const
{
	return m_hWnd;
}

const std::wstring& Window::GetWindowName() const
{
	return m_windowName;
}

void Window::Show()
{
	::ShowWindow(m_hWnd, SW_SHOW);
}

void Window::Hide()
{
	::ShowWindow(m_hWnd, SW_HIDE);
}

void Window::Destroy()
{
	if (auto pGame = m_pGame.lock())
	{
		pGame->OnWindowDestroy();
	}
	if (m_hWnd)
	{
		DestroyWindow(m_hWnd);
		m_hWnd = nullptr;
	}
}

uint32_t Window::GetScreenWidth() const
{
	return m_screenWidth;
}

uint32_t Window::GetScreenHeight() const
{
	return m_screenHeight;
}

bool Window::IsVSync() const
{
	return m_VSync;
}

void Window::SetVSync(bool bVSync)
{
	m_VSync = bVSync;
}

void Window::ToggleVSync()
{
	SetVSync(!m_VSync);
}

bool Window::IsFullScreen() const
{
	return m_fullScreen;
}

void Window::SetFullScreen(bool bFullScreen)
{
	if (m_fullScreen != bFullScreen)
	{
		m_fullScreen = bFullScreen;
		if (m_fullScreen)
		{
			::GetWindowRect(m_hWnd, &m_windowRect);
			UINT windowStyle = WS_OVERLAPPEDWINDOW & ~(WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX);

			::SetWindowLongW(m_hWnd, GWL_STYLE, windowStyle);

			HMONITOR hMonitor = ::MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST);
			MONITORINFOEX monitorInfo = {};
			monitorInfo.cbSize = sizeof(MONITORINFOEX);
			::GetMonitorInfo(hMonitor, &monitorInfo);

			::SetWindowPos(m_hWnd, HWND_TOP,
				monitorInfo.rcMonitor.left,
				monitorInfo.rcMonitor.top,
				monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left,
				monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top,
				SWP_FRAMECHANGED | SWP_NOACTIVATE);

			::ShowWindow(m_hWnd, SW_MAXIMIZE);
		}
		else
		{
			// Restore all the window decorators.
			::SetWindowLong(m_hWnd, GWL_STYLE, WS_OVERLAPPEDWINDOW);

			::SetWindowPos(m_hWnd, HWND_NOTOPMOST,
				m_windowRect.left,
				m_windowRect.top,
				m_windowRect.right - m_windowRect.left,
				m_windowRect.bottom - m_windowRect.top,
				SWP_FRAMECHANGED | SWP_NOACTIVATE);

			::ShowWindow(m_hWnd, SW_NORMAL);
		
		}
	}
}

void Window::ToggleFullScreen()
{
	SetFullScreen(!m_fullScreen);
}

void Window::RegisterCallbacks(std::shared_ptr<Game> pGame)
{
	m_pGame = pGame;

	return;
}

void Window::OnUpdate(UpdateEventArgs& e)
{
	m_updateClock.Tick();

	if (auto pGame = m_pGame.lock())
	{
		m_frameCounter++;

		UpdateEventArgs updateEventArgs(m_updateClock.GetDeltaSeconds(), m_updateClock.GetTotalSeconds());
		pGame->OnUpdate(updateEventArgs);
	}
}

void Window::OnRender(RenderEventArgs&)
{
	m_renderClock.Tick();

	if (auto pGame = m_pGame.lock())
	{
		RenderEventArgs renderEventArgs(m_renderClock.GetDeltaSeconds(), m_renderClock.GetTotalSeconds());
		pGame->OnRender();
	}
}

void Window::OnKeyPress(KeyEventArgs& e)
{
	if (auto pGame = m_pGame.lock())
	{
		pGame->OnKeyPress(e);
	}
}

void Window::OnKeyRelease(KeyEventArgs& e)
{
	if (auto pGame = m_pGame.lock())
	{
		pGame->OnKeyRelease(e);
	}
}

void Window::OnMouseMove(MouseMotionEventArgs& e)
{

	e.deltaX = e.xPosition - m_previousMouseX;
	e.deltaY = e.yPosition - m_previousMouseY;

	m_previousMouseX = e.xPosition;
	m_previousMouseY = e.yPosition;

	if (auto pGame = m_pGame.lock())
	{
		pGame->OnMouseMove(e);
	}
}

void Window::OnMouseButtonPress(MouseButtonEventArgs& e)
{
	if (auto pGame = m_pGame.lock())
	{
		pGame->OnMouseButtonPress(e);
	}
}

void Window::OnMouseButtonRelease(MouseButtonEventArgs& e)
{
	if (auto pGame = m_pGame.lock())
	{
		pGame->OnMouseButtonRelease(e);
	}
}

void Window::OnMouseWheel(MouseWheelEventArgs& e)
{
	if (auto pGame = m_pGame.lock())
	{
		pGame->OnMouseWheel(e);
	}
}


// The DPI scaling of the window has changed.
void Window::OnDPIScaleChanged(DPIScaleEventArgs& e)
{
	m_DPIScaling = e.DPIScale;
	DPIScaleChanged(e);
}


void Window::OnResize(ResizeEventArgs& e)
{
	// if (m_screenWidth != e.windowWidth || m_screenHeight!= e.windowHeight)
	// {
	// 	m_screenWidth = std::max(1, e.windowWidth);
	// 	m_screenHeight = std::max(1, e.windowHeight);
	//
	// 	Application::Get().Flush();
	//
	// 	for (int i = 0; i < BufferCount; ++i)
	// 	{
	// 		m_backBufferTextures[i]->Reset();
	// 	}
	//
	// 	DXGI_SWAP_CHAIN_DESC desc = {};
	// 	ThrowIfFailed(m_dxgiSwapChain->GetDesc(&desc));
	// 	ThrowIfFailed(m_dxgiSwapChain->ResizeBuffers(BufferCount, m_screenWidth, m_screenHeight, desc.BufferDesc.Format, desc.Flags));
	//
	// 	m_currentBackBufferIndex= m_dxgiSwapChain->GetCurrentBackBufferIndex();
	// 	UpdateRenderTargetViews();
	// }
	//
	// if (auto pGame = m_pGame.lock())
	// {
	// 	pGame->OnResize(e);
	// }
}

void Window::OnClose(WindowCloseEventArgs& e)
{
	Close(e);
}

Microsoft::WRL::ComPtr<IDXGISwapChain4> Window::CreateSwapChain()
{
	Application& app = Application::Get();

	ComPtr<IDXGISwapChain4> dxgiSwapChain4;
	ComPtr<IDXGIFactory4> dxgiFactory4;
	UINT createFactoryFlags = 0;
#if defined(_DEBUG)
	createFactoryFlags = DXGI_CREATE_FACTORY_DEBUG;
#endif

	ThrowIfFailed(CreateDXGIFactory2(createFactoryFlags, IID_PPV_ARGS(&dxgiFactory4)));

	DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
	swapChainDesc.Width = m_screenWidth;
	swapChainDesc.Height = m_screenHeight;
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.Stereo = FALSE;
	swapChainDesc.SampleDesc = { 1, 0 };
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.BufferCount = BufferCount;
	swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
	// It is recommended to always allow tearing if tearing support is available.
	swapChainDesc.Flags = m_isTearingSupported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
	ID3D12CommandQueue* pCommandQueue = app.GetCommandQueue().GetCommandQueue().Get();

	ComPtr<IDXGISwapChain1> swapChain1;
	ThrowIfFailed(dxgiFactory4->CreateSwapChainForHwnd(
		pCommandQueue,
		m_hWnd,
		&swapChainDesc,
		nullptr,
		nullptr,
		&swapChain1));

	// Disable the Alt+Enter fullscreen toggle feature. Switching to fullscreen
	// will be handled manually.
	ThrowIfFailed(dxgiFactory4->MakeWindowAssociation(m_hWnd, DXGI_MWA_NO_ALT_ENTER));

	ThrowIfFailed(swapChain1.As(&dxgiSwapChain4));

	m_currentBackBufferIndex = dxgiSwapChain4->GetCurrentBackBufferIndex();

	return dxgiSwapChain4;
}

// Update the render target views for the swapchain back buffers.
void Window::UpdateRenderTargetViews()
{

	for (int i = 0; i < BufferCount; ++i)
	{
		ComPtr<ID3D12Resource> backBuffer;
		ThrowIfFailed(m_dxgiSwapChain->GetBuffer(i, IID_PPV_ARGS(&backBuffer)));

		ResourceStateTracker::AddGlobalResourceState(backBuffer.Get(), D3D12_RESOURCE_STATE_COMMON);

		// m_backBufferTextures[i].SetD3D12Resource(backBuffer);
		// m_backBufferTextures[i].CreateViews();
		m_backBufferTextures[i] = Application::Get().CreateTexture(backBuffer);
		m_backBufferTextures[i]->SetName(L"Backbuffer[" + std::to_wstring(i) + L"]");
	}
}

// D3D12_CPU_DESCRIPTOR_HANDLE Window::GetCurrentRTV() const
// {
// 	return CD3DX12_CPU_DESCRIPTOR_HANDLE(m_d3d12RTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
// 		m_currentBackBufferIndex, m_RTVDescriptorSize);
// }
//
//
// Microsoft::WRL::ComPtr<ID3D12Resource> Window::GetCurrentBackBuffer() const
// {
// 	return m_d3d12BackBuffers[m_currentBackBufferIndex];
// }

UINT Window::GetCurrentBackBufferID() const
{
	return m_currentBackBufferIndex;
}

UINT Window::Present(const std::shared_ptr<Texture>& texture)
{
	auto& commandQueue = Application::Get().GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT);
	auto commandList = commandQueue.GetCommandList();

	auto& backBuffer = m_backBufferTextures[m_currentBackBufferIndex];

	if (texture->IsValid())
	{
		if (texture->GetD3D12ResourceDesc().SampleDesc.Count > 1)
		{
			commandList->ResolveSubresource(backBuffer, texture);
		}
		else
		{
			commandList->CopyResource(backBuffer, texture);
		}
	}

	// RenderTarget renderTarget;
	// renderTarget.AttachTexture(AttachmentPoint::Color0, backBuffer);
	//
	// // m_GUI.Render(commandList, renderTarget);
	//
	// commandList->TransitionBarrier(backBuffer, D3D12_RESOURCE_STATE_PRESENT);
	// commandQueue->ExecuteCommandList(commandList);
	//
	// UINT syncInterval = m_VSync ? 1 : 0;
	// UINT presentFlags = m_isTearingSupported && !m_VSync ? DXGI_PRESENT_ALLOW_TEARING : 0;
	// ThrowIfFailed(m_dxgiSwapChain->Present(syncInterval, presentFlags));
	//
	// m_fenceValues[m_currentBackBufferIndex] = commandQueue->Signal();
	// m_frameValues[m_currentBackBufferIndex] = Application::GetFrameCount();
	//
	// m_currentBackBufferIndex = m_dxgiSwapChain->GetCurrentBackBufferIndex();
	//
	// commandQueue->WaitForFenceValue(m_fenceValues[m_currentBackBufferIndex]);
	//
	// Application::Get().ReleaseStaleDescriptors(m_frameValues[m_currentBackBufferIndex]);
	//
	// return m_currentBackBufferIndex;

	commandList->TransitionBarrier(backBuffer, D3D12_RESOURCE_STATE_PRESENT);
	commandQueue.ExecuteCommandList(commandList);

	UINT syncInterval = m_VSync ? 1 : 0;
	UINT presentFlags = m_isTearingSupported && !m_VSync ? DXGI_PRESENT_ALLOW_TEARING : 0;
	ThrowIfFailed(m_dxgiSwapChain->Present(syncInterval, presentFlags));

	m_fenceValues[m_currentBackBufferIndex] = commandQueue.Signal();

	m_currentBackBufferIndex = m_dxgiSwapChain->GetCurrentBackBufferIndex();

	auto fenceValue = m_fenceValues[m_currentBackBufferIndex];
	commandQueue.WaitForFenceValue(fenceValue);

	Application::Get().ReleaseStaleDescriptors();

	return m_currentBackBufferIndex;

}

