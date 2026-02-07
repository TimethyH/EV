#include "ocean_scene.h"

// #include "demo.h"

#include <iostream>
#include <Shlwapi.h>

// #include "core/application.h"
// #include "DX12/command_list.h"
// #include "DX12/command_queue.h"
// #include "utility/helpers.h"
// #include "core/window.h"
//
// #include "DX12/dx12_includes.h"
// #include "DX12/effect_pso.h"
// #include "resources/material.h"
// #include "DX12/render_target.h"
// #include "DX12/Scene.h"
// #include "DX12/scene_node.h"
// #include "DX12/scene_visitor.h"
// #include "DX12/swapchain.h"


#include "core/EV.h"

#include <DirectXColors.h>

using namespace DirectX;
using namespace EV;

// struct Matrices
// {
//     XMMATRIX ModelMatrix;
//     XMMATRIX ModelViewMatrix;
//     XMMATRIX InverseTransposeModelView;
//     XMMATRIX MVP;
// };
//
// struct VertexData
// {
// 	XMFLOAT3 position;
// 	XMFLOAT3 color;
// };

// enum RootParameters
// {
// 	MATRICES_CB,
//     MATERIAL_CB, // is in space 1, not sure why..
//     // LIGHT_PROPERTIES_CB,
//     // POINTLIGHTS,
//     // SPOTLIGHTS,
//     TEXTURES,
//     NUM_PARAMETERS
// };

Camera Ocean::m_camera; // Staticly defined in .h to get its position for the effectsPSO -> to shader

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

Ocean::Ocean(const std::wstring& name, uint32_t width, uint32_t height, bool bVSync)
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
    , m_width(width)
    , m_height(height)
    , m_vSync(bVSync)
    , m_contentLoaded(false)
{
    m_pWindow = Application::Get().CreateRenderWindow(name, width, height);

    m_pWindow->Update += UpdateEvent::slot(&Ocean::OnUpdate, this);

    // Hookup Window callbacks.
    m_pWindow->Update += UpdateEvent::slot(&Ocean::OnUpdate, this);
    m_pWindow->Resize += ResizeEvent::slot(&Ocean::OnResize, this);
    // m_pWindow->DPIScaleChanged += DPIScaleEvent::slot(&Tutorial5::OnDPIScaleChanged, this);
    m_pWindow->KeyPressed += KeyboardEvent::slot(&Ocean::OnKeyPress, this);
    m_pWindow->KeyReleased += KeyboardEvent::slot(&Ocean::OnKeyRelease, this);
    m_pWindow->MouseMoved += MouseMotionEvent::slot(&Ocean::OnMouseMove, this);

    XMVECTOR cameraPos = XMVectorSet(0, 5, -20, 1);
    XMVECTOR cameraTarget = XMVectorSet(0, 5, 0, 1);
    XMVECTOR cameraUp = XMVectorSet(0, 1, 0, 0);

    m_camera.SetLookAt(cameraPos, cameraTarget, cameraUp);

    m_pAlignedCameraData = (CameraData*)_aligned_malloc(sizeof(CameraData), 16);
    m_pAlignedCameraData->m_initialPosition = m_camera.GetTranslation();
    m_pAlignedCameraData->m_initialRotation = m_camera.GetRotation();
}

Ocean::~Ocean()
{
    _aligned_free(m_pAlignedCameraData);
}

void Ocean::UpdateBufferResource(Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2> pCommandList, ID3D12Resource** pDestination, ID3D12Resource** pIntermediate, size_t numElements, size_t elementSize, const void* pBufferData, D3D12_RESOURCE_FLAGS flags)
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

void Ocean::ResizeDepthBuffer(uint32_t width, uint32_t height)
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

