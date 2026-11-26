#pragma once
// agility_sdk.h – exports that make D3D12 load EV's own D3D12Core.dll (Agility SDK) from .\D3D12\ instead of System32.
// A macro, not an engine .cpp: the linker drops unreferenced objects from a static lib, so the exports must live in the
// exe. Expand EV_AGILITY_SDK_EXPORTS(version) once per executable, at file scope in main.cpp.

#include <Windows.h> // UINT

#define EV_AGILITY_SDK_EXPORTS(version) \
	extern "C" {__declspec(dllexport) extern const UINT D3D12SDKVersion = version; } \
	extern "C" {__declspec(dllexport) extern const char* D3D12SDKPath =  ".\\D3D12\\"; }
