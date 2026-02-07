#define WIN32_LEAN_AND_MEAN
// #include <dxgi1_3.h>
#include <Windows.h>
#include <Shlwapi.h>

#include <ocean_scene.h>

#include <dxgidebug.h>
#include <memory>

#include "core/application.h"

void ReportLiveObjects()
{
	IDXGIDebug1* dxgiDebug;
	DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgiDebug));

	dxgiDebug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_IGNORE_INTERNAL);
	dxgiDebug->Release();
}

int CALLBACK wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR lpCmdLine, int nCmdShow)
{
	int retCode = 0;

	// // Set the working directory to the path of the executable.
	// WCHAR path[MAX_PATH];
	// HMODULE hModule = GetModuleHandleW(NULL);
	// if (GetModuleFileNameW(hModule, path, MAX_PATH) > 0)
	// {
	// 	PathRemoveFileSpecW(path);
	// 	SetCurrentDirectoryW(path);
	// }

	EV::Application::Create(hInstance);
	{
		std::shared_ptr<EV::Ocean> demo = std::make_shared<EV::Ocean>(L"EV Engine", 1920, 1080);
		retCode = EV::Application::Get().Run(demo);
	}
	EV::Application::Destroy();
	
	atexit(&ReportLiveObjects);

	return retCode;
}


////////////////////////////////////////////////////////////////////////////// TODOs /////////////////////////////////////////////////////////////
/*

												GENERAL

[ ] Get rid of Demo in the lib and all usages.
[ ] Understand ins and outs of the engine, how it works in its entirety. (Make a good mindmap)
[ ] Find all TODOs in the engine and adres them
[ ] Read the DX12 samples and understand how they do it.
[ ] Code cleanup. Remove all unused comments, structure the layout of demo, cleanup the class instantiation functions in Application.cpp (all in the top)
[ ] Structure project folders better, seperate files into these
[ ] use namespaces? like dx12lib. These are nice to know what is part of engine initialization.
[ ]
[ ]
[ ]
[ ]
[ ]
[ ]


												SPECIFIC

Scene
	[ ] Implement own model loader (Y2)
	[ ]
	[ ] Add transparant pass for foliage
	[ ]

Engine
	[ ] ECS oriented engine
	[ ] Add custom profiler. (Y3)
	[ ] Setup ImGUI in a way, so it can adjust metrics of the scene at runtime (so you dont have to reload for every iteration)
	[ ] Make model assets downloadable with a batchfile


Features
	[ ] PBR
	[ ] IBL
	[ ] Cascaded Shadowmaps
	[ ] Ocean rendering using FFT
	[ ] NormalMapping
	[ ]




*/
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////