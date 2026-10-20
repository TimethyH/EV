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

namespace
{
    // Image based lighting: resolution of the BRDF lookup table and the specular irradiance cubemap.
    constexpr uint32_t g_brdfTextureSize = 512;
    constexpr uint32_t g_specularMipLevels = 5;
    constexpr uint32_t g_skyboxCubemapSize = 1024;
}


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

// 32bit to 16bit precision helper
static uint16_t FloatToHalf(float f) {
    uint32_t x;
    memcpy(&x, &f, sizeof(float));
    uint16_t sign = (x >> 31) << 15;
    uint16_t exponent = ((x >> 23) & 0xFF);
    uint32_t mantissa = x & 0x7FFFFF;
    if (exponent == 0)       return sign;               // zero/denormal -> zero
    if (exponent == 255)     return sign | 0x7C00 | (mantissa ? 0x200 : 0); // inf/nan
    int16_t e = (int16_t)exponent - 127 + 15;
    if (e >= 31)             return sign | 0x7C00;      // overflow -> inf
    if (e <= 0)              return sign;               // underflow -> zero
    return sign | (e << 10) | (uint16_t)(mantissa >> 13);
}


Ocean::Ocean(const std::wstring& name, uint32_t width, uint32_t height, bool bVSync)
    : super(name, width, height, bVSync)
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

    // The scene itself is rendered at the size of the editor's Scene panel, see ResizeViewport.
    m_swapChain->Resize(m_width, m_height);
}

void Ocean::ResizeViewport(uint32_t width, uint32_t height)
{
    m_viewportWidth = std::max(1u, width);
    m_viewportHeight = std::max(1u, height);

    float aspectRatio = m_viewportWidth / (float)m_viewportHeight;
    m_camera.SetProjection(45.0f, aspectRatio, 0.1f, 1000.0f);

    // The HDR color and depth buffers are render graph textures, created at the size RecordRenderGraph asks for.
    m_viewportRenderTarget.Resize(m_viewportWidth, m_viewportHeight);
}

void Ocean::CreateBRDFLUT()
{
    auto& app = Application::Get();
    auto& computeQueue = app.GetCommandQueue(D3D12_COMMAND_LIST_TYPE_COMPUTE);
    auto commandList = computeQueue.GetCommandList();

    DXGI_FORMAT brdfLutFormat = DXGI_FORMAT_R16G16_FLOAT; // TODO: less precision might also be fine
    auto BRDFDesc = CD3DX12_RESOURCE_DESC::Tex2D(brdfLutFormat, g_brdfTextureSize, g_brdfTextureSize);
    BRDFDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    auto brdfLUT = app.CreateTexture(BRDFDesc);
    brdfLUT->SetName(L"BRDF LUT ");

    D3D12_SHADER_RESOURCE_VIEW_DESC brdfSRVDesc = {};
    brdfSRVDesc.Format = DXGI_FORMAT_R16G16_FLOAT;
    brdfSRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    brdfSRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    brdfSRVDesc.Texture2D.MostDetailedMip = 0;
    brdfSRVDesc.Texture2D.MipLevels = 1;

    auto brdfLUTSRV = app.CreateShaderResourceView(brdfLUT, &brdfSRVDesc);

    m_brdfLutPSO->DispatchBRDFLUT(commandList, brdfLUT, g_brdfTextureSize);
    computeQueue.WaitForFenceValue(computeQueue.ExecuteCommandList(commandList));

    // A new texture instead of overwriting the old one, which frames still in flight may be reading.
    // SetEnvironment hands it to the PSOs.
    m_brdfLUT = brdfLUT;
    m_brdfLUTSRV = brdfLUTSRV;
}

void Ocean::UpdateImageBasedLighting()
{
    const uint32_t shaderVersion = m_convolutionPSO->GetShaderVersion() + m_specularConvolutionPSO->GetShaderVersion() +
        m_brdfLutPSO->GetShaderVersion();
    if (shaderVersion == m_iblShaderVersion || !m_skyboxTexture)
    {
        return;
    }
    m_iblShaderVersion = shaderVersion;

    CreateBRDFLUT();
    SetEnvironment(m_skyboxTexture);
}