void Ocean::OnResize(ResizeEventArgs& e)
{
    m_width = std::max(1, e.width);
    m_height = std::max(1, e.height);

    m_swapChain->Resize(m_width, m_height);

    float aspectRatio = m_width / (float)m_height;
    m_camera.SetProjection(45.0f, aspectRatio, 0.1f, 100.0f);

    m_viewport = CD3DX12_VIEWPORT(0.0f, 0.0f, static_cast<float>(m_width), static_cast<float>(m_height));

    m_renderTarget.Resize(m_width, m_height);
}

std::wstring Ocean::GetModulePath()
{
    WCHAR buffer[MAX_PATH];
    GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    PathRemoveFileSpecW(buffer);
    return std::wstring(buffer);
}

bool Ocean::LoadingProgress(float loadingProgress)
{
    m_loadingProgress = loadingProgress;

    // This function should return false to cancel the loading process.
    return !m_cancelLoading;
}

bool Ocean::LoadScene(const std::wstring& sceneFile)
{
    using namespace std::placeholders;  // For _1 used to denote a placeholder argument for std::bind.

    m_isLoading = true;
    m_cancelLoading = false;

    auto& commandQueue = Application::Get().GetCommandQueue(D3D12_COMMAND_LIST_TYPE_COPY);
    auto  commandList = commandQueue.GetCommandList();

    // Load a scene, passing an optional function object for receiving loading progress events.
    m_loadingText = std::string("Loading ") + ConvertString(sceneFile) + "...";
    auto scene = commandList->LoadSceneFromFile(sceneFile, std::bind(&Ocean::LoadingProgress, this, _1));

    if (scene)
    {
        // Scale the scene so it fits in the camera frustum.
        DirectX::BoundingSphere s;
        BoundingSphere::CreateFromBoundingBox(s, scene->GetAABB());
        auto scale = 50.0f / (s.Radius * 2.0f);
        s.Radius *= scale;

        scene->GetRootNode()->SetLocalTransform(XMMatrixScaling(scale, scale, scale));

        // Position the camera so that it is looking at the loaded scene.
        auto cameraRotation = m_camera.GetRotation();
        auto cameraFoV = m_camera.GetFov();
        auto distanceToObject = s.Radius / std::tanf(XMConvertToRadians(cameraFoV) / 2.0f);

        auto cameraPosition = XMVectorSet(0, 0, -distanceToObject, 1);
        //        cameraPosition      = XMVector3Rotate( cameraPosition, cameraRotation );
        auto focusPoint = XMVectorSet(s.Center.x * scale, s.Center.y * scale, s.Center.z * scale, 1.0f);
        cameraPosition = cameraPosition + focusPoint;

        // TODO: Make cam look a .
        // m_camera.SetTranslation(cameraPosition);
        // m_camera.SetLookAt(focusPoint);

        m_scene = scene;
    }

    commandQueue.ExecuteCommandList(commandList);

    // Ensure that the scene is completely loaded before rendering.
    commandQueue.Flush();

    // Loading is finished.
    m_isLoading = false;

    return scene != nullptr;
}

