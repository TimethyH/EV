#include "DX12/dx12_includes.h"

#include "DX12/shader_compiler.h"

#include <algorithm>
#include <cstring>
#include <functional>

#include "utility/helpers.h"

// RegGetValueW, used to find the Windows SDK.
#pragma comment(lib, "Advapi32.lib")

using namespace EV;

ShaderCompiler::~ShaderCompiler()
{
    // The interfaces live in the DLLs, release them before unloading.
    m_includeHandler.Reset();
    m_compiler.Reset();
    m_utils.Reset();
    if (m_compilerModule)
    {
        FreeLibrary(m_compilerModule);
    }
    if (m_dxilModule)
    {
        FreeLibrary(m_dxilModule);
    }
}

std::vector<std::filesystem::path> ShaderCompiler::GetCompilerDirectories()
{
    std::vector<std::filesystem::path> directories;

    // Next to the executable.
    wchar_t modulePath[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, modulePath, MAX_PATH) > 0)
    {
        directories.push_back(std::filesystem::path(modulePath).parent_path());
    }

    // The Windows SDK's bin folder, newest version first. The build compiles the .cso files with the DXC from there.
#if defined(_M_ARM64)
    const wchar_t* architecture = L"arm64";
#elif defined(_M_X64)
    const wchar_t* architecture = L"x64";
#else
    const wchar_t* architecture = L"x86";
#endif

    std::vector<std::filesystem::path> kitRoots;
    for (DWORD view : { DWORD(RRF_SUBKEY_WOW6432KEY), DWORD(RRF_SUBKEY_WOW6464KEY) })
    {
        wchar_t root[MAX_PATH] = {};
        DWORD size = sizeof(root);
        if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows Kits\\Installed Roots", L"KitsRoot10",
            RRF_RT_REG_SZ | view, nullptr, root, &size) == ERROR_SUCCESS)
        {
            kitRoots.push_back(root);
        }
    }
    wchar_t programFiles[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"ProgramFiles(x86)", programFiles, MAX_PATH) > 0)
    {
        kitRoots.push_back(std::filesystem::path(programFiles) / L"Windows Kits" / L"10");
    }

    for (const auto& root : kitRoots)
    {
        std::error_code error;
        std::vector<std::filesystem::path> versions;
        for (const auto& entry : std::filesystem::directory_iterator(root / L"bin", error))
        {
            if (entry.is_directory(error) && entry.path().filename().wstring().starts_with(L"10."))
            {
                versions.push_back(entry.path());
            }
        }
        // Version folders all look like 10.0.xxxxx.x, so sorting the names sorts the versions.
        std::sort(versions.begin(), versions.end(), std::greater<>());
        for (const auto& version : versions)
        {
            if (std::filesystem::exists(version / architecture / L"dxcompiler.dll", error) &&
                std::find(directories.begin(), directories.end(), version / architecture) == directories.end())
            {
                directories.push_back(version / architecture);
            }
        }
    }

    // Last, the default DLL search order (which includes PATH). Tried last because other SDKs on the PATH may
    // ship dxcompiler.dll without dxil.dll, and D3D12 rejects the unsigned shaders that compiles.
    directories.push_back({});
    return directories;
}

