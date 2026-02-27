#include "ocean_scene.h"


#include <iostream>
#include <Shlwapi.h>

#include "core/EV.h"

#include <DirectXColors.h>
#include <random>

#include "compute_pso.h"
#include "convolution_pso.h"
#include "ocean_pso.h"
#include "DX12/skybox_pso.h"
#include "DX12/root_signature.h"
#include "DX12/sdr_pso.h"
#include "DX12/skybox_pso.h"

#define PI 3.14159265359f


using namespace DirectX;
using namespace EV;

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
    m_pWindow->DPIScaleChanged += DPIScaleEvent::slot(&Ocean::OnDPIScaleChanged, this);
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

    // Init();     
}

Ocean::~Ocean()
{
    _aligned_free(m_pAlignedCameraData);
}

void Ocean::OnDPIScaleChanged(DPIScaleEventArgs& e)
{
    m_GUI->SetScaling(e.DPIScale);
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
    // m_loadingTask = std::async(std::launch::async, std::bind(&Ocean::LoadScene, this,
    //     L"assets/sponza/sponza_nobanner.obj"));    
	
	// m_loadingTask = std::async(std::launch::async, std::bind(&Ocean::LoadScene, this,
	//        L"assets/dragon/DragonAttenuation.gltf"));

    // Loading helmet to not load big ass sponza.
    // m_loadingTask = std::async(std::launch::async, std::bind(&Ocean::LoadScene, this,
    //     L"assets/damaged_helmet/DamagedHelmet.gltf"));


    auto& commandQueue = app.GetCommandQueue(D3D12_COMMAND_LIST_TYPE_COPY);
    auto commandList = commandQueue.GetCommandList();

    m_oceanPlane = commandList->CreatePlane(OCEAN_SIZE, OCEAN_SIZE, OCEAN_SUBRES, OCEAN_SUBRES, false);

    m_skybox = commandList->CreateCube(1.0f, true);
    m_skyboxTexture = commandList->LoadTextureFromFile(L"assets/cloudSky.hdr", true);

    // Create a cubemap for the HDR panorama.
    auto cubemapDesc = m_skyboxTexture->GetD3D12ResourceDesc();
    cubemapDesc.Width = cubemapDesc.Height = 1024;
    cubemapDesc.DepthOrArraySize = 6;
    cubemapDesc.MipLevels = 0;

    m_skyboxCubemap = app.CreateTexture(cubemapDesc);
    m_skyboxCubemap->SetName(L"Cloud Cubemap");

    // Convert the 2D panorama to a 3D cubemap.
    commandList->PanoToCubemap(m_skyboxCubemap, m_skyboxTexture);

    // m_cubeMesh = commandList->CreateCube();
    m_helmet = commandList->LoadSceneFromFile(L"assets/damaged_helmet/DamagedHelmet.gltf");
    m_chessboard = commandList->LoadSceneFromFile(L"assets/chess/ABeautifulGame.gltf");
    m_boat = commandList->LoadSceneFromFile(L"assets/kenny/ship-large.obj");

    m_sphere = commandList->CreateSphere(0.1f);

    // m_defaultTexture = commandList->LoadTextureFromFile(L"assets/Mona_Lisa.jpg", true);
    UpdateSpectrumParameters();
    GenerateH0(commandList);

    auto fence = commandQueue.ExecuteCommandList(commandList);

    // RootParams to update H0 each frame
    CD3DX12_DESCRIPTOR_RANGE1 h0DescriptorRangeSRV(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0,
        D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE);
    CD3DX12_DESCRIPTOR_RANGE1 h0DescriptorRangeUAV(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 0, 0,
        D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE); // Volatile was needed because fft shader would have varying shaders. Sometimes SRV sometimes not, which causes UAV to be bound to SRV.

    CD3DX12_ROOT_PARAMETER1 H0RootParameters[OceanCompute::RootParameters::NumRootParameters];
    H0RootParameters[OceanCompute::RootParameters::ReadTextures].InitAsDescriptorTable(1, &h0DescriptorRangeSRV); // SRV t0
    H0RootParameters[OceanCompute::RootParameters::WriteTextures].InitAsDescriptorTable(1, &h0DescriptorRangeUAV); // UAV u0
    H0RootParameters[OceanCompute::RootParameters::Time].InitAsConstants(1, 0); // CBV b0

    // FFT 
    CD3DX12_DESCRIPTOR_RANGE1 FFTDescriptorRangeUAV(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0,
        D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE);

    CD3DX12_ROOT_PARAMETER1 FFTRootParameters[2];
    FFTRootParameters[0].InitAsDescriptorTable(1, &FFTDescriptorRangeUAV); // UAV u0
    FFTRootParameters[1].InitAsConstants(1, 0); // CBV b0

    // Permute
    CD3DX12_DESCRIPTOR_RANGE1 permuteDescriptorRangeUAV(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 3, 0);
    CD3DX12_ROOT_PARAMETER1 permuteRootParameters[1];
    permuteRootParameters[0].InitAsDescriptorTable(1, &permuteDescriptorRangeUAV); // UAV

    // Convolution
    CD3DX12_DESCRIPTOR_RANGE1 convolutionSRVRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
    CD3DX12_DESCRIPTOR_RANGE1 convolutionUAVRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);

    CD3DX12_ROOT_PARAMETER1 convolutionRootParameters[3];
    convolutionRootParameters[0].InitAsConstants(4, 0);                    
    convolutionRootParameters[1].InitAsDescriptorTable(1, &convolutionSRVRange);   
    convolutionRootParameters[2].InitAsDescriptorTable(1, &convolutionUAVRange);   

    m_unlitPSO = std::make_shared<EffectPSO>(m_camera, L"/vertex.cso", L"/pixel.cso");
    m_displacementPSO = std::make_shared<OceanPSO>(m_camera, L"/ocean_vertex.cso", L"/ocean_pixel.cso");
    m_oceanPSO = std::make_shared<OceanCompute>(L"/animate_waves.cso", H0RootParameters, _countof(H0RootParameters));
    m_fftPSO = std::make_shared<OceanCompute>(L"/fft.cso", FFTRootParameters, _countof(FFTRootParameters));
    m_permutePSO = std::make_shared<OceanCompute>(L"/permute.cso", permuteRootParameters, _countof(permuteRootParameters));
    m_convolutionPSO = std::make_shared<ConvolutionCompute>(L"/ibl_convolution.cso", convolutionRootParameters, _countof(convolutionRootParameters));
    m_skyboxPSO = std::make_shared<SkyboxPSO>(L"/skybox_VS.cso", L"/skybox_PS.cso");
    m_sdrPSO = std::make_shared<SDRPSO>(L"/HDR_to_SDR_VS.cso", L"/HDR_to_SDR_PS.cso");
    // m_permuteSlopePSO = std::make_shared<OceanCompute>(L"/permute_slopemap.cso", FFTRootParameters, _countof(FFTRootParameters));


    D3D12_SHADER_RESOURCE_VIEW_DESC cubeMapSRVDesc = {};
    cubeMapSRVDesc.Format = cubemapDesc.Format;
    cubeMapSRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    cubeMapSRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
    cubeMapSRVDesc.TextureCube.MipLevels = (UINT)-1;  // Use all mips.

    m_skyboxCubemapSRV = app.CreateShaderResourceView(m_skyboxCubemap, &cubeMapSRVDesc);

    commandQueue.WaitForFenceValue(fence);

    auto& computeQueue = app.GetCommandQueue(D3D12_COMMAND_LIST_TYPE_COMPUTE);
    auto computeCommandList = computeQueue.GetCommandList();

    DXGI_FORMAT irradianceFormat = DXGI_FORMAT_R16G16B16A16_FLOAT; // TODO: less precision might also be fine

    auto irradianceDesc = CD3DX12_RESOURCE_DESC::Tex2D(irradianceFormat, m_convolutionPlaneSize, m_convolutionPlaneSize, 6);
    irradianceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

	// diffuse irradiance texture
    m_diffuseIrradianceMap = Application::Get().CreateTexture(irradianceDesc);
    m_diffuseIrradianceMap->SetName(L"Diffuse Irradiance Texture");

    // Convolution IBL
    m_convolutionPSO->Dispatch(computeCommandList, m_skyboxCubemapSRV, m_diffuseIrradianceMap, m_convolutionPlaneSize, m_convolutionSampleCount);


    DXGI_FORMAT HDRbackBufferFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
    DXGI_FORMAT depthBufferFormat = DXGI_FORMAT_D32_FLOAT;

    DXGI_SAMPLE_DESC sampleDesc = app.GetMultisampleQualityLevels(HDRbackBufferFormat, D3D12_MAX_MULTISAMPLE_SAMPLE_COUNT);

    // offscreen render target
    auto colorDesc = CD3DX12_RESOURCE_DESC::Tex2D(HDRbackBufferFormat, m_width, m_height, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);

    D3D12_CLEAR_VALUE clearColor;
    clearColor.Format = colorDesc.Format;
    clearColor.Color[0] = 0.0f;
    clearColor.Color[1] = 0.0f;
    clearColor.Color[2] = 0.3f;
    clearColor.Color[3] = 1.0f;

    // Texture colorTexture = Texture(colorDesc, &clearColor, TextureUsage::RenderTarget, L"Color Render Target");
    m_HDRTexture = Application::Get().CreateTexture(colorDesc, &clearColor);
    m_HDRTexture->SetName(L"Color Render Target");

    auto depthDesc = CD3DX12_RESOURCE_DESC::Tex2D(depthBufferFormat, m_width, m_height, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);

    D3D12_CLEAR_VALUE depthClearValue;
    depthClearValue.Format = depthDesc.Format;
    depthClearValue.DepthStencil = { 1.0f, 0 };

    // Texture depthTexture = Texture(depthDesc, &depthClearValue, TextureUsage::Depth, L"Depth Render Target");
    auto depthTexture = Application::Get().CreateTexture(depthDesc, &depthClearValue);
    depthTexture->SetName(L"Depth Render Target");

    // attach textures to the render target
    m_renderTarget.AttachTexture(AttachmentPoint::Color0, m_HDRTexture);
    m_renderTarget.AttachTexture(AttachmentPoint::DepthStencil, depthTexture);

    auto convolutionFence = computeQueue.ExecuteCommandList(computeCommandList);
    computeQueue.WaitForFenceValue(convolutionFence);

    m_pWindow->RegisterCallbacks(shared_from_this());
    m_pWindow->Show();

    return true;
}

