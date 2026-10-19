#include "DX12/dx12_includes.h"

#include "DX12/shader.h"

#include <cstring>
#include <d3d12sdklayers.h>

#include "core/application.h"
#include "utility/helpers.h"

using namespace EV;

namespace
{
    constexpr uint32_t MakeFourCC(char a, char b, char c, char d)
    {
        return static_cast<uint32_t>(a) | (static_cast<uint32_t>(b) << 8) | (static_cast<uint32_t>(c) << 16) |
            (static_cast<uint32_t>(d) << 24);
    }

    /**
     * Reads the shader profile (e.g. L"ps_6_0") from a compiled shader, so a reload compiles it the same way the
     * build did. The program version is the first value of the DXIL part (DXC) or the shader code part (FXC);
     * both encode it as (type << 16) | (major << 4) | minor.
     */
    std::wstring ReadShaderTarget(ID3DBlob* blob)
    {
        const auto* data = static_cast<const uint8_t*>(blob->GetBufferPointer());
        const size_t size = blob->GetBufferSize();
        auto read32 = [&](size_t offset, uint32_t& value)
        {
            if (offset + sizeof(uint32_t) > size)
            {
                return false;
            }
            memcpy(&value, data + offset, sizeof(uint32_t));
            return true;
        };

        // Container header: fourCC, 16 byte digest, version, size, part count (at byte 28), then the part offsets.
        uint32_t fourCC = 0, partCount = 0;
        if (!read32(0, fourCC) || fourCC != MakeFourCC('D', 'X', 'B', 'C') || !read32(28, partCount))
        {
            return {};
        }

        for (uint32_t i = 0; i < partCount; ++i)
        {
            uint32_t partOffset = 0, partFourCC = 0, version = 0;
            if (!read32(32 + i * sizeof(uint32_t), partOffset) || !read32(partOffset, partFourCC))
            {
                return {};
            }
            if (partFourCC != MakeFourCC('D', 'X', 'I', 'L') && partFourCC != MakeFourCC('S', 'H', 'E', 'X') &&
                partFourCC != MakeFourCC('S', 'H', 'D', 'R'))
            {
                continue;
            }
            // Part header: fourCC and size, followed by the program version.
            if (!read32(partOffset + 8, version))
            {
                return {};
            }

            const wchar_t* stage = nullptr;
            switch (version >> 16)
            {
            case 0: stage = L"ps"; break;
            case 1: stage = L"vs"; break;
            case 2: stage = L"gs"; break;
            case 3: stage = L"hs"; break;
            case 4: stage = L"ds"; break;
            case 5: stage = L"cs"; break;
            case 13: stage = L"ms"; break;
            case 14: stage = L"as"; break;
            default: return {};
            }

            // The runtime compiler (DXC) only targets shader model 6 and up.
            uint32_t major = (version >> 4) & 0xF;
            uint32_t minor = version & 0xF;
            if (major < 6)
            {
                major = 6;
                minor = 0;
            }
            return std::wstring(stage) + L"_" + std::to_wstring(major) + L"_" + std::to_wstring(minor);
        }
        return {};
    }

    constexpr D3D12_MESSAGE_SEVERITY g_breakSeverities[] = {
        D3D12_MESSAGE_SEVERITY_CORRUPTION, D3D12_MESSAGE_SEVERITY_ERROR, D3D12_MESSAGE_SEVERITY_WARNING };

    /**
     * Turns off the debug layer's break on errors while a pipeline state is created from a recompiled shader, and
     * collects the debug layer's messages. A shader that compiles but no longer matches its root signature must not
     * end the process, and the messages explain what does not match.
     */
    class DebugLayerCapture
    {
    public:
        DebugLayerCapture()
        {
            if (FAILED(Application::Get().GetDevice().As(&m_infoQueue)))
            {
                return;
            }
            for (size_t i = 0; i < _countof(g_breakSeverities); ++i)
            {
                m_breakOnSeverity[i] = m_infoQueue->GetBreakOnSeverity(g_breakSeverities[i]);
                m_infoQueue->SetBreakOnSeverity(g_breakSeverities[i], FALSE);
            }
            m_infoQueue->ClearStoredMessages();
        }

