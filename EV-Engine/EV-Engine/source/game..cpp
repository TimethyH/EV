#include <dx12_includes.h>

#include <application.h>
#include <game.h>
#include <window.h>

Game::Game(const std::wstring& name, int width, int height, bool vSync)
    : m_name(name)
    , m_width(width)
    , m_height(height)
    , m_VSync(vSync)
{
}

Game::~Game()
{
    assert(!m_pWindow && "Use Game::Destroy() before destruction.");
}

bool Game::Initialize()
{
    // Check for DirectX Math library support.
    if (!DirectX::XMVerifyCPUSupport())
    {
        MessageBoxA(NULL, "Failed to verify DirectX Math library support.", "Error", MB_OK | MB_ICONERROR);
        return false;
    }

    m_pWindow = Application::Get().CreateRenderWindow(m_name, m_width, m_height, m_VSync);
    m_pWindow->RegisterCallbacks(shared_from_this());
    m_pWindow->Show();

    return true;
}

void Game::Destroy()
{
    Application::Get().DestroyWindow(m_pWindow);
    m_pWindow.reset();
}

void Game::OnUpdate(UpdateEventArgs& e)
{

}

void Game::OnRender(RenderEventArgs& e)
{

}

void Game::OnKeyPress(KeyEventArgs& e)
{
    // By default, do nothing.
}

void Game::OnKeyRelease(KeyEventArgs& e)
{
    // By default, do nothing.
}

void Game::OnMouseMove(class MouseMotionEventArgs& e)
{
    // By default, do nothing.
}

void Game::OnMouseButtonPress(MouseButtonEventArgs& e)
{
    // By default, do nothing.
}

void Game::OnMouseButtonRelease(MouseButtonEventArgs& e)
{
    // By default, do nothing.
}

void Game::OnMouseWheel(MouseWheelEventArgs& e)
{
    // By default, do nothing.
}

void Game::OnResize(ResizeEventArgs& e)
{
    m_width = e.windowWidth;
    m_height = e.windowHeight;
}

void Game::OnWindowDestroy()
{
    // If the Window which we are registered to is 
    // destroyed, then any resources which are associated 
    // to the window must be released.
    UnloadContent();
}

