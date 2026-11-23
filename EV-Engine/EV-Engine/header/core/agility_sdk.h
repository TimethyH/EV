#pragma once
// ---------------------------------------------------------------------------------------------------------------------
// agility_sdk.h – exports that make D3D12 load EV's own D3D12Core.dll (DirectX 12 Agility SDK) instead of the one in
// System32. Path tracing milestone M0, decision D3. See docs/path-tracing/knowledge.md section 6.1.
//
// How it works: when d3d12.dll starts, it looks in the *executable* for two exported variables:
//   D3D12SDKVersion – the SDK version the app was built against (must match the DLL, e.g. 620)
//   D3D12SDKPath    – folder of D3D12Core.dll relative to the exe (".\\D3D12\\", trailing slash required)
// If either is missing, D3D12 silently falls back to the OS runtime.
//
// Why a macro and not a .cpp in the engine: EV-Engine is a static library. The linker only takes an object file out
// of a .lib when something references one of its symbols – nothing references these variables, so exports defined in
// the engine would be silently dropped. Expanding the macro in each app's main.cpp puts them in the exe's own object.
//
// Usage: expand EV_AGILITY_SDK_EXPORTS(<version>) exactly once per executable, at file scope (OceanRenderer and
// ShaderArt main.cpp, after the includes).
// ---------------------------------------------------------------------------------------------------------------------

#include <Windows.h> // UINT

// EV_AGILITY_SDK_EXPORTS(version)
// Defines and exports D3D12SDKVersion (= version) and D3D12SDKPath (= ".\\D3D12\\") from the executable.
#define EV_AGILITY_SDK_EXPORTS(version) \
	extern "C" {__declspec(dllexport) extern const UINT D3D12SDKVersion = version; } \
	extern "C" {__declspec(dllexport) extern const char* D3D12SDKPath =  ".\\D3D12\\"; }
