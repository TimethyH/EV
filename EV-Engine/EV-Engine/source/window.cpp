#include "window.h"
#include "game.h"
#include "dx12_includes.h"
#include "application.h"
#include "command_list.h"
#include "command_queue.h"
#include "resource_state_tracker.h"
#include "Texture.h"


bool Window::Initialize()
{
	// m_Window = window;
	// m_pImGuiCtx = ImGui::CreateContext();
	// ImGui::SetCurrentContext(m_pImGuiCtx);
	// if (!m_Window || !ImGui_ImplWin32_Init(m_Window->GetWindowHandle()))
	// {
		// return false;
	// }

	// ImGuiIO& io = ImGui::GetIO();

	// Build texture atlas
	
	//
	// unsigned char* pixelData = nullptr;
	// int width, height;
	// io.Fonts->GetTexDataAsRGBA32(&pixelData, &width, &height);
	//
	// auto device = Application::Get().GetDevice();
	// auto commandQueue = Application::Get().GetCommandQueue(D3D12_COMMAND_LIST_TYPE_COPY);
	// auto commandList = commandQueue->GetCommandList();
	//
	// auto fontTextureDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, width, height);
	//
	// m_FontTexture = std::make_unique<Texture>(fontTextureDesc);
	// m_FontTexture->SetName(L"ImGui Font Texture");
	//
	// size_t rowPitch, slicePitch;
	// GetSurfaceInfo(width, height, DXGI_FORMAT_R8G8B8A8_UNORM, &slicePitch, &rowPitch, nullptr);
	//
	// D3D12_SUBRESOURCE_DATA subresourceData;
	// subresourceData.pData = pixelData;
	// subresourceData.RowPitch = rowPitch;
	// subresourceData.SlicePitch = slicePitch;
	//
	// commandList->CopyTextureSubresource(*m_FontTexture, 0, 1, &subresourceData);
	// commandList->GenerateMips(*m_FontTexture);
	//
	// commandQueue->ExecuteCommandList(commandList);
	//
	// // Create the root signature for the ImGUI shaders.
	// D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};
	// featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
	// if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
	// {
	// 	featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
	// }
	// // Allow input layout and deny unnecessary access to certain pipeline stages.
	// D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags =
	// 	D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
	// 	D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
	// 	D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
	// 	D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;
	//
	// CD3DX12_DESCRIPTOR_RANGE1 descriptorRage(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
	//
	// CD3DX12_ROOT_PARAMETER1 rootParameters[RootParameters::NumRootParameters];
	// rootParameters[RootParameters::MatrixCB].InitAsConstants(sizeof(DirectX::XMMATRIX) / 4, 0, D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY_VERTEX);
	// rootParameters[RootParameters::FontTexture].InitAsDescriptorTable(1, &descriptorRage, D3D12_SHADER_VISIBILITY_PIXEL);
	//
	// CD3DX12_STATIC_SAMPLER_DESC linearRepeatSampler(0, D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR);
	// linearRepeatSampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
	// linearRepeatSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	//
	// CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDescription;
	// rootSignatureDescription.Init_1_1(RootParameters::NumRootParameters, rootParameters, 1, &linearRepeatSampler, rootSignatureFlags);
	//
	// m_RootSignature = std::make_unique<RootSignature>(rootSignatureDescription.Desc_1_1, featureData.HighestVersion);
	//
	// const D3D12_INPUT_ELEMENT_DESC inputLayout[] =
	// {
	// 	{ "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,   0, offsetof(ImDrawVert, pos), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	// 	{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,   0, offsetof(ImDrawVert, uv),  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	// 	{ "COLOR",    0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, offsetof(ImDrawVert, col), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	// };
	//
	// D3D12_RT_FORMAT_ARRAY rtvFormats = {};
	// rtvFormats.NumRenderTargets = 1;
	// rtvFormats.RTFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	//
	// D3D12_BLEND_DESC blendDesc = {};
	// blendDesc.RenderTarget[0].BlendEnable = true;
	// blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
	// blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
	// blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	// blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
	// blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
	// blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
	// blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	//
	// D3D12_RASTERIZER_DESC rasterizerDesc = {};
	// rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;
	// rasterizerDesc.CullMode = D3D12_CULL_MODE_NONE;
	// rasterizerDesc.FrontCounterClockwise = FALSE;
	// rasterizerDesc.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
	// rasterizerDesc.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
	// rasterizerDesc.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
	// rasterizerDesc.DepthClipEnable = true;
	// rasterizerDesc.MultisampleEnable = FALSE;
	// rasterizerDesc.AntialiasedLineEnable = FALSE;
	// rasterizerDesc.ForcedSampleCount = 0;
	// rasterizerDesc.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
	//
	// D3D12_DEPTH_STENCIL_DESC depthStencilDesc = {};
	// depthStencilDesc.DepthEnable = false;
	// depthStencilDesc.StencilEnable = false;
	//
	// // Setup the pipeline state.
	// struct PipelineStateStream
	// {
	// 	CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE pRootSignature;
	// 	CD3DX12_PIPELINE_STATE_STREAM_INPUT_LAYOUT InputLayout;
	// 	CD3DX12_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY PrimitiveTopologyType;
	// 	CD3DX12_PIPELINE_STATE_STREAM_VS VS;
	// 	CD3DX12_PIPELINE_STATE_STREAM_PS PS;
	// 	CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS RTVFormats;
	// 	CD3DX12_PIPELINE_STATE_STREAM_SAMPLE_DESC SampleDesc;
	// 	CD3DX12_PIPELINE_STATE_STREAM_BLEND_DESC BlendDesc;
	// 	CD3DX12_PIPELINE_STATE_STREAM_RASTERIZER RasterizerState;
	// 	CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL DepthStencilState;
	// } pipelineStateStream;
	//
	// pipelineStateStream.pRootSignature = m_RootSignature->GetRootSignature().Get();
	// pipelineStateStream.InputLayout = { inputLayout, 3 };
	// pipelineStateStream.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	// pipelineStateStream.VS = { g_ImGUI_VS, sizeof(g_ImGUI_VS) };
	// pipelineStateStream.PS = { g_ImGUI_PS, sizeof(g_ImGUI_PS) };
	// pipelineStateStream.RTVFormats = rtvFormats;
	// pipelineStateStream.SampleDesc = { 1, 0 };
	// pipelineStateStream.BlendDesc = CD3DX12_BLEND_DESC(blendDesc);
	// pipelineStateStream.RasterizerState = CD3DX12_RASTERIZER_DESC(rasterizerDesc);
	// pipelineStateStream.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(depthStencilDesc);
	//
	// D3D12_PIPELINE_STATE_STREAM_DESC pipelineStateStreamDesc = {
	// 	sizeof(PipelineStateStream), &pipelineStateStream
	// };
	// ThrowIfFailed(device->CreatePipelineState(&pipelineStateStreamDesc, IID_PPV_ARGS(&m_PipelineState)));

	return true;
}

