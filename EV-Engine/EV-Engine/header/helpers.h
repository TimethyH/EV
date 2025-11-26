#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h> 

// min max macros conflict 
// use std::min and std::max
#ifdef min
#undef min
#endif

#ifdef max
#undef max
#endif

// To create a window, the windows macro needs to be undefined. (from Windows.h)
#ifdef CreateWindow
#undef CreateWindow
#endif

inline void ThrowIfFailed(HRESULT hr)
{
	if (FAILED(hr))
	{
		throw std::exception();
	}
}