void Ocean::SetEnvironment(const std::shared_ptr<Texture>& panorama)
{
    auto& app = Application::Get();
    auto& computeQueue = app.GetCommandQueue(D3D12_COMMAND_LIST_TYPE_COMPUTE);
    auto commandList = computeQueue.GetCommandList();

    // Convert the 2D panorama to a cubemap.
    auto cubemapDesc = panorama->GetD3D12ResourceDesc();
    cubemapDesc.Width = cubemapDesc.Height = g_skyboxCubemapSize;
    cubemapDesc.DepthOrArraySize = 6;
    cubemapDesc.MipLevels = 0;

    auto skyboxCubemap = app.CreateTexture(cubemapDesc);
    skyboxCubemap->SetName(L"Skybox Cubemap");
    commandList->PanoToCubemap(skyboxCubemap, panorama, 0); // tweak mip quality to blur HDR

    D3D12_SHADER_RESOURCE_VIEW_DESC cubemapSRVDesc = {};
    cubemapSRVDesc.Format = cubemapDesc.Format;
    cubemapSRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    cubemapSRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
    cubemapSRVDesc.TextureCube.MipLevels = (UINT)-1;  // Use all mips.
    auto skyboxCubemapSRV = app.CreateShaderResourceView(skyboxCubemap, &cubemapSRVDesc);

    // Diffuse and specular irradiance cubemaps.
    DXGI_FORMAT irradianceFormat = DXGI_FORMAT_R16G16B16A16_FLOAT; // TODO: less precision might also be fine
    auto irradianceDesc = CD3DX12_RESOURCE_DESC::Tex2D(irradianceFormat, m_convolutionPlaneSize, m_convolutionPlaneSize, 6);
    irradianceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    auto specularDesc = CD3DX12_RESOURCE_DESC::Tex2D(irradianceFormat, g_brdfTextureSize, g_brdfTextureSize, 6, g_specularMipLevels);
    specularDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    auto diffuseIrradianceMap = app.CreateTexture(irradianceDesc);
    diffuseIrradianceMap->SetName(L"Diffuse Irradiance Texture");
    auto specularIrradianceMap = app.CreateTexture(specularDesc);
    specularIrradianceMap->SetName(L"Specular Irradiance Texture");

    D3D12_SHADER_RESOURCE_VIEW_DESC irradianceSRVDesc = {};
    irradianceSRVDesc.Format = irradianceFormat;
    irradianceSRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    irradianceSRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
    irradianceSRVDesc.TextureCube.MipLevels = 1;
    auto diffuseCubemapSRV = app.CreateShaderResourceView(diffuseIrradianceMap, &irradianceSRVDesc);
    irradianceSRVDesc.TextureCube.MipLevels = g_specularMipLevels;
    auto specularCubemapSRV = app.CreateShaderResourceView(specularIrradianceMap, &irradianceSRVDesc);

    m_convolutionPSO->Dispatch(commandList, skyboxCubemapSRV, diffuseIrradianceMap, m_convolutionPlaneSize, m_convolutionSampleCount);
    m_specularConvolutionPSO->DispatchSpecular(commandList, skyboxCubemapSRV, specularIrradianceMap, g_brdfTextureSize, 4096, g_specularMipLevels);

    computeQueue.WaitForFenceValue(computeQueue.ExecuteCommandList(commandList));

    // Swap them in. Frames still in flight keep the previous textures alive (command lists track them).
    m_skyboxTexture = panorama;
    m_skyboxCubemap = skyboxCubemap;
    m_skyboxCubemapSRV = skyboxCubemapSRV;
    m_diffuseIrradianceMap = diffuseIrradianceMap;
    m_specularIrradianceMap = specularIrradianceMap;
    m_diffuseCubemapSRV = diffuseCubemapSRV;
    m_specularCubemapSRV = specularCubemapSRV;

    m_unlitPSO->SetIBLTextures(m_diffuseCubemapSRV, m_specularCubemapSRV, m_brdfLUTSRV);
    m_displacementPSO->SetIBLTextures(m_diffuseCubemapSRV, m_specularCubemapSRV, m_brdfLUTSRV);
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

    // Editor UI: the scene is shown in a dockable viewport, the parameter windows become docked panels.
    m_editor = std::make_unique<Editor>(m_GUI);
    m_editor->SetCamera(&m_camera);
    m_editor->SetCameraClipPlanes(0.1f, 1000.0f);
    m_editor->AddMenuCallback([this]() { DrawMenu(); });
    m_editor->AddPanel("Ocean Parameters", [this]() { DrawOceanParameters(); }, Editor::DockSlot::Right, m_showOceanParams);
    m_editor->AddPanel("Light Parameters", [this]() { DrawLightParameters(); }, Editor::DockSlot::Right, m_showLightParams);

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

    m_oceanPlane = commandList->CreatePlane(OCEAN_PLANE_SIZE, OCEAN_PLANE_SIZE, OCEAN_SUBRES, OCEAN_SUBRES, false);

    m_skybox = commandList->CreateCube(1.0f, true);
    // Turned into the skybox cubemap and image based lighting by SetEnvironment, once the PSOs exist.
    auto skyPanorama = commandList->LoadTextureFromFile(L"assets/sky4k.hdr", true);

    // m_cubeMesh = commandList->CreateCube();
    m_helmet = commandList->LoadSceneFromFile(L"assets/damaged_helmet/DamagedHelmet.gltf");
    m_chessboard = commandList->LoadSceneFromFile(L"assets/chess/ABeautifulGame.gltf");
    m_boat = commandList->LoadSceneFromFile(L"assets/kenny/ship-large.obj");

    m_sphere = commandList->CreateSphere(0.1f);

    // Place the helmet once so it can be moved in the editor.
    if (m_helmet)
    {
        m_helmet->GetRootNode()->SetLocalTransform(
            XMMatrixRotationRollPitchYaw(XMConvertToRadians(-90.0f), 0.0f, 0.0f) * XMMatrixTranslation(0.0f, 2.0f, 0.0f));
        m_helmet->GetRootNode()->SetName("Damaged Helmet");
        m_editor->AddScene("Damaged Helmet", m_helmet);
    }
    m_oceanPlane->GetRootNode()->SetName("Ocean Plane");
    m_editor->AddScene("Ocean", m_oceanPlane);

    // m_defaultTexture = commandList->LoadTextureFromFile(L"assets/Mona_Lisa.jpg", true);
    // Set the patch sizes for the cascades.
    m_oceanPatchSizes.resize(m_oceanCascadesNumber);
    m_oceanPatchSizes[0] = 1500.0f;
    m_oceanPatchSizes[1] = 250.0f;
    m_oceanPatchSizes[2] = 17.0f;
    m_oceanPatchSizes[3] = 5.0f;
    UpdateSpectrumParameters();
    for (UINT i = 0; i < m_oceanCascadesNumber; ++i)
    {
    	GenerateH0(commandList, i);
    }

    auto fence = commandQueue.ExecuteCommandList(commandList);

    // RootParams to update H0 each frame
    CD3DX12_DESCRIPTOR_RANGE1 h0DescriptorRangeSRV(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0,
        D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE);
    CD3DX12_DESCRIPTOR_RANGE1 h0DescriptorRangeUAV(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 0, 0,
        D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE); // Volatile was needed because fft shader would have varying shaders. Sometimes SRV sometimes not, which causes UAV to be bound to SRV.

    CD3DX12_ROOT_PARAMETER1 H0RootParameters[OceanCompute::RootParameters::NumRootParameters];
    H0RootParameters[OceanCompute::RootParameters::ReadTextures].InitAsDescriptorTable(1, &h0DescriptorRangeSRV); // SRV t0
    H0RootParameters[OceanCompute::RootParameters::WriteTextures].InitAsDescriptorTable(1, &h0DescriptorRangeUAV); // UAV u0
    H0RootParameters[OceanCompute::RootParameters::Constants].InitAsConstants(2, 0); // CBV b0

    // FFT 
    CD3DX12_DESCRIPTOR_RANGE1 FFTDescriptorRangeUAV(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0,
        D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE);

    CD3DX12_ROOT_PARAMETER1 FFTRootParameters[2];
    FFTRootParameters[0].InitAsDescriptorTable(1, &FFTDescriptorRangeUAV); // UAV u0
    FFTRootParameters[1].InitAsConstants(1, 0); // CBV b0

    // Permute
    CD3DX12_DESCRIPTOR_RANGE1 permuteDescriptorRangeUAV(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 3, 0);
    CD3DX12_ROOT_PARAMETER1 permuteRootParameters[2];
    permuteRootParameters[0].InitAsDescriptorTable(1, &permuteDescriptorRangeUAV); // UAV
    permuteRootParameters[1].InitAsConstants(4, 0); // 4 floats, register b0

    // Convolution
    CD3DX12_DESCRIPTOR_RANGE1 convolutionSRVRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
    CD3DX12_DESCRIPTOR_RANGE1 convolutionUAVRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);

    CD3DX12_ROOT_PARAMETER1 convolutionRootParameters[3];
    convolutionRootParameters[0].InitAsConstants(4, 0);                    
    convolutionRootParameters[1].InitAsDescriptorTable(1, &convolutionSRVRange);   
    convolutionRootParameters[2].InitAsDescriptorTable(1, &convolutionUAVRange);   

    // brdf 
    CD3DX12_DESCRIPTOR_RANGE1 brdfUAVRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);
    CD3DX12_ROOT_PARAMETER1 brdfLutRootParameters[1];
    brdfLutRootParameters[0].InitAsDescriptorTable(1, &brdfUAVRange);

    m_unlitPSO = std::make_shared<EffectPSO>(m_camera, L"/vertex.cso", L"/pixel.cso");
    m_displacementPSO = std::make_shared<OceanPSO>(m_camera, L"/ocean_vertex.cso", L"/ocean_pixel.cso", m_oceanCascadesNumber, m_oceanPatchSizes);
    m_oceanPSO = std::make_shared<OceanCompute>(L"/animate_waves.cso", H0RootParameters, _countof(H0RootParameters));
    m_fftPSO = std::make_shared<OceanCompute>(L"/fft.cso", FFTRootParameters, _countof(FFTRootParameters));
    m_permutePSO = std::make_shared<OceanCompute>(L"/permute.cso", permuteRootParameters, _countof(permuteRootParameters));
    m_convolutionPSO = std::make_shared<ConvolutionCompute>(L"/ibl_convolution.cso", convolutionRootParameters, _countof(convolutionRootParameters));
    m_specularConvolutionPSO = std::make_shared<ConvolutionCompute>(L"/ibl_specular.cso", convolutionRootParameters, _countof(convolutionRootParameters));
    m_brdfLutPSO = std::make_shared<ConvolutionCompute>(L"/brdf_lut.cso", brdfLutRootParameters, _countof(brdfLutRootParameters));
    m_skyboxPSO = std::make_shared<SkyboxPSO>(L"/skybox_VS.cso", L"/skybox_PS.cso");
    m_sdrPSO = std::make_shared<SDRPSO>(L"/HDR_to_SDR_VS.cso", L"/HDR_to_SDR_PS.cso");
    // m_permuteSlopePSO = std::make_shared<OceanCompute>(L"/permute_slopemap.cso", FFTRootParameters, _countof(FFTRootParameters));


    commandQueue.WaitForFenceValue(fence);

    auto& computeQueue = app.GetCommandQueue(D3D12_COMMAND_LIST_TYPE_COMPUTE);
    auto computeCommandList = computeQueue.GetCommandList();

    // The BRDF lookup table does not depend on the environment.
    CreateBRDFLUT();

    // Update Render params for the ocean.
    m_displacementPSO->m_oceanRenderParams.scatterColor = XMFLOAT4(0.0f, 0.5f, 0.4f, 1.0f);
    m_displacementPSO->m_oceanRenderParams.oceanrColor = XMFLOAT4(0.0f, 0.15f, 0.3f, 1.0f);
    m_displacementPSO->m_oceanRenderParams.IBLIntensity = 0.4f;
    m_displacementPSO->m_oceanRenderParams.heightMod = 2.0f;
    m_displacementPSO->m_oceanRenderParams.peakScatterIntensity = 0.6f;


    struct Constants
    {
        float foamDecay;
        float foamBias;
        float foamAdd;
        float foamThreshold;
    } cbv;
    // // old foam settings (mayb good mayb shit)
    // cbv.foamDecay = 0.005f;
    // cbv.foamBias = 0.3f;
    // cbv.foamAdd = 1.0f;
    // cbv.foamThreshold = 0.0f;

    // Foam settings after playing with it
    cbv.foamDecay = 0.006f;
    cbv.foamBias = 0.407f;
    cbv.foamAdd = 0.051f;
    cbv.foamThreshold = 0.181f;

    m_foamParameters.resize(sizeof(Constants) / 4);
    m_foamParameters = { cbv.foamDecay, cbv.foamBias, cbv.foamAdd, cbv.foamThreshold };
    
    // Tonemapped output shown in the editor's Scene panel (same format as the swapchain for the SDR pass).
    auto viewportDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, m_width, m_height, 1, 1, 1, 0,
        D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
    m_viewportTexture = Application::Get().CreateTexture(viewportDesc);
    m_viewportTexture->SetName(L"Scene Viewport (LDR)");
    m_viewportRenderTarget.AttachTexture(AttachmentPoint::Color0, m_viewportTexture);
    ResizeViewport(m_editor->GetViewportWidth(), m_editor->GetViewportHeight());

    auto convolutionFence = computeQueue.ExecuteCommandList(computeCommandList);
    computeQueue.WaitForFenceValue(convolutionFence);

    // Skybox cubemap and image based lighting (also when an HDR file is dropped into the editor).
    SetEnvironment(skyPanorama);
    m_editor->SetEnvironmentCallback([this](const std::shared_ptr<Texture>& panorama) { SetEnvironment(panorama); });
    m_editor->SetRenderGraph(&m_renderGraph);

    m_pWindow->RegisterCallbacks(shared_from_this());
    m_pWindow->Show();

    return true;
}