void Ocean::OnUpdate(UpdateEventArgs& e)
{
    static uint64_t frameCount = 0;
    static double totalTime = 0.0;
    static double oceanTime = 0.0;

    auto& commandQueue = Application::Get().GetCommandQueue(D3D12_COMMAND_LIST_TYPE_COMPUTE);
    auto commandList = commandQueue.GetCommandList();

    // super::OnUpdate(e);

    totalTime += e.deltaTime;
    oceanTime += e.deltaTime;
    frameCount++;

    if (totalTime > 1.0)
    {
        m_FPS = frameCount / totalTime;

        // char buffer[512];
        // sprintf_s(buffer, "FPS: %f\n", m_FPS);
        // OutputDebugStringA(buffer);

        frameCount = 0;
        totalTime = 0.0;
    }

    m_swapChain->WaitForSwapChain();

    // Update Camera
    float speedMultiplier = (m_shift ? 64.0f : 8.0f);
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

        XMVECTOR positionWS = XMLoadFloat3(&l.position);

        XMVECTOR directionWS = XMVector3Normalize(XMVectorNegate(positionWS));
        XMVECTOR directionVS = XMVector3TransformNormal(directionWS, viewMatrix);

        XMStoreFloat4(&l.directionWS, directionWS);
        XMStoreFloat4(&l.directionVS, directionVS);

        // l.color = XMFLOAT4(LightColors[i]);
    }

    // skybox 

    // auto viewMatrix = XMMatrixTranspose(XMMatrixRotationQuaternion(m_camera.GetRotation()));
    auto projMatrix = m_camera.GetProjectionMatrix();
    auto viewProjMatrix = viewMatrix * projMatrix;

    m_skyboxPSO->SetSkyboxSRV(m_skyboxCubemapSRV);
    m_skyboxPSO->SetViewMatrix(viewMatrix);
    m_skyboxPSO->SetProjectionMatrix(projMatrix);

    uint32_t phaseDispatchSize = OCEAN_SUBRES / 16;
    m_oceanPSO->Dispatch(commandList, m_H0Texture, m_slopeTexture, m_displacementTexture, oceanTime, XMUINT3(phaseDispatchSize, phaseDispatchSize, 1));
    commandList->UAVBarrier(m_slopeTexture);
    commandList->UAVBarrier(m_displacementTexture);
	
    // Displacement
	m_fftPSO->Dispatch(commandList, m_displacementTexture, XMUINT3(OCEAN_SUBRES, 1, 1), 0); // horizontal fft
    commandList->UAVBarrier(m_displacementTexture);
	m_fftPSO->Dispatch(commandList, m_displacementTexture, XMUINT3(OCEAN_SUBRES, 1, 1), 1); // vertical fft

    // Slope
	m_fftPSO->Dispatch(commandList, m_slopeTexture, XMUINT3(OCEAN_SUBRES, 1, 1), 0); // horizontal fft
    commandList->UAVBarrier(m_slopeTexture);
	m_fftPSO->Dispatch(commandList, m_slopeTexture, XMUINT3(OCEAN_SUBRES, 1, 1), 1); // vertical fft
	
    // Permutation
    commandList->UAVBarrier(m_slopeTexture);
    commandList->UAVBarrier(m_displacementTexture);
    // m_permuteSlopePSO->Dispatch(commandList, m_slopeTexture, XMUINT3(phaseDispatchSize, phaseDispatchSize, 1), 0);
    m_permutePSO->Dispatch(commandList, m_slopeTexture,  m_displacementTexture, m_foamTexture, XMUINT3(phaseDispatchSize, phaseDispatchSize, 1));


    auto fence = commandQueue.ExecuteCommandList(commandList);

    m_unlitPSO->SetDirectionalLights(m_directionalLights);
    m_displacementPSO->SetDirectionalLights(m_directionalLights);
    // m_lightingPSO->SetDirectionalLights(m_DirectionalLights);
    // m_decalPSO->SetDirectionalLights(m_DirectionalLights);

    // Set the skybox SRV before rendering
    m_unlitPSO->SetDiffuseIBL(m_skyboxCubemapSRV);
    m_displacementPSO->SetDiffuseIBL(m_skyboxCubemapSRV);

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
    m_displacementPSO->SetPointLights(m_pointLights);

    // Update heightmap
    // UpdateSpectrum(time);

    commandQueue.WaitForFenceValue(fence);
    OnRender();

}

