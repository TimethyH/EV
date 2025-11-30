#pragma once
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>

#include <memory>
#include <string>

class Window;
class Game;
class CommandQueue;

class Application
{
public:
	// static because the Application is a singleton,
	// we dont have an instance of it yet when making the class
	// static makes it a class level function rather than instance level
	static void Create(HINSTANCE hinstance);

	static void Destroy();

	static Application& Get();

	bool TearingSupported() const;

	// Window Functions
	std::shared_ptr<Window> CreateRenderWindow(const std::wstring& windowName, uint32_t windowWidth, uint32_t windowHeight, bool bVSync = true);
	void DestroyWindow(const std::wstring& name);
	void DestroyWindow(std::shared_ptr<Window> pWindow);
	// Find the window using its name
	std::shared_ptr<Window> GetWindow(const std::wstring& name);

	// Application Functionality
	int Run(std::shared_ptr<Game> pGame);
	void Quit(int exitCode = 0);

	Microsoft::WRL::ComPtr<ID3D12Device13> GetDevice() const;
	std::shared_ptr<CommandQueue> GetCommandQueue(
		D3D12_COMMAND_LIST_TYPE type = D3D12_COMMAND_LIST_TYPE_DIRECT) const;
	void Flush();

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> CreateDescriptorHeap(UINT numDescriptors, D3D12_DESCRIPTOR_HEAP_TYPE type);
	UINT GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE type) const;

protected:
	Application(HINSTANCE hInstance);
	virtual ~Application();

	Microsoft::WRL::ComPtr<IDXGIAdapter4> GetAdapter(bool bUseWarp);
	Microsoft::WRL::ComPtr<ID3D12Device13> CreateDevice(Microsoft::WRL::ComPtr<IDXGIAdapter4> pAdapter);
	bool CheckTearingSupport();
private:
	Application(const Application& copy) = delete; // deletes copy constructor
	Application& operator=(const Application& other) = delete; // deletes assignment operator

	// Instance the application was made with
	HINSTANCE m_hInstance = {};
	Microsoft::WRL::ComPtr<IDXGIAdapter4> m_dxgiAdapter = {};
	Microsoft::WRL::ComPtr<ID3D12Device13> m_device = {};

	std::shared_ptr<CommandQueue> m_DirectCommandQueue;
	std::shared_ptr<CommandQueue> m_ComputeCommandQueue;
	std::shared_ptr<CommandQueue> m_CopyCommandQueue;

	bool m_tearingSupported = false;

};