void Ocean::OnUpdate(UpdateEventArgs& e)
{
    static uint64_t frameCount = 0;
    static double totalTime = 0.0;
    static double oceanTime = 0.0;

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

    UpdateImageBasedLighting();

    // Update Camera
    float speedMultiplier = (m_shift ? 64.0f : 16.0f);
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
    m_directionalLights[1].position = XMFLOAT3(0.0f, 30.0f, 0.0f);
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

    // The ocean simulation runs in the render graph (RecordRenderGraph).
    m_oceanTime = oceanTime;

    m_unlitPSO->SetDirectionalLights(m_directionalLights);
    m_displacementPSO->SetDirectionalLights(m_directionalLights);
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
    m_displacementPSO->SetPointLights(m_pointLights);

    // Update heightmap
    // UpdateSpectrum(time);

    OnRender();

}

void Ocean::OnRender()
{
    // Full screen toggles requested by the menu or F11 are applied here, before the frame is recorded:
    // switching resizes the swap chain. (The window itself also enters full screen when it is maximized.)
    if (m_toggleFullscreen)
    {
        m_pWindow->ToggleFullScreen();
        m_toggleFullscreen = false;
    }

    // Follow the size of the editor's Scene panel (known from the previous frame).
    if (m_editor->ConsumeViewportResize())
    {
        ResizeViewport(m_editor->GetViewportWidth(), m_editor->GetViewportHeight());
    }

    auto& commandQueue = Application::Get().GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT);
    auto commandList = commandQueue.GetCommandList();

    if (m_isLoading)
    {
        FLOAT clearColor[] = { 0.9f, 0.2f, 0.4f, 1.0f };
        commandList->ClearTexture(m_swapChain->GetRenderTarget().GetTexture(AttachmentPoint::Color0), clearColor);
    }
    else
    {
        RecordRenderGraph(commandList);
    }

    // The scene is shown inside the editor, so the back buffer only holds the GUI.
    {
        FLOAT clearColor[] = { 0.078f, 0.078f, 0.078f, 1.0f };
        commandList->ClearTexture(m_swapChain->GetRenderTarget().GetTexture(AttachmentPoint::Color0), clearColor);
    }

	    // Render the GUI on the render target
	    OnGUI(commandList, m_swapChain->GetRenderTarget());
	    commandQueue.ExecuteCommandList(commandList);

    m_swapChain->Present();
}