void Ocean::OnUpdate(UpdateEventArgs& e)
{
    static uint64_t frameCount = 0;
    static double totalTime = 0.0;

    // super::OnUpdate(e);

    totalTime += e.deltaTime;
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

    m_swapChain->WaitForSwapChain();

    // Update Camera
    float speedMultiplier = (m_shift ? 16.0f : 4.0f);
    XMVECTOR cameraTranslate = XMVectorSet(m_right - m_left, 0.0f, m_forward - m_backward, 1.0f) * speedMultiplier * static_cast<float>(e.deltaTime);
    XMVECTOR cameraPan = XMVectorSet(0.0f, m_up - m_down, 0.0f, 1.0f) * speedMultiplier * static_cast<float>(e.deltaTime);
    m_camera.Translate(cameraTranslate, Space::LOCAL);
    m_camera.Translate(cameraPan, Space::LOCAL);
    XMVECTOR cameraRotation = XMQuaternionRotationRollPitchYaw(XMConvertToRadians(m_pitch), XMConvertToRadians(m_yaw), 0.0f);
    m_camera.SetRotation(cameraRotation);

    XMMATRIX viewMatrix = m_camera.GetViewMatrix();

    const int numDirectionalLights = 3;

    static const XMVECTORF32 LightColors[] = { DirectX::Colors::White, Colors::OrangeRed, Colors::Blue };

    static float lightAnimTime = 0.0f;
    // if (m_AnimateLights)
    // {
    //     lightAnimTime += static_cast<float>(e.DeltaTime) * 0.5f * XM_PI;
    // }

    const float radius = 1.0f;
    float       directionalLightOffset = numDirectionalLights > 0 ? 2.0f * XM_PI / numDirectionalLights : 0;

    m_directionalLights.resize(numDirectionalLights);
    for (int i = 0; i < numDirectionalLights; ++i)
    {
        DirectionalLight& l = m_directionalLights[i];

        float angle = lightAnimTime + directionalLightOffset * i;

        XMVECTORF32 positionWS = { -15.0f,
                                   20.0f, -10.0f, 1.0f };

        XMVECTOR directionWS = XMVector3Normalize(XMVectorNegate(positionWS));
        XMVECTOR directionVS = XMVector3TransformNormal(directionWS, viewMatrix);

        XMStoreFloat4(&l.directionWS, directionWS);
        XMStoreFloat4(&l.directionVS, directionVS);

        l.color = XMFLOAT4(LightColors[i]);
    }

    m_unlitPSO->SetDirectionalLights(m_directionalLights);
    // m_lightingPSO->SetDirectionalLights(m_DirectionalLights);
    // m_decalPSO->SetDirectionalLights(m_DirectionalLights);

    float time = static_cast<float>(e.totalTime);

    // Initialize point lights BEFORE the scene visitor
    m_pointLights.resize(1);

    // Populate the point light with valid data
    PointLight& light = m_pointLights[0];
    light.positionWS = XMFLOAT4(sinf(time) * 5, 1.0F, 0.0f, 1.0f);
    light.color = XMFLOAT4(0.0f, 0.4f, 0.8f, 1.0f);   // White light for testing
    light.ambient = 0.1f;
    light.constantAttenuation = 1.0f;
    light.linearAttenuation = 0.0f;
    light.quadraticAttenuation = 0.0f;

    // Transform to view space
    XMVECTOR posWS = XMLoadFloat4(&light.positionWS);
    XMVECTOR posVS = XMVector3TransformCoord(posWS, viewMatrix);
    XMStoreFloat4(&light.positionVS, posVS);

    // Set lights ONCE before rendering
    m_unlitPSO->SetPointLights(m_pointLights);

    OnRender();

    //
    // // Update Model Matrix
    // float angle = static_cast<float>(e.totalTime);
    // const XMVECTOR rotationAxis = XMVectorSet(0, 1, 1, 0);
    // m_model = XMMatrixRotationAxis(rotationAxis, angle);
    //
    // // Update View Matrix
    // const XMVECTOR eyePosition = XMVectorSet(0, 0, -10, 1);
    // const XMVECTOR focusPoint = XMVectorSet(0, 0, 0, 1);
    // const XMVECTOR upDirection = XMVectorSet(0, 1, 0, 0);
    // m_view = XMMatrixLookAtLH(eyePosition, focusPoint, upDirection);
    //
    // // Update Projection
    // float aspecRatio = GetScreenWidth() / static_cast<float>(GetScreenHeight());
    // m_projection = XMMatrixPerspectiveFovLH(XMConvertToRadians(m_fov), aspecRatio, 0.1f, 100.0f);

}

// void XM_CALLCONV ComputeMatrices( FXMMATRIX model, CXMMATRIX view, CXMMATRIX viewProj, Matrices& mat)
// {
//     mat.ModelMatrix = model;
//     mat.ModelViewMatrix = model * view;
//     mat.InverseTransposeModelView = XMMatrixTranspose(XMMatrixInverse(nullptr, mat.ModelViewMatrix));
//     mat.MVP = model * viewProj;
// }

