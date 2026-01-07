#pragma once

#include "key_codes.h"
#include "signals.hpp"

/**
 * A Delegate holds function callbacks.
 */
namespace del
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
using Event = del::Delegate<void(EventArgs&)>;


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

class ResizeEventArgs : EventArgs
{
public:
	typedef EventArgs base;
	ResizeEventArgs(int width, int height)
		:windowWidth(width)
		,windowHeight(height)
	{}

	int windowWidth = 0;
	int windowHeight = 0;
};

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

using UpdateEvent = del::Delegate<void(UpdateEventArgs&)>;

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