void Ocean::RecordRenderGraph(const std::shared_ptr<CommandList>& commandList)
{
    RenderGraph& rg = m_renderGraph;

    // Ocean simulation, per cascade: animate the spectrum, inverse FFT (horizontal, then vertical) the displacement
    // and slope, then permute and accumulate foam. The cascade textures are simulation state that lives across
    // frames, so they are imported. The graph adds the UAV barriers between the passes.
    struct CascadeTextures
    {
        RGTexture h0, slope, displacement, foam;
    } cascades[m_oceanCascadesNumber];

    const uint32_t phaseGroups = OCEAN_SUBRES / 16;
    for (UINT i = 0; i < m_oceanCascadesNumber; ++i)
    {
        const std::string cascade = " " + std::to_string(i);
        CascadeTextures& c = cascades[i];
        c.h0 = rg.Import("H0" + cascade, m_oceanCascades[i].H0Texture);
        c.slope = rg.Import("Slope" + cascade, m_oceanCascades[i].slopeTexture);
        c.displacement = rg.Import("Displacement" + cascade, m_oceanCascades[i].displacementTexture);
        c.foam = rg.Import("Foam" + cascade, m_oceanCascades[i].foamTexture);

        rg.AddPass("Ocean Spectrum" + cascade)
            .Read(c.h0, RGAccess::NonPixelShaderRead)
            .Write(c.slope)
            .Write(c.displacement)
            .Execute([this, i, c, phaseGroups](RGPassContext& context)
            {
                m_oceanPSO->Dispatch(context.GetCommandListPtr(), context.GetTexture(c.h0), context.GetTexture(c.slope),
                    context.GetTexture(c.displacement), static_cast<float>(m_oceanTime), m_oceanPatchSizes[i],
                    XMUINT3(phaseGroups, phaseGroups, 1));
            });

        const std::pair<const char*, RGTexture> ffts[] = { { "Displacement", c.displacement }, { "Slope", c.slope } };
        for (const auto& fft : ffts)
        {
            const RGTexture texture = fft.second;
            for (uint32_t direction = 0; direction < 2; ++direction)
            {
                rg.AddPass(std::string("FFT ") + fft.first + (direction == 0 ? " Horizontal" : " Vertical") + cascade)
                    .Write(texture)
                    .Execute([this, texture, direction](RGPassContext& context)
                    {
                        m_fftPSO->Dispatch(context.GetCommandListPtr(), context.GetTexture(texture), XMUINT3(OCEAN_SUBRES, 1, 1), direction);
                    });
            }
        }

        rg.AddPass("Ocean Permute" + cascade)
            .Write(c.slope)
            .Write(c.displacement)
            .Write(c.foam)
            .Execute([this, c, phaseGroups](RGPassContext& context)
            {
                m_permutePSO->Dispatch(context.GetCommandListPtr(), context.GetTexture(c.slope), context.GetTexture(c.displacement),
                    context.GetTexture(c.foam), m_foamParameters, XMUINT3(phaseGroups, phaseGroups, 1));
            });
    }

    // The skybox and image based lighting come from SetEnvironment. Imported so the graph shows who reads them.
    const RGTexture skyboxCubemap = rg.Import("Skybox Cubemap", m_skyboxCubemap);
    const RGTexture diffuseIrradiance = rg.Import("Diffuse Irradiance", m_diffuseIrradianceMap);
    const RGTexture specularIrradiance = rg.Import("Specular Irradiance", m_specularIrradianceMap);
    const RGTexture brdfLUT = rg.Import("BRDF LUT", m_brdfLUT);

    // The scene is rendered at the size of the editor's Scene panel. These only live during the frame: the graph
    // creates them (again when the panel is resized) and reuses them in the next frames.
    const RGTexture hdrColor = rg.Create("HDR Color", RGTextureDesc::Tex2D(DXGI_FORMAT_R16G16B16A16_FLOAT, m_viewportWidth, m_viewportHeight));
    const RGTexture depth = rg.Create("Depth Render Target", RGTextureDesc::Tex2D(DXGI_FORMAT_D32_FLOAT, m_viewportWidth, m_viewportHeight));

    rg.AddPass("Skybox")
        .Read(skyboxCubemap, RGAccess::PixelShaderRead)
        .RenderTarget(hdrColor, RGClear::Color(0.4f, 0.6f, 0.9f, 1.0f))
        .DepthStencil(depth, RGClear::Depth(1.0f))
        .Execute([this](RGPassContext& context)
        {
            SceneVisitor skyboxVisitor(context.GetCommandList(), m_camera, *m_skyboxPSO, false);
            m_skybox->Accept(skyboxVisitor);
        });

    // The ocean's vertex shader displaces the plane, its pixel shader shades it: both read the cascades.
    auto oceanPass = rg.AddPass("Ocean Surface");
    for (const auto& c : cascades)
    {
        oceanPass.Read(c.displacement).Read(c.slope).Read(c.foam);
    }
    // The plane's geometry, so its vertices and indices can be inspected in the editor (Render Graph > Buffer).
    for (const auto& mesh : m_oceanPlane->GetRootNode()->GetMeshes())
    {
        for (const auto& [slot, vertexBuffer] : mesh->GetVertexBuffers())
        {
            oceanPass.Read(rg.Import("Ocean Plane Vertices", vertexBuffer, static_cast<uint32_t>(vertexBuffer->GetVertexStride())),
                RGAccess::VertexBuffer);
        }
        if (const auto indexBuffer = mesh->GetIndexBuffer())
        {
            const uint32_t indexSize = indexBuffer->GetIndexFormat() == DXGI_FORMAT_R16_UINT ? 2 : 4;
            oceanPass.Read(rg.Import("Ocean Plane Indices", indexBuffer, indexSize), RGAccess::IndexBuffer);
        }
    }
    oceanPass
        .Read(diffuseIrradiance)
        .Read(specularIrradiance)
        .Read(brdfLUT)
        .RenderTarget(hdrColor)
        .DepthStencil(depth)
        .Execute([this](RGPassContext& context)
        {
            for (UINT i = 0; i < m_oceanCascadesNumber; ++i)
            {
                m_displacementPSO->SetOceanTextures(m_oceanCascades[i].displacementTexture, m_oceanCascades[i].slopeTexture,
                    m_oceanCascades[i].foamTexture, i);
            }
            SceneVisitor oceanVisitor(context.GetCommandList(), m_camera, *m_displacementPSO, false);
            m_oceanPlane->Accept(oceanVisitor);
        });

    rg.AddPass("Objects")
        .Read(diffuseIrradiance, RGAccess::PixelShaderRead)
        .Read(specularIrradiance, RGAccess::PixelShaderRead)
        .Read(brdfLUT, RGAccess::PixelShaderRead)
        .RenderTarget(hdrColor)
        .DepthStencil(depth)
        .Execute([this](RGPassContext& context)
        {
            SceneVisitor visitor(context.GetCommandList(), m_camera, *m_unlitPSO, false);
            if (m_helmet)
            {
                m_helmet->Accept(visitor);
            }

            // Models dropped into the scene from the editor's Assets panel.
            for (const auto& scene : m_editor->GetSpawnedScenes())
            {
                scene->Accept(visitor);
            }

            // Visualize the point lights as small spheres.
            for (const auto& light : m_pointLights)
            {
                m_sphere->GetRootNode()->SetLocalTransform(XMMatrixTranslationFromVector(XMLoadFloat4(&light.positionWS)));
                m_sphere->Accept(visitor);
            }
        });

    // HDR -> SDR into the image the editor's Scene panel shows.
    const RGTexture viewport = rg.Import("Scene Viewport", m_viewportTexture);
    rg.AddPass("Tonemap")
        .Read(hdrColor, RGAccess::PixelShaderRead)
        .RenderTarget(viewport)
        .Execute([this, hdrColor](RGPassContext& context)
        {
            m_sdrPSO->SetHDRTexture(context.GetTexture(hdrColor));
            m_sdrPSO->Apply(context.GetCommandList());
        });

    rg.Execute(commandList);
}

