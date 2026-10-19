#include "DX12/dx12_includes.h"

#include "DX12/base_pso.h"

#include "core/application.h"

using namespace EV;

void BasePSO::LoadShaders(const std::wstring& vertexPath, const std::wstring& pixelPath)
{
    auto& app = Application::Get();
    m_vertexShader = app.LoadShader(vertexPath);
    m_pixelShader = app.LoadShader(pixelPath);
    m_pipelineShaders = PipelineShaders({ m_vertexShader, m_pixelShader });
}

void BasePSO::UpdatePipelineState()
{
    m_pipelineShaders.Update([this]() { CreatePipelineState(); });
}
