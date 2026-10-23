// "Happy Birthday.exe" at the top of the packaged game's folder. It starts the game in the "files" folder next to
// it, so the folder shows only this file: the game needs its shaders and the Visual C++ runtime next to its own
// .exe. The launcher is linked with the static runtime, so it needs no DLLs itself. Arguments (such as --autopilot)
// are passed on, and it returns the game's exit code.

#include <windows.h>

#include <string>

namespace
{
    // The command line without the program name (which may be quoted).
    const wchar_t* SkipProgramName(const wchar_t* commandLine)
    {
        const wchar_t* c = commandLine;
        if (*c == L'"')
        {
            ++c;
            while (*c && *c != L'"')
            {
                ++c;
            }
            if (*c == L'"')
            {
                ++c;
            }
        }
        else
        {
            while (*c && *c != L' ' && *c != L'\t')
            {
                ++c;
            }
        }
        while (*c == L' ' || *c == L'\t')
        {
            ++c;
        }
        return c;
    }
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    wchar_t path[MAX_PATH];
    const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring folder(path, length);
    folder = folder.substr(0, folder.find_last_of(L"\\/")) + L"\\files";
    const std::wstring game = folder + L"\\DadsBirthday.exe";

    std::wstring commandLine = L"\"" + game + L"\"";
    const wchar_t* arguments = SkipProgramName(GetCommandLineW());
    if (*arguments)
    {
        commandLine += L" ";
        commandLine += arguments;
    }

    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process = {};
    if (!CreateProcessW(game.c_str(), commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr, folder.c_str(), &startup, &process))
    {
        const std::wstring message = L"The game could not be started:\n" + game +
            L"\n\nKeep the 'files' folder next to 'Happy Birthday.exe'.";
        MessageBoxW(nullptr, message.c_str(), L"Happy Birthday", MB_ICONERROR);
        return 1;
    }
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exitCode = 0;
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return static_cast<int>(exitCode);
}