void Ocean::OnGUI(const std::shared_ptr<CommandList>& commandList, const RenderTarget& renderTarget)
{
    m_editor->BeginFrame();
    m_editor->SetSceneTexture(m_isLoading ? nullptr : m_viewportTexture);

    if (m_isLoading)
    {
        ImGui::SetNextWindowPos(ImVec2(m_width / 2.0f, m_height / 2.0f), 0, ImVec2(0.5, 0.5));
        ImGui::SetNextWindowSize(ImVec2(m_width / 2.0f, 0));
        ImGui::Begin("Loading", nullptr,
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar);
        ImGui::ProgressBar(m_loadingProgress);
        ImGui::Text(m_loadingText.c_str());
        if (!m_cancelLoading) { if (ImGui::Button("Cancel")) m_cancelLoading = true; }
        else ImGui::Text("Cancelling...");
        ImGui::End();
    }

    m_editor->EndFrame(commandList, renderTarget);
}

void Ocean::DrawMenu()
{
    if (ImGui::BeginMenu("File"))
    {
        if (ImGui::MenuItem("Open file...", "Ctrl+O", nullptr, !m_isLoading))
            m_showFileOpenDialog = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Exit", "Esc"))
            Application::Get().Stop();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Options"))
    {
        bool vSync = m_swapChain->GetVSync();
        if (ImGui::MenuItem("V-Sync", "V", &vSync))
            m_swapChain->SetVSync(vSync);
        if (ImGui::MenuItem("Full screen", "F11", m_pWindow->IsFullScreen()))
            m_toggleFullscreen = true;
        ImGui::EndMenu();
    }
}

// ── Ocean Parameters ─────────────────────────────────────────────────────
void Ocean::DrawOceanParameters()
{
    bool paramsChanged = false;

    auto Slider = [&](const char* label, float* v, float vmin, float vmax, const char* tooltip = nullptr) -> bool
        {
            bool changed = ImGui::SliderFloat(label, v, vmin, vmax);
            if (tooltip && ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", tooltip);
            return ImGui::IsItemDeactivatedAfterEdit();
        };

    auto SectionHeader = [](const char* label, ImVec4 color)
        {
            ImGui::Spacing();
            ImGui::TextColored(color, label);
            ImGui::Separator();
        };

    // ── Wind ──
    if (ImGui::CollapsingHeader("  Wind", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Indent();
        paramsChanged |= Slider("Speed (m/s)", &m_jonswapParams.windSpeed, 0.0f, 100.0f, "Wind speed in metres per second");
        paramsChanged |= Slider("Direction (deg)", &m_jonswapParams.windDirection, 0.0f, 360.0f, "Wind direction in degrees");
        paramsChanged |= Slider("Fetch (m)", &m_jonswapParams.fetch, 1000.0f, 1000000.0f, "Distance over which wind acts on the surface");
        ImGui::Unindent();
    }

    // ── Spectrum ──
    if (ImGui::CollapsingHeader("  Spectrum", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Indent();
        paramsChanged |= Slider("Scale", &m_jonswapParams.scale, 0.0f, 5.0f, "Overall wave amplitude scale");
        paramsChanged |= Slider("Gamma", &m_jonswapParams.gamma, 0.0f, 7.0f, "Peak sharpness of the JONSWAP spectrum");
        paramsChanged |= Slider("Swell", &m_jonswapParams.swell, 0.0f, 1.0f, "Swell influence on wave choppiness");
        paramsChanged |= Slider("Spread Blend", &m_jonswapParams.spreadBlend, 0.0f, 1.0f, "Blend between directional and isotropic spreading");
        paramsChanged |= Slider("Short Wave Fade", &m_jonswapParams.shortWavesFade, 0.0f, 1.0f, "Fade out short high-frequency waves");

        ImGui::Spacing();
        ImGui::TextDisabled("Alpha:      %.6f", m_jonswapParams.alpha);
        ImGui::TextDisabled("Peak Omega: %.4f", m_jonswapParams.peakOmega);
        ImGui::Unindent();
    }

    // ── Lighting ──
    if (ImGui::CollapsingHeader("  Lighting", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Indent();
        ImGui::Text("Ocean Color");
        ImGui::ColorEdit4("##oceanColor", &m_displacementPSO->m_oceanRenderParams.oceanrColor.x,
            ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel);
        ImGui::SameLine(); ImGui::Text("Scatter Color");
        ImGui::ColorEdit4("##scatterColor", &m_displacementPSO->m_oceanRenderParams.scatterColor.x,
            ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel);

        ImGui::Spacing();
        Slider("Height Modifier", &m_displacementPSO->m_oceanRenderParams.heightMod, 0.0f, 5.0f, "Vertical scale of SSS wave height proxy");
        Slider("Peak Scatter", &m_displacementPSO->m_oceanRenderParams.peakScatterIntensity, 0.0f, 1.0f, "Subsurface scatter intensity at wave crests");
        Slider("IBL Intensity", &m_displacementPSO->m_oceanRenderParams.IBLIntensity, 0.0f, 1.0f, "Image-based lighting contribution");

        float objectIBL = m_unlitPSO->GetIBLIntensity();
        if (ImGui::SliderFloat("Object IBL", &objectIBL, 0.0f, 2.0f))
        {
            m_unlitPSO->SetIBLIntensity(objectIBL);
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Image-based lighting strength of the objects (models), 1 = physically based");
        }
        ImGui::Unindent();
    }

    // ── Foam ──
    if (ImGui::CollapsingHeader("  Foam", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Indent();
        ImGui::SliderFloat("Decay", &m_foamParameters[0], 0.0f, 0.1f);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("How quickly foam fades per frame (lower = longer lasting)");
        ImGui::SliderFloat("Bias", &m_foamParameters[1], -1.0f, 1.0f);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Jacobian threshold for foam generation (lower = more foam)");
        ImGui::SliderFloat("Intensity", &m_foamParameters[2], 0.0f, 2.0f);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Amount of foam added when a wave breaks");
        ImGui::SliderFloat("Threshold", &m_foamParameters[3], 0.0f, 1.0f);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Minimum biased Jacobian value to trigger foam");
        ImGui::Unindent();
    }

    if (paramsChanged)
    {
        m_jonswapParams.angle = m_jonswapParams.windDirection / 180.0f * PI;
        m_jonswapParams.alpha = JonswapAlpha(m_jonswapParams.fetch, m_jonswapParams.windSpeed);
        m_jonswapParams.peakOmega = JonswapPeakFequency(m_jonswapParams.fetch, m_jonswapParams.windSpeed);
        auto& cq = Application::Get().GetCommandQueue(D3D12_COMMAND_LIST_TYPE_COPY);
        auto cl = cq.GetCommandList();
        for (UINT i = 0; i < m_oceanCascadesNumber; ++i)
            GenerateH0(cl, i);
        cq.ExecuteCommandList(cl);
    }
}

// ── Light Parameters ─────────────────────────────────────────────────────
void Ocean::DrawLightParameters()
{
    bool paramsChanged = false;

    if (ImGui::CollapsingHeader("  Directional Light", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Indent();
        paramsChanged |= ImGui::DragFloat3("Position", &m_directionalLights[0].position.x, 0.1f);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("World-space position (used to derive light direction)");
        ImGui::Text("Color");
        paramsChanged |= ImGui::ColorEdit4("##dirColor", &m_directionalLights[0].color.x,
            ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel);
        ImGui::Unindent();
    }
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
    for (UINT i = 0; i < m_oceanCascadesNumber; ++i)
    {
    	m_oceanCascades[i].H0Texture.reset();
        m_oceanCascades[i].slopeTexture.reset();
        m_oceanCascades[i].displacementTexture.reset();
        m_oceanCascades[i].foamTexture.reset();
    }
    m_skyboxTexture.reset();
    m_skyboxCubemap.reset();
    m_skyboxCubemapSRV.reset();
    m_skyboxSignature.reset();
    m_HDRRootSignature.reset();
    m_SDRRootSignature.reset();
    m_HDRPSO.reset();
	m_SDRPipelineState.reset();

    m_editor.reset();
    m_viewportTexture.reset();

    // m_GUI.reset();
    // m_swapChain.reset();

}

float Ocean::InitPhillipsSpectrum(XMFLOAT2 k, XMFLOAT2 windDir, float windSpeed, float A)
{
    // A * exp(-1/(magnitudeK * L)^2)/k^4 * dot(k,w)^2 * exp(-k^2 * L^2)
    
    const float l = 128 * 0.00001; // Patch size hardcoded because i dont want to remove this function yet.

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


void Ocean::GenerateH0(std::shared_ptr<CommandList> commandList, const UINT cascade) {

    float patchSize = m_oceanPatchSizes[cascade];
    float deltaK = 2.0f * PI / patchSize;
    float highestK = (OCEAN_SUBRES / 2.0f) * deltaK; // nyquist limit

    // moved to heap to avoid stack overflow
    std::vector<std::complex<float>> H0(OCEAN_SUBRES * OCEAN_SUBRES, { 0,0 });
    std::vector<std::complex<float>> H0Conj(OCEAN_SUBRES * OCEAN_SUBRES, { 0,0 });

    const float lowCutoff = cascade == 0 ? 0.001f : (OCEAN_SUBRES * PI / m_oceanPatchSizes[cascade - 1]); // nyquist limit of previous cascade;
    const float highCutoff = highestK;
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
            H0[m * OCEAN_SUBRES + n] = std::complex<float>(xiR * amplitude, xiI * amplitude);
            }
        }
    }

    // Needs its own loop since the conjugate needs all data of H0 to be valid
    for (int m = 0; m < OCEAN_SUBRES; m++) {
        for (int n = 0; n < OCEAN_SUBRES; n++) {
            int m_minus = (OCEAN_SUBRES - m) % OCEAN_SUBRES;
            int n_minus = (OCEAN_SUBRES - n) % OCEAN_SUBRES;
            H0Conj[m * OCEAN_SUBRES + n] = std::conj(H0[m_minus * OCEAN_SUBRES + n_minus]);
        }
    }
	
    // TODO: the tex formats can probably be 16bit rather than 32
    // Input texture SRV
    DXGI_FORMAT H0Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    auto H0Desc = CD3DX12_RESOURCE_DESC::Tex2D(H0Format, OCEAN_SUBRES, OCEAN_SUBRES);
    
    // output phase texture UAV
    DXGI_FORMAT phaseFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
    auto phaseDesc = CD3DX12_RESOURCE_DESC::Tex2D(phaseFormat, OCEAN_SUBRES, OCEAN_SUBRES);
    phaseDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    DXGI_FORMAT foamFormat = DXGI_FORMAT_R32_FLOAT;
    auto foamDesc = CD3DX12_RESOURCE_DESC::Tex2D(foamFormat, OCEAN_SUBRES, OCEAN_SUBRES);
    foamDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;


	// Copy H0 texture data
    m_oceanCascades[cascade].H0Texture = Application::Get().CreateTexture(H0Desc);
    m_oceanCascades[cascade].H0Texture->SetName(L"H0 Texture" + std::to_wstring(cascade));

    std::vector<uint16_t> combinedData(OCEAN_SUBRES * OCEAN_SUBRES * 4);

    for (int m = 0; m < OCEAN_SUBRES; m++) {
        for (int n = 0; n < OCEAN_SUBRES; n++) {
            int index = (m * OCEAN_SUBRES + n) * 4;
            combinedData[index + 0] = FloatToHalf(H0[m * OCEAN_SUBRES + n].real());       // R: H0 real
            combinedData[index + 1] = FloatToHalf(H0[m * OCEAN_SUBRES + n].imag());       // G: H0 imaginary
            combinedData[index + 2] = FloatToHalf(H0Conj[m * OCEAN_SUBRES + n].real());   // B: H0_conj real
            combinedData[index + 3] = FloatToHalf(H0Conj[m * OCEAN_SUBRES + n].imag());   // A: H0_conj imaginary
        }
    }

    D3D12_SUBRESOURCE_DATA subData = {};
    subData.pData = combinedData.data();
    subData.RowPitch = OCEAN_SUBRES * 4 * sizeof(uint16_t); // 4 channels * 2 bytes
    subData.SlicePitch = subData.RowPitch * OCEAN_SUBRES;


    // TODO clean this up... separate the jobs properly
    // TODO also use one application get rather than keep calling it

    // Create slope output texture
    m_oceanCascades[cascade].slopeTexture = Application::Get().CreateTexture(phaseDesc);
    m_oceanCascades[cascade].slopeTexture->SetName(L"Slope Output Texture" + std::to_wstring(cascade));

    // Create phase output texture
    m_oceanCascades[cascade].displacementTexture = Application::Get().CreateTexture(phaseDesc);
    m_oceanCascades[cascade].displacementTexture->SetName(L"Displacement Output Texture" + std::to_wstring(cascade));

    // foam texture
    m_oceanCascades[cascade].foamTexture = Application::Get().CreateTexture(foamDesc);
    m_oceanCascades[cascade].foamTexture->SetName(L"Jacobian foam Texture" + std::to_wstring(cascade));



    commandList->CopyTextureSubresource(m_oceanCascades[cascade].H0Texture, 0, 1, &subData);
}

float Ocean::GaussianRandom() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::normal_distribution<float> dist(0.0f, 1.0f); // mean=0, std=1

    return dist(gen);
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

    // Only react to keys when the scene viewport has focus (not while typing in a panel).
    if (m_editor && !m_editor->IsViewportKeyboardActive())
    {
        return;
    }

    switch (e.key)
    {
    case KeyCode::Escape:
        Application::Get().Stop();
        break;
    case KeyCode::Enter:
        if (e.alt)
        {
    case KeyCode::F11:
        m_toggleFullscreen = true;
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

    if (m_editor && m_editor->IsViewportMouseActive())
    {
        if (e.rightButton)
        {
            m_pitch += e.deltaY * mouseSpeed;

            m_pitch = std::clamp(m_pitch, -90.0f, 90.0f);

            m_yaw += e.deltaX * mouseSpeed;
        }
    }
}

void Ocean::OnMouseWheel(MouseWheelEventArgs& e)
{
    if (m_editor && m_editor->IsViewportMouseActive())
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



