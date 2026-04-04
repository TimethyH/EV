#pragma once

#include "core/camera.h"
#include "core/game.h"
#include "core/window.h"
#include "DX12/render_target.h"

class UpdateEventArgs;
class KeyEventArgs;
class MouseMotionEventArgs;
class MouseWheelEventArgs;
class ResizeEventArgs;

namespace EV
{
	class LandscapePSO;
	class GUI;
	class SwapChain;

	class Landscape : public EV::Game
{

public:
	using super = EV::Game;
	Landscape(const std::wstring& name, uint32_t width, uint32_t height, bool bVSync = false);
	~Landscape();

	bool LoadContent() override;
	void UnloadContent() override;


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


	std::shared_ptr<EV::Window> m_pWindow = nullptr;
	std::shared_ptr<EV::SwapChain> m_swapChain = nullptr;
	std::shared_ptr<EV::GUI> m_GUI = nullptr;

	std::shared_ptr<LandscapePSO> m_landscapePSO;

	EV::RenderTarget m_renderTarget = {};

	std::shared_ptr<Texture> m_colorTexture;

	D3D12_VIEWPORT m_viewport = {};
	D3D12_RECT m_scissorRect = {};

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

	EV::Camera m_camera;

	struct alignas(16) CameraData
	{
		DirectX::XMVECTOR m_initialPosition;
		DirectX::XMVECTOR m_initialRotation;
	};
	CameraData* m_pAlignedCameraData;
};

}
