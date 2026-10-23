#include "core/EV.h"

#include <filesystem>
#include <string>

#include "birthday_game.h"

namespace fs = std::filesystem;

namespace
{
    fs::path ExecutableFolder()
    {
        wchar_t path[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        return fs::path(path).parent_path();
    }

    // The folder with assets/birthday: next to the .exe when the game is unzipped, EV-Engine/ while developing
    // (the .exe is then in x64/Debug/DadsBirthday).
    bool FindDataFolder(fs::path& folder)
    {
        std::error_code error;
        const fs::path executable = ExecutableFolder();
        const fs::path candidates[] = { fs::current_path(error), executable, executable.parent_path(), executable.parent_path().parent_path(),
            executable.parent_path().parent_path().parent_path() };
        for (const auto& candidate : candidates)
        {
            if (!candidate.empty() && fs::is_directory(candidate / "assets" / "birthday", error))
            {
                folder = candidate;
                return true;
            }
        }
        return false;
    }
}

int CALLBACK wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int)
{
    fs::path root;
    if (!FindDataFolder(root))
    {
        MessageBoxW(nullptr,
            L"The game's pictures and models were not found (the folder assets\\birthday).\n\n"
            L"Keep the game's folder together, or run download-birthday-assets.bat in EV-Engine first.",
            L"Happy Birthday", MB_ICONERROR);
        return 1;
    }
    // Every path in the game (assets/..., game/...) is relative to this folder.
    fs::current_path(root);

    BirthdayGame::Options options;
    std::error_code error;
    options.gameFolder = fs::is_directory(root / "game", error) ? root / "game" : root / "DadsBirthday" / "game";

    // --editor starts in the editor (F5 plays); --autopilot <folder> plays a fixed tour and saves screenshots there;
    // --export-models <folder> writes the models made in code as OBJ files there.
    int argumentCount = 0;
    if (LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount))
    {
        for (int i = 1; i < argumentCount; ++i)
        {
            const std::wstring argument = arguments[i];
            if (argument == L"--editor")
            {
                options.editor = true;
            }
            else if (argument == L"--autopilot" && i + 1 < argumentCount)
            {
                options.autopilotFolder = fs::absolute(arguments[++i], error);
            }
            else if (argument == L"--export-models" && i + 1 < argumentCount)
            {
                options.exportFolder = fs::absolute(arguments[++i], error);
            }
        }
        LocalFree(arguments);
    }

    int result = 0;
    EV::Application::Create(hInstance);
    try
    {
        auto game = std::make_shared<BirthdayGame>(L"Happy Birthday!", 1280, 720, options);
        result = EV::Application::Get().Run(game);
    }
    catch (const std::exception& e)
    {
        const std::string message = std::string("Something went wrong:\n\n") + e.what();
        MessageBoxA(nullptr, message.c_str(), "Happy Birthday", MB_ICONERROR);
        result = 1;
    }
    EV::Application::Destroy();
    return result;
}
