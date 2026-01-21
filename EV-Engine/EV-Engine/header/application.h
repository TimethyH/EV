#pragma once
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>

#include <memory>
#include <string>

#include "descriptor_allocation.h"
#include "events.h"
#include "GUI.h"
#include "render_target.h"

class SwapChain;
class UnorderedAccessView;
class Resource;
class ShaderResourceView;
class RootSignature;
class VertexBuffer;
class Texture;
class IndexBuffer;
class DescriptorAllocator;
class Window;
class Game;
class CommandQueue;
class PipelineStateObject;


/**
 * Windows message handler.
 */
using WndProcEvent = del::Delegate<LRESULT(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)>;


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
	CommandQueue& GetCommandQueue(
		D3D12_COMMAND_LIST_TYPE type = D3D12_COMMAND_LIST_TYPE_DIRECT) const;
	void Flush();

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> CreateDescriptorHeap(UINT numDescriptors, D3D12_DESCRIPTOR_HEAP_TYPE type);
	UINT GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE type) const;

	static uint64_t GetFrameCount();
	DescriptorAllocation AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t numDescriptors = 1);
	DXGI_SAMPLE_DESC GetMultisampleQualityLevels(DXGI_FORMAT format, UINT numSamples,
	                                             D3D12_MULTISAMPLE_QUALITY_LEVEL_FLAGS flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE) const;

	void ReleaseStaleDescriptors();


	/**
 * Create a ConstantBuffer from a given ID3D12Resoure.
 */
	// std::shared_ptr<ConstantBuffer> CreateConstantBuffer(Microsoft::WRL::ComPtr<ID3D12Resource> resource);

	/**
	 * Create a ByteAddressBuffer resource.
	 *
	 * @param resDesc A description of the resource.
	 */
	// std::shared_ptr<ByteAddressBuffer> CreateByteAddressBuffer(size_t bufferSize);
	// std::shared_ptr<ByteAddressBuffer> CreateByteAddressBuffer(Microsoft::WRL::ComPtr<ID3D12Resource> resource);

	/**
	 * Create a structured buffer resource.
	 */
	// std::shared_ptr<StructuredBuffer> CreateStructuredBuffer(size_t numElements, size_t elementSize);
	// std::shared_ptr<StructuredBuffer> CreateStructuredBuffer(Microsoft::WRL::ComPtr<ID3D12Resource> resource,
		// size_t numElements, size_t elementSize);

	/**
	 * Create a Texture resource.
	 *
	 * @param resourceDesc A description of the texture to create.
	 * @param [clearVlue] Optional optimized clear value for the texture.
	 * @param [textureUsage] Optional texture usage flag provides a hint about how the texture will be used.
	 *
	 * @returns A pointer to the created texture.
	 */
	std::shared_ptr<Texture> CreateTexture(const D3D12_RESOURCE_DESC& resourceDesc,
		const D3D12_CLEAR_VALUE* clearValue = nullptr);
	std::shared_ptr<Texture> CreateTexture(Microsoft::WRL::ComPtr<ID3D12Resource> resource,
		const D3D12_CLEAR_VALUE* clearValue = nullptr);
	std::shared_ptr<GUI> CreateGUI(HWND hWnd, const RenderTarget& renderTarget);

	std::shared_ptr<IndexBuffer> CreateIndexBuffer(size_t numIndices, DXGI_FORMAT indexFormat);
	std::shared_ptr<IndexBuffer> CreateIndexBuffer(Microsoft::WRL::ComPtr<ID3D12Resource> resource, size_t numIndices,
		DXGI_FORMAT indexFormat);

	std::shared_ptr<VertexBuffer> CreateVertexBuffer(size_t numVertices, size_t vertexStride);
	std::shared_ptr<VertexBuffer> CreateVertexBuffer(Microsoft::WRL::ComPtr<ID3D12Resource> resource,
		size_t numVertices, size_t vertexStride);

	std::shared_ptr<RootSignature> CreateRootSignature(const D3D12_ROOT_SIGNATURE_DESC1& rootSignatureDesc);

	template<class PipelineStateStream>
	std::shared_ptr<PipelineStateObject> CreatePipelineStateObject(PipelineStateStream& pipelineStateStream)
	{
		D3D12_PIPELINE_STATE_STREAM_DESC pipelineStateStreamDesc = { sizeof(PipelineStateStream),
																	 &pipelineStateStream };

		return DoCreatePipelineStateObject(pipelineStateStreamDesc);
	}

	// std::shared_ptr<ConstantBufferView> CreateConstantBufferView(const std::shared_ptr<ConstantBuffer>& constantBuffer,
	// 	size_t                                 offset = 0);

	std::shared_ptr<ShaderResourceView>
		CreateShaderResourceView(const std::shared_ptr<Resource>& resource,
			const D3D12_SHADER_RESOURCE_VIEW_DESC* srv = nullptr);

	std::shared_ptr<UnorderedAccessView>
		CreateUnorderedAccessView(const std::shared_ptr<Resource>& resource,
			const std::shared_ptr<Resource>& counterResource = nullptr,
			const D3D12_UNORDERED_ACCESS_VIEW_DESC* uav = nullptr);



	D3D_ROOT_SIGNATURE_VERSION GetHighestRootSignatureVersion() const
	{
		return m_highestRootSignatureVersion;
	}

	Microsoft::WRL::ComPtr<IDXGIAdapter4> GetAdapter();

	std::shared_ptr<SwapChain> CreateSwapchain(HWND hWnd, DXGI_FORMAT backBufferFormat = DXGI_FORMAT_R10G10B10A2_UNORM);

	/**
	 * Invoked when a message is sent to a window.
	 */
	WndProcEvent wndProcHandler;

protected:
	Application(HINSTANCE hInstance);
	virtual ~Application();
	void Initialize();

	Microsoft::WRL::ComPtr<IDXGIAdapter4> CreateAdapter(bool bUseWarp);
	Microsoft::WRL::ComPtr<ID3D12Device13> CreateDevice(Microsoft::WRL::ComPtr<IDXGIAdapter4> pAdapter);
	bool CheckTearingSupport();
	std::shared_ptr<PipelineStateObject>
		DoCreatePipelineStateObject(const D3D12_PIPELINE_STATE_STREAM_DESC& pipelineStateStreamDesc);
private:
	Application(const Application& copy) = delete; // deletes copy constructor
	Application& operator=(const Application& other) = delete; // deletes assignment operator

	friend LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

	// Instance the application was made with
	HINSTANCE m_hInstance = {};
	Microsoft::WRL::ComPtr<IDXGIAdapter4> m_dxgiAdapter = {};
	Microsoft::WRL::ComPtr<ID3D12Device13> m_device = {};

	std::shared_ptr<CommandQueue> m_DirectCommandQueue;
	std::shared_ptr<CommandQueue> m_ComputeCommandQueue;
	std::shared_ptr<CommandQueue> m_CopyCommandQueue;

	bool m_tearingSupported = false;
	static uint64_t m_frameCount;

	std::unique_ptr<DescriptorAllocator> m_descriptorAllocators[D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES];
	D3D_ROOT_SIGNATURE_VERSION m_highestRootSignatureVersion;
	
};