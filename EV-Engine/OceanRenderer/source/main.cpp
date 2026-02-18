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

[ ] Cleanup engine.
[ ] Understand ins and outs of the engine, how it works in its entirety. (Make a good mindmap)
[ ] Find all TODOs in the engine and adres them
[ ] Code cleanup. Remove all unused comments, structure the layout of demo, cleanup the class instantiation functions in Application.cpp (all in the top)
[ ] Add ImGui for proper UI support.
[ ]
[ ]
[ ]
[ ]


												SPECIFIC

												
Ocean
	[ ] (Tesselation?) Make ocean detail close to camera higher resolition while further waves get less precision ( Or wave cascades, JumpTrajectory )
	[ ] Proper PBR water lighting
	[ ] Foam is incorrect. foam gets added even when the waves dont collapse into themselves
	[ ] Add tweakable parameters to ImGui
	[ ] Store gaussian randoms in a texture for GPU sampling. Move wave generation to compute (JONSWAP)
	[ ]
	[ ]
	[ ] Fully understand every component of the Ocean
	[ ] Make a blogpost
	[ ] Skybox for atmosphere.
	[ ] Make texture resolution DXGI_FORMAT_R16G16B16A16_FLOAT instead of 32bit
	[ ] 

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