void Ocean::TransitionResource(Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2> pCommandList, Microsoft::WRL::ComPtr<ID3D12Resource> pResource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
    CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        pResource.Get(),
        before, after);

    pCommandList->ResourceBarrier(1, &barrier);
}

void Ocean::ClearDepth(Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2> pCommandList, D3D12_CPU_DESCRIPTOR_HANDLE DSV, FLOAT depth)
{
    pCommandList->ClearDepthStencilView(DSV, D3D12_CLEAR_FLAG_DEPTH, depth, 0, 0, nullptr);
}

void Ocean::ClearRTV(Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2> pCommandList, D3D12_CPU_DESCRIPTOR_HANDLE RTV, FLOAT* clearColor)
{
    pCommandList->ClearRenderTargetView(RTV, clearColor, 0, nullptr);
}

void Ocean::OnRender()
{
    // super::OnRender(e);

    auto& commandQueue = Application::Get().GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT);
    auto commandList = commandQueue.GetCommandList();

    const auto& renderTarget = m_isLoading ? m_swapChain->GetRenderTarget() : m_renderTarget;

    if (m_isLoading)
    {
        // Clear the render targets.
            // TransitionResource(commandList, backbuffer,
            //     D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

        FLOAT clearColor[] = { 0.9f, 0.2f, 0.4f, 1.0f };

        commandList->ClearTexture(renderTarget.GetTexture(AttachmentPoint::Color0), clearColor);
        // commandList->ClearDepthStencilTexture(renderTarget.GetTexture(AttachmentPoint::DepthStencil), D3D12_CLEAR_FLAG_DEPTH);

        // ClearRTV(commandList, rtv, clearColor);
        // ClearDepth(commandList, dsv);
    }
    else
    {
        SceneVisitor visitor(*commandList, m_camera, *m_unlitPSO, false);

        // Create a scene visitor that is used to perform the actual rendering of the meshes in the scenes.

        // UINT currentBackBufferID = m_pWindow->GetCurrentBackBufferID();
        // auto backbuffer = m_pWindow->GetCurrentBackBuffer();
        // auto rtv = m_pWindow->GetCurrentRTV();
        // auto dsv = m_depthDescriptorHeap->GetCPUDescriptorHandleForHeapStart();

         // Clear the render targets.
        {
            FLOAT clearColor[] = { 0.4f, 0.6f, 0.9f, 1.0f };

            commandList->ClearTexture(renderTarget.GetTexture(AttachmentPoint::Color0), clearColor);
            commandList->ClearDepthStencilTexture(renderTarget.GetTexture(AttachmentPoint::DepthStencil),
                D3D12_CLEAR_FLAG_DEPTH);
        }

        // commandList->SetPipelineState(m_pipelineState);
        // commandList->SetGraphicsRootSignature(m_rootSignature);

        commandList->SetViewport(m_viewport);
        commandList->SetScissorRect(m_scissorRect);
        commandList->SetRenderTarget(m_renderTarget);

        // REMINDER: Transform is built with Scale * Rotation * Translation (SRT)
        XMMATRIX scale = XMMatrixScaling(10.0f, 10.0f, 10.0f);
        XMMATRIX rotation = XMMatrixRotationRollPitchYaw(XMConvertToRadians(-90.0f), 0.0f, 0.0f);
        XMMATRIX translation = XMMatrixTranslation(0.0f, -12.0f, 0.0f);

        // m_scene->GetRootNode()->SetLocalTransform(scale * XMMatrixIdentity() * translation);
        m_scene->Accept(visitor);


        XMMATRIX helmetTranslation = XMMatrixTranslation(0.0f, 2.0f, 0.0f);
        m_helmet->GetRootNode()->SetLocalTransform(XMMatrixIdentity() * rotation * helmetTranslation);
        m_helmet->Accept(visitor);

        // m_chessboard->GetRootNode()->SetLocalTransform(XMMatrixIdentity() * XMMatrixIdentity() * translation);
        m_chessboard->Accept(visitor);

        // Visualize the point light as a small sphere
        for (const auto& l : m_pointLights)
        {
            auto lightPos = XMLoadFloat4(&l.positionWS);
            auto worldMatrix = XMMatrixTranslationFromVector(lightPos);
            m_sphere->GetRootNode()->SetLocalTransform(worldMatrix);
            m_sphere->Accept(visitor);
        }




        // TODO: Look into EffectsPSO. think this is what you're missing.

     //    // Draw Cube
     //    XMMATRIX translationMatrix = XMMatrixTranslation(4.0f, 4.0f, 4.0f);
     //    XMMATRIX rotationMatrix = XMMatrixIdentity();
     //    XMMATRIX scaleMatrix = XMMatrixScaling(4.0f, 4.0f, 4.0f);
     //    XMMATRIX worldMatrix = scaleMatrix * rotationMatrix * translationMatrix;
     //    XMMATRIX viewMatrix = m_camera.GetViewMatrix();
     //    XMMATRIX viewProjectionMatrix = viewMatrix * m_camera.GetProjectionMatrix();
     //
     //    Matrices cubeMatrix;
     //    ComputeMatrices(worldMatrix, viewMatrix, viewProjectionMatrix, cubeMatrix);
     //    
     //    commandList->SetGraphicsDynamicConstantBuffer(RootParameters::MATRICES_CB, cubeMatrix);
     //    commandList->SetGraphicsDynamicConstantBuffer(RootParameters::MATERIAL_CB, Material::White);
        // commandList->SetShaderResourceView(RootParameters::TEXTURES, 0, m_defaultTexture, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
     //
     //    m_cubeMesh->Accept(visitor);

        // Resolve the MSAA render target to the swapchain's backbuffer.
        auto& swapChainRT = m_swapChain->GetRenderTarget();
        auto  swapChainBackBuffer = swapChainRT.GetTexture(AttachmentPoint::Color0);
        auto  msaaRenderTarget = m_renderTarget.GetTexture(AttachmentPoint::Color0);

        commandList->ResolveSubresource(swapChainBackBuffer, msaaRenderTarget);

    }
    // Render the GUI on the render target
    OnGUI(commandList, m_swapChain->GetRenderTarget());

    commandQueue.ExecuteCommandList(commandList);

    // Present
    // m_pWindow->Present(m_renderTarget.GetTexture(AttachmentPoint::Color0));
    m_swapChain->Present();

    // // prepare rendering pipeline
    // commandList->SetPipelineState(m_pipelineState.Get());
    // commandList->SetGraphicsRootSignature(m_rootSignature.Get());
    //
    // // Setup Input Assembler
    //
    // commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    // commandList->IASetIndexBuffer(&m_indexBufferView);
    // commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
    //
    // // Setup Rastertizer state (RS)
    // commandList->RSSetViewports(1, &m_viewport);
    // commandList->RSSetScissorRects(1, &m_scissorRect);
    //
    // // Bind Render Targets to Output Merger (OM)
    // commandList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
    //
    // // Update Root Parameters
    //
    // // MVP
    // XMMATRIX mvp = XMMatrixMultiply(m_model, m_view);
    // mvp = XMMatrixMultiply(mvp, m_projection);
    // commandList->SetGraphicsRoot32BitConstants(0, sizeof(XMMATRIX) / 4, &mvp, 0);
    //
    //
    // // Draw !!!!!
    //
    // commandList->DrawIndexedInstanced(_countof(g_Indicies), 1, 0, 0, 0);
    //
    // // Present the rtv
    //
    // {
    //     TransitionResource(commandList, backbuffer,
    //         D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    //
    //     m_fenceValues[currentBackBufferID] = commandQueue->ExecuteCommandList(commandList);
    //
    //     currentBackBufferID = m_pWindow->Present();
    //
    //     commandQueue->WaitForFenceValue(m_fenceValues[currentBackBufferID]);
    // }
}

