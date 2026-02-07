#pragma once

#include "utility/key_codes.h"
#include "utility/signals.hpp"

/**
 * A Delegate holds function callbacks.
 */
namespace EV
{

	// Primary delegate template.
	template<typename Func>
	class Delegate;

	template<typename R, typename... Args>
	class Delegate<R(Args...)>
	{
	public:
		using slot = sig::slot<R(Args...)>;
		using signal = sig::signal<R(Args...)>;
		using connection = sig::connection;
		using scoped_connection = sig::scoped_connection;

		/**
	 * Add function callback to the delegate.
	 *
	 * @param f The function to add to the delegate.
	 * @returns The connection object that can be used to remove the callback
	 * from the delegate.
	 */
		template<typename Func>
		connection operator+=(Func&& f)
		{
			return m_Callbacks.connect(std::forward<Func>(f));
		}

		/**
	 * Remove a callback function from the delegate.
	 *
	 * @param f The function to remove from the delegate.
	 * @returns The number of callback functions removed.
	 */
		template<typename Func>
		std::size_t operator-=(Func&& f)
		{
			return m_Callbacks.disconnect(std::forward<Func>(f));
		}

		/**
	 * Invoke the delegate.
	 */
		opt::optional<R> operator()(Args... args)
		{
			return m_Callbacks(std::forward<Args>(args)...);
		}

	private:
		signal m_Callbacks;
	};
}


class EventArgs
{
public:
	EventArgs() {}
};

// Define the default event.
using Event = EV::Delegate<void(EventArgs&)>;


class KeyEventArgs : public EventArgs
{
public:
	enum KeyState
	{
		RELEASED = 0,
		PRESSED = 1,
	};

	typedef EventArgs base;
	KeyEventArgs(KeyCode::Key key_code, unsigned int c, KeyState key_state, bool bControl, bool bShift, bool bAlt)
		:key(key_code)
		, char_code(c)
		, state(key_state)
		, control(bControl)
		, shift(bShift)
		, alt(bAlt)
	{}

	KeyCode::Key key;
	unsigned int char_code = {};
	KeyState state = {};
	bool control = {};
	bool shift = {};
	bool alt = {};
};
using KeyboardEvent = EV::Delegate<void(KeyEventArgs&)>;

class MouseMotionEventArgs : public EventArgs
{
public: 
	typedef EventArgs base;
	MouseMotionEventArgs(bool bLeft, bool bRight, bool bMiddle, bool bControl, bool bShift, int mouseX, int mouseY)
		:leftButton(bLeft)
		,rightButton(bRight)
		,middleButton(bMiddle)
		,controlButton(bControl)
		,shiftButton(bShift)
		,xPosition(mouseX)
		,yPosition(mouseY)
	{}

	bool leftButton = false;
	bool rightButton = false;
	bool middleButton = false;
	bool controlButton = false;
	bool shiftButton = false;

	// these are relative to the top left corner of the screen
	int xPosition = 0;
	int yPosition = 0;
	// movement since last captured position 
	int deltaX = 0;
	int deltaY = 0;
};
using MouseMotionEvent = EV::Delegate<void(MouseMotionEventArgs&)>;


class MouseButtonEventArgs : EventArgs
{
public:
	typedef EventArgs base;
	enum MouseButton
	{
		NONE = 0,
		LEFT = 1,
		RIGHT = 2,
		MIDDLE = 3,
	};

	enum ButtonState
	{
		RELEASED = 0,
		PRESSED = 1
	};

	MouseButtonEventArgs(MouseButton buttonID, ButtonState buttonState, bool bLeft, bool bRight, bool bMiddle, bool bControl, bool bShift, int x, int y)
		:button(buttonID)
		,state(buttonState)
		,leftButton(bLeft)
		,rightButton(bRight)
		,middleButton(bMiddle)
		,controlButton(bControl)
		,shiftButton(bShift)
		,xPos(x)
		,yPos(y)
	{}

	MouseButton button = {};
	ButtonState state = RELEASED;
	bool leftButton = false;
	bool rightButton = false;
	bool middleButton = false;
	bool controlButton = false;
	bool shiftButton = false;

	// positions relative to upper left corner.
	int xPos = 0;
	int yPos = 0;
};
using MouseButtonEvent = EV::Delegate<void(MouseButtonEventArgs&)>;

class MouseWheelEventArgs : public EventArgs
{
public:
	typedef EventArgs base;
	MouseWheelEventArgs(float wheelDisplacement, bool bLeft, bool bRight, bool bMiddle, bool bControl, bool bShift, int x, int y)
		:wheelDelta(wheelDisplacement)
		, leftButton(bLeft)
		, rightButton(bRight)
		, middleButton(bMiddle)
		, controlButton(bControl)
		, shiftButton(bShift)
		, xPos(x)
		, yPos(y)
	{}

	float wheelDelta = 0.0f;
	bool leftButton = false;
	bool rightButton = false;
	bool middleButton = false;
	bool controlButton = false;
	bool shiftButton = false;

	// positions relative to upper left corner.
	int xPos = 0;
	int yPos = 0;
};
using MouseWheelEvent = EV::Delegate<void(MouseWheelEventArgs&)>;

enum class WindowState
{
	Restored = 0,  // The window has been resized.
	Minimized = 1,  // The window has been minimized.
	Maximized = 2,  // The window has been maximized.
};

/**
 * Event args to indicate the window has been resized.
 */
class ResizeEventArgs : public EventArgs
{
public:
	using base = EventArgs;

	ResizeEventArgs(int _width, int _height, WindowState _state)
		: width(_width)
		, height(_height)
		, state(_state)
	{
	}

	// The new width of the window
	int width;
	// The new height of the window.
	int height;
	// If the window was minimized or maximized.
	WindowState state;
};
using ResizeEvent = EV::Delegate<void(ResizeEventArgs&)>;

class UpdateEventArgs : EventArgs
{
public:
	typedef EventArgs base;
	UpdateEventArgs(double fDeltaTime, double fTotalTime)
		:deltaTime(fDeltaTime)
		,totalTime(fTotalTime)
	{}


	double deltaTime = 0.0;
	double totalTime = 0.0;
};

using UpdateEvent = EV::Delegate<void(UpdateEventArgs&)>;

class RenderEventArgs : EventArgs
{
public:
	typedef EventArgs base;
	RenderEventArgs(double fDeltaTime, double fTotalTime)
		:elapsedTime(fDeltaTime)
		, totalTime(fTotalTime)
	{
	}


	double elapsedTime = 0.0;
	double totalTime = 0.0;
};

class UserEventArgs : EventArgs
{
public:
	typedef EventArgs base;
	UserEventArgs(int user_code, void* user_data1, void* user_data2)
		:code(user_code)
		,data1(user_data1)
		,data2(user_data2)
	{}

	int code = 0;
	void* data1;
	void* data2;
};

/**
 * EventArgs for a WindowClosing event.
 */
class WindowCloseEventArgs : public EventArgs
{
public:
	using base = EventArgs;
	WindowCloseEventArgs()
		: confirmClose(true)
	{
	}

	/**
	 * The user can cancel a window closing operation by registering for the
	 * Window::Close event on the Window and setting the
	 * WindowCloseEventArgs::ConfirmClose property to false if the window should
	 * be kept open (for example, if closing the window would cause unsaved
	 * file changes to be lost).
	 */
	bool confirmClose;
};

using WindowCloseEvent = EV::Delegate<void(WindowCloseEventArgs&)>;