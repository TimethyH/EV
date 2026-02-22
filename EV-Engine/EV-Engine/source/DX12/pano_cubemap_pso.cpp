#include "DX12/dx12_includes.h"

#include <DX12/pano_cubemap_pso.h>

#include <DX12/root_signature.h>

#include <d3dx12.h>
#include <Shlwapi.h>

#include "core/application.h"

// Compiled shader
// #include <PanoToCubemap_CS.h>

using namespace EV;

std::wstring GetModulePath()
{
    WCHAR buffer[MAX_PATH];
    GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    PathRemoveFileSpecW(buffer);
    return std::wstring(buffer);
}

PanoToCubemapPSO::PanoToCubemapPSO()
{
    auto& app = Application::Get();
    auto d3d12Device = app.GetDevice();

    CD3DX12_DESCRIPTOR_RANGE1 srcMip(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE);
    CD3DX12_DESCRIPTOR_RANGE1 outMip(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 5, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE);

    CD3DX12_ROOT_PARAMETER1 rootParameters[PanoToCubemapRS::NumRootParameters];
    rootParameters[PanoToCubemapRS::PanoToCubemapCB].InitAsConstants(sizeof(PanoToCubemapCB) / 4, 0);
    rootParameters[PanoToCubemapRS::SrcTexture].InitAsDescriptorTable(1, &srcMip);
    rootParameters[PanoToCubemapRS::DstMips].InitAsDescriptorTable(1, &outMip);

    CD3DX12_STATIC_SAMPLER_DESC linearRepeatSampler(0,
        D3D12_FILTER_MIN_MAG_MIP_LINEAR,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP
    );

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc(PanoToCubemapRS::NumRootParameters,
        rootParameters, 1, &linearRepeatSampler);

    m_rootSignature = app.CreateRootSignature(rootSignatureDesc.Desc_1_1);

    // Create the PSO for GenerateMips shader.
    struct PipelineStateStream
    {
        CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE pRootSignature;
        CD3DX12_PIPELINE_STATE_STREAM_CS CS;
    } pipelineStateStream;

    std::wstring parentPath = GetModulePath();
    Microsoft::WRL::ComPtr<ID3DBlob> csBlob;
    std::wstring path = parentPath + L"/pano_to_cubemap_CS.cso";
    ThrowIfFailed(D3DReadFileToBlob(path.c_str(), &csBlob));

    pipelineStateStream.pRootSignature = m_rootSignature->GetRootSignature().Get();
    pipelineStateStream.CS = CD3DX12_SHADER_BYTECODE(csBlob.Get());

    m_pipelineState = app.CreatePipelineStateObject(pipelineStateStream);

    // Create some default texture UAV's to pad any unused UAV's during mip map generation.
    m_defaultUAV = app.AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 5);
    UINT descriptorHandleIncrementSize = app.GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    for (UINT i = 0; i < 5; ++i)
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
        uavDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        uavDesc.Texture2DArray.ArraySize = 6; // Cubemap.
        uavDesc.Texture2DArray.FirstArraySlice = 0;
        uavDesc.Texture2DArray.MipSlice = i;
        uavDesc.Texture2DArray.PlaneSlice = 0;

        d3d12Device->CreateUnorderedAccessView(nullptr, nullptr, &uavDesc, m_defaultUAV.GetDescriptorHandle(i));
    }
}