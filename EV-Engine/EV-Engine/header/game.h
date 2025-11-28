#pragma once

#include <events.h>
#include <memory>
#include <string>

class Window;

class Game : public std::enable_shared_from_this<Game>
{
public:
	Game(const std::wstring& name, int width, int height, bool bVSync);
	virtual ~Game();

	int GetScreenWidth() const { return m_width; }
	int GetScreenHeight() const { return m_height; }

	virtual bool Initialize();
	virtual bool LoadContent() = 0;
	virtual void UnloadContent() = 0;
	virtual void Destroy();

protected:
	friend class Window;
	
	virtual void OnUpdate(UpdateEventArgs& e);
	virtual void OnRender(RenderEventArgs& e);
	virtual void OnKeyPress(KeyEventArgs& e);
	virtual void OnKeyRelease(KeyEventArgs& e);
	virtual void OnMouseMove(MouseMotionEventArgs& e);
	virtual void OnMouseButtonPress(MouseButtonEventArgs& e);
	virtual void OnMouseButtonRelease(MouseButtonEventArgs& e);
	virtual void OnMouseWheel(MouseWheelEventArgs& e);
	virtual void OnResize(ResizeEventArgs& e);
	virtual void OnWindowDestroy();

	std::shared_ptr<Window> m_pWindow = nullptr;

private:
	std::wstring m_name = {};
	int m_width = 0;
	int m_height = 0;
	bool m_VSync = false;


};