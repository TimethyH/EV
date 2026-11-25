#include "ocean_scene.h"


#include <iostream>
#include <Shlwapi.h>

#include "core/EV.h"

#include <DirectXColors.h>
#include <filesystem>
#include <fstream>
#include <random>

#include "nlohmann/json.hpp"

#include "compute_pso.h"
#include "convolution_pso.h"
#include "ocean_pso.h"
#include "rc_toy.h"
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
using json = nlohmann::json;

namespace
{
    constexpr const char* g_defaultSkybox = "assets/sky4k.hdr";

    /*
     * Ocean preset file (JSON), relative to the working directory. Only the user's presets are saved in it, the
     * built-in ones are made in code (BuiltInOceanPresets):
     *
     *  {
     *    "format": "EV Ocean Presets", "version": 1,
     *    "presets": [ { "name": "My Sea", "windSpeed": 12.0, ..., "oceanColor": [r, g, b, a], ...,
     *                   "skybox": "assets/bay2k.hdr" } ]
     *  }
     *
     * A missing key keeps the value of the Default preset, so older files still load.
     */
    constexpr const char* g_oceanPresetFile = "presets/ocean_presets.json";
    constexpr const char* g_oceanPresetFormat = "EV Ocean Presets";
    constexpr int         g_oceanPresetVersion = 1;

    // The preset values as they are named in the file.
    constexpr std::pair<const char*, float OceanPreset::*> g_oceanPresetFloats[] = {
        { "windSpeed", &OceanPreset::windSpeed },
        { "windDirection", &OceanPreset::windDirection },
        { "fetch", &OceanPreset::fetch },
        { "scale", &OceanPreset::scale },
        { "gamma", &OceanPreset::gamma },
        { "swell", &OceanPreset::swell },
        { "spreadBlend", &OceanPreset::spreadBlend },
        { "shortWavesFade", &OceanPreset::shortWavesFade },
        { "heightMod", &OceanPreset::heightMod },
        { "peakScatterIntensity", &OceanPreset::peakScatterIntensity },
        { "IBLIntensity", &OceanPreset::IBLIntensity },
        { "foamDecay", &OceanPreset::foamDecay },
        { "foamBias", &OceanPreset::foamBias },
        { "foamTrailingIntensity", &OceanPreset::foamTrailingIntensity },
        { "foamSoftness", &OceanPreset::foamSoftness },
        { "foamTextureSize", &OceanPreset::foamTextureSize },
        { "foamSharpness", &OceanPreset::foamSharpness },
        { "foamAlbedo", &OceanPreset::foamAlbedo },
    };
    constexpr std::pair<const char*, XMFLOAT4 OceanPreset::*> g_oceanPresetFloat4s[] = {
        { "oceanColor", &OceanPreset::oceanColor },
        { "scatterColor", &OceanPreset::scatterColor },
        { "foamCascadeWeights", &OceanPreset::foamCascadeWeights },
        { "sunColor", &OceanPreset::sunColor },
    };

    json WriteOceanPreset(const OceanPreset& preset)
    {
        json object = { { "name", preset.name } };
        for (const auto& [key, member] : g_oceanPresetFloats)
        {
            object[key] = preset.*member;
        }
        for (const auto& [key, member] : g_oceanPresetFloat4s)
        {
            const XMFLOAT4& value = preset.*member;
            object[key] = json::array({ value.x, value.y, value.z, value.w });
        }
        object["skybox"] = preset.skybox;
        object["sunPosition"] = json::array({ preset.sunPosition.x, preset.sunPosition.y, preset.sunPosition.z });
        return object;
    }

