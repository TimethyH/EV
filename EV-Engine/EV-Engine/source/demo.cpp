#include "demo.h"
#include "demo.h"
#include "demo.h"

#include "application.h"
#include "command_queue.h"
#include "helpers.h"
#include "window.h"

#include "dx12_includes.h"
using namespace DirectX;

struct Matrices
{
    XMMATRIX ModelMatrix;
    XMMATRIX ViewMatrix;
    XMMATRIX InverseTransposeMVP;
    XMMATRIX MVP;
};



struct VertexData
{
	XMFLOAT3 position;
	XMFLOAT3 color;
};

enum RootParameters
{
	MATRICES_CB,
    MATERIAL_CB,
    LIGHT_PROPERTIES_CB,
    POINTLIGHTS,
    SPOTLIGHTS,
    TEXTURES,
    NUM_PARAMETERS
};

XMMATRIX XM_CALLCONV LookAtMatrix(FXMVECTOR position, FXMVECTOR direction, FXMVECTOR up)
{
    assert(!XMVector3Equal(direction, XMVectorZero()));
    assert(!XMVector3IsInfinite(direction));
    assert(!XMVector3Equal(up, XMVectorZero()));
    assert(!XMVector3IsInfinite(up));


    XMVECTOR zAxis = XMVector3Normalize(direction);
    XMVECTOR xAxis = XMVector3Cross(up, zAxis);
    xAxis = XMVector3Normalize(xAxis);

    XMVECTOR yAxis = XMVector3Cross(zAxis, xAxis);
    yAxis = XMVector3Normalize(yAxis);

    XMMATRIX matrix(xAxis, yAxis, zAxis, position);
    return matrix;
}


// static VertexData g_Vertices[8] = {
//     { XMFLOAT3(-1.0f, -1.0f, -1.0f), XMFLOAT3(0.0f, 0.0f, 0.0f) }, // 0
//     { XMFLOAT3(-1.0f,  1.0f, -1.0f), XMFLOAT3(0.0f, 1.0f, 0.0f) }, // 1
//     { XMFLOAT3(1.0f,  1.0f, -1.0f), XMFLOAT3(1.0f, 1.0f, 0.0f) }, // 2
//     { XMFLOAT3(1.0f, -1.0f, -1.0f), XMFLOAT3(1.0f, 0.0f, 0.0f) }, // 3
//     { XMFLOAT3(-1.0f, -1.0f,  1.0f), XMFLOAT3(0.0f, 0.0f, 1.0f) }, // 4
//     { XMFLOAT3(-1.0f,  1.0f,  1.0f), XMFLOAT3(0.0f, 1.0f, 1.0f) }, // 5
//     { XMFLOAT3(1.0f,  1.0f,  1.0f), XMFLOAT3(1.0f, 1.0f, 1.0f) }, // 6
//     { XMFLOAT3(1.0f, -1.0f,  1.0f), XMFLOAT3(1.0f, 0.0f, 1.0f) }  // 7
// };
//
// static WORD g_Indicies[36] =
// {
//     0, 1, 2, 0, 2, 3,
//     4, 6, 5, 4, 7, 6,
//     4, 5, 1, 4, 1, 0,
//     3, 2, 6, 3, 6, 7,
//     1, 5, 6, 1, 6, 2,
//     4, 0, 3, 4, 3, 7
// };

Demo::Demo(const std::wstring& name, uint32_t width, uint32_t height, bool bVSync)
    : super(name, width, height, bVSync)
    , m_scissorRect(CD3DX12_RECT(0, 0, LONG_MAX, LONG_MAX))
    , m_viewport(CD3DX12_VIEWPORT(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)))
    , m_forward(0)
    , m_backward(0)
    , m_left(0)
    , m_right(0)
    , m_up(0)
	, m_down(0)
	, m_pitch(0)
	, m_yaw(0)
	, m_width(0)
	, m_height(0)
    , m_contentLoaded(false)
{
    XMVECTOR cameraPos = XMVectorSet(0, 5, -20, 1);
    XMVECTOR cameraTarget = XMVectorSet(0, 5, 0, 1);
    XMVECTOR cameraUp = XMVectorSet(0, 1, 0, 0);

    m_camera.SetLookAt(cameraPos, cameraTarget, cameraUp);

    m_pAlignedCameraData = (CameraData*)_aligned_malloc(sizeof(CameraData), 16);
    m_pAlignedCameraData->m_initialPosition = m_camera.GetTranslation();
    m_pAlignedCameraData->m_initialRotation = m_camera.GetRotation();
}