Window::Window(HWND hWnd, const std::wstring& windowName, uint32_t windowWidth, uint32_t windowHeight, bool bVSync)
	:m_hWnd(hWnd)
	,m_windowName(windowName)
	,m_screenWidth(windowWidth)
	,m_screenHeight(windowHeight)
	,m_VSync(bVSync)
{
	// Application& app = Application::Get();
	//
	// m_isTearingSupported = app.TearingSupported();
	//
	// for (int i = 0; i < BufferCount; ++i)
	// {
	// 	m_backBufferTextures[i]->SetName(L"Backbuffer[" + std::to_wstring(i) + L"]");
	// }
	//
	// m_dxgiSwapChain = CreateSwapChain();
	// UpdateRenderTargetViews();
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

void Window::OnResize(ResizeEventArgs& e)
{
	if (m_screenWidth != e.windowWidth || m_screenHeight!= e.windowHeight)
	{
		m_screenWidth = std::max(1, e.windowWidth);
		m_screenHeight = std::max(1, e.windowHeight);

		Application::Get().Flush();

		for (int i = 0; i < BufferCount; ++i)
		{
			m_backBufferTextures[i]->Reset();
		}

		DXGI_SWAP_CHAIN_DESC desc = {};
		ThrowIfFailed(m_dxgiSwapChain->GetDesc(&desc));
		ThrowIfFailed(m_dxgiSwapChain->ResizeBuffers(BufferCount, m_screenWidth, m_screenHeight, desc.BufferDesc.Format, desc.Flags));

		m_currentBackBufferIndex= m_dxgiSwapChain->GetCurrentBackBufferIndex();
		UpdateRenderTargetViews();
	}

	if (auto pGame = m_pGame.lock())
	{
		pGame->OnResize(e);
	}
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

