#pragma once

// This file handles automatic linking of all dependencies
// Include this in your main EV.h header

// Automatically link the EV-Engine library itself
#if defined(_MSC_VER)  // Only for Microsoft Visual C++

    // Link EV-Engine library (Debug vs Release)
#ifdef _DEBUG
#pragma comment(lib, "EV-Engine.lib")
#else
#pragma comment(lib, "EV-Engine.lib")
#endif

// Windows/DirectX libraries
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "d3dcompiler.lib")

// Third-party libraries
#ifdef _DEBUG
#pragma comment(lib, "assimp-vc145-mtd.lib")  // Debug version
// #pragma comment(lib, "DirectXTexd.lib")  // Debug version
#pragma comment(lib, "Shlwapi.lib")  // Debug version
#pragma comment(lib, "zlibstaticd.lib")  // Debug version
#else
#pragma comment(lib, "assimp-vc145-mtd.lib")   // Release version
#pragma comment(lib, "DirectXTexd.lib")   // Release version
#pragma comment(lib, "zlibstaticd.lib")   // Release version
#endif

// Add other third-party libs here as needed
// #pragma comment(lib, "DirectXTex.lib")
// etc.

#endif // _MSC_VER



