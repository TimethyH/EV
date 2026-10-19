#include "DX12/dx12_includes.h"

#include "DX12/shader_library.h"

#include <fstream>

#include "DX12/shader_compiler.h"
#include "utility/helpers.h"

using namespace EV;

namespace
{
    // How often Update checks the source files' write times.
    constexpr std::chrono::milliseconds g_checkInterval(250);

    // Visual Studio's HLSL compiler settings use "main" unless a file sets another entry point.
    const wchar_t* g_entryPoint = L"main";

    // How many folders above the executable's folder are searched for shader sources.
    constexpr int g_sourceSearchDepth = 6;
}

ShaderLibrary::ShaderLibrary()
    : m_compiler(std::make_unique<ShaderCompiler>())
{
    wchar_t modulePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    const std::filesystem::path executable(modulePath);
    m_moduleDirectory = executable.parent_path();

    // Executables are built into <solution>/x64/Debug, the shaders are in <solution>/<project>/shaders.
    std::filesystem::path directory = m_moduleDirectory;
    for (int depth = 0; depth <= g_sourceSearchDepth && !directory.empty(); ++depth)
    {
        for (const auto& candidate : { directory / executable.stem() / L"shaders", directory / L"shaders" })
        {
            std::error_code error;
            if (std::filesystem::is_directory(candidate, error))
            {
                m_defaultSourceDirectories.push_back(candidate);
            }
        }
        if (directory == directory.parent_path())
        {
            break;
        }
        directory = directory.parent_path();
    }
}

ShaderLibrary::~ShaderLibrary() = default;

std::shared_ptr<Shader> ShaderLibrary::Load(const std::wstring& compiledPath)
{
    const std::filesystem::path path = std::filesystem::path(m_moduleDirectory.wstring() + compiledPath).lexically_normal();
    for (const auto& shader : m_shaders)
    {
        if (shader->GetCompiledPath() == path)
        {
            return shader;
        }
    }

    auto shader = std::make_shared<Shader>(path);
    FindSource(*shader);
    m_shaders.push_back(shader);
    return shader;
}

void ShaderLibrary::AddSourceDirectory(const std::filesystem::path& directory)
{
    m_sourceDirectories.push_back(std::filesystem::absolute(directory));
    for (const auto& shader : m_shaders)
    {
        FindSource(*shader);
    }
}

void ShaderLibrary::FindSource(Shader& shader) const
{
    const std::filesystem::path fileName = shader.GetCompiledPath().stem().wstring() + L".hlsl";
    for (const auto* directories : { &m_sourceDirectories, &m_defaultSourceDirectories })
    {
        for (const auto& directory : *directories)
        {
            std::error_code error;
            const auto candidate = directory / fileName;
            if (std::filesystem::is_regular_file(candidate, error))
            {
                if (shader.m_sourcePath != candidate)
                {
                    shader.m_sourcePath = candidate;
                    shader.m_watchedFiles = GetWatchedFiles(candidate);
                }
                return;
            }
        }
    }
}

std::vector<Shader::WatchedFile> ShaderLibrary::GetWatchedFiles(const std::filesystem::path& sourcePath)
{
    std::vector<Shader::WatchedFile> files;
    std::vector<std::filesystem::path> pending = { sourcePath };
    while (!pending.empty())
    {
        const std::filesystem::path path = pending.back();
        pending.pop_back();
        if (std::any_of(files.begin(), files.end(), [&](const Shader::WatchedFile& file) { return file.path == path; }))
        {
            continue;
        }

        // Includes that do not exist (e.g. inside a comment) are not watched.
        std::error_code error;
        const auto writeTime = std::filesystem::last_write_time(path, error);
        if (error)
        {
            continue;
        }
        files.push_back({ path, writeTime });

        // #include "file" is relative to the including file. <file> includes are not watched.
        std::ifstream stream(path);
        std::string line;
        while (std::getline(stream, line))
        {
            const size_t start = line.find_first_not_of(" \t");
            if (start == std::string::npos || line.compare(start, 8, "#include") != 0)
            {
                continue;
            }
            const size_t open = line.find('"', start + 8);
            const size_t close = open == std::string::npos ? std::string::npos : line.find('"', open + 1);
            if (close != std::string::npos)
            {
                pending.push_back((path.parent_path() / line.substr(open + 1, close - open - 1)).lexically_normal());
            }
        }
    }
    return files;
}

bool ShaderLibrary::HasChanged(const Shader& shader)
{
    for (const auto& file : shader.m_watchedFiles)
    {
        // A file that cannot be read right now (an editor replacing it) is checked again next time.
        std::error_code error;
        const auto writeTime = std::filesystem::last_write_time(file.path, error);
        if (!error && writeTime != file.writeTime)
        {
            return true;
        }
    }
    return false;
}

void ShaderLibrary::Update()
{
    const auto now = std::chrono::steady_clock::now();
    if (!m_autoReload || now < m_nextCheck)
    {
        return;
    }
    m_nextCheck = now + g_checkInterval;

    for (const auto& shader : m_shaders)
    {
        if (!shader->CanReload() || !HasChanged(*shader))
        {
            continue;
        }
        // Editors may still be writing the file; it is compiled once it can be opened.
        if (!std::ifstream(shader->GetSourcePath()).is_open())
        {
            continue;
        }
        Compile(*shader);
    }
}

void ShaderLibrary::Reload(Shader& shader)
{
    if (shader.CanReload())
    {
        Compile(shader);
    }
}

void ShaderLibrary::ReloadAll()
{
    for (const auto& shader : m_shaders)
    {
        Reload(*shader);
    }
}

const std::string& ShaderLibrary::GetCompilerStatus() const
{
    return m_compiler->GetStatus();
}

void ShaderLibrary::Compile(Shader& shader)
{
    // Watch the files as they are now, which also picks up #includes that were added or removed.
    shader.m_watchedFiles = GetWatchedFiles(shader.m_sourcePath);
    shader.m_reloadSerial = ++m_reloadCount;
    shader.m_reloadTime = std::chrono::steady_clock::now();

    Microsoft::WRL::ComPtr<ID3DBlob> bytecode;
    std::string output;
    if (m_compiler->Compile(shader.m_sourcePath, g_entryPoint, shader.m_target, bytecode, output))
    {
        shader.m_bytecode = bytecode;
        shader.m_error.clear();
        ++shader.m_version;
        OutputDebugStringA(("Reloaded shader " + shader.GetName() + "\n" + output).c_str());
    }
    else
    {
        shader.m_error = output;
        OutputDebugStringA(("Shader " + shader.GetName() + " failed to compile:\n" + output + "\n").c_str());
    }
}
