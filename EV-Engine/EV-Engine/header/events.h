#pragma once

#include "key_codes.h"

class EventArgs
{
public:
	EventArgs() {}
};

class KeyEventArgs : public EventArgs
{
public:
	enum KeyState
	{
		Released = 0,
		Pressed = 1,
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