Demo::~Demo()
{
    _aligned_free(m_pAlignedCameraData);
}

void Demo::UpdateBufferResource(Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2> pCommandList, ID3D12Resource** pDestination, ID3D12Resource** pIntermediate, size_t numElements, size_t elementSize, const void* pBufferData, D3D12_RESOURCE_FLAGS flags)
{
    auto device = Application::Get().GetDevice();

    size_t bufferSize = numElements * elementSize;

    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
    CD3DX12_RESOURCE_DESC resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize, flags);
    ThrowIfFailed(device->CreateCommittedResource(&heapProps,
                                                  D3D12_HEAP_FLAG_NONE,
                                                  &resourceDesc,
                                                  D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(pDestination)));
    if (pBufferData)
    {
        CD3DX12_HEAP_PROPERTIES uploadProp(D3D12_HEAP_TYPE_UPLOAD);
        CD3DX12_RESOURCE_DESC uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize);
	    ThrowIfFailed(device->CreateCommittedResource(&uploadProp, 
            D3D12_HEAP_FLAG_NONE, 
            &uploadDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, // when heap_type is upload this flag NEEDS to be generic_read
            nullptr,
            IID_PPV_ARGS(pIntermediate)));
    }

    D3D12_SUBRESOURCE_DATA subData = {};
    subData.pData = pBufferData;
    subData.RowPitch = bufferSize;
    subData.SlicePitch = subData.RowPitch;

    UpdateSubresources(pCommandList.Get(), *pDestination, *pIntermediate, 0, 0, 1, &subData);

}

void Demo::ResizeDepthBuffer(uint32_t width, uint32_t height)
{
    if (m_contentLoaded)
    {
	    // flush the gpu to make sure the depth buffer is not being referenced in any command queue
        Application::Get().Flush();

        width = std::max(1u, width);
        height = std::max(1u, height);

        auto device = Application::Get().GetDevice();

        D3D12_CLEAR_VALUE optimizedClearValue = {};
        optimizedClearValue.Format = DXGI_FORMAT_D32_FLOAT;
        optimizedClearValue.DepthStencil = { 1.0f, 0 };

        CD3DX12_HEAP_PROPERTIES depthProperty(D3D12_HEAP_TYPE_DEFAULT);
        CD3DX12_RESOURCE_DESC depthDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_D32_FLOAT, width, height, 1, 0, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
        ThrowIfFailed(device->CreateCommittedResource(&depthProperty, D3D12_HEAP_FLAG_NONE, &depthDesc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &optimizedClearValue, IID_PPV_ARGS(&m_depthBuffer)));

        // Update the depth-stencil view.
        D3D12_DEPTH_STENCIL_VIEW_DESC dsv = {};
        dsv.Format = DXGI_FORMAT_D32_FLOAT;
        dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        dsv.Texture2D.MipSlice = 0;
        dsv.Flags = D3D12_DSV_FLAG_NONE;

        device->CreateDepthStencilView(m_depthBuffer.Get(), &dsv, m_depthDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
    }
}

void Demo::OnResize(ResizeEventArgs& e)
{
    if (e.windowWidth != GetScreenWidth() || e.windowHeight!= GetScreenHeight())
    {
        super::OnResize(e);

        m_viewport = CD3DX12_VIEWPORT(0.0f, 0.0f,
            static_cast<float>(e.windowWidth), static_cast<float>(e.windowHeight));

        ResizeDepthBuffer(e.windowWidth, e.windowHeight);
    }
}

void Demo::OnUpdate(UpdateEventArgs& e)
{
    static uint64_t frameCount = 0;
    static double totalTime = 0.0;

    super::OnUpdate(e);

    totalTime += e.elapsedTime;
    frameCount++;

    if (totalTime > 1.0)
    {
        double fps = frameCount / totalTime;

        char buffer[512];
        sprintf_s(buffer, "FPS: %f\n", fps);
        OutputDebugStringA(buffer);

        frameCount = 0;
        totalTime = 0.0;
    }

    // Update Model Matrix
    float angle = static_cast<float>(e.totalTime);
    const XMVECTOR rotationAxis = XMVectorSet(0, 1, 1, 0);
    m_model = XMMatrixRotationAxis(rotationAxis, angle);

    // Update View Matrix
    const XMVECTOR eyePosition = XMVectorSet(0, 0, -10, 1);
    const XMVECTOR focusPoint = XMVectorSet(0, 0, 0, 1);
    const XMVECTOR upDirection = XMVectorSet(0, 1, 0, 0);
    m_view = XMMatrixLookAtLH(eyePosition, focusPoint, upDirection);

    // Update Projection
    float aspecRatio = GetScreenWidth() / static_cast<float>(GetScreenHeight());
    m_projection = XMMatrixPerspectiveFovLH(XMConvertToRadians(m_fov), aspecRatio, 0.1f, 100.0f);

}

void Demo::TransitionResource(Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2> pCommandList, Microsoft::WRL::ComPtr<ID3D12Resource> pResource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
    CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        pResource.Get(),
        before, after);

    pCommandList->ResourceBarrier(1, &barrier);
}

