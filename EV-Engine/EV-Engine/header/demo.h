#pragma once
#include "camera.h"
#include "game.h"
#include "window.h"
#include "DirectXMath.h"
#include "mesh.h"
#include "render_target.h"
#include "root_signature.h"
#include "Texture.h"

class PipelineStateObject;
class Scene;
class GUI;
class SwapChain;

class Demo : public Game
{
public:
	using super = Game;
	Demo(const std::wstring& name, uint32_t width, uint32_t height, bool bVSync = false);
	virtual ~Demo();
	bool LoadContent() override;
	void UnloadContent() override;

protected:
	void OnUpdate(UpdateEventArgs& e) override;
	void OnRender() override;
	void OnGUI(const std::shared_ptr<CommandList>& commandList, const RenderTarget& renderTarget);
	void OnKeyPress(KeyEventArgs& e) override;
	void OnKeyRelease(KeyEventArgs& e) override;
	void OnMouseMove(MouseMotionEventArgs& e) override;
	void OnMouseWheel(MouseWheelEventArgs& e) override;
	void OnResize(ResizeEventArgs& e) override;

private:
	/// <summary>
	/// Gets the file path of the currently running executable.
	/// </summary>
	/// <returns>A wide string containing the full path to the running executable.</returns>
	static std::wstring GetModulePath();

	std::shared_ptr<Scene> m_cubeMesh;
	std::unique_ptr<Mesh> m_sphereMesh;
	std::unique_ptr<Mesh> m_coneMesh;
	std::unique_ptr<Mesh> m_torusMesh;
	std::unique_ptr<Mesh> m_planeMesh;


	// TODO: add textures
	std::shared_ptr<Texture> m_defaultTexture;

	RenderTarget m_renderTarget = {};

	std::shared_ptr<RootSignature> m_rootSignature = nullptr;

	std::shared_ptr<PipelineStateObject> m_pipelineState;

	Camera m_camera;

	struct alignas(16) CameraData
	{
		DirectX::XMVECTOR m_initialPosition;
		DirectX::XMVECTOR m_initialRotation;
	};
	CameraData* m_pAlignedCameraData;

	// Camera Controls
	float m_forward;
	float m_backward;
	float m_left;
	float m_right;
	float m_up;
	float m_down;

	float m_pitch;
	float m_yaw;

	uint32_t m_width;
	uint32_t m_height;


	void TransitionResource(Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2> pCommandList, Microsoft::WRL::ComPtr<ID3D12Resource> pResource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after);
	void ClearRTV(Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2> pCommandList, D3D12_CPU_DESCRIPTOR_HANDLE RTV, FLOAT* clearColor);
	void ClearDepth(Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2> pCommandList, D3D12_CPU_DESCRIPTOR_HANDLE DSV, FLOAT depth = 1.0f);
	// Create GPU buffer
	void UpdateBufferResource(Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2> pCommandList, ID3D12Resource** pDestination, ID3D12Resource** pIntermediate, size_t numElements, size_t elementSize, const void* pBufferData, D3D12_RESOURCE_FLAGS = D3D12_RESOURCE_FLAG_NONE);
	void ResizeDepthBuffer(uint32_t width, uint32_t height);

	uint64_t m_fenceValues[Window::BufferCount] = {};

	Microsoft::WRL::ComPtr<ID3D12Resource> m_vertexBuffer = nullptr;
	D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView = {};

	Microsoft::WRL::ComPtr<ID3D12Resource> m_indexBuffer = nullptr;
	D3D12_INDEX_BUFFER_VIEW m_indexBufferView = {};

	Microsoft::WRL::ComPtr<ID3D12Resource> m_depthBuffer = nullptr;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_depthDescriptorHeap = {};

	// Microsoft::WRL::ComPtr<ID3D12RootSignature> m_rootSignature = {};
	// Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pipelineState = {};

	D3D12_VIEWPORT m_viewport = {};
	D3D12_RECT m_scissorRect = {};

	// float m_fov = 0.0f;
	// DirectX::XMMATRIX m_model = {};
	// DirectX::XMMATRIX m_view = {};
	// DirectX::XMMATRIX m_projection = {};

	bool m_contentLoaded = false;
	bool m_shift = false;
	bool m_vSync = false;

	std::shared_ptr<Window> m_pWindow = nullptr;
	std::shared_ptr<SwapChain> m_swapChain = nullptr;
	std::shared_ptr<GUI> m_GUI = nullptr;

};