void Ocean::OnGUI(const std::shared_ptr<CommandList>& commandList, const RenderTarget& renderTarget)
{
    m_GUI->NewFrame();

    static bool showDemoWindow = false;
    if (showDemoWindow)
    {
        ImGui::ShowDemoWindow(&showDemoWindow);
    }

    m_GUI->Render(commandList, renderTarget);
}


void Ocean::OnKeyPress(KeyEventArgs& e)
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
    case KeyCode::R:
        // Reset camera transform
        m_camera.SetTranslation(m_pAlignedCameraData->m_initialPosition);
        m_camera.SetRotation(m_pAlignedCameraData->m_initialRotation);
        m_pitch = 0.0f;
        m_yaw = 0.0f;
        break;
    case KeyCode::Up:
    case KeyCode::W:
        // m_camera // TODO: switch to camera class movement.
        m_forward = 1.0f;
        break;
    case KeyCode::Left:
    case KeyCode::A:
        m_left = 1.0f;
        break;
    case KeyCode::Down:
    case KeyCode::S:
        m_backward = 1.0f;
        break;
    case KeyCode::Right:
    case KeyCode::D:
        m_right = 1.0f;
        break;
    case KeyCode::Q:
        m_down = 1.0f;
        break;
    case KeyCode::E:
        m_up = 1.0f;
        break;
    case KeyCode::Space:
        // m_animateLights = !m_AnimateLights;
        break;
    case KeyCode::ShiftKey:
        m_shift = true;
        break;
    }
}