void Ocean::OnRender()
{

    m_pWindow->SetFullScreen(m_fullscreen);

    auto& commandQueue = Application::Get().GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT);
    auto commandList = commandQueue.GetCommandList();

    const auto& renderTarget = m_isLoading ? m_swapChain->GetRenderTarget() : m_renderTarget;

    if (m_isLoading)
    {

        FLOAT clearColor[] = { 0.9f, 0.2f, 0.4f, 1.0f };

        commandList->ClearTexture(renderTarget.GetTexture(AttachmentPoint::Color0), clearColor);
    }
    else
    {
        SceneVisitor visitor(*commandList, m_camera, *m_unlitPSO, false);
        SceneVisitor oceanVisitor(*commandList, m_camera, *m_displacementPSO, false);
        SceneVisitor skyboxVisitor(*commandList, m_camera, *m_skyboxPSO, false);


         // Clear the render targets.
        {
            FLOAT clearColor[] = { 0.4f, 0.6f, 0.9f, 1.0f };

            commandList->ClearTexture(renderTarget.GetTexture(AttachmentPoint::Color0), clearColor);
            commandList->ClearDepthStencilTexture(renderTarget.GetTexture(AttachmentPoint::DepthStencil),
                D3D12_CLEAR_FLAG_DEPTH);
        }

        commandList->SetViewport(m_viewport);
        commandList->SetScissorRect(m_scissorRect);
        commandList->SetRenderTarget(m_renderTarget);

        m_skybox->Accept(skyboxVisitor);


        // m_scene->Accept(visitor);
        XMMATRIX duckTranslation = XMMatrixTranslation(-4.0f, 0.0f, 0.0f);
        m_boat->GetRootNode()->SetLocalTransform(XMMatrixIdentity() * XMMatrixIdentity() * duckTranslation);
        m_boat->Accept(visitor);

        // Set Ocean Textures
        m_displacementPSO->SetOceanTextures(m_displacementTexture, m_slopeTexture, m_foamTexture);

        m_displacementPSO->SetDirectionalLights(m_directionalLights);
        m_displacementPSO->SetPointLights(m_pointLights);



        // REMINDER: Transform is built with Scale * Rotation * Translation (SRT)
        XMMATRIX scale = XMMatrixScaling(10.0f, 10.0f, 10.0f);
        XMMATRIX rotation = XMMatrixRotationRollPitchYaw(XMConvertToRadians(-90.0f), 0.0f, 0.0f);
        XMMATRIX translation = XMMatrixTranslation(0.0f, -12.0f, 0.0f);

        
        // m_scene->GetRootNode()->SetLocalTransform(scale * XMMatrixIdentity() * translation);

        m_oceanPlane->Accept(oceanVisitor);

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

        // // Resolve the MSAA render target to the swapchain's backbuffer.
        // auto& swapChainRT = m_swapChain->GetRenderTarget();
        // auto  swapChainBackBuffer = swapChainRT.GetTexture(AttachmentPoint::Color0);
        // auto  msaaRenderTarget = m_renderTarget.GetTexture(AttachmentPoint::Color0);
        //
        // commandList->ResolveSubresource(swapChainBackBuffer, msaaRenderTarget);

	    // Perform HDR -> SDR tonemapping directly to the SwapChain's render target.

	    commandList->SetRenderTarget(m_swapChain->GetRenderTarget());
        m_sdrPSO->SetHDRTexture(m_HDRTexture);
        m_sdrPSO->Apply(*commandList);

    }

	    commandList->TransitionBarrier(m_slopeTexture, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	    commandList->TransitionBarrier(m_displacementTexture, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	    commandList->TransitionBarrier(m_foamTexture, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	    // Render the GUI on the render target
	    OnGUI(commandList, m_swapChain->GetRenderTarget());
	    commandQueue.ExecuteCommandList(commandList);

    m_swapChain->Present();
}

void Ocean::OnGUI(const std::shared_ptr<CommandList>& commandList, const RenderTarget& renderTarget)
{
    m_GUI->NewFrame();

    if (m_isLoading)
    {
        // Show a progress bar.
        ImGui::SetNextWindowPos(ImVec2(m_width / 2.0f, m_height / 2.0f), 0,
            ImVec2(0.5, 0.5));
        ImGui::SetNextWindowSize(ImVec2(m_width / 2.0f, 0));

        ImGui::Begin("Loading", nullptr,
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoScrollbar);
        ImGui::ProgressBar(m_loadingProgress);
        ImGui::Text(m_loadingText.c_str());
        if (!m_cancelLoading)
        {
            if (ImGui::Button("Cancel"))
            {
                m_cancelLoading = true;
            }
        }
        else
        {
            ImGui::Text("Cancel Loading...");
        }

        ImGui::End();
    }

    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("Open file...", "Ctrl+O", nullptr, !m_isLoading))
            {
                m_showFileOpenDialog = true;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Esc"))
            {
                Application::Get().Stop();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View"))
        {

            ImGui::MenuItem("Ocean Parameters", nullptr, &m_showOceanParams);
            
        	ImGui::MenuItem("Light Parameters", nullptr, &m_showLightParams);

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Options"))
        {
            bool vSync = m_swapChain->GetVSync();
            if (ImGui::MenuItem("V-Sync", "V", &vSync))
            {
                m_swapChain->SetVSync(vSync);
            }

            bool fullscreen = m_pWindow->IsFullScreen();
            if (ImGui::MenuItem("Full screen", "Alt+Enter", &fullscreen))
            {
                // m_Window->SetFullscreen( fullscreen );
                // Defer the window resizing until the reference to the render target is released.
                m_fullscreen = fullscreen;
            }

            // ImGui::MenuItem("Animate Lights", "Space", &m_AnimateLights);

            // bool invertY = m_CameraController.IsInverseY();
            // if (ImGui::MenuItem("Inverse Y", nullptr, &invertY))
            // {
            //     m_CameraController.SetInverseY(invertY);
            // }
            // if (ImGui::MenuItem("Reset view", "R"))
            // {
            //     m_CameraController.ResetView();
            // }

            ImGui::EndMenu();
        }

        char buffer[256];
        {
            sprintf_s(buffer, _countof(buffer), "FPS: %.2f (%.2f ms)  ", m_FPS, 1.0 / m_FPS * 1000.0);
            auto fpsTextSize = ImGui::CalcTextSize(buffer);
            ImGui::SameLine(ImGui::GetWindowWidth() - fpsTextSize.x);
            ImGui::Text(buffer);
        }

        ImGui::EndMainMenuBar();
    }

    if (m_showOceanParams && ImGui::Begin("Ocean Parameters", &m_showOceanParams))
    {
        bool paramsChanged = false;

        ImGui::Separator();
        ImGui::Text("Wind");
        paramsChanged |= ImGui::SliderFloat("Wind Speed", &m_jonswapParams.windSpeed, 0.0f, 100.0f);
        paramsChanged |= ImGui::SliderFloat("Wind Direction", &m_jonswapParams.windDirection, 0.0f, 360.0f);
        paramsChanged |= ImGui::SliderFloat("Fetch", &m_jonswapParams.fetch, 1000.0f, 1000000.0f);

        ImGui::Separator();
        ImGui::Text("Spectrum");
        paramsChanged |= ImGui::SliderFloat("Scale", &m_jonswapParams.scale, 0.0f, 5.0f);
        paramsChanged |= ImGui::SliderFloat("Gamma", &m_jonswapParams.gamma, 0.0f, 7.0f);
        paramsChanged |= ImGui::SliderFloat("Swell", &m_jonswapParams.swell, 0.0f, 1.0f);
        paramsChanged |= ImGui::SliderFloat("Spread Blend", &m_jonswapParams.spreadBlend, 0.0f, 1.0f);
        paramsChanged |= ImGui::SliderFloat("Short Waves Fade", &m_jonswapParams.shortWavesFade, 0.0f, 1.0f);

        ImGui::Separator();
        ImGui::Text("Derived (read-only)");
        ImGui::LabelText("Alpha", "%.6f", m_jonswapParams.alpha);
        ImGui::LabelText("Peak Omega", "%.4f", m_jonswapParams.peakOmega);

        if (paramsChanged)
        {
            // Recompute derived params and regenerate H0
            m_jonswapParams.angle = m_jonswapParams.windDirection / 180.0f * PI;
            m_jonswapParams.alpha = JonswapAlpha(m_jonswapParams.fetch, m_jonswapParams.windSpeed);
            m_jonswapParams.peakOmega = JonswapPeakFequency(m_jonswapParams.fetch, m_jonswapParams.windSpeed);

            auto& commandQueue = Application::Get().GetCommandQueue(D3D12_COMMAND_LIST_TYPE_COPY);
            auto commandList = commandQueue.GetCommandList();
            GenerateH0(commandList);
            commandQueue.ExecuteCommandList(commandList);
            // commandQueue.Flush();
        }

        ImGui::End();
    }

    if (m_showLightParams && ImGui::Begin("Light Parameters", &m_showLightParams))
    {
        bool paramsChanged = false;

        ImGui::Separator();
        ImGui::Text("Directional Light");
        paramsChanged |= ImGui::DragFloat3("Position", &m_directionalLights[0].position.x);
        paramsChanged |= ImGui::ColorPicker4("Color", &m_directionalLights[0].color.x);

        ImGui::Separator();
        ImGui::Text("Point Light");
        // paramsChanged |= ImGui::SliderFloat("Scale", &m_jonswapParams.scale, 0.0f, 5.0f);
        // paramsChanged |= ImGui::SliderFloat("Gamma", &m_jonswapParams.gamma, 0.0f, 7.0f);
        // paramsChanged |= ImGui::SliderFloat("Swell", &m_jonswapParams.swell, 0.0f, 1.0f);
        // paramsChanged |= ImGui::SliderFloat("Spread Blend", &m_jonswapParams.spreadBlend, 0.0f, 1.0f);
        // paramsChanged |= ImGui::SliderFloat("Short Waves Fade", &m_jonswapParams.shortWavesFade, 0.0f, 1.0f);

        if (paramsChanged)
        {
            auto& commandQueue = Application::Get().GetCommandQueue(D3D12_COMMAND_LIST_TYPE_COPY);
            auto commandList = commandQueue.GetCommandList();
            commandQueue.ExecuteCommandList(commandList);
        }

        ImGui::End();
    }

    m_GUI->Render(commandList, renderTarget);
}


void Ocean::UnloadContent()
{
    m_cubeMesh.reset();
    m_scene.reset();
    m_helmet.reset();
    m_chessboard.reset();
    m_boat.reset();
    m_oceanPlane.reset();
    m_skybox.reset();

    m_defaultTexture.reset();
    m_rootSignature.reset();
    m_pipelineState.reset();
    m_sphere.reset();
    m_unlitPSO.reset();
    m_displacementPSO.reset();
    m_skyboxPSO.reset();
    m_sdrPSO.reset();
    m_oceanPSO.reset();
    m_fftPSO.reset();
    m_permutePSO.reset();
    m_H0Texture.reset();
    m_slopeTexture.reset();
    m_displacementTexture.reset();
    m_heightTexture.reset();
    m_normalTexture.reset();
    m_intermediateTextureSlope.reset();
    m_intermediateTextureHeight.reset();
    m_permutedSlope.reset();
    m_permutedHeight.reset();
    m_foamTexture.reset();
    m_skyboxTexture.reset();
    m_skyboxCubemap.reset();
    m_HDRTexture.reset();
    m_skyboxCubemapSRV.reset();
    m_skyboxSignature.reset();
    m_HDRRootSignature.reset();
    m_SDRRootSignature.reset();
    m_HDRPSO.reset();
	m_SDRPipelineState.reset();

    // m_GUI.reset();
    // m_swapChain.reset();

}

float Ocean::InitPhillipsSpectrum(XMFLOAT2 k, XMFLOAT2 windDir, float windSpeed, float A)
{
    // A * exp(-1/(magnitudeK * L)^2)/k^4 * dot(k,w)^2 * exp(-k^2 * L^2)
    
    const float l = OCEAN_SIZE * 0.00001; // Patch size

    float g = 9.81f;
    float magnitudeK = sqrtf(k.x * k.x + k.y * k.y);
    if (magnitudeK < 0.0001f) return 0.0f;
    float V2 = windSpeed * windSpeed;
    float L = V2 / g;   
    float L2 = L * L;
    float k2 = magnitudeK * magnitudeK;
    float k4 = k2 * k2;

    float kDotW = k.x * windDir.x + k.y * windDir.y;
    float kDotW2 = kDotW * kDotW;

    return 0.02f * (exp(-1.0f / (k2 * L2)) / k4) * kDotW2 * exp(-k2 * l*l);

}

float DispersionRelation(float kMag)
{
    return sqrt(9.81f * kMag * tanh(std::min(kMag * OCEAN_DEPTH, 20.0f)));
}

float DispersionDerivative(float kMag)
{
    float th = tanh(std::min(kMag * OCEAN_DEPTH, 20.0f));
    float ch = cosh(kMag * 20.0f);
    return 9.81f * (OCEAN_DEPTH * kMag / ch / ch + th) / DispersionRelation(kMag) / 2.0f;
}

float NormalizationFactor(float s)
{
    float s2 = s * s;
    float s3 = s2 * s;
    float s4 = s3 * s;
    if (s < 5) return -0.000564f * s4 + 0.00776f * s3 - 0.044f * s2 + 0.192f * s + 0.163f;
    else return -4.80e-08f * s4 + 1.07e-05f * s3 - 9.53e-04f * s2 + 5.90e-02f * s + 3.93e-01f;
}

float Cosine2s(float theta, float s)
{
    return NormalizationFactor(s) * pow(abs(cos(0.5f * theta)), 2.0f * s);
}

float SpreadPower(float omega, float peakOmega)
{
    if (omega > peakOmega)
        return 9.77f * pow(abs(omega / peakOmega), -2.5f);
    else
        return 6.97f * pow(abs(omega / peakOmega), 5.0f);
}

float Ocean::DirectionSpectrum(float theta, float omega)
{
    float s = SpreadPower(omega, m_jonswapParams.peakOmega) + 16 * tanh(std::min(omega / m_jonswapParams.peakOmega, 20.0f)) * m_jonswapParams.swell * m_jonswapParams.swell;

    return std::lerp(2.0f / 3.1415f * cos(theta) * cos(theta), Cosine2s(theta - m_jonswapParams.angle, s), m_jonswapParams.spreadBlend);
}

float Ocean::ShortWavesFade(float kLength)
{
    return exp(-m_jonswapParams.shortWavesFade * m_jonswapParams.shortWavesFade * kLength * kLength);
}


void Ocean::GenerateH0(std::shared_ptr<CommandList> commandList) {
    const float L = OCEAN_SIZE; // Patch size
    float deltaK = 2.0f * PI / L;

    const float lowCutoff = 0.0001f;
    const float highCutoff = 9000.0f;
    for (int m = 0; m < OCEAN_SUBRES; m++) {
        for (int n = 0; n < OCEAN_SUBRES; n++) {
            // Get wave vector for this frequency
            float kx = (n - OCEAN_SUBRES / 2.0f) * deltaK;
            float ky = (m - OCEAN_SUBRES / 2.0f) * deltaK;
            float k(sqrtf(kx * kx + ky * ky));

            if (k >= lowCutoff && k <= highCutoff)
            {
	            
            float kAngle = atan2(ky, kx);
            float omega = DispersionRelation(k);
            float dOmegadk = DispersionDerivative(k);

            float spectrum = JONSWAP(omega) * DirectionSpectrum(kAngle, omega) * ShortWavesFade(k);
            
            // Generate two independent gaussian random numbers
            float xiR = GaussianRandom();
            float xiI = GaussianRandom();
            //
            // XMFLOAT2 windDir(0.7f, 0.7f); // diagonal wind
            // float windSpeed = 30.0f;

            // float Ph = InitPhillipsSpectrum(k, windDir,windSpeed);

            // Equation 42: h0(k) = (1/√2) * (gausRandom + i*gausRandomI) * sqrt(Ph(k))
            // H0[m][n] = (1.0f / sqrtf(2.0f)) * std::complex<float>(xiR, xiI) * sqrtf(Ph);
            float amplitude = sqrtf(2.0f * spectrum * fabsf(dOmegadk) / k * deltaK * deltaK);
            H0[m][n] = std::complex<float>(xiR * amplitude, xiI * amplitude);
            }
        }
    }

    // Needs its own loop since the conjugate needs all data of H0 to be valid
    for (int m = 0; m < OCEAN_SUBRES; m++) {
        for (int n = 0; n < OCEAN_SUBRES; n++) {
            int m_minus = (OCEAN_SUBRES - m) % OCEAN_SUBRES;
            int n_minus = (OCEAN_SUBRES - n) % OCEAN_SUBRES;

            H0Conj[m][n] = std::conj(H0[m_minus][n_minus]);
        }
    }
	
    // TODO: the tex formats can probably be 16bit rather than 32
    // Input texture SRV
	DXGI_FORMAT H0Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    auto H0Desc = CD3DX12_RESOURCE_DESC::Tex2D(H0Format, OCEAN_SUBRES, OCEAN_SUBRES);
    
    // output phase texture UAV
    DXGI_FORMAT phaseFormat = DXGI_FORMAT_R32G32B32A32_FLOAT;
    auto phaseDesc = CD3DX12_RESOURCE_DESC::Tex2D(phaseFormat, OCEAN_SUBRES, OCEAN_SUBRES);
    phaseDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    DXGI_FORMAT foamFormat = DXGI_FORMAT_R32_FLOAT;
    auto foamDesc = CD3DX12_RESOURCE_DESC::Tex2D(foamFormat, OCEAN_SUBRES, OCEAN_SUBRES);
    foamDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;


	// Copy H0 texture data
    m_H0Texture = Application::Get().CreateTexture(H0Desc);
    m_H0Texture->SetName(L"H0 Texture");

    std::vector<float> combinedData(OCEAN_SUBRES * OCEAN_SUBRES * 4);

    for (int m = 0; m < OCEAN_SUBRES; m++) {
        for (int n = 0; n < OCEAN_SUBRES; n++) {
            int index = (m * OCEAN_SUBRES + n) * 4;
            combinedData[index + 0] = H0[m][n].real();        // R: H0 real
            combinedData[index + 1] = H0[m][n].imag();        // G: H0 imaginary
            combinedData[index + 2] = H0Conj[m][n].real();    // B: H0_conj real
            combinedData[index + 3] = H0Conj[m][n].imag();    // A: H0_conj imaginary
        }
    }

    D3D12_SUBRESOURCE_DATA subData = {};
    subData.pData = combinedData.data();
    subData.RowPitch = OCEAN_SUBRES * 4 * sizeof(float);
    subData.SlicePitch = subData.RowPitch * OCEAN_SUBRES;

    // TODO clean this up... separate the jobs properly
    // TODO also use one application get rather than keep calling it

    // Create slope output texture
    m_slopeTexture = Application::Get().CreateTexture(phaseDesc);
    m_slopeTexture->SetName(L"Slope Output Texture");

    // Create phase output texture
    m_displacementTexture = Application::Get().CreateTexture(phaseDesc);
    m_displacementTexture->SetName(L"Displacement Output Texture");

    // Create height texture
    m_heightTexture = Application::Get().CreateTexture(phaseDesc);
    m_heightTexture->SetName(L"Heightmap Texture");

    // Create intermediate height texture
    m_normalTexture = Application::Get().CreateTexture(phaseDesc);
    m_normalTexture->SetName(L"Normal Texture");

    // Create height texture
    m_intermediateTextureSlope = Application::Get().CreateTexture(phaseDesc);
    m_intermediateTextureSlope->SetName(L"inter slopemap Texture");

    // Create intermediate height texture
    m_intermediateTextureHeight = Application::Get().CreateTexture(phaseDesc);
    m_intermediateTextureHeight->SetName(L"inter heightmap Texture");

    // Create intermediate height texture
    m_permutedSlope = Application::Get().CreateTexture(phaseDesc);
    m_permutedSlope->SetName(L"permuted slopemap Texture");

    // Create intermediate height texture
    m_permutedHeight = Application::Get().CreateTexture(phaseDesc);
    m_permutedHeight->SetName(L"permuted heightmap Texture");

    // foam texture
    m_foamTexture = Application::Get().CreateTexture(foamDesc);
    m_foamTexture->SetName(L"Jacobian foam Texture");



    commandList->CopyTextureSubresource(m_H0Texture, 0, 1, &subData);
}

float Ocean::GaussianRandom() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::normal_distribution<float> dist(0.0f, 1.0f); // mean=0, std=1

    return dist(gen);
}

// TODO: Do this in a compute shader?!
void Ocean::UpdateSpectrum(float time) {
    const float g = 9.81f;  // Gravity

    for (int m = 0; m < OCEAN_SUBRES; m++) {
        for (int n = 0; n < OCEAN_SUBRES; n++) {
            // Calculate k TODO: already computed, store and reuse
            float kx = (2.0f * PI * (n - OCEAN_SUBRES / 2.0f)) / OCEAN_SIZE;
            float ky = (2.0f * PI * (m - OCEAN_SUBRES / 2.0f)) / OCEAN_SIZE;
            float k_length = sqrtf(kx * kx + ky * ky);

            // Dispersion: ω(k) = sqrt(g * |k|)
            float omega = sqrtf(g * k_length);

            // phase: ω(k) * t
            float phase = omega * time;

            // exp(iωt) = cos(ωt) + i*sin(ωt)
            std::complex<float> iwt(cosf(phase), sinf(phase));

            // exp(-iωt) = cos(ωt) - i*sin(ωt)
            std::complex<float> negIwt(cosf(phase), -sinf(phase));

            // Equation 43: height(k,t) = h0(k)*exp(iωt) + h0*(-k)*exp(-iωt)
            heightMap[m][n] = H0[m][n] * iwt +
                H0Conj[m][n] * negIwt;
        }
    }
}


float TMACorrection(float omega)
{
    // Acerola uses 20 for depth
    float omegaH = omega * sqrt(OCEAN_DEPTH / 9.81f);
    if (omegaH <= 1.0f)
        return 0.5f * omegaH * omegaH;
    if (omegaH < 2.0f)
        return 1.0f - 0.5f * (2.0f - omegaH) * (2.0f - omegaH);

    return 1.0f;
}

float Ocean::JONSWAP(float omega)
{
    float sigma = (omega <= m_jonswapParams.peakOmega) ? 0.07f : 0.09f;
    float r = exp(-(omega - m_jonswapParams.peakOmega) * (omega - m_jonswapParams.peakOmega) / 2.0f / sigma / sigma / m_jonswapParams.peakOmega / m_jonswapParams.peakOmega);
    float g = 9.81f;

    float oneOverOmega = 1.0f / (omega + 1e-6f);
    float peakOmegaOverOmega = m_jonswapParams.peakOmega / omega;

    return m_jonswapParams.scale * TMACorrection(omega) * m_jonswapParams.alpha * g * g * oneOverOmega * oneOverOmega * oneOverOmega * oneOverOmega * oneOverOmega 
			* exp(-1.25f * peakOmegaOverOmega * peakOmegaOverOmega * peakOmegaOverOmega * peakOmegaOverOmega) * pow(abs(m_jonswapParams.gamma), r);
}
float Ocean::JonswapAlpha(float fetch, float windSpeed)
{
    return 0.076f * pow(9.81f * fetch / windSpeed / windSpeed, - 0.22f);
}

float Ocean::JonswapPeakFequency(float fetch, float windSpeed)
{
    return 22.0f * pow(windSpeed * fetch / 9.81f / 9.81f, -0.33f);
}

void Ocean::UpdateSpectrumParameters()
{
    // Parameter vvalues taken from: https://github.com/gasgiant/FFT-Ocean/tree/main
    m_jonswapParams.scale = 1.0f; // Used to scale the Spectrum [1.0f, 5.0f] --> Value Range
    m_jonswapParams.spreadBlend = 1.0f; // Used to blend between agitated water motion, and windDirection [0.0f, 1.0f]
    m_jonswapParams.swell = 0.198f; // Influences wave choppines, the bigger the swell, the longer the wave length [0.0f, 1.0f]
    m_jonswapParams.gamma = 3.3f; // Defines the Spectrum Peak [0.0f, 7.0f]
    m_jonswapParams.shortWavesFade = 0.01f; // [0.0f, 1.0f]
    
    m_jonswapParams.windDirection = 0.0f; // [0.0f, 360.0f]
    m_jonswapParams.fetch = 100000.0f; // Distance over which Wind impacts Wave Formation [0.0f, 10000.0f]
    m_jonswapParams.windSpeed = 0.5f; // [0.0f, 100.0f]

    m_jonswapParams.angle = m_jonswapParams.windDirection / 180.0f * PI;
    m_jonswapParams.alpha = JonswapAlpha(m_jonswapParams.fetch, m_jonswapParams.windSpeed);
    m_jonswapParams.peakOmega = JonswapPeakFequency(m_jonswapParams.fetch, m_jonswapParams.windSpeed);
}


void Ocean::OnKeyPress(KeyEventArgs& e)
{
    super::OnKeyPress(e);

    switch (e.key)
    {
    case KeyCode::Escape:
        Application::Get().Stop();
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



