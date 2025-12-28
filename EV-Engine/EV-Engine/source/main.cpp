/*
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <shellapi.h> // For CommandLineToArgvW

// Windows Runtime Library, needed for Microsoft::WRD::ComPtr<> template class
// All DX12 objects are COM objects, the ComPtr tracks their lifetimes
#include <wrl.h>
using namespace Microsoft::WRL;

// DX12 specific headers

#include <d3d12.h>
#include <dxgi1_6.h> // for enumerating GPU adapters, presenting the image and handling transitions
#include <d3dcompiler.h>
#include <DirectXMath.h>

// D3D12 Extension Lib

#include <d3dx12.h>


// STL Headers
#include <algorithm>
#include <cassert>
#include <chrono>


#include "helpers.h"

// Number of swapchan buffers
const uint8_t g_NumFrames = 3;
// Use WARP adapter (Windows Advanced Rasterization Platform)
bool g_UseWarp = false;

uint32_t g_ScreenWidth = 1280;
uint32_t g_ScreenHeight = 720;

// Set to true when DX12 fully initialized
bool g_IsInitialized = false;

// Window Handle
HWND g_hWnd;

// Window Rectangle (for FullScreen)
RECT g_WindowRect;

// DX12 Objects
ComPtr<ID3D12Device13> g_device;
ComPtr<ID3D12CommandQueue> g_CommandQueue;
ComPtr<IDXGISwapChain4> g_SwapChain;
ComPtr<ID3D12Resource2> g_BackBuffers[g_NumFrames];
ComPtr<ID3D12GraphicsCommandList> g_CommandList;
ComPtr<ID3D12CommandAllocator> g_CommandAllocators[g_NumFrames];
ComPtr<ID3D12DescriptorHeap> g_RTVDescriptorHeap;
UINT g_RTVDescriptorSize; // size of a descriptor is vendor specific
UINT g_CurrentBackBufferID;

// Synchronization Objects
ComPtr<ID3D12Fence> g_Fence;
uint64_t g_FenceValue;
uint64_t g_FrameFenceValues[g_NumFrames] = {};
HANDLE g_FenceEvent;

bool g_VSync = true;
bool g_TearingSupported = false;
bool g_Fullscreen = false;

// Window Callback Function
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

// Function to overwrite some of the variables declared above when executing the application
void ParseCommandLineArguments()
{
	int argc;
	wchar_t** argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);

	for (size_t i = 0; i < argc; ++i)
	{
		if (::wcscmp(argv[i], L"-w") == 0 || ::wcscmp(argv[i], L"--width") == 0)
		{
			g_ScreenWidth = ::wcstol(argv[++i], nullptr, 10);
		}
		if (::wcscmp(argv[i], L"-h") == 0 || ::wcscmp(argv[i], L"--height") == 0)
		{
			g_ScreenHeight = ::wcstol(argv[++i], nullptr, 10);
		}
		if (::wcscmp(argv[i], L"-warp") == 0 || ::wcscmp(argv[i], L"--warp") == 0)
		{
			g_UseWarp = true;
		}
	}

	// Free CommandLineToArgvW memory
	::LocalFree(argv);
}


// Debug Layer
void EnableDebugLayer()
{
#ifdef _DEBUG
	ComPtr<ID3D12Debug6> debugInterface;
	ThrowIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(&debugInterface)));
	debugInterface->EnableDebugLayer();
#endif // _DEBUG
}

void RegisterWindowClass(HINSTANCE hInst, const wchar_t* windowClassName)
{
	WNDCLASSEXW windowClass = {};

	windowClass.cbSize = sizeof(WNDCLASSEX);
	windowClass.style = CS_HREDRAW | CS_VREDRAW;
	windowClass.lpfnWndProc = &WndProc;
	windowClass.cbClsExtra = 0;
	windowClass.cbWndExtra = 0;
	windowClass.hInstance = hInst;
	windowClass.hIcon = ::LoadIcon(hInst, NULL);
	windowClass.hCursor = ::LoadCursor(NULL, IDC_ARROW);
	windowClass.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	windowClass.lpszMenuName = NULL;
	windowClass.lpszClassName = windowClassName;
	windowClass.hIconSm = ::LoadIcon(hInst, NULL);

	static ATOM atom = ::RegisterClassExW(&windowClass);
	assert(atom > 0);
}

HWND CreateWindow(const wchar_t* windowClassName, HINSTANCE hInst, const wchar_t* windowTitle, uint32_t width,
	uint32_t height)
{
	int screenWidth = ::GetSystemMetrics(SM_CXSCREEN);
	int screenHeight = ::GetSystemMetrics(SM_CYSCREEN);

	RECT windowRect = { 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
	::AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);

	int windowWidth = windowRect.right - windowRect.left;
	int windowHeight = windowRect.bottom - windowRect.top;

	int windowX = std::max(0, (screenWidth - windowWidth) / 2);
	int windowY = std::max(0, (screenHeight - windowHeight) / 2);

	HWND hWnd = ::CreateWindowExW(NULL, windowClassName, windowTitle,
		WS_OVERLAPPEDWINDOW,
		windowX,
		windowY,
		windowWidth,
		windowHeight,
		NULL,
		NULL,
		hInst,
		nullptr);

	assert(hWnd && "Failed to create window");

	return hWnd;
}

// Query Adapter
ComPtr<IDXGIAdapter4> GetAdapter(bool useWarp)
{
	ComPtr<IDXGIFactory4> dxgiFactory;
	UINT createFactoryFlags = 0;
#ifdef _DEBUG
	createFactoryFlags = DXGI_CREATE_FACTORY_DEBUG;
#endif // _DEBUG

	ThrowIfFailed(CreateDXGIFactory2(createFactoryFlags, IID_PPV_ARGS(&dxgiFactory)));

	ComPtr<IDXGIAdapter1> dxgiAdapter1;
	ComPtr<IDXGIAdapter4> dxgiAdapter4;

	if (useWarp)
	{
		ThrowIfFailed(dxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(&dxgiAdapter1)));
		ThrowIfFailed(dxgiAdapter1.As(&dxgiAdapter4)); // use as, dont static_cast !!
	}
	else
	{
		SIZE_T maxDedicatedVideoMemory = 0;
		for (UINT i = 0; dxgiFactory->EnumAdapters1(i, &dxgiAdapter1) != DXGI_ERROR_NOT_FOUND; ++i)
		{
			DXGI_ADAPTER_DESC1 dxgiAdapterDesc1;
			dxgiAdapter1->GetDesc1(&dxgiAdapterDesc1);

			// check if device creation is possible, use highest dedicated VRAM
			// Since we only consider hardware adapters here, we ignore WARP adapters that have the software flag
			if ((dxgiAdapterDesc1.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0 &&
				SUCCEEDED(D3D12CreateDevice(dxgiAdapter1.Get(), D3D_FEATURE_LEVEL_11_0,
					__uuidof(ID3D12Device), nullptr)) &&
				dxgiAdapterDesc1.DedicatedVideoMemory > maxDedicatedVideoMemory)
			{
				maxDedicatedVideoMemory = dxgiAdapterDesc1.DedicatedVideoMemory;
				ThrowIfFailed(dxgiAdapter1.As(&dxgiAdapter4));
			}
		}
	}
	return dxgiAdapter4;
}

// Create the Device
ComPtr<ID3D12Device13> CreateDevice(ComPtr<IDXGIAdapter4> adapter)
{
	ComPtr<ID3D12Device13> d3d12Device13;
	ThrowIfFailed(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&d3d12Device13)));

	// enable debug messages
#ifdef _DEBUG
	ComPtr<ID3D12InfoQueue> pInfoQueue;
	if (SUCCEEDED(d3d12Device13.As(&pInfoQueue)))
	{
		pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
		pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
		pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, TRUE);
	}

	// surpress whole categories
	// D3D12_MESSAGE_CATEGORY Categories[] = {};

	// Surpress based on severity
	D3D12_MESSAGE_SEVERITY Severeties[] =
	{
		D3D12_MESSAGE_SEVERITY_INFO
	};

	// Surpress by ID
	D3D12_MESSAGE_ID DenyIDs[] =
	{
		D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE,
		D3D12_MESSAGE_ID_MAP_INVALID_NULLRANGE,
		D3D12_MESSAGE_ID_UNMAP_INVALID_NULLRANGE
	};

	D3D12_INFO_QUEUE_FILTER NewFilter = {};
	// NewFilter.DenyList.NumCategories = _countof(Categories);
	// NewFilter.DenyList.pCategoryList = Categories;
	NewFilter.DenyList.NumSeverities = _countof(Severeties);
	NewFilter.DenyList.pSeverityList = Severeties;
	NewFilter.DenyList.NumIDs = _countof(DenyIDs);
	NewFilter.DenyList.pIDList = DenyIDs;

	ThrowIfFailed(pInfoQueue->PushStorageFilter(&NewFilter));

#endif // _DEBUG

	return d3d12Device13;
}

// Creating the Command Queue

ComPtr<ID3D12CommandQueue> CreateCommandQueue(ComPtr<ID3D12Device13> device, D3D12_COMMAND_LIST_TYPE type)
{
	ComPtr<ID3D12CommandQueue> d3d12CommandQueue;

	D3D12_COMMAND_QUEUE_DESC desc = {};
	desc.Type = type;
	desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
	desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	desc.NodeMask = 0;

	ThrowIfFailed(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&d3d12CommandQueue)));

	return d3d12CommandQueue;
}

// G-Sync and FreeSync (NVIDIA / AMD) monitors require tearing to be enabled (so VSync off)
bool CheckTearingSupport()
{
	BOOL allowTearing = FALSE;
	ComPtr<IDXGIFactory4> factory4;
	if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory4))))
	{
		ComPtr<IDXGIFactory5> factory5;
		if (SUCCEEDED(factory4.As(&factory5)))
		{
			if (FAILED(
				factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing, sizeof(allowTearing))))
			{
				allowTearing = FALSE;
			}
		}
	}

	return allowTearing == TRUE;
}

// Create the Swapchain
ComPtr<IDXGISwapChain4> CreateSwapChain(HWND hWnd, ComPtr<ID3D12CommandQueue> commandQueue, uint32_t width,
	uint32_t height, uint32_t bufferCount)
{
	ComPtr<IDXGISwapChain4> dxgiSwapChain4;
	ComPtr<IDXGIFactory4> dxgiFactory4;
	UINT createFactoryFlags = 0;
#ifdef _DEBUG
	createFactoryFlags = DXGI_CREATE_FACTORY_DEBUG;
#endif // _DEBUG
	ThrowIfFailed(CreateDXGIFactory2(createFactoryFlags, IID_PPV_ARGS(&dxgiFactory4)));

	DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
	swapChainDesc.Width = width;
	swapChainDesc.Height = height;
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.Stereo = false;
	swapChainDesc.SampleDesc = { 1, 0 }; // only valid with bit-block, otherwise specify {1,0}
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.BufferCount = bufferCount;
	swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
	swapChainDesc.Flags = CheckTearingSupport() ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

	ComPtr<IDXGISwapChain1> swapChain1;
	ThrowIfFailed(
		dxgiFactory4->CreateSwapChainForHwnd(commandQueue.Get(), hWnd, &swapChainDesc, nullptr, nullptr, &swapChain1));

	// disable alt+Enter for fullscreen
	ThrowIfFailed(dxgiFactory4->MakeWindowAssociation(hWnd, DXGI_MWA_NO_ALT_ENTER));
	ThrowIfFailed(swapChain1.As(&dxgiSwapChain4));
	return dxgiSwapChain4;
}

// Create Descriptor Heap! (And uninstall old VisualStudio)

ComPtr<ID3D12DescriptorHeap> CreateDescriptorHeap(ComPtr<ID3D12Device13> device, D3D12_DESCRIPTOR_HEAP_TYPE type,
	uint32_t numDescriptors)
{
	ComPtr<ID3D12DescriptorHeap> descriptorHeap;
	D3D12_DESCRIPTOR_HEAP_DESC desc = {};
	desc.Type = type;
	desc.NumDescriptors = numDescriptors;

	ThrowIfFailed(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&descriptorHeap)));
	return descriptorHeap;
}

// Create RenderTargetViews (RTV)
void UpdateRenderView(ComPtr<ID3D12Device13> device, ComPtr<ID3D12DescriptorHeap> descriptorHeap,
	ComPtr<IDXGISwapChain4> swapchain)
{
	auto rtvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(descriptorHeap->GetCPUDescriptorHandleForHeapStart());

	for (int i = 0; i < g_NumFrames; ++i)
	{
		ComPtr<ID3D12Resource2> backBuffer;
		ThrowIfFailed(swapchain->GetBuffer(i, IID_PPV_ARGS(&backBuffer)));

		device->CreateRenderTargetView(backBuffer.Get(), nullptr, rtvHandle);
		g_BackBuffers[i] = backBuffer;
		rtvHandle.Offset(rtvDescriptorSize);
	}
}

// Create Command Allocator
ComPtr<ID3D12CommandAllocator> CreateCommandAllocator(ComPtr<ID3D12Device13> device, D3D12_COMMAND_LIST_TYPE type)
{
	ComPtr<ID3D12CommandAllocator> commandAllocator;
	ThrowIfFailed(device->CreateCommandAllocator(type, IID_PPV_ARGS(&commandAllocator)));
	return commandAllocator;
}

// Create Command List
ComPtr<ID3D12GraphicsCommandList> CreateCommandList(ComPtr<ID3D12Device13> device,
	ComPtr<ID3D12CommandAllocator> allocator,
	D3D12_COMMAND_LIST_TYPE type)
{
	ComPtr<ID3D12GraphicsCommandList> commandList;
	ThrowIfFailed(device->CreateCommandList(0, type, allocator.Get(), nullptr, IID_PPV_ARGS(&commandList)));
	ThrowIfFailed(commandList->Close());
	// Created in recording state, meaning we need to call Close() when creating it so we can call Reset() to start recording
	return commandList;
}

// Create Fence
ComPtr<ID3D12Fence> CreateFence(ComPtr<ID3D12Device13> device)
{
	ComPtr<ID3D12Fence> fence;
	ThrowIfFailed(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
	return fence;
}

// Create Event (used to stall CPU)
HANDLE CreateEventHandle()
{
	HANDLE fenceEvent;
	fenceEvent = ::CreateEvent(NULL, FALSE, FALSE, NULL);
	assert(fenceEvent && "Failed to create fence event.");
	return fenceEvent;
}

// Signal Fence
uint64_t SignalFence(ComPtr<ID3D12CommandQueue> commandQueue, ComPtr<ID3D12Fence> fence, uint64_t& fenceValue)
{
	uint64_t fenceSignalValue = ++fenceValue;
	ThrowIfFailed(commandQueue->Signal(fence.Get(), fenceSignalValue));
	return fenceSignalValue;
}

// Wait for Fence

void WaitForFenceValue(ComPtr<ID3D12Fence> fence, uint64_t fenceValue, HANDLE fenceEvent,
	std::chrono::milliseconds duration = std::chrono::milliseconds::max())
{
	if (fence->GetCompletedValue() < fenceValue)
	{
		ThrowIfFailed(fence->SetEventOnCompletion(fenceValue, fenceEvent));
		::WaitForSingleObject(fenceEvent, static_cast<DWORD>(duration.count()));
	}
}

void Flush(ComPtr<ID3D12Fence> fence, ComPtr<ID3D12CommandQueue> commandQueue, uint64_t& fenceValue, HANDLE fenceEvent)
{
	uint64_t fenceSignalValue = SignalFence(commandQueue, fence, fenceValue);
	WaitForFenceValue(fence, fenceSignalValue, fenceEvent);
}

void Resize(uint32_t width, uint32_t height)
{
	if (g_ScreenWidth != width || g_ScreenHeight != height)
	{
		g_ScreenWidth = std::max(1u, width);
		g_ScreenHeight = std::max(1u, height);

		Flush(g_Fence, g_CommandQueue, g_FenceValue, g_FenceEvent);

		for (int i = 0; i < g_NumFrames; ++i)
		{
			g_BackBuffers[i].Reset();
			g_FrameFenceValues[i] = g_FrameFenceValues[g_CurrentBackBufferID];
		}

		DXGI_SWAP_CHAIN_DESC desc = {};
		ThrowIfFailed(g_SwapChain->GetDesc(&desc));
		ThrowIfFailed(g_SwapChain->ResizeBuffers(g_NumFrames, g_ScreenWidth, g_ScreenHeight, desc.BufferDesc.Format, desc.Flags));

		g_CurrentBackBufferID = g_SwapChain->GetCurrentBackBufferIndex();
		UpdateRenderView(g_device, g_RTVDescriptorHeap, g_SwapChain);
	}
}

// FullScreen functionality 

void SetFullscreen(bool fullscreen)
{
	if (g_Fullscreen != fullscreen)
	{
		g_Fullscreen = fullscreen;

		if (g_Fullscreen) // Switching to fullscreen.
		{
			// Store the current window dimensions so they can be restored 
			// when switching out of fullscreen state.
			::GetWindowRect(g_hWnd, &g_WindowRect);

			// Set the window style to a borderless window so the client area fills
			// the entire screen.
			UINT windowStyle = WS_OVERLAPPEDWINDOW & ~(WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX);

			::SetWindowLongW(g_hWnd, GWL_STYLE, windowStyle);

			HMONITOR hMonitor = ::MonitorFromWindow(g_hWnd, MONITOR_DEFAULTTONEAREST);
			MONITORINFOEX monitorInfo = {};
			monitorInfo.cbSize = sizeof(MONITORINFOEX);
			::GetMonitorInfo(hMonitor, &monitorInfo);

			::SetWindowPos(g_hWnd, HWND_TOP,
				monitorInfo.rcMonitor.left,
				monitorInfo.rcMonitor.top,
				monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left,
				monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top,
				SWP_FRAMECHANGED | SWP_NOACTIVATE);

			::ShowWindow(g_hWnd, SW_MAXIMIZE);
		}
		else
		{
			// Restore all the window decorators.
			::SetWindowLong(g_hWnd, GWL_STYLE, WS_OVERLAPPEDWINDOW);

			::SetWindowPos(g_hWnd, HWND_NOTOPMOST,
				g_WindowRect.left,
				g_WindowRect.top,
				g_WindowRect.right - g_WindowRect.left,
				g_WindowRect.bottom - g_WindowRect.top,
				SWP_FRAMECHANGED | SWP_NOACTIVATE);

			::ShowWindow(g_hWnd, SW_NORMAL);
		}
	}
}



// UPDATE

void Update()
{
	static uint64_t frameCounter = 0;
	static double elapsedSeconds = 0.0;
	static std::chrono::high_resolution_clock clock;
	static auto t0 = clock.now();

	frameCounter++;
	auto t1 = clock.now();
	auto deltaTime = t1 - t0;
	t0 = t1;

	elapsedSeconds += deltaTime.count() * 1e-9;
	if (elapsedSeconds > 1.0)
	{
		char buffer[500];
		auto fps = frameCounter / elapsedSeconds;
		sprintf_s(buffer, 500, "FPS: %f\n", fps);
		OutputDebugStringA(buffer);

		frameCounter = 0;
		elapsedSeconds = 0.0;
	}
}

// Render 

void Render()
{
	auto commandAllocator = g_CommandAllocators[g_CurrentBackBufferID];
	auto backBuffer = g_BackBuffers[g_CurrentBackBufferID];

	commandAllocator->Reset();
	g_CommandList->Reset(commandAllocator.Get(), nullptr);

	// clear render target

	CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
		backBuffer.Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
	g_CommandList->ResourceBarrier(1, &barrier);

	FLOAT clearColor[] = { 0.3f, 0.6f, 0.9f, 1.0f };
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtv = {
		g_RTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), static_cast<INT>(g_CurrentBackBufferID),
		g_RTVDescriptorSize
	};
	g_CommandList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);

	// present the backbuffer

	CD3DX12_RESOURCE_BARRIER presentBarrier = CD3DX12_RESOURCE_BARRIER::Transition(backBuffer.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
	g_CommandList->ResourceBarrier(1, &presentBarrier);


	// close command list and execute it
	ThrowIfFailed(g_CommandList->Close());

	ID3D12CommandList* const commandLists[] = { g_CommandList.Get() };
	g_CommandQueue->ExecuteCommandLists(_countof(commandLists), commandLists);

	// Present the buffer
	UINT syncInterval = g_VSync ? 1 : 0;
	UINT presentFlags = g_TearingSupported && !g_VSync ? DXGI_PRESENT_ALLOW_TEARING : 0;
	ThrowIfFailed(g_SwapChain->Present(syncInterval, presentFlags));

	g_FrameFenceValues[g_CurrentBackBufferID] = SignalFence(g_CommandQueue, g_Fence, g_FenceValue);

	g_CurrentBackBufferID = g_SwapChain->GetCurrentBackBufferIndex();
	WaitForFenceValue(g_Fence, g_FenceValue, g_FenceEvent);



}

LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	if (g_IsInitialized)
	{
		switch (message)
		{
		case WM_PAINT:
			Update();
			Render();
			break;
		case WM_SYSKEYDOWN:
			break;
		case WM_KEYDOWN:
		{
			bool alt = (::GetAsyncKeyState(VK_MENU) & 0x8000) != 0;

			switch (wParam)
			{
			case 'V':
				g_VSync = !g_VSync;
				break;
			case VK_ESCAPE:
				::PostQuitMessage(0);
				break;
			case VK_RETURN:
				if (alt)
				{
			case VK_F11:
				SetFullscreen(!g_Fullscreen);
				}
				break;
			}
		}
		break;
		case WM_SYSCHAR:
			break;
		case WM_SIZE:
		{
			RECT clientRect = {};
			::GetClientRect(g_hWnd, &clientRect);

			int width = clientRect.right - clientRect.left;
			int height = clientRect.bottom - clientRect.top;

			Resize(width, height);
		}
		break;
		case WM_DESTROY:
			::PostQuitMessage(0);
			break;
		default:
			return ::DefWindowProcW(hwnd, message, wParam, lParam);
		}
	}
	else
	{
		return ::DefWindowProcW(hwnd, message, wParam, lParam);
	}
	return 0;
}

int CALLBACK wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR lpCmdLine, int nCmdShow)
{
	// Windows 10 Creators update adds Per Monitor V2 DPI awareness context.
	// Using this awareness context allows the client area of the window 
	// to achieve 100% scaling while still allowing non-client window content to 
	// be rendered in a DPI sensitive fashion.
	SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

	// Window class name. Used for registering / creating the window.
	const wchar_t* windowClassName = L"EV Engine";
	ParseCommandLineArguments();
	EnableDebugLayer();

	g_TearingSupported = CheckTearingSupport();

	RegisterWindowClass(hInstance, windowClassName);
	g_hWnd = CreateWindow(windowClassName, hInstance, L"DX12 Prototype", g_ScreenWidth, g_ScreenHeight);

	::GetWindowRect(g_hWnd, &g_WindowRect);

	// Create DX12 objects.
	ComPtr<IDXGIAdapter4> dxgiAdapter4 = GetAdapter(g_UseWarp);
	g_device = CreateDevice(dxgiAdapter4);
	g_CommandQueue = CreateCommandQueue(g_device, D3D12_COMMAND_LIST_TYPE_DIRECT);
	g_SwapChain = CreateSwapChain(g_hWnd, g_CommandQueue, g_ScreenWidth, g_ScreenHeight, g_NumFrames);
	g_CurrentBackBufferID = g_SwapChain->GetCurrentBackBufferIndex();
	g_RTVDescriptorHeap = CreateDescriptorHeap(g_device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, g_NumFrames);
	g_RTVDescriptorSize = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	UpdateRenderView(g_device, g_RTVDescriptorHeap, g_SwapChain);

	for (int i = 0; i < g_NumFrames; ++i)
	{
		g_CommandAllocators[i] = CreateCommandAllocator(g_device, D3D12_COMMAND_LIST_TYPE_DIRECT);
	}
	g_CommandList = CreateCommandList(g_device, g_CommandAllocators[g_CurrentBackBufferID], D3D12_COMMAND_LIST_TYPE_DIRECT);

	g_Fence = CreateFence(g_device);
	g_FenceEvent = CreateEventHandle();

	g_IsInitialized = true;

	::ShowWindow(g_hWnd, SW_SHOW);
	MSG msg = {};

	while (msg.message != WM_QUIT)
	{
		if (::PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
		{
			::TranslateMessage(&msg);
			::DispatchMessage(&msg);
		}
	}

	Flush(g_Fence, g_CommandQueue, g_FenceValue, g_FenceEvent);

	::CloseHandle(g_FenceEvent);

	return 0;
}
*/

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <Shlwapi.h>

