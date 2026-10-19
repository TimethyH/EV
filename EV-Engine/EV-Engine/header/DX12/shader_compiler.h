#pragma once

/**
 *  @file shader_compiler.h
 *
 *  @brief Compiles HLSL files at runtime with DXC, the compiler the build uses for the .cso files.
 *
 *  dxcompiler.dll is not part of Windows, so it is loaded when the first shader is compiled: from the executable's
 *  folder (or the DLL search path), otherwise from the newest Windows SDK. The engine does not link against it,
 *  so applications run without it; only hot reloading needs it.
 */

#include <d3d12.h>
#include <dxcapi.h>
#include <wrl/client.h>

#include <filesystem>
#include <string>
#include <vector>

namespace EV
{
    class ShaderCompiler
    {
    public:
        ShaderCompiler() = default;
        ~ShaderCompiler();

        ShaderCompiler(const ShaderCompiler&) = delete;
        ShaderCompiler& operator=(const ShaderCompiler&) = delete;

        /**
         * Compiles the entry point of an HLSL file for a shader profile (e.g. L"ps_6_0"). #include paths are
         * resolved relative to the file. Returns false with the compiler output in errors when it fails.
         */
        bool Compile(const std::filesystem::path& sourcePath, const std::wstring& entryPoint, const std::wstring& target,
            Microsoft::WRL::ComPtr<ID3DBlob>& bytecode, std::string& errors);

        // Loads DXC if that was not tried yet. False when it could not be loaded (see GetStatus).
        bool Initialize();

        // The loaded compiler's path, why it could not be loaded, or that it is loaded on first use.
        const std::string& GetStatus() const { return m_status; }

    private:
        // Folders that may contain dxcompiler.dll and dxil.dll, in the order they are tried.
        static std::vector<std::filesystem::path> GetCompilerDirectories();

        bool m_initialized = false;
        std::string m_status = "Loaded when a shader is first recompiled";

        HMODULE m_dxilModule = nullptr;
        HMODULE m_compilerModule = nullptr;
        Microsoft::WRL::ComPtr<IDxcUtils> m_utils;
        Microsoft::WRL::ComPtr<IDxcCompiler3> m_compiler;
        Microsoft::WRL::ComPtr<IDxcIncludeHandler> m_includeHandler;
    };
}