void Ocean::OnKeyRelease(KeyEventArgs& e)
{
    super::OnKeyRelease(e);

    switch (e.key)
    {
    case KeyCode::Up:
    case KeyCode::W:
        m_forward = 0.0f;
        break;
    case KeyCode::Left:
    case KeyCode::A:
        m_left = 0.0f;
        break;
    case KeyCode::Down:
    case KeyCode::S:
        m_backward = 0.0f;
        break;
    case KeyCode::Right:
    case KeyCode::D:
        m_right = 0.0f;
        break;
    case KeyCode::Q:
        m_down = 0.0f;
        break;
    case KeyCode::E:
        m_up = 0.0f;
        break;
    case KeyCode::ShiftKey:
        m_shift = false;
        break;
    }
}

void Ocean::OnMouseMove(MouseMotionEventArgs& e)
{
    Game::OnMouseMove(e);

    const float mouseSpeed = 0.1f;

    if (!ImGui::GetIO().WantCaptureMouse)
    {
        if (e.leftButton)
        {
            m_pitch += e.deltaY * mouseSpeed;

            m_pitch = std::clamp(m_pitch, -90.0f, 90.0f);

            m_yaw += e.deltaX * mouseSpeed;
        }
    }
}

void Ocean::OnMouseWheel(MouseWheelEventArgs& e)
{
    if (!ImGui::GetIO().WantCaptureMouse)
    {
        auto fov = m_camera.GetFov();

        fov -= e.wheelDelta;
        fov = std::clamp(fov, 12.0f, 90.0f);

        m_camera.SetFov(fov);

        char buffer[256];
        sprintf_s(buffer, "FoV: %f\n", fov);
        OutputDebugStringA(buffer);
    }
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
    CD3DX12_PIPELINE_STATE_STREAM_SAMPLE_DESC sampleDesc;
} pipelineStateStream;


