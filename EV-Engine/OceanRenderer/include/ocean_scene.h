#pragma once
#include <future>


#include "DirectXMath.h"
#include "core/camera.h"
#include "core/game.h"
#include "core/window.h"
#include "DX12/render_target.h"
#include <complex>

#define OCEAN_SUBRES 512
#define OCEAN_SIZE 500.0f
#define OCEAN_DEPTH 20.0f

class OceanCompute;
class UpdateEventArgs;
class KeyEventArgs;
class MouseMotionEventArgs;
class MouseWheelEventArgs;
class ResizeEventArgs;

namespace EV
{
	class GUI;
	class SwapChain;
	class PipelineStateObject;
	struct DirectionalLight;
	struct PointLight;
	class EffectPSO;
	class Scene;
	class Game;
	class CommandList;
	class RootSignature;
}

// struct DirectionalLight;
// struct SpotLight;
// struct PointLight;
// class EffectPSO;
// class PipelineStateObject;
// class Scene;
// class GUI;
// class SwapChain;

namespace EV
{


class Ocean : public EV::Game
{
public:
	using super = EV::Game;
	Ocean(const std::wstring& name, uint32_t width, uint32_t height, bool bVSync = false);
	virtual ~Ocean();
	bool LoadContent() override;
	void UnloadContent() override;

	float InitPhillipsSpectrum(DirectX::XMFLOAT2 k, DirectX::XMFLOAT2 windDir, float windSpeed, float A = 0.05f);
	float DirectionSpectrum(float theta, float omega);
	float ShortWavesFade(float kLength);
	void GenerateH0(std::shared_ptr<CommandList> commandList);
	float GaussianRandom();
	void UpdateSpectrum(float time);
	float JONSWAP(float omega);

	float JonswapAlpha(float fetch, float windSpeed);
	float JonswapPeakFequency(float fetch, float windSpeed);
	void UpdateSpectrumParameters();

protected:
	void OnUpdate(UpdateEventArgs& e) override;
	void OnRender() override;
	void OnGUI(const std::shared_ptr<EV::CommandList>& commandList, const EV::RenderTarget& renderTarget);
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
	bool LoadingProgress(float loadingProgress);
	bool LoadScene(const std::wstring& sceneFile);

	std::shared_ptr<EV::Scene> m_cubeMesh;

	std::shared_ptr<EV::Scene> m_scene;
	std::shared_ptr<EV::Scene> m_helmet;
	std::shared_ptr<EV::Scene> m_chessboard;

	std::shared_ptr<EV::Scene> m_oceanPlane;

	std::atomic_bool  m_isLoading;
	bool m_cancelLoading;
	std::string m_loadingText;
	float m_loadingProgress;

	// TODO: add textures
	std::shared_ptr<EV::Texture> m_defaultTexture;

	EV::RenderTarget m_renderTarget = {};

	std::shared_ptr<EV::RootSignature> m_rootSignature = nullptr;

	std::shared_ptr<EV::PipelineStateObject> m_pipelineState;

	static EV::Camera m_camera;

	struct alignas(16) CameraData
	{
		DirectX::XMVECTOR m_initialPosition;
		DirectX::XMVECTOR m_initialRotation;
	};
	CameraData* m_pAlignedCameraData;

	std::vector<EV::PointLight> m_pointLights;
	// std::vector<SpotLight>  m_spotLights;
	std::vector<EV::DirectionalLight> m_directionalLights;
	std::shared_ptr<EV::Scene> m_sphere;

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


	// void TransitionResource(Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2> pCommandList, Microsoft::WRL::ComPtr<ID3D12Resource> pResource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after);
	// void ClearRTV(Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2> pCommandList, D3D12_CPU_DESCRIPTOR_HANDLE RTV, FLOAT* clearColor);
	// void ClearDepth(Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2> pCommandList, D3D12_CPU_DESCRIPTOR_HANDLE DSV, FLOAT depth = 1.0f);
	// // Create GPU buffer
	// void UpdateBufferResource(Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2> pCommandList, ID3D12Resource** pDestination, ID3D12Resource** pIntermediate, size_t numElements, size_t elementSize, const void* pBufferData, D3D12_RESOURCE_FLAGS = D3D12_RESOURCE_FLAG_NONE);
	// void ResizeDepthBuffer(uint32_t width, uint32_t height);

	uint64_t m_fenceValues[EV::Window::BufferCount] = {};

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

	std::shared_ptr<EV::Window> m_pWindow = nullptr;
	std::shared_ptr<EV::SwapChain> m_swapChain = nullptr;
	std::shared_ptr<EV::GUI> m_GUI = nullptr;

	std::shared_ptr<EV::EffectPSO> m_unlitPSO;
	std::shared_ptr<EV::EffectPSO> m_displacementPSO;
	std::shared_ptr<OceanCompute> m_oceanPSO;
	std::shared_ptr<OceanCompute> m_fftPSO;
	std::shared_ptr<OceanCompute> m_permuteHeightPSO;
	std::shared_ptr<OceanCompute> m_permuteSlopePSO;

	std::future<bool> m_loadingTask;

	// Ocean

	// FFT and JONSWAP Implementation largely referenced from https://github.com/gasgiant/FFT-Ocean/
	struct JonswapParameters
	{
		float scale = 0.0f; // Used to scale the Spectrum [1.0f, 5.0f] --> Value Range
		float spreadBlend = 0.0f; // Used to blend between agitated water motion, and windDirection [0.0f, 1.0f]
		float swell = 0.0f; // Influences wave choppines, the bigger the swell, the longer the wave length [0.0f, 1.0f]
		float gamma = 0.0f; // Defines the Spectrum Peak [0.0f, 7.0f]
		float shortWavesFade = 0.0f; // [0.0f, 1.0f]

		float windDirection = 0.0f; // [0.0f, 360.0f]
		float fetch = 0.0f; // Distance over which Wind impacts Wave Formation [0.0f, 10000.0f]
		float windSpeed = 0.0f; // [0.0f, 100.0f]

		float angle = 0.0f;
		float alpha = 0.0f;
		float peakOmega = 0.0f;
	}m_jonswapParams;

	std::complex<float> H0[OCEAN_SUBRES][OCEAN_SUBRES];
	std::complex<float> heightMap[OCEAN_SUBRES][OCEAN_SUBRES];
	std::complex<float> H0Conj[OCEAN_SUBRES][OCEAN_SUBRES];

	std::shared_ptr<Texture> m_H0Texture;
	std::shared_ptr<Texture> m_slopeTexture;
	std::shared_ptr<Texture> m_displacementTexture;
	std::shared_ptr<Texture> m_heightTexture;
	std::shared_ptr<Texture> m_normalTexture; 
	std::shared_ptr<Texture> m_intermediateTextureSlope; 
	std::shared_ptr<Texture> m_intermediateTextureHeight; 
	std::shared_ptr<Texture> m_permutedSlope; 
	std::shared_ptr<Texture> m_permutedHeight; 
};

}