#include <application.h>
#include <demo.h>

#include <dxgidebug.h>

void ReportLiveObjects()
{
	IDXGIDebug1* dxgiDebug;
	DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgiDebug));

	dxgiDebug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_IGNORE_INTERNAL);
	dxgiDebug->Release();
}

int CALLBACK wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR lpCmdLine, int nCmdShow)
{
	int retCode = 0;

	// Set the working directory to the path of the executable.
	WCHAR path[MAX_PATH];
	HMODULE hModule = GetModuleHandleW(NULL);
	if (GetModuleFileNameW(hModule, path, MAX_PATH) > 0)
	{
		PathRemoveFileSpecW(path);
		SetCurrentDirectoryW(path);
	}

	Application::Create(hInstance);
	{
		std::shared_ptr<Demo> demo = std::make_shared<Demo>(L"EV Engine", 1280, 720);
		retCode = Application::Get().Run(demo);
	}
	Application::Destroy();

	atexit(&ReportLiveObjects);

	return retCode;
}

////////////////////////////////////////////////////////////////////////////// TODOs /////////////////////////////////////////////////////////////
/*

												GENERAL

[ ] Understand ins and outs of the engine, how it works in its entirety.
[ ] Find all TODOs in the engine and adres them
[ ] 
[ ]
[ ]
[ ]
[ ]
[ ]
[ ]
[ ]
[ ]
[ ]


												SPECIFIC
[ ] Compile DirectXTex and build it as a separate library, so this project can simply include it and link to it.
[ ]
[ ]
[ ]
[ ]
[ ]
[ ]
[ ]
[ ]
[ ]
[ ]
[ ]




*/
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////