void Demo::ClearDepth(Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2> pCommandList, D3D12_CPU_DESCRIPTOR_HANDLE DSV, FLOAT depth)
{
	pCommandList->ClearDepthStencilView(DSV, D3D12_CLEAR_FLAG_DEPTH, depth, 0, 0, nullptr);	
}

void Demo::ClearRTV(Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2> pCommandList, D3D12_CPU_DESCRIPTOR_HANDLE RTV, FLOAT* clearColor)
{
    pCommandList->ClearRenderTargetView(RTV, clearColor, 0, nullptr);
}

void Demo::OnRender(RenderEventArgs& e)
{
    super::OnRender(e);

    auto commandQueue = Application::Get().GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT);
    auto commandList = commandQueue->GetCommandList();

    UINT currentBackBufferID = m_pWindow->GetCurrentBackBufferID();
    auto backbuffer = m_pWindow->GetCurrentBackBuffer();
    auto rtv = m_pWindow->GetCurrentRTV();
    auto dsv = m_depthDescriptorHeap->GetCPUDescriptorHandleForHeapStart();

    // Clear the render targets.
    {
        TransitionResource(commandList, backbuffer,
            D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

        FLOAT clearColor[] = { 0.4f, 0.6f, 0.9f, 1.0f };

        ClearRTV(commandList, rtv, clearColor);
        ClearDepth(commandList, dsv);
    }

    // prepare rendering pipeline
    commandList->SetPipelineState(m_pipelineState.Get());
    commandList->SetGraphicsRootSignature(m_rootSignature.Get());

    // Setup Input Assembler

    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->IASetIndexBuffer(&m_indexBufferView);
    commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);

    // Setup Rastertizer state (RS)
    commandList->RSSetViewports(1, &m_viewport);
    commandList->RSSetScissorRects(1, &m_scissorRect);

    // Bind Render Targets to Output Merger (OM)
    commandList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

    // Update Root Parameters

    // MVP
    XMMATRIX mvp = XMMatrixMultiply(m_model, m_view);
    mvp = XMMatrixMultiply(mvp, m_projection);
    commandList->SetGraphicsRoot32BitConstants(0, sizeof(XMMATRIX) / 4, &mvp, 0);


    // Draw !!!!!

    commandList->DrawIndexedInstanced(_countof(g_Indicies), 1, 0, 0, 0);

    // Present the rtv

    {
        TransitionResource(commandList, backbuffer,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);

        m_fenceValues[currentBackBufferID] = commandQueue->ExecuteCommandList(commandList);

        currentBackBufferID = m_pWindow->Present();

        commandQueue->WaitForFenceValue(m_fenceValues[currentBackBufferID]);
    }
}

void Demo::OnKeyPress(KeyEventArgs& e)
{
    super::OnKeyPress(e);

    switch (e.key)
    {
    case KeyCode::Escape:
        Application::Get().Quit(0);
        break;
    case KeyCode::Enter:
        if (e.alt)
        {
    case KeyCode::F11:
        m_pWindow->ToggleFullScreen();
        break;
        }
    case KeyCode::V:
        m_pWindow->ToggleVSync();
        break;
    }
}

void Demo::OnMouseMove(MouseMotionEventArgs& e)
{
	Game::OnMouseMove(e);
}

void Demo::OnMouseWheel(MouseWheelEventArgs& e)
{
    m_fov -= e.wheelDelta;
    m_fov = std::clamp(m_fov, 12.0f, 90.0f);

    char buffer[256];
    sprintf_s(buffer, "FoV: %f\n", m_fov);
    OutputDebugStringA(buffer);
}

struct PipelineStateStream
{
    CD3DX12_PIPELINE_STATE_STREAM_INPUT_LAYOUT inputLayout;
    CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE rootSignature;
    CD3DX12_PIPELINE_STATE_STREAM_VS vertexShader;
    CD3DX12_PIPELINE_STATE_STREAM_PS pixelShader;
    CD3DX12_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY primitiveTopologyType;
    CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL_FORMAT depthFormat;
    CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS renderTargetFormats;
} pipelineStateStream;