        ~DebugLayerCapture()
        {
            if (!m_infoQueue)
            {
                return;
            }
            for (size_t i = 0; i < _countof(g_breakSeverities); ++i)
            {
                m_infoQueue->SetBreakOnSeverity(g_breakSeverities[i], m_breakOnSeverity[i]);
            }
        }

        DebugLayerCapture(const DebugLayerCapture&) = delete;
        DebugLayerCapture& operator=(const DebugLayerCapture&) = delete;

        // Warnings and errors reported since construction.
        std::string GetMessages() const
        {
            std::string messages;
            if (!m_infoQueue)
            {
                return messages;
            }
            const UINT64 count = m_infoQueue->GetNumStoredMessages();
            for (UINT64 i = 0; i < count; ++i)
            {
                SIZE_T length = 0;
                if (FAILED(m_infoQueue->GetMessage(i, nullptr, &length)) || length == 0)
                {
                    continue;
                }
                std::vector<uint64_t> storage((length + sizeof(uint64_t) - 1) / sizeof(uint64_t));
                auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
                if (SUCCEEDED(m_infoQueue->GetMessage(i, message, &length)) && message->Severity <= D3D12_MESSAGE_SEVERITY_WARNING)
                {
                    messages += message->pDescription;
                    messages += "\n";
                }
            }
            return messages;
        }

    private:
        Microsoft::WRL::ComPtr<ID3D12InfoQueue> m_infoQueue;
        BOOL m_breakOnSeverity[_countof(g_breakSeverities)] = {};
    };
}

// ------------------------------------------------------------------------------------------------
// Shader

Shader::Shader(const std::filesystem::path& compiledPath)
    : m_name(ConvertString(compiledPath.stem().wstring()))
    , m_compiledPath(compiledPath)
{
    ThrowIfFailed(D3DReadFileToBlob(compiledPath.c_str(), &m_bytecode));
    m_target = ReadShaderTarget(m_bytecode.Get());
}

D3D12_SHADER_BYTECODE Shader::GetBytecode() const
{
    return { m_bytecode->GetBufferPointer(), m_bytecode->GetBufferSize() };
}

std::vector<std::filesystem::path> Shader::GetWatchedFiles() const
{
    std::vector<std::filesystem::path> files;
    files.reserve(m_watchedFiles.size());
    for (const auto& file : m_watchedFiles)
    {
        files.push_back(file.path);
    }
    return files;
}

// ------------------------------------------------------------------------------------------------
// PipelineShaders

PipelineShaders::PipelineShaders(std::initializer_list<std::shared_ptr<Shader>> shaders)
{
    for (const auto& shader : shaders)
    {
        m_entries.push_back({ shader, shader->GetVersion() });
    }
}

void PipelineShaders::Update(const std::function<void()>& createPipelineState)
{
    std::vector<Shader*> recompiled;
    for (auto& entry : m_entries)
    {
        if (entry.version != entry.shader->GetVersion())
        {
            entry.version = entry.shader->GetVersion();
            recompiled.push_back(entry.shader.get());
        }
    }
    if (recompiled.empty())
    {
        return;
    }

    DebugLayerCapture capture;
    try
    {
        createPipelineState();
    }
    catch (const std::exception& e)
    {
        std::string error = "The shader compiled, but the pipeline state could not be created with it (does it still "
            "match the root signature and input layout?).\n" + std::string(e.what()) + "\n" + capture.GetMessages();
        OutputDebugStringA(error.c_str());
        for (Shader* shader : recompiled)
        {
            shader->m_error = error;
        }
    }
}