    // Values missing from the object keep the ones of 'fallback'.
    OceanPreset ReadOceanPreset(const json& object, const OceanPreset& fallback)
    {
        OceanPreset preset = fallback;
        preset.builtIn = false;
        preset.name = object.value("name", "");
        for (const auto& [key, member] : g_oceanPresetFloats)
        {
            preset.*member = object.value(key, preset.*member);
        }
        for (const auto& [key, member] : g_oceanPresetFloat4s)
        {
            const auto it = object.find(key);
            if (it != object.end() && it->is_array() && it->size() == 4)
            {
                preset.*member = XMFLOAT4((*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>(), (*it)[3].get<float>());
            }
        }
        preset.skybox = object.value("skybox", preset.skybox);
        if (const auto it = object.find("sunPosition"); it != object.end() && it->is_array() && it->size() == 3)
        {
            preset.sunPosition = XMFLOAT3((*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>());
        }
        return preset;
    }

    XMFLOAT4 ColorFromHex(uint32_t rgb)
    {
        return XMFLOAT4(((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, 1.0f);
    }

    // The built-in presets, each a different sea. 'defaults' is the look the application starts with. Wind speed in
    // m/s and fetch in m go into JONSWAP: stronger wind over a longer fetch gives higher and longer waves. Swell
    // narrows the spreading around the wind direction, a low spread blend widens it into a confused, two-way sea.
    // The scatter colour (lighter than the ocean colour) glows through crests lit from behind, so most suns sit low in
    // front of the default view; heightMod is larger for smaller waves, it scales the wave height into that glow.
    std::vector<OceanPreset> BuiltInOceanPresets(const OceanPreset& defaults)
    {
        std::vector<OceanPreset> presets;
        auto add = [&](const char* name) -> OceanPreset&
        {
            OceanPreset& preset = presets.emplace_back(defaults);
            preset.name = name;
            preset.builtIn = true;
            return preset;
        };

        add("Default");

        // Almost no wind under a clear dawn sky: tiny ripples on a long, low undulation, a mirror for the sky.
        {
            OceanPreset& p = add("Glassy Calm");
            p.windSpeed = 0.2f;
            p.fetch = 1000000.0f;
            p.scale = 0.6f;
            p.gamma = 1.5f;
            p.swell = 1.0f;
            p.spreadBlend = 1.0f;
            p.shortWavesFade = 0.3f;
            p.oceanColor = ColorFromHex(0x0A2A3C);
            p.scatterColor = ColorFromHex(0x2E8C8A);
            p.heightMod = 5.0f;
            p.peakScatterIntensity = 0.6f;
            p.IBLIntensity = 0.7f;
            p.foamDecay = 2.0f;
            p.foamBias = -0.5f;
            p.foamTrailingIntensity = 0.5f;
            p.skybox = "assets/qwantani_dawn_puresky_2k.hdr";
            p.sunPosition = XMFLOAT3(264.0f, 59.0f, 360.0f);
            p.sunColor = XMFLOAT4(1.0f, 0.85f, 0.7f, 1.0f);
        }

        // Light breeze over a short fetch under a high sun: small choppy waves, bright turquoise water lit through
        // the crests, a little foam.
        {
            OceanPreset& p = add("Tropical Lagoon");
            p.windSpeed = 1.3f;
            p.fetch = 20000.0f;
            p.scale = 1.0f;
            p.gamma = 3.3f;
            p.swell = 0.1f;
            p.spreadBlend = 0.8f;
            p.shortWavesFade = 0.05f;
            p.oceanColor = ColorFromHex(0x0A5C78);
            p.scatterColor = ColorFromHex(0x3FE0C8);
            p.heightMod = 5.0f;
            p.peakScatterIntensity = 1.0f;
            p.IBLIntensity = 0.45f;
            p.foamDecay = 1.2f;
            p.foamBias = -0.1f;
            p.foamTrailingIntensity = 2.0f;
            p.skybox = "assets/sunflowers_puresky_2k.hdr";
            p.sunPosition = XMFLOAT3(240.0f, 190.0f, 330.0f); // Lower than the sky's sun (out of view) so the crests are back-lit.
            p.sunColor = XMFLOAT4(1.0f, 0.97f, 0.9f, 1.0f);
        }

        // Moderate open-ocean sea under fair-weather clouds: mid-blue, regular whitecaps.
        {
            OceanPreset& p = add("Trade Winds");
            p.windSpeed = 2.2f;
            p.fetch = 300000.0f;
            p.scale = 1.0f;
            p.gamma = 3.3f;
            p.swell = 0.3f;
            p.spreadBlend = 0.9f;
            p.shortWavesFade = 0.01f;
            p.oceanColor = ColorFromHex(0x0A3270);
            p.scatterColor = ColorFromHex(0x1E9A8A);
            p.heightMod = 4.0f;
            p.peakScatterIntensity = 1.0f;
            p.IBLIntensity = 0.4f;
            p.foamDecay = 0.5f;
            p.foamBias = 0.55f;
            p.foamTrailingIntensity = 4.0f;
            p.skybox = "assets/kloofendal_48d_partly_cloudy_puresky_2k.hdr";
            p.sunPosition = XMFLOAT3(240.0f, 190.0f, 330.0f); // Lower than the sky's sun (out of view) so the crests are back-lit.
            p.sunColor = XMFLOAT4(1.0f, 1.0f, 0.97f, 1.0f);
        }

        // Long-period swell from far away at sunset: big rolling hills of water from one direction, little local chop.
        {
            OceanPreset& p = add("Long Swell");
            p.windSpeed = 3.5f;
            p.windDirection = 250.0f; // Rolling towards the camera.
            p.fetch = 1000000.0f;
            p.scale = 1.0f;
            p.gamma = 7.0f;
            p.swell = 1.0f;
            p.spreadBlend = 1.0f;
            p.shortWavesFade = 0.15f;
            p.oceanColor = ColorFromHex(0x0A3358);
            p.scatterColor = ColorFromHex(0x2A8C8C);
            p.heightMod = 2.0f;
            p.peakScatterIntensity = 0.8f;
            p.IBLIntensity = 0.5f;
            p.foamDecay = 1.0f;
            p.foamBias = 0.0f;
            p.foamTrailingIntensity = 1.0f;
            p.skybox = "assets/sky4k.hdr";
        }

        // Rough, high wind under a cold, broken sky: blue-green to grey water, lots of choppy whitecaps.
        {
            OceanPreset& p = add("North Atlantic");
            p.windSpeed = 4.0f;
            p.fetch = 500000.0f;
            p.scale = 1.2f;
            p.gamma = 3.3f;
            p.swell = 0.2f;
            p.spreadBlend = 0.7f;
            p.shortWavesFade = 0.01f;
            p.oceanColor = ColorFromHex(0x0A1E26);
            p.scatterColor = ColorFromHex(0x1F8A6A);
            p.heightMod = 2.5f;
            p.peakScatterIntensity = 1.0f;
            p.IBLIntensity = 0.35f;
            p.foamDecay = 0.35f;
            p.foamBias = 0.4f;
            p.foamTrailingIntensity = 5.0f;
            p.skybox = "assets/snow_field_puresky_2k.hdr";
            p.sunPosition = XMFLOAT3(259.0f, 94.0f, 356.0f); // Low behind the clouds, in front of the camera: back-lit crests.
            p.sunColor = XMFLOAT4(0.7f, 0.7f, 0.7f, 1.0f);
        }

        // Very high wind from changing directions under heavy cloud: a confused sea, dark slate water, deep green
        // light through the crests, heavy foam that trails far behind the breakers.
        {
            OceanPreset& p = add("Storm");
            p.windSpeed = 6.0f;
            p.fetch = 800000.0f;
            p.scale = 1.5f;
            p.gamma = 3.3f;
            p.swell = 0.0f;
            p.spreadBlend = 0.2f;
            p.shortWavesFade = 0.01f;
            p.oceanColor = ColorFromHex(0x08121C);
            p.scatterColor = ColorFromHex(0x1A7A5E);
            p.heightMod = 1.0f;
            p.peakScatterIntensity = 1.0f;
            p.IBLIntensity = 0.3f;
            p.foamDecay = 0.3f;
            p.foamBias = 0.15f;
            p.foamTrailingIntensity = 4.0f;
            p.foamTextureSize = 10.0f;
            p.skybox = "assets/overcast_soil_puresky_2k.hdr";
            p.sunPosition = XMFLOAT3(259.0f, 94.0f, 356.0f); // Low behind the clouds, in front of the camera: back-lit crests.
            p.sunColor = XMFLOAT4(0.6f, 0.62f, 0.65f, 1.0f);
        }

        // A moonlit night: a gentle sea, near-black navy water, a cold glitter path under the moon.
        {
            OceanPreset& p = add("Midnight");
            p.windSpeed = 1.5f;
            p.fetch = 200000.0f;
            p.scale = 1.0f;
            p.gamma = 3.3f;
            p.swell = 0.4f;
            p.spreadBlend = 0.9f;
            p.shortWavesFade = 0.02f;
            p.oceanColor = ColorFromHex(0x020812);
            p.scatterColor = ColorFromHex(0x0E3A44);
            p.heightMod = 3.0f;
            p.peakScatterIntensity = 1.0f;
            p.IBLIntensity = 0.4f;
            p.foamDecay = 0.8f;
            p.foamBias = 0.2f;
            p.foamTrailingIntensity = 2.0f;
            p.foamAlbedo = 0.6f;
            p.skybox = "assets/kloppenheim_02_puresky_2k.hdr";
            p.sunPosition = XMFLOAT3(251.0f, 132.0f, 350.0f);
            p.sunColor = XMFLOAT4(0.3f, 0.33f, 0.42f, 1.0f);
        }

        // Harbour water at night: short, steep chop from a short fetch, dark green water, the waterfront's lights
        // smeared across the ripples.
        {
            OceanPreset& p = add("Harbour Lights");
            p.windSpeed = 1.2f;
            p.fetch = 5000.0f;
            p.scale = 1.0f;
            p.gamma = 2.0f;
            p.swell = 0.0f;
            p.spreadBlend = 0.6f;
            p.shortWavesFade = 0.02f;
            p.oceanColor = ColorFromHex(0x061A16);
            p.scatterColor = ColorFromHex(0x1A5040);
            p.heightMod = 4.0f;
            p.peakScatterIntensity = 1.0f;
            p.IBLIntensity = 0.6f;
            p.foamDecay = 1.5f;
            p.foamBias = 0.0f;
            p.foamTrailingIntensity = 1.0f;
            p.skybox = "assets/bay2k.hdr";
            p.sunPosition = XMFLOAT3(255.0f, 137.0f, 344.0f);
            p.sunColor = XMFLOAT4(0.6f, 0.35f, 0.15f, 1.0f);
        }

        return presets;
    }
}

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
    if (m_viewportTexture)
    {
        m_viewportTexture->Resize(m_viewportWidth, m_viewportHeight);
    }
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
    // OnUpdate rebuilds the camera rotation from pitch and yaw, so follow the editor when it moves the camera
    // (a scene file was opened or play mode stopped).
    m_editor->SetCameraChangedCallback([this]()
    {
        XMFLOAT3 forward;
        XMStoreFloat3(&forward, XMVector3Rotate(XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), m_camera.GetRotation()));
        m_pitch = XMConvertToDegrees(std::asin(std::clamp(-forward.y, -1.0f, 1.0f)));
        m_yaw = XMConvertToDegrees(std::atan2(forward.x, forward.z));
    });
    m_editor->AddMenuCallback([this]() { DrawMenu(); });
    m_editor->AddPanel("Ocean Parameters", [this]() { DrawOceanParameters(); }, Editor::DockSlot::Right, m_showOceanParams);
    m_editor->AddPanel("Light Parameters", [this]() { DrawLightParameters(); }, Editor::DockSlot::Right, m_showLightParams);
    m_editor->AddPanel("Path Tracing", [this]() { DrawPathTracingPanel(); }, Editor::DockSlot::Right, m_showPathTracingPanel);

    // RC-0 (2D radiance cascades toy): its canvas is a tab next to the Scene view.
    m_rcToy = std::make_shared<RcToy>();
    m_rcToy->Initialize();
    m_editor->AddPanel("RC-0", [this]() { m_rcToy->DrawPanel(*m_editor); }, Editor::DockSlot::Center, m_showRcToyPanel);

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
    auto skyPanorama = commandList->LoadTextureFromFile(std::filesystem::path(g_defaultSkybox).wstring(), true);
    // Linear data, not colour.
    m_foamDetailTexture = commandList->LoadTextureFromFile(L"assets/ocean/foam.png", false);

    // m_cubeMesh = commandList->CreateCube();
    m_helmet = commandList->LoadSceneFromFile(L"assets/damaged_helmet/DamagedHelmet.gltf");
    m_chessboard = commandList->LoadSceneFromFile(L"assets/chess/ABeautifulGame.gltf");
    m_boat = commandList->LoadSceneFromFile(L"assets/kenny/ship-large.obj");


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
    permuteRootParameters[1].InitAsConstants(5, 0); // foam parameters + delta time, b0

    // Mip chains (ocean_mips.hlsl)
    CD3DX12_DESCRIPTOR_RANGE1 mipChainRangeUAV(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2 * OceanCompute::MipChainLevels, 0, 0,
        D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE);
    CD3DX12_ROOT_PARAMETER1 mipChainRootParameters[2];
    mipChainRootParameters[0].InitAsDescriptorTable(1, &mipChainRangeUAV); // u0-u19: slope, foam mips
    mipChainRootParameters[1].InitAsUnorderedAccessView(2 * OceanCompute::MipChainLevels); // u20: counter

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
    m_displacementPSO->SetFoamDetailTexture(m_foamDetailTexture);
    m_oceanPSO = std::make_shared<OceanCompute>(L"/animate_waves.cso", H0RootParameters, _countof(H0RootParameters));
    m_fftPSO = std::make_shared<OceanCompute>(L"/fft.cso", FFTRootParameters, _countof(FFTRootParameters));
    m_permutePSO = std::make_shared<OceanCompute>(L"/permute.cso", permuteRootParameters, _countof(permuteRootParameters));
    m_mipChainPSO = std::make_shared<OceanCompute>(L"/ocean_mips.cso", mipChainRootParameters, _countof(mipChainRootParameters));
    // Committed resources start zeroed, as the shader expects.
    m_mipChainCounters = Application::Get().CreateBuffer(
        CD3DX12_RESOURCE_DESC::Buffer(m_oceanCascadesNumber * sizeof(uint32_t), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS));
    m_mipChainCounters->SetName(L"Ocean Mip Chain Counters");
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
    m_displacementPSO->m_oceanRenderParams.scatterColor = XMFLOAT4(20.0f / 255.0f, 11.0f / 255.0f, 1.0f / 255.0f, 1.0f); // #140B01
    m_displacementPSO->m_oceanRenderParams.oceanrColor = XMFLOAT4(0.0f, 0.15f, 0.3f, 1.0f);
    m_displacementPSO->m_oceanRenderParams.IBLIntensity = 0.9f;
    m_displacementPSO->m_oceanRenderParams.heightMod = 2.0f;
    m_displacementPSO->m_oceanRenderParams.peakScatterIntensity = 0.6f;
    // Trailing foam per cascade (1500 m, 250 m, 17 m, 5 m): the small ones only hold ripples and tile visibly.
    m_displacementPSO->m_oceanRenderParams.foamCascadeWeights = XMFLOAT4(1.0f, 1.0f, 0.5f, 0.25f);
    m_displacementPSO->m_oceanRenderParams.foamTextureSize = 6.0f;
    m_displacementPSO->m_oceanRenderParams.foamSharpness = 4.0f;
    m_displacementPSO->m_oceanRenderParams.foamAlbedo = 0.9f;
    m_displacementPSO->m_oceanRenderParams.foamDebugView = 0.0f;

    // permute.hlsl b0. Per second, so frame-rate independent.
    struct Constants
    {
        float foamDecay; // per second
        float foamBias;  // foam forms below this Jacobian
        float foamAdd;   // per second at full strength
        float foamRange; // ramp width below foamBias
        float deltaTime; // set in OnUpdate
    } cbv;
    cbv.foamDecay = 0.4f;
    cbv.foamBias = 0.3f;
    cbv.foamAdd = 5.0f;
    cbv.foamRange = 0.3f;
    cbv.deltaTime = 0.0f;

    m_foamParameters = { cbv.foamDecay, cbv.foamBias, cbv.foamAdd, cbv.foamRange, cbv.deltaTime };
    
    // Tonemapped output shown in the editor's Scene panel (same format as the swapchain for the SDR pass).
    auto viewportDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, m_width, m_height, 1, 1, 1, 0,
        D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
    m_viewportTexture = Application::Get().CreateTexture(viewportDesc);
    m_viewportTexture->SetName(L"Scene Viewport (LDR)");
    ResizeViewport(m_editor->GetViewportWidth(), m_editor->GetViewportHeight());

    auto convolutionFence = computeQueue.ExecuteCommandList(computeCommandList);
    computeQueue.WaitForFenceValue(convolutionFence);

    // Skybox cubemap and image based lighting (also when an HDR file is dropped into the editor).
    SetEnvironment(skyPanorama);
    m_editor->SetEnvironmentPath(g_defaultSkybox);
    m_editor->SetEnvironmentCallback([this](const std::shared_ptr<Texture>& panorama) { SetEnvironment(panorama); });
    m_editor->SetRenderGraph(&m_renderGraph);

    // The sun is part of the presets; OnUpdate fills in the directions every frame.
    m_directionalLights.resize(3);

    // The values above are the Default preset. "--preset <name>" on the command line starts with another one.
    LoadOceanPresets();
    int argumentCount = 0;
    if (LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount))
    {
        for (int i = 1; i + 1 < argumentCount; ++i)
        {
            if (std::wstring(arguments[i]) == L"--preset")
            {
                const std::string name = ConvertString(arguments[++i]);
                for (size_t preset = 0; preset < m_oceanPresets.size(); ++preset)
                {
                    if (m_oceanPresets[preset].name == name)
                    {
                        m_selectedOceanPreset = static_cast<int>(preset);
                        ApplyOceanPreset(m_oceanPresets[preset]);
                    }
                }
            }
        }
        LocalFree(arguments);
    }

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
    // Clamped: a hitch (breakpoint, window drag) would add or remove all foam at once.
    m_foamParameters[4] = static_cast<float>(std::min(e.deltaTime, 0.1));
    // Crest foam in the pixel shader uses the same thresholds.
    m_displacementPSO->m_oceanRenderParams.foamBias = m_foamParameters[1];
    m_displacementPSO->m_oceanRenderParams.foamRange = m_foamParameters[3];

    m_unlitPSO->SetDirectionalLights(m_directionalLights);
    m_displacementPSO->SetDirectionalLights(m_directionalLights);
    // m_lightingPSO->SetDirectionalLights(m_DirectionalLights);
    // m_decalPSO->SetDirectionalLights(m_DirectionalLights);


    // Initialize point lights BEFORE the scene visitor
    m_pointLights.resize(1);

    // Populate the point light with valid data
    PointLight& light = m_pointLights[0];
    light.positionWS = XMFLOAT4(0.0f, 1.0f, 0.0f, 1.0f);
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

        // The pixel shader samples slope and foam with mips; the passes above only write mip 0.
        rg.AddPass("Ocean Mips" + cascade)
            .Write(c.slope)
            .Write(c.foam)
            .Execute([this, c, i](RGPassContext& context)
            {
                m_mipChainPSO->DispatchMipChain(context.GetCommandList(), context.GetTexture(c.slope),
                    context.GetTexture(c.foam), m_mipChainCounters, i);
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
            // Captures find resources by name, so every slot gets its own.
            const std::string name = slot == 0 ? "Ocean Plane Vertices" : "Ocean Plane Vertices " + std::to_string(slot);
            oceanPass.Read(rg.Import(name, vertexBuffer, static_cast<uint32_t>(vertexBuffer->GetVertexStride())), RGAccess::VertexBuffer);
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

    // RC-0's passes (nothing while its panel's Enable box is off).
    m_rcToy->AddPasses(rg);

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

    DrawOceanPresets();

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
        ImGui::ColorEdit4("Ocean Color", &m_displacementPSO->m_oceanRenderParams.oceanrColor.x,
            ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel);
        ImGui::ColorEdit4("Scatter Color", &m_displacementPSO->m_oceanRenderParams.scatterColor.x,
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
        ImGui::SliderFloat("Decay", &m_foamParameters[0], 0.0f, 2.0f);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("How quickly foam fades, per second (lower = longer lasting)");
        ImGui::SliderFloat("Bias", &m_foamParameters[1], -1.0f, 1.0f);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Foam forms where the Jacobian drops below this, for both crest and trailing foam (higher = more foam)");
        ImGui::SliderFloat("Trailing Intensity", &m_foamParameters[2], 0.0f, 10.0f);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Trailing foam added per second where a wave breaks (crest foam ignores this)");
        ImGui::SliderFloat("Softness", &m_foamParameters[3], 0.01f, 1.0f);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Jacobian range below Bias over which foam forming ramps up (lower = harder onset)");

        auto& params = m_displacementPSO->m_oceanRenderParams;
        ImGui::Spacing();
        ImGui::SliderFloat4("Cascade Weights", &params.foamCascadeWeights.x, 0.0f, 2.0f);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Foam contribution of each cascade (1500 m, 250 m, 17 m, 5 m)");
        ImGui::SliderFloat("Texture Size (m)", &params.foamTextureSize, 0.5f, 30.0f);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Metres covered by one repeat of the foam texture (assets/ocean/foam.png)");
        ImGui::SliderFloat("Sharpness", &params.foamSharpness, 1.0f, 20.0f);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("How hard the edge between foam and water is");
        ImGui::SliderFloat("Albedo", &params.foamAlbedo, 0.0f, 1.0f);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Foam reflectance (real foam is about 0.8-0.9)");
        int debugView = static_cast<int>(params.foamDebugView);
        if (ImGui::Combo("Debug View", &debugView, "Off\0Foam amount\0Foam coverage\0Crest foam only\0"))
        {
            params.foamDebugView = static_cast<float>(debugView);
        }
        ImGui::Unindent();
    }

    if (paramsChanged)
    {
        RegenerateSpectrum();
    }
}

void Ocean::DrawOceanPresets()
{
    if (!ImGui::CollapsingHeader("  Presets", ImGuiTreeNodeFlags_DefaultOpen))
    {
        return;
    }
    ImGui::Indent();

    // Picking a preset applies it; Apply goes back to it after the sliders were moved.
    const OceanPreset& selected = m_oceanPresets[m_selectedOceanPreset];
    if (ImGui::BeginCombo("Preset", selected.name.c_str()))
    {
        for (int i = 0; i < static_cast<int>(m_oceanPresets.size()); ++i)
        {
            const OceanPreset& preset = m_oceanPresets[i];
            if (i > 0 && preset.builtIn != m_oceanPresets[i - 1].builtIn)
            {
                ImGui::Separator(); // Built-in presets above, the user's below.
            }
            ImGui::PushID(i);
            if (ImGui::Selectable(preset.name.c_str(), i == m_selectedOceanPreset))
            {
                m_selectedOceanPreset = i;
                ApplyOceanPreset(preset);
                if (!preset.builtIn)
                {
                    strncpy_s(m_oceanPresetName, preset.name.c_str(), _TRUNCATE);
                }
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    if (ImGui::Button("Apply"))
    {
        ApplyOceanPreset(m_oceanPresets[m_selectedOceanPreset]);
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(m_oceanPresets[m_selectedOceanPreset].builtIn);
    if (ImGui::Button("Delete"))
    {
        m_oceanPresetStatus = "Deleted " + m_oceanPresets[m_selectedOceanPreset].name;
        m_oceanPresets.erase(m_oceanPresets.begin() + m_selectedOceanPreset);
        m_selectedOceanPreset = std::min(m_selectedOceanPreset, static_cast<int>(m_oceanPresets.size()) - 1);
        SaveOceanPresets();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("Skybox: %s", ConvertString(std::filesystem::path(m_oceanPresets[m_selectedOceanPreset].skybox).filename().wstring()).c_str());

    // Saves the current settings (and skybox) under the name, replacing a saved preset of the same name.
    ImGui::Spacing();
    ImGui::InputText("Name", m_oceanPresetName, sizeof(m_oceanPresetName));
    const std::string name = m_oceanPresetName;
    const auto existing = std::find_if(m_oceanPresets.begin(), m_oceanPresets.end(),
        [&](const OceanPreset& preset) { return preset.name == name; });
    const bool builtInName = existing != m_oceanPresets.end() && existing->builtIn;
    ImGui::BeginDisabled(name.empty() || builtInName);
    if (ImGui::Button(existing != m_oceanPresets.end() && !builtInName ? "Overwrite" : "Save Current As"))
    {
        OceanPreset preset = CaptureOceanPreset(name);
        if (existing != m_oceanPresets.end())
        {
            *existing = preset;
            m_selectedOceanPreset = static_cast<int>(existing - m_oceanPresets.begin());
        }
        else
        {
            m_oceanPresets.push_back(preset);
            m_selectedOceanPreset = static_cast<int>(m_oceanPresets.size()) - 1;
        }
        m_oceanPresetStatus = "Saved " + name;
        SaveOceanPresets();
    }
    ImGui::EndDisabled();
    if (builtInName)
    {
        ImGui::SameLine();
        ImGui::TextDisabled("Built-in presets can't be overwritten");
    }
    if (!m_oceanPresetStatus.empty())
    {
        ImGui::TextDisabled("%s", m_oceanPresetStatus.c_str());
    }
    ImGui::Unindent();
}

void Ocean::RegenerateSpectrum()
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

OceanPreset Ocean::CaptureOceanPreset(const std::string& name) const
{
    const auto& params = m_displacementPSO->m_oceanRenderParams;
    OceanPreset preset;
    preset.name = name;

    preset.windSpeed = m_jonswapParams.windSpeed;
    preset.windDirection = m_jonswapParams.windDirection;
    preset.fetch = m_jonswapParams.fetch;
    preset.scale = m_jonswapParams.scale;
    preset.gamma = m_jonswapParams.gamma;
    preset.swell = m_jonswapParams.swell;
    preset.spreadBlend = m_jonswapParams.spreadBlend;
    preset.shortWavesFade = m_jonswapParams.shortWavesFade;

    preset.oceanColor = params.oceanrColor;
    preset.scatterColor = params.scatterColor;
    preset.heightMod = params.heightMod;
    preset.peakScatterIntensity = params.peakScatterIntensity;
    preset.IBLIntensity = params.IBLIntensity;

    preset.foamDecay = m_foamParameters[0];
    preset.foamBias = m_foamParameters[1];
    preset.foamTrailingIntensity = m_foamParameters[2];
    preset.foamSoftness = m_foamParameters[3];
    preset.foamCascadeWeights = params.foamCascadeWeights;
    preset.foamTextureSize = params.foamTextureSize;
    preset.foamSharpness = params.foamSharpness;
    preset.foamAlbedo = params.foamAlbedo;

    preset.skybox = ConvertString(m_editor->GetEnvironmentPath().generic_wstring());
    preset.sunPosition = m_directionalLights[0].position;
    preset.sunColor = m_directionalLights[0].color;
    return preset;
}

void Ocean::ApplyOceanPreset(const OceanPreset& preset)
{
    auto& params = m_displacementPSO->m_oceanRenderParams;

    m_jonswapParams.windSpeed = preset.windSpeed;
    m_jonswapParams.windDirection = preset.windDirection;
    m_jonswapParams.fetch = preset.fetch;
    m_jonswapParams.scale = preset.scale;
    m_jonswapParams.gamma = preset.gamma;
    m_jonswapParams.swell = preset.swell;
    m_jonswapParams.spreadBlend = preset.spreadBlend;
    m_jonswapParams.shortWavesFade = preset.shortWavesFade;

    params.oceanrColor = preset.oceanColor;
    params.scatterColor = preset.scatterColor;
    params.heightMod = preset.heightMod;
    params.peakScatterIntensity = preset.peakScatterIntensity;
    params.IBLIntensity = preset.IBLIntensity;

    m_foamParameters[0] = preset.foamDecay;
    m_foamParameters[1] = preset.foamBias;
    m_foamParameters[2] = preset.foamTrailingIntensity;
    m_foamParameters[3] = preset.foamSoftness;
    params.foamCascadeWeights = preset.foamCascadeWeights;
    params.foamTextureSize = preset.foamTextureSize;
    params.foamSharpness = preset.foamSharpness;
    params.foamAlbedo = preset.foamAlbedo;

    m_directionalLights[0].position = preset.sunPosition;
    m_directionalLights[0].color = preset.sunColor;

    // The wind and spectrum parameters only take effect in a new H0, like when their sliders are released.
    RegenerateSpectrum();

    // Loaded in the background like a skybox dropped into the Scene, which also bakes the image based lighting again.
    if (!preset.skybox.empty() && std::filesystem::path(preset.skybox) != m_editor->GetEnvironmentPath())
    {
        m_editor->LoadEnvironment(preset.skybox);
    }
}

void Ocean::LoadOceanPresets()
{
    // The Default preset is what LoadContent set up.
    m_oceanPresets = BuiltInOceanPresets(CaptureOceanPreset("Default"));

    std::ifstream file(g_oceanPresetFile);
    if (!file)
    {
        return; // No presets saved yet.
    }
    try
    {
        const json root = json::parse(file);
        if (root.value("format", "") != g_oceanPresetFormat || root.value("version", 0) > g_oceanPresetVersion)
        {
            m_oceanPresetStatus = std::string("Not an ocean preset file: ") + g_oceanPresetFile;
            return;
        }
        for (const auto& object : root.value("presets", json::array()))
        {
            OceanPreset preset = ReadOceanPreset(object, m_oceanPresets[0]);
            const bool taken = std::any_of(m_oceanPresets.begin(), m_oceanPresets.end(),
                [&](const OceanPreset& other) { return other.name == preset.name; });
            if (!preset.name.empty() && !taken)
            {
                m_oceanPresets.push_back(preset);
            }
        }
    }
    catch (const std::exception& e)
    {
        m_oceanPresetStatus = std::string("Could not read ") + g_oceanPresetFile + ": " + e.what();
    }
}

void Ocean::SaveOceanPresets()
{
    json presets = json::array();
    for (const auto& preset : m_oceanPresets)
    {
        if (!preset.builtIn)
        {
            presets.push_back(WriteOceanPreset(preset));
        }
    }
    const json root = { { "format", g_oceanPresetFormat }, { "version", g_oceanPresetVersion }, { "presets", presets } };

    const std::filesystem::path path = g_oceanPresetFile;
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    std::ofstream file(path);
    if (!file || !(file << root.dump(2) << '\n'))
    {
        m_oceanPresetStatus = std::string("Could not write ") + g_oceanPresetFile;
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

// ── Path Tracing ─────────────────────────────────────────────────────────
// Draws the contents of the "Path Tracing" panel; the Editor already wraps it in ImGui::Begin/End (Editor::AddPanel).
// Asks RenderCaps whether this device can path trace and greys the toggle out with the reason when it can't.
void Ocean::DrawPathTracingPanel()
{
	const RenderCaps& caps = Application::Get().GetRenderCaps();
	bool pathTracingSupported = caps.SupportsPathTracing();
    ImGui::BeginDisabled(!pathTracingSupported);
    // Checkbox both shows and flips m_pathTracingEnabled; disabled (greyed out) when the device can't path trace.
    ImGui::Checkbox("Enable Path Tracing", &m_pathTracingEnabled);
    ImGui::EndDisabled();
    if (!pathTracingSupported)
    {
		ImGui::TextWrapped("Path tracing is not supported on this device. Reason: %s", caps.GetPathTracingUnsupportedReason());
    }
	ImGui::Text("Adapter: %ls", caps.adapterName.c_str());
	ImGui::Text("Raytracing Tier: %d.%d", caps.raytracingTier / 10, caps.raytracingTier % 10);
	ImGui::Text("Shader Model: %d.%d", caps.highestShaderModel >> 4, caps.highestShaderModel & 0xF);
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
    m_unlitPSO.reset();
    m_displacementPSO.reset();
    m_skyboxPSO.reset();
    m_sdrPSO.reset();
    m_oceanPSO.reset();
    m_fftPSO.reset();
    m_permutePSO.reset();
    m_mipChainPSO.reset();
    m_mipChainCounters.reset();
    if (m_rcToy)
    {
        m_rcToy->Release();
        m_rcToy.reset();
    }
    for (UINT i = 0; i < m_oceanCascadesNumber; ++i)
    {
    	m_oceanCascades[i].H0Texture.reset();
        m_oceanCascades[i].slopeTexture.reset();
        m_oceanCascades[i].displacementTexture.reset();
        m_oceanCascades[i].foamTexture.reset();
    }
    m_foamDetailTexture.reset();
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

    return std::lerp(2.0f / 3.1415f * cos(theta - m_jonswapParams.angle) * cos(theta - m_jonswapParams.angle),
                     Cosine2s(theta - m_jonswapParams.angle, s), m_jonswapParams.spreadBlend);
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

    // Boundary between cascades i and i+1 at 6 wavelengths per patch of i+1: every wave is >= ~6 texels long in its
    // cascade (gasgiant's FFT-Ocean split). At the Nyquist limit the shortest waves were 2 texels long and their
    // derivatives (normals, Jacobian, foam) aliased.
    const float boundaryFactor = 6.0f;
    auto boundaryK = [&](UINT i) { return boundaryFactor * 2.0f * PI / m_oceanPatchSizes[i + 1]; };
    const float lowCutoff = cascade == 0 ? 0.001f : boundaryK(cascade - 1);
    const float highCutoff = cascade + 1 < m_oceanCascadesNumber ? boundaryK(cascade) : highestK;
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

    // r = foam, g = lambda * Dxz. Needs typed UAV loads of R32G32_FLOAT (TypedUAVLoadAdditionalFormats).
    DXGI_FORMAT foamFormat = DXGI_FORMAT_R32G32_FLOAT;
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
        // While playing, Esc goes back to the editor (or quits a game started with --play); the editor handles it.
        if (!m_editor || !m_editor->IsPlaying())
        {
            Application::Get().Stop();
        }
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