bool Demo::LoadContent()
{
    auto device = Application::Get().GetDevice();
    auto commandQueue = Application::Get().GetCommandQueue(D3D12_COMMAND_LIST_TYPE_COPY);
    auto commandList = commandQueue->GetCommandList();
    
    m_cubeMesh = Mesh::CreateCube(commandList);
    commandList->


	// Upload vertex data
	ComPtr<ID3D12Resource> intermediateVertexBuffer;
    UpdateBufferResource(commandList.Get(), &m_vertexBuffer, &intermediateVertexBuffer, _countof(g_Vertices),sizeof(VertexData), g_Vertices);

    m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
    m_vertexBufferView.SizeInBytes = sizeof(g_Vertices);
    m_vertexBufferView.StrideInBytes = sizeof(VertexData);

    // Upload index data
    ComPtr<ID3D12Resource> intermediataIndexBuffer;
    UpdateBufferResource(commandList.Get(), &m_indexBuffer, &intermediataIndexBuffer, _countof(g_Indicies), sizeof(WORD), g_Indicies);

    m_indexBufferView.BufferLocation = m_indexBuffer->GetGPUVirtualAddress();
    m_indexBufferView.Format = DXGI_FORMAT_R16_UINT;
    m_indexBufferView.SizeInBytes = sizeof(g_Indicies);

    // Create a descriptor heap for depth stencil view
    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
    dsvHeapDesc.NumDescriptors = 1;
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ThrowIfFailed(device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_depthDescriptorHeap)));

    // Load Vertex Shader
    ComPtr<ID3DBlob> vertexShaderBlob;
    ThrowIfFailed(D3DReadFileToBlob(L"vertex.cso", &vertexShaderBlob));

    // Load Pixel Shader
    ComPtr<ID3DBlob> pixelShaderBlob;
    ThrowIfFailed(D3DReadFileToBlob(L"pixel.cso", &pixelShaderBlob));

    // Create Vertex input layout
    D3D12_INPUT_ELEMENT_DESC inputLayout[] =
    {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    // Root Signature
    D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};
    featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
    if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
    {
        featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
    }

    D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags = {
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS
    };

    CD3DX12_ROOT_PARAMETER1 rootParameters[1];
    rootParameters[0].InitAsConstants(sizeof(XMMATRIX) / 4, 0, 0, D3D12_SHADER_VISIBILITY_VERTEX);


    // Root Signature Description

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
    rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters, 0, nullptr, rootSignatureFlags);


    // Serialize root signature
    ComPtr<ID3DBlob> rootSignatureBlob;
    ComPtr<ID3DBlob> errorBlob;

    ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &rootSignatureBlob, &errorBlob));

    // Create RootSignature
    ThrowIfFailed(device->CreateRootSignature(0, rootSignatureBlob->GetBufferPointer(), rootSignatureBlob->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)));

    D3D12_RT_FORMAT_ARRAY rtvFormats = {};
    rtvFormats.NumRenderTargets = 1;
    rtvFormats.RTFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;

    // initialize pipeline state stream
    pipelineStateStream.rootSignature = m_rootSignature.Get();
    pipelineStateStream.inputLayout = { inputLayout, _countof(inputLayout) };
    pipelineStateStream.primitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pipelineStateStream.vertexShader = CD3DX12_SHADER_BYTECODE(vertexShaderBlob.Get());
    pipelineStateStream.pixelShader = CD3DX12_SHADER_BYTECODE(pixelShaderBlob.Get());
    pipelineStateStream.depthFormat = DXGI_FORMAT_D32_FLOAT;
    pipelineStateStream.renderTargetFormats = rtvFormats;

    // using the pipeline state stream, we create the pipeline state object.
    D3D12_PIPELINE_STATE_STREAM_DESC pipelineStateStreamDesc = { sizeof(PipelineStateStream), &pipelineStateStream };
    ThrowIfFailed(device->CreatePipelineState(&pipelineStateStreamDesc, IID_PPV_ARGS(&m_pipelineState)));


    auto fenceValue = commandQueue->ExecuteCommandList(commandList);
    commandQueue->WaitForFenceValue(fenceValue);

    m_contentLoaded = true;

    // now that depth descriptorheap was created, we can create/resize the depth bufffer
    ResizeDepthBuffer(GetScreenWidth(), GetScreenHeight());

    return true;
}

void Demo::UnloadContent()
{

}