bool ShaderCompiler::Initialize()
{
    if (m_initialized)
    {
        return m_compiler != nullptr;
    }
    m_initialized = true;

    for (const auto& directory : GetCompilerDirectories())
    {
        // dxcompiler.dll loads dxil.dll (which signs the compiled shaders) by name. Loading it first, from the same
        // folder, makes sure the matching version is used.
        HMODULE dxil = nullptr;
        HMODULE compiler = nullptr;
        if (directory.empty())
        {
            dxil = LoadLibraryW(L"dxil.dll");
            compiler = LoadLibraryW(L"dxcompiler.dll");
        }
        else
        {
            dxil = LoadLibraryExW((directory / L"dxil.dll").c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
            compiler = LoadLibraryExW((directory / L"dxcompiler.dll").c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        }

        auto createInstance = compiler ? reinterpret_cast<DxcCreateInstanceProc>(GetProcAddress(compiler, "DxcCreateInstance")) : nullptr;
        if (createInstance &&
            SUCCEEDED(createInstance(CLSID_DxcUtils, IID_PPV_ARGS(&m_utils))) &&
            SUCCEEDED(createInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&m_compiler))) &&
            SUCCEEDED(m_utils->CreateDefaultIncludeHandler(&m_includeHandler)))
        {
            m_dxilModule = dxil;
            m_compilerModule = compiler;

            wchar_t compilerPath[MAX_PATH] = {};
            GetModuleFileNameW(compiler, compilerPath, MAX_PATH);
            m_status = ConvertString(std::wstring(compilerPath));
            if (!dxil)
            {
                m_status += " (dxil.dll not found, recompiled shaders are not signed)";
            }
            return true;
        }

        m_includeHandler.Reset();
        m_compiler.Reset();
        m_utils.Reset();
        if (compiler)
        {
            FreeLibrary(compiler);
        }
        if (dxil)
        {
            FreeLibrary(dxil);
        }
    }

    m_status = "dxcompiler.dll was not found next to the executable or in a Windows SDK "
        "(Windows Kits\\10\\bin\\<version>\\x64), shaders cannot be recompiled";
    return false;
}

bool ShaderCompiler::Compile(const std::filesystem::path& sourcePath, const std::wstring& entryPoint,
    const std::wstring& target, Microsoft::WRL::ComPtr<ID3DBlob>& bytecode, std::string& errors)
{
    errors.clear();
    if (!Initialize())
    {
        errors = m_status;
        return false;
    }

    Microsoft::WRL::ComPtr<IDxcBlobEncoding> source;
    if (FAILED(m_utils->LoadFile(sourcePath.c_str(), nullptr, &source)))
    {
        errors = "Could not read " + ConvertString(sourcePath.wstring());
        return false;
    }
    DxcBuffer buffer = { source->GetBufferPointer(), source->GetBufferSize(), DXC_CP_ACP };

    // The same options as the build (Visual Studio's HLSL compiler settings), so a reload behaves like a rebuild.
    const std::wstring sourceName = sourcePath.wstring();
    const std::wstring includeDirectory = sourcePath.parent_path().wstring();
    std::vector<LPCWSTR> arguments = {
        sourceName.c_str(),
        L"-E", entryPoint.c_str(),
        L"-T", target.c_str(),
        L"-I", includeDirectory.c_str(),
#if defined(_DEBUG)
        L"-Zi", L"-Qembed_debug", L"-Od",
#endif
    };

    Microsoft::WRL::ComPtr<IDxcResult> result;
    HRESULT status = m_compiler->Compile(&buffer, arguments.data(), static_cast<UINT32>(arguments.size()),
        m_includeHandler.Get(), IID_PPV_ARGS(&result));
    if (SUCCEEDED(status))
    {
        result->GetStatus(&status);
    }

    // Errors on failure, warnings on success.
    Microsoft::WRL::ComPtr<IDxcBlobUtf8> output;
    if (result && SUCCEEDED(result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&output), nullptr)) && output &&
        output->GetStringLength() > 0)
    {
        errors.assign(output->GetStringPointer(), output->GetStringLength());
    }

    Microsoft::WRL::ComPtr<IDxcBlob> object;
    if (FAILED(status) || FAILED(result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&object), nullptr)) || !object ||
        object->GetBufferSize() == 0)
    {
        if (errors.empty())
        {
            char message[64];
            snprintf(message, sizeof(message), "Compilation failed (HRESULT 0x%08X)", static_cast<unsigned int>(status));
            errors = message;
        }
        return false;
    }

    // The pipeline state descriptions take ID3DBlob bytecode (like D3DReadFileToBlob gives for .cso files).
    if (FAILED(D3DCreateBlob(object->GetBufferSize(), &bytecode)))
    {
        errors = "Out of memory";
        return false;
    }
    memcpy(bytecode->GetBufferPointer(), object->GetBufferPointer(), object->GetBufferSize());
    return true;
}
