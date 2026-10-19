#pragma once

/**
 *  @file shader.h
 *
 *  @brief A compiled shader that is hot reloaded when its HLSL source changes.
 *
 *  Shaders are loaded through the ShaderLibrary (Application::LoadShader). The library watches the .hlsl file
 *  (and the files it includes) and recompiles the shader when one of them is saved. Pipeline states built from
 *  the shader use PipelineShaders to notice the new bytecode and build themselves again.
 */

#include <d3d12.h>
#include <wrl/client.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

namespace EV
{
    class Shader
    {
    public:
        // Loads a compiled shader (.cso). Throws if the file cannot be read.
        explicit Shader(const std::filesystem::path& compiledPath);

        // File name without extension, e.g. "ocean_pixel".
        const std::string& GetName() const { return m_name; }

        // The current bytecode: the .cso file, or the last successful hot reload.
        D3D12_SHADER_BYTECODE GetBytecode() const;

        const std::filesystem::path& GetCompiledPath() const { return m_compiledPath; }

        // The .hlsl file the shader is recompiled from, empty when it was not found.
        const std::filesystem::path& GetSourcePath() const { return m_sourcePath; }

        // Shader profile read from the compiled shader, e.g. L"ps_6_0". Empty when the bytecode is not recognized.
        const std::wstring& GetTarget() const { return m_target; }

        // Whether the shader can be recompiled (its source was found and its profile is known).
        bool CanReload() const { return !m_sourcePath.empty() && !m_target.empty(); }

        // Increased by every successful recompile.
        uint32_t GetVersion() const { return m_version; }

        // Why the last recompile (or rebuilding a pipeline state with it) failed, empty if it succeeded.
        const std::string& GetError() const { return m_error; }

        // The ShaderLibrary reload count when this shader was last recompiled, 0 if it never was.
        uint64_t GetReloadSerial() const { return m_reloadSerial; }
        std::chrono::steady_clock::time_point GetReloadTime() const { return m_reloadTime; }

        // Source and included files that trigger a recompile when they change.
        std::vector<std::filesystem::path> GetWatchedFiles() const;

    private:
        friend class ShaderLibrary;
        friend class PipelineShaders;

        struct WatchedFile
        {
            std::filesystem::path           path;
            std::filesystem::file_time_type writeTime;
        };

        std::string                      m_name;
        std::filesystem::path            m_compiledPath;
        std::filesystem::path            m_sourcePath;
        std::wstring                     m_target;
        Microsoft::WRL::ComPtr<ID3DBlob> m_bytecode;
        std::vector<WatchedFile>         m_watchedFiles;

        uint32_t                              m_version = 0;
        std::string                           m_error;
        uint64_t                              m_reloadSerial = 0;
        std::chrono::steady_clock::time_point m_reloadTime = {};
    };

    /**
     * The shaders a pipeline state is created from. Call Update before using the pipeline state: when one of the
     * shaders was recompiled since the last call, the pipeline state is created again with the new bytecode.
     */
    class PipelineShaders
    {
    public:
        PipelineShaders() = default;
        PipelineShaders(std::initializer_list<std::shared_ptr<Shader>> shaders);

        /**
         * Calls createPipelineState if a shader was recompiled since the last call. If creating the pipeline state
         * fails (for example because the new shader no longer matches the root signature), the error is shown on the
         * recompiled shaders and the pipeline state built from the previous bytecode stays in use.
         */
        void Update(const std::function<void()>& createPipelineState);

    private:
        struct Entry
        {
            std::shared_ptr<Shader> shader;
            uint32_t                version;
        };
        std::vector<Entry> m_entries;
    };
}
