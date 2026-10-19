#pragma once

/**
 *  @file shader_library.h
 *
 *  @brief Loads compiled shaders and hot reloads them when their HLSL source is saved.
 *
 *  The application owns one library (Application::GetShaderLibrary) and calls Update every frame. For each
 *  loaded .cso file the library looks for the .hlsl file it was built from, in a "<executable name>/shaders" or
 *  "shaders" folder next to or above the executable's folder (the layout of this repository's projects) and in
 *  folders added with AddSourceDirectory. When the source or a file it #includes changes, the shader is
 *  recompiled with the same profile; pipeline states pick up the new bytecode through PipelineShaders.
 *
 *  The .cso files on disk are not changed: the next build compiles the edited source as usual.
 */

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "DX12/shader.h"

namespace EV
{
    class ShaderCompiler;

    class ShaderLibrary
    {
    public:
        ShaderLibrary();
        ~ShaderLibrary();

        ShaderLibrary(const ShaderLibrary&) = delete;
        ShaderLibrary& operator=(const ShaderLibrary&) = delete;

        /**
         * Loads a compiled shader. The path is relative to the executable's folder (e.g. L"/vertex.cso"), like the
         * paths the PSOs take. Loading the same file again returns the same shader. Throws if the file is missing.
         */
        std::shared_ptr<Shader> Load(const std::wstring& compiledPath);

        // An extra folder to look for .hlsl sources in, searched before the default folders.
        void AddSourceDirectory(const std::filesystem::path& directory);

        // Recompiles the shaders whose source files changed. Checks the files a few times per second.
        void Update();

        // Recompile now, also when the source did not change.
        void Reload(Shader& shader);
        void ReloadAll();

        // Whether Update recompiles changed shaders (on by default).
        bool IsAutoReloadEnabled() const { return m_autoReload; }
        void SetAutoReload(bool enabled) { m_autoReload = enabled; }

        const std::vector<std::shared_ptr<Shader>>& GetShaders() const { return m_shaders; }

        // Increased by every recompile, successful or not. Shader::GetReloadSerial tells which shaders it was.
        uint64_t GetReloadCount() const { return m_reloadCount; }

        // The shader compiler's path, or why it could not be loaded.
        const std::string& GetCompilerStatus() const;

    private:
        void FindSource(Shader& shader) const;
        void Compile(Shader& shader);
        static bool HasChanged(const Shader& shader);

        // The source and the files it #includes (recursively), with their current write times.
        static std::vector<Shader::WatchedFile> GetWatchedFiles(const std::filesystem::path& sourcePath);

        std::unique_ptr<ShaderCompiler>      m_compiler;
        std::vector<std::shared_ptr<Shader>> m_shaders;
        std::filesystem::path                m_moduleDirectory;
        std::vector<std::filesystem::path>   m_sourceDirectories;  // Added with AddSourceDirectory.
        std::vector<std::filesystem::path>   m_defaultSourceDirectories;

        bool     m_autoReload = true;
        uint64_t m_reloadCount = 0;
        std::chrono::steady_clock::time_point m_nextCheck = {};
    };
}