bool Ocean::LoadContent()
{
    auto& app = Application::Get();
    auto device = app.GetDevice();

    // Create Swapchain
    m_swapChain = app.CreateSwapchain(m_pWindow->GetWindowHandle(), DXGI_FORMAT_R8G8B8A8_UNORM);
    m_swapChain->SetVSync(m_vSync);

    m_GUI = app.CreateGUI(m_pWindow->GetWindowHandle(), m_swapChain->GetRenderTarget());

    // This magic here allows ImGui to process window messages.(TODO: not sure how this works yet)
    app.wndProcHandler += WndProcEvent::slot(&GUI::WndProcHandler, m_GUI);

    // Start the loading task to perform async loading of the scene file.
    m_loadingTask = std::async(std::launch::async, std::bind(&Ocean::LoadScene, this,
        L"assets/sponza/sponza_nobanner.obj"));



    auto& commandQueue = app.GetCommandQueue(D3D12_COMMAND_LIST_TYPE_COPY);
    auto commandList = commandQueue.GetCommandList();

    // m_cubeMesh = commandList->CreateCube();
    m_helmet = commandList->LoadSceneFromFile(L"assets/damaged_helmet/DamagedHelmet.gltf");
    m_chessboard = commandList->LoadSceneFromFile(L"assets/chess/ABeautifulGame.gltf");

    m_sphere = commandList->CreateSphere(0.1f);
    // m_sphere = commandList->LoadSceneFromFile(L"assets/damaged_helmet/DamagedHelmet.gltf");

    // m_defaultTexture = commandList->LoadTextureFromFile(L"assets/Mona_Lisa.jpg", true);

    auto fence = commandQueue.ExecuteCommandList(commandList);

    m_unlitPSO = std::make_shared<EffectPSO>(m_camera, false, false);

    DXGI_FORMAT backBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    DXGI_FORMAT depthBufferFormat = DXGI_FORMAT_D32_FLOAT;

    DXGI_SAMPLE_DESC sampleDesc = app.GetMultisampleQualityLevels(backBufferFormat, D3D12_MAX_MULTISAMPLE_SAMPLE_COUNT);

    // offscreen render target
    auto colorDesc = CD3DX12_RESOURCE_DESC::Tex2D(backBufferFormat, m_width, m_height, 1, 1, sampleDesc.Count, sampleDesc.Quality, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);

    D3D12_CLEAR_VALUE clearColor;
    clearColor.Format = colorDesc.Format;
    clearColor.Color[0] = 0.0f;
    clearColor.Color[1] = 0.0f;
    clearColor.Color[2] = 0.3f;
    clearColor.Color[3] = 1.0f;

    // Texture colorTexture = Texture(colorDesc, &clearColor, TextureUsage::RenderTarget, L"Color Render Target");
    auto colorTexture = Application::Get().CreateTexture(colorDesc, &clearColor);
    colorTexture->SetName(L"Color Render Target");

    auto depthDesc = CD3DX12_RESOURCE_DESC::Tex2D(depthBufferFormat, m_width, m_height, 1, 1, sampleDesc.Count, sampleDesc.Quality, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);

    D3D12_CLEAR_VALUE depthClearValue;
    depthClearValue.Format = depthDesc.Format;
    depthClearValue.DepthStencil = { 1.0f, 0 };

    // Texture depthTexture = Texture(depthDesc, &depthClearValue, TextureUsage::Depth, L"Depth Render Target");
    auto depthTexture = Application::Get().CreateTexture(depthDesc, &depthClearValue);
    depthTexture->SetName(L"Depth Render Target");

    // attach textures to the render target
    m_renderTarget.AttachTexture(AttachmentPoint::Color0, colorTexture);
    m_renderTarget.AttachTexture(AttachmentPoint::DepthStencil, depthTexture);

    // auto fenceValue = commandQueue.ExecuteCommandList(commandList);
    // commandQueue.WaitForFenceValue(fenceValue);


    commandQueue.WaitForFenceValue(fence);
    m_pWindow->RegisterCallbacks(shared_from_this());
    m_pWindow->Show();

    return true;

    //
 //    // Upload vertex data
    // ComPtr<ID3D12Resource> intermediateVertexBuffer;
 //    UpdateBufferResource(commandList.Get(), &m_vertexBuffer, &intermediateVertexBuffer, _countof(g_Vertices),sizeof(VertexData), g_Vertices);
 //
 //    m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
 //    m_vertexBufferView.SizeInBytes = sizeof(g_Vertices);
 //    m_vertexBufferView.StrideInBytes = sizeof(VertexData);
 //
 //    // Upload index data
 //    ComPtr<ID3D12Resource> intermediataIndexBuffer;
 //    UpdateBufferResource(commandList.Get(), &m_indexBuffer, &intermediataIndexBuffer, _countof(g_Indicies), sizeof(WORD), g_Indicies);
 //
 //    m_indexBufferView.BufferLocation = m_indexBuffer->GetGPUVirtualAddress();
 //    m_indexBufferView.Format = DXGI_FORMAT_R16_UINT;
 //    m_indexBufferView.SizeInBytes = sizeof(g_Indicies);
 //
 //    // Create a descriptor heap for depth stencil view
 //    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
 //    dsvHeapDesc.NumDescriptors = 1;
 //    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
 //    dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
 //    ThrowIfFailed(device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_depthDescriptorHeap)));
 //
 //    // Create Vertex input layout
 //    D3D12_INPUT_ELEMENT_DESC inputLayout[] =
 //    {
 //        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    // 	{"COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
 //    };
 //
 //    // Root Signature
 //    D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};
 //    featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
 //    if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
 //    {
 //        featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
 //    }
 //
 //    D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags = {
 //        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
 //        D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
 //        D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
 //        D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
 //        D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS
 //    };
 //
 //    CD3DX12_ROOT_PARAMETER1 rootParameters[1];
 //    rootParameters[0].InitAsConstants(sizeof(XMMATRIX) / 4, 0, 0, D3D12_SHADER_VISIBILITY_VERTEX);
 //
 //
 //    // Root Signature Description
 //
 //    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
 //    rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters, 0, nullptr, rootSignatureFlags);
 //
 //
 //    // Serialize root signature
 //    ComPtr<ID3DBlob> rootSignatureBlob;
 //    ComPtr<ID3DBlob> errorBlob;
 //
 //    ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &rootSignatureBlob, &errorBlob));
 //
 //    // Create RootSignature
 //    ThrowIfFailed(device->CreateRootSignature(0, rootSignatureBlob->GetBufferPointer(), rootSignatureBlob->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)));
 //
 //    D3D12_RT_FORMAT_ARRAY rtvFormats = {};
 //    rtvFormats.NumRenderTargets = 1;
 //    rtvFormats.RTFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
 //
 //    // initialize pipeline state stream
 //    pipelineStateStream.rootSignature = m_rootSignature.Get();
 //    pipelineStateStream.inputLayout = { inputLayout, _countof(inputLayout) };
 //    pipelineStateStream.primitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
 //    pipelineStateStream.vertexShader = CD3DX12_SHADER_BYTECODE(vertexShaderBlob.Get());
 //    pipelineStateStream.pixelShader = CD3DX12_SHADER_BYTECODE(pixelShaderBlob.Get());
 //    pipelineStateStream.depthFormat = DXGI_FORMAT_D32_FLOAT;
 //    pipelineStateStream.renderTargetFormats = rtvFormats;
 //
 //    // using the pipeline state stream, we create the pipeline state object.
 //    D3D12_PIPELINE_STATE_STREAM_DESC pipelineStateStreamDesc = { sizeof(PipelineStateStream), &pipelineStateStream };
 //    ThrowIfFailed(device->CreatePipelineState(&pipelineStateStreamDesc, IID_PPV_ARGS(&m_pipelineState)));
 //
 //
 //    auto fenceValue = commandQueue->ExecuteCommandList(commandList);
 //    commandQueue->WaitForFenceValue(fenceValue);
 //
 //    m_contentLoaded = true;
 //
 //    // now that depth descriptorheap was created, we can create/resize the depth bufffer
 //    ResizeDepthBuffer(GetScreenWidth(), GetScreenHeight());
 //
 //    return true;
}

void Ocean::UnloadContent()
{

}





