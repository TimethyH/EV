#define WIN32_LEAN_AND_MEAN
// #include <dxgi1_3.h>
#include <Windows.h>
#include <Shlwapi.h>

#include <landscape.h>

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
		std::shared_ptr<EV::Landscape> demo = std::make_shared<EV::Landscape>(L"EV Engine", 1920, 1080);
		retCode = EV::Application::Get().Run(demo);
	}
	EV::Application::Destroy();

	atexit(&ReportLiveObjects);

	return retCode;
}
