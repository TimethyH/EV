#include "landscape.h"

#include "core/EV.h"
#include "core/application.h"


using namespace EV;
using namespace DirectX;

Landscape::Landscape(const std::wstring& name, uint32_t width, uint32_t height, bool bVSync)
    : super(name, width, height, bVSync)
    , m_scissorRect(CD3DX12_RECT(0, 0, LONG_MAX, LONG_MAX))
    , m_viewport(CD3DX12_VIEWPORT(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)))
    , m_forward(0)
    , m_backward(0)
    , m_left(0)
    , m_right(0)
    , m_up(0)
    , m_down(0)
    , m_pitch(0)
    , m_yaw(0)
    , m_width(width)
    , m_height(height)
{
    m_pWindow = Application::Get().CreateRenderWindow(name, width, height);

    m_pWindow->Update += UpdateEvent::slot(&Landscape::OnUpdate, this);

    // Hookup Window callbacks.
    m_pWindow->Update += UpdateEvent::slot(&Landscape::OnUpdate, this);
    m_pWindow->Resize += ResizeEvent::slot(&Landscape::OnResize, this);
    m_pWindow->KeyPressed += KeyboardEvent::slot(&Landscape::OnKeyPress, this);
    m_pWindow->KeyReleased += KeyboardEvent::slot(&Landscape::OnKeyRelease, this);
    m_pWindow->MouseMoved += MouseMotionEvent::slot(&Landscape::OnMouseMove, this);

    XMVECTOR cameraPos = XMVectorSet(0, 5, -20, 1);
    XMVECTOR cameraTarget = XMVectorSet(0, 5, 0, 1);
    XMVECTOR cameraUp = XMVectorSet(0, 1, 0, 0);

    m_camera.SetLookAt(cameraPos, cameraTarget, cameraUp);

    m_pAlignedCameraData = (CameraData*)_aligned_malloc(sizeof(CameraData), 16);
    m_pAlignedCameraData->m_initialPosition = m_camera.GetTranslation();
    m_pAlignedCameraData->m_initialRotation = m_camera.GetRotation();

    // Init();     
}

Landscape::~Landscape()
{
	
}

bool Landscape::LoadContent()
{
    return 1;
}

void Landscape::UnloadContent()
{
}

void Landscape::OnUpdate(UpdateEventArgs& e)
{
	Game::OnUpdate(e);
}

void Landscape::OnRender()
{
	Game::OnRender();
}

void Landscape::OnGUI(const std::shared_ptr<EV::CommandList>& commandList, const EV::RenderTarget& renderTarget)
{

}

void Landscape::OnKeyPress(KeyEventArgs& e)
{
	Game::OnKeyPress(e);
}

void Landscape::OnKeyRelease(KeyEventArgs& e)
{
	Game::OnKeyRelease(e);
}

void Landscape::OnMouseMove(MouseMotionEventArgs& e)
{
	Game::OnMouseMove(e);
}

void Landscape::OnMouseWheel(MouseWheelEventArgs& e)
{
	Game::OnMouseWheel(e);
}

void Landscape::OnResize(ResizeEventArgs& e)
{
	Game::OnResize(e);
}

