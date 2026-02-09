#include "ocean_scene.h"


#include <iostream>
#include <Shlwapi.h>

#include "core/EV.h"

#include <DirectXColors.h>
#include <random>

using namespace DirectX;
using namespace EV;

#define OCEAN_SUBRES 256
#define OCEAN_SIZE 100.0f
#define PI 3.14159265359f


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

    Init();     
}

Ocean::~Ocean()
{
    _aligned_free(m_pAlignedCameraData);
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

    // Loading helmet to not load big ass sponza.
    // m_loadingTask = std::async(std::launch::async, std::bind(&Ocean::LoadScene, this,
    //     L"assets/damaged_helmet/DamagedHelmet.gltf"));


    auto& commandQueue = app.GetCommandQueue(D3D12_COMMAND_LIST_TYPE_COPY);
    auto commandList = commandQueue.GetCommandList();

    m_oceanPlane = commandList->CreatePlane(OCEAN_SIZE, OCEAN_SIZE, OCEAN_SUBRES, OCEAN_SUBRES);

    // m_cubeMesh = commandList->CreateCube();
    // m_helmet = commandList->LoadSceneFromFile(L"assets/damaged_helmet/DamagedHelmet.gltf");
    // m_chessboard = commandList->LoadSceneFromFile(L"assets/chess/ABeautifulGame.gltf");

    m_sphere = commandList->CreateSphere(0.1f);
    // m_sphere = commandList->LoadSceneFromFile(L"assets/damaged_helmet/DamagedHelmet.gltf");

    // m_defaultTexture = commandList->LoadTextureFromFile(L"assets/Mona_Lisa.jpg", true);

    auto fence = commandQueue.ExecuteCommandList(commandList);

    m_unlitPSO = std::make_shared<EffectPSO>(m_camera, L"/vertex.cso", L"/pixel.cso", false, false);
    m_oceanPSO = std::make_shared<EffectPSO>(m_camera, L"/vertex.cso", L"/ocean_pixel.cso", false, false);


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

    // Update heightmap
    UpdateSpectrum(time);
    OnRender();

}

void Ocean::OnRender()
{

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
        SceneVisitor oceanVisitor(*commandList, m_camera, *m_oceanPSO, false);


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

        // REMINDER: Transform is built with Scale * Rotation * Translation (SRT)
        XMMATRIX scale = XMMatrixScaling(10.0f, 10.0f, 10.0f);
        XMMATRIX rotation = XMMatrixRotationRollPitchYaw(XMConvertToRadians(-90.0f), 0.0f, 0.0f);
        XMMATRIX translation = XMMatrixTranslation(0.0f, -12.0f, 0.0f);

        // m_scene->GetRootNode()->SetLocalTransform(scale * XMMatrixIdentity() * translation);
        // m_scene->Accept(visitor);

        m_oceanPlane->Accept(oceanVisitor);

        XMMATRIX helmetTranslation = XMMatrixTranslation(0.0f, 2.0f, 0.0f);
        // m_helmet->GetRootNode()->SetLocalTransform(XMMatrixIdentity() * rotation * helmetTranslation);
        // m_helmet->Accept(visitor);

        // m_chessboard->GetRootNode()->SetLocalTransform(XMMatrixIdentity() * XMMatrixIdentity() * translation);

        // m_chessboard->Accept(visitor);

    	// Visualize the point light as a small sphere
        for (const auto& l : m_pointLights)
        {
            auto lightPos = XMLoadFloat4(&l.positionWS);
            auto worldMatrix = XMMatrixTranslationFromVector(lightPos);
            m_sphere->GetRootNode()->SetLocalTransform(worldMatrix);
            m_sphere->Accept(visitor);
        }

        // Resolve the MSAA render target to the swapchain's backbuffer.
        auto& swapChainRT = m_swapChain->GetRenderTarget();
        auto  swapChainBackBuffer = swapChainRT.GetTexture(AttachmentPoint::Color0);
        auto  msaaRenderTarget = m_renderTarget.GetTexture(AttachmentPoint::Color0);

        commandList->ResolveSubresource(swapChainBackBuffer, msaaRenderTarget);

    }
    // Render the GUI on the render target
    OnGUI(commandList, m_swapChain->GetRenderTarget());

    commandQueue.ExecuteCommandList(commandList);

    m_swapChain->Present();
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


void Ocean::UnloadContent()
{

}

bool Ocean::Init()
{
    GenerateH0();
    return 1;
}


float Ocean::InitPhillipsSpectrum(XMFLOAT2 k, XMFLOAT2 windDir, float windSpeed, float A)
{
    // A * exp(-1/(magnitudeK * L)^2)/k^4 * dot(k,w)^2 * exp(-k^2 * L^2)
    
    float g = 9.81f;
    float magnitudeK = sqrtf(k.x * k.x + k.y * k.y);
    float V2 = windSpeed * windSpeed;
    float L = V2 / g;
    float L2 = L * L;
    float k2 = magnitudeK * magnitudeK;
    float k4 = k2 * k2;

    float kDotW = k.x * windDir.x + k.y * windDir.y;
    float kDotW2 = kDotW * kDotW;

    return A * (exp(-1.0f / (k2 * L2)) / k4) * kDotW2 * exp(-k2 * L2);

}


void Ocean::GenerateH0() {
    const float L = OCEAN_SIZE; // Patch size

    for (int m = 0; m < OCEAN_SUBRES; m++) {
        for (int n = 0; n < OCEAN_SUBRES; n++) {
            // Get wave vector for this frequency
            float kx = (2.0f * PI * (n - OCEAN_SUBRES / 2.0f)) / L;
            float ky = (2.0f * PI * (m - OCEAN_SUBRES / 2.0f)) / L;
            XMFLOAT2 k(kx, ky);

            // Generate two independent gaussian random numbers
            float xiR = GaussianRandom();
            float xiI = GaussianRandom();

            XMFLOAT2 windDir(1.0f, 0.0f);
            float windSpeed = 30.0f;

            float Ph = InitPhillipsSpectrum(k, windDir,windSpeed);

            // Equation 42: h0(k) = (1/√2) * (gausRandom + i*gausRandomI) * sqrt(Ph(k))
            H0[m][n] = (1.0f / sqrtf(2.0f)) * std::complex<float>(xiR, xiI) * sqrtf(Ph);

            int m_minus = (OCEAN_SUBRES - m) % OCEAN_SUBRES;
            int n_minus = (OCEAN_SUBRES - n) % OCEAN_SUBRES;

            H0Conj[m][n] = std::conj(H0[m_minus][n_minus]);

        }
    }

    // TODO: Should this be in a different loop or can it be in the same one?
    for (int m = 0; m < OCEAN_SUBRES; m++) {
        for (int n = 0; n < OCEAN_SUBRES; n++) {
            int m_minus = (OCEAN_SUBRES - m) % OCEAN_SUBRES;
            int n_minus = (OCEAN_SUBRES - n) % OCEAN_SUBRES;

            H0Conj[m][n] = std::conj(H0[m_minus][n_minus]);
        }
    }

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



