#include "landscape.h"

#include "landscape_pso.h"
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
    auto& app = Application::Get();

    m_swapChain = app.CreateSwapchain(m_pWindow->GetWindowHandle(), DXGI_FORMAT_R8G8B8A8_UNORM);
    m_swapChain->SetVSync(false);

    m_GUI = app.CreateGUI(m_pWindow->GetWindowHandle(), m_swapChain->GetRenderTarget());
    app.wndProcHandler += WndProcEvent::slot(&GUI::WndProcHandler, m_GUI);

    m_landscapePSO = std::make_shared<LandscapePSO>(L"/canvas_VS.cso", L"/canvas_PS.cso");

    m_pWindow->RegisterCallbacks(shared_from_this());
    m_pWindow->Show();

    return true;
}

void Landscape::UnloadContent()
{
}

void Landscape::OnUpdate(UpdateEventArgs& e)
{
    OnRender();
}

void Landscape::OnRender()
{
    auto& commandQueue = Application::Get().GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT);
    auto commandList = commandQueue.GetCommandList();

    auto& swapChainRT = m_swapChain->GetRenderTarget();

    FLOAT clearColor[] = { 0.0f, 0.0f, 0.0f, 1.0f };
    commandList->ClearTexture(swapChainRT.GetTexture(AttachmentPoint::Color0), clearColor);

    commandList->SetViewport(m_viewport);
    commandList->SetScissorRect(m_scissorRect);
    commandList->SetRenderTarget(swapChainRT);

    m_landscapePSO->Apply(*commandList);

    OnGUI(commandList, swapChainRT);
    commandQueue.ExecuteCommandList(commandList);

    m_swapChain->Present();
}

void Landscape::OnGUI(const std::shared_ptr<EV::CommandList>& commandList, const EV::RenderTarget& renderTarget)
{
    m_GUI->NewFrame();
    m_GUI->Render(commandList, renderTarget);
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

