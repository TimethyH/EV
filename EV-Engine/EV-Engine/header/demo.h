#pragma once
#include "game.h"
#include "window.h"
#include "DirectXMath.h"

class Demo : Game
{
	using super = Game;
	Demo(const std::wstring& name, uint32_t width, uint32_t height, bool bVSync = false);

	bool LoadContent() override;
	void UnloadContent() override;

protected:
	void OnUpdate(UpdateEventArgs& e) override;
	void OnRender(RenderEventArgs& e) override;	
	void OnKeyPress(KeyEventArgs& e) override;
	void OnMouseMove(MouseMotionEventArgs& e) override;
	void OnMouseWheel(MouseWheelEventArgs& e) override;
	void OnResize(ResizeEventArgs& e) override;

private:
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

	Microsoft::WRL::ComPtr<ID3D12RootSignature> m_rootSignature = {};
	Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pipelineState = {};

	D3D12_VIEWPORT m_viewport = {};
	D3D12_RECT m_scissorRect = {};

	float m_fov = 0.0f;
	DirectX::XMMATRIX m_model = {};
	DirectX::XMMATRIX m_view = {};
	DirectX::XMMATRIX m_projection = {};

	bool m_contentLoaded = false;
};
