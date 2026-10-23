#include "core/EV.h"

#include "birthday_game.h"

#include <algorithm>
#include <cmath>

#include "geometry.h"

using namespace DirectX;
using namespace EV;
using namespace Birthday;
using namespace Birthday::Layout;

namespace
{
    // Assets (assets/birthday, see CREDITS.txt there).
    const wchar_t* g_skyFile = L"assets/birthday/sky/kloofendal_48d_partly_cloudy_puresky.hdr";
    const wchar_t* g_duckFile = L"assets/birthday/duck/Duck.gltf";
    const wchar_t* g_rifleFile = L"assets/birthday/rifle/blaster-e.glb";
    const wchar_t* g_cakeFile = L"assets/birthday/cake/cake-birthday.glb";
    const wchar_t* g_palmFiles[] = { L"assets/birthday/palms/palm-detailed-straight.glb", L"assets/birthday/palms/palm-detailed-bend.glb" };

    // Where the sun is in the sky panorama (its brightest pixel), so the sunlight matches the sky.
    const XMFLOAT3 g_sunDirection = { 0.378f, 0.741f, 0.555f };

    // The duck model looks along +x; RotationY(heading - this) turns it to fly along heading.
    constexpr float g_duckForward = XM_PIDIV2;
    constexpr float g_duckScale = 0.75f;
    constexpr float g_cakeScale = 1.4f;
    constexpr float g_birdSpeed = 0.55f; // Radians per second of the back and forth.

    float SmoothStep(float edge0, float edge1, float x)
    {
        const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

    float WrapAngle(float angle)
    {
        while (angle > XM_PI) angle -= XM_2PI;
        while (angle < -XM_PI) angle += XM_2PI;
        return angle;
    }

    std::shared_ptr<Material> CreateColorMaterial(const XMFLOAT4& color, float roughness, float metallic)
    {
        auto material = std::make_shared<Material>();
        material->SetDiffuseColor(color);
        material->SetRoughness(roughness);
        material->SetMetallic(metallic);
        return material;
    }
}

// ------------------------------------------------------------------------------------------------
// Setup

BirthdayGame::BirthdayGame(const std::wstring& name, uint32_t width, uint32_t height, const Options& options)
    : Ocean(name, width, height, true)
    , m_options(options)
{
    m_loadDemoContent = false;
    m_skyPanoramaFile = g_skyFile;

    // Distance fog in the color of the sky at the horizon: the far ocean and the edge of the ocean plane fade into it.
    m_fogColor = XMFLOAT3(0.42f, 0.46f, 0.56f);
    m_fogDensity = 0.0065f;
    m_fogStart = 60.0f;

    // The sun's shadows over the beach and the promenade.
    m_shadowsEnabled = true;
    m_shadowCenter = XMFLOAT3(0.0f, 0.0f, -8.0f);
    m_shadowRadius = 45.0f;
    m_shadowStrength = 0.8f;
}

BirthdayGame::~BirthdayGame() = default;

bool BirthdayGame::LoadContent()
{
    if (!Ocean::LoadContent())
    {
        return false;
    }

    // Sunlight from where the sun is in the sky.
    m_directionalLights.resize(3);
    m_directionalLights[0].position = XMFLOAT3(g_sunDirection.x * 1000.0f, g_sunDirection.y * 1000.0f, g_sunDirection.z * 1000.0f);
    m_directionalLights[0].color = XMFLOAT4(2.6f, 2.45f, 2.2f, 1.0f);

    m_editor->SetPlayModeCallback([this](bool playing)
    {
        m_paused = false;
        ResetQuiz();
        m_messageVisible = false; // The welcome message comes after waking up.
        m_intro = playing ? IntroState::Menu : IntroState::Playing;
        m_introTime = 0.0f;
        m_scoped = false;
        m_scope = 0.0f;
        if (playing)
        {
            // The ocean's fly camera must not keep moving with keys that were held in the editor.
            m_forward = m_backward = m_left = m_right = m_up = m_down = 0.0f;
            m_shift = false;
            ResetPlayer();
        }
        else
        {
            CaptureMouse(false);
        }
    });
    // Without the editor, Esc pauses first; Esc again quits.
    m_editor->SetStandaloneEscapeCallback([this]()
    {
        if (m_paused)
        {
            Application::Get().Stop();
        }
        else
        {
            m_paused = true;
            CaptureMouse(false);
        }
    });

    // A layout made in the editor (File > Save Scene As... game/scene.evscene) moves the screens, the cake and so on.
    const auto sceneFile = m_options.gameFolder / "scene.evscene";
    std::error_code error;
    if (std::filesystem::is_regular_file(sceneFile, error))
    {
        m_editor->LoadScene(sceneFile);
    }

    if (!m_options.autopilotFolder.empty())
    {
        m_toggleFullscreen = true; // The autopilot starts playing itself (UpdateAutopilot).
    }
    else if (!m_options.editor)
    {
        // The game: no editor, full screen.
        m_editor->PlayStandalone();
        m_toggleFullscreen = true;
    }

    ResetPlayer();
    return true;
}

void BirthdayGame::UnloadContent()
{
    CaptureMouse(false);
    m_litScenes.clear();
    m_beach.reset();
    m_rifle.reset();
    m_cake.reset();
    m_cakeRoot.reset();
    m_promenade.reset();
    m_pedestal.reset();
    m_ship.reset();
    m_balloons.clear();
    m_balloonMesh.reset();
    m_stringMesh.reset();
    for (auto& screen : m_screens)
    {
        screen = {};
    }
    for (auto& bird : m_birds)
    {
        bird = {};
    }
    m_answerCards.clear();
    m_idleCards[0].reset();
    m_idleCards[1].reset();
    m_finaleCard.reset();
    m_pictures.clear();
    m_screenPSO.reset();
    Ocean::UnloadContent();
}

void BirthdayGame::LoadGameContent(const std::shared_ptr<CommandList>& commandList)
{
    // Pictures go through the tonemapper like everything else; this shader undoes the tonemapping for them.
    m_screenPSO = std::make_shared<EffectPSO>(m_camera, L"/vertex.cso", L"/screen_pixel.cso");

    BuildScene(*commandList);
    LoadQuiz(*commandList);
}

std::shared_ptr<Scene> BirthdayGame::AddEditorScene(const std::string& name, const std::shared_ptr<SceneNode>& root)
{
    auto scene = std::make_shared<Scene>();
    scene->SetRootNode(root);
    m_editor->AddScene(name, scene);
    return scene;
}

std::shared_ptr<Material> BirthdayGame::LoadSurface(CommandList& commandList, const std::wstring& color, const std::wstring& normal,
    const std::wstring& ambientRoughnessMetal)
{
    auto material = std::make_shared<Material>();
    material->SetTexture(Material::TextureType::Diffuse, commandList.LoadTextureFromFile(color, true));
    material->SetTexture(Material::TextureType::Normal, commandList.LoadTextureFromFile(normal, false));
    // Poly Haven's "arm" maps: ambient occlusion in red, roughness in green, metalness in blue.
    auto arm = commandList.LoadTextureFromFile(ambientRoughnessMetal, false);
    material->SetTexture(Material::TextureType::MetallicRoughness, arm);
    material->SetTexture(Material::TextureType::Ambient, arm);
    return material;
}

std::shared_ptr<Scene> BirthdayGame::LoadModel(CommandList& commandList, const std::wstring& file)
{
    auto scene = commandList.LoadSceneFromFile(file);
    if (!scene || !scene->GetRootNode())
    {
        throw std::runtime_error("Could not load " + ConvertString(file));
    }
    return scene;
}

void BirthdayGame::BuildScene(CommandList& commandList)
{
    // Balloons (spheres) and their strings, made in code and shared by every balloon.
    m_balloonMesh = commandList.CreateSphere(1.0f, 24)->GetRootNode()->GetMesh(0);
    m_stringMesh = CreateMesh(commandList, BuildBox(XMFLOAT3(0.012f, 1.0f, 0.012f), 0.0f),
        CreateColorMaterial(XMFLOAT4(0.95f, 0.95f, 0.95f, 1.0f), 0.8f, 0.0f));

    // The beach: one heightfield from the dunes behind the promenade down into the sea.
    auto sand = LoadSurface(commandList, L"assets/birthday/sand/sand_01_diff_2k.jpg", L"assets/birthday/sand/sand_01_nor_dx_2k.jpg",
        L"assets/birthday/sand/sand_01_arm_2k.jpg");
    auto beach = CreateNode("Beach", nullptr);
    beach->AddMesh(CreateMesh(commandList, BuildTerrain(-80.0f, 80.0f, -110.0f, 75.0f, 160, 185, 0.25f, BeachHeight), sand));
    m_beach = AddEditorScene("Beach", beach);
    m_litScenes.push_back(m_beach);

    // The promenade: a wooden boardwalk between the beach and the dunes.
    auto planks = LoadSurface(commandList, L"assets/birthday/boardwalk/wood_planks_diff_2k.jpg",
        L"assets/birthday/boardwalk/wood_planks_nor_dx_2k.jpg", L"assets/birthday/boardwalk/wood_planks_arm_2k.jpg");
    const float slabDepth = floorFront - floorBack;
    auto promenade = CreateNode("Promenade", nullptr, XMMatrixTranslation(0.0f, floorTop - 0.3f, (floorFront + floorBack) * 0.5f));
    promenade->AddMesh(CreateMesh(commandList, BuildBox(XMFLOAT3(floorHalfWidth * 2.0f, 0.6f, slabDepth), 0.35f), planks));
    m_promenade = AddEditorScene("Promenade", promenade);
    m_litScenes.push_back(m_promenade);

    // The two screens, turned a little towards the middle. Left and right as seen when facing them.
    m_screens[static_cast<int>(Side::Left)] = CreateScreen(commandList, "Left Screen", screenX, -screenTurn);
    m_screens[static_cast<int>(Side::Right)] = CreateScreen(commandList, "Right Screen", -screenX, screenTurn);

    // A bird above each screen.
    auto duck = LoadModel(commandList, g_duckFile);
    const char* birdNames[] = { "Left Bird", "Right Bird" };
    for (int i = 0; i < 2; ++i)
    {
        Bird& bird = m_birds[i];
        bird.body = CreateNode(birdNames[i], nullptr);
        CopyNodes(duck->GetRootNode(), bird.body);
        bird.scene = AddEditorScene(birdNames[i], bird.body);
        bird.phase = i * 1.7f;
        m_litScenes.push_back(bird.scene);
    }

    // The cake on a pedestal in the middle; shooting it starts the next question.
    m_cakeRoot = CreateNode("Birthday Cake", nullptr, XMMatrixTranslation(0.0f, floorTop, cakeZ));
    m_pedestal = CreateNode("Pedestal", m_cakeRoot, XMMatrixTranslation(0.0f, 0.5f, 0.0f));
    m_pedestal->AddMesh(CreateMesh(commandList, BuildBox(XMFLOAT3(1.3f, 1.0f, 1.3f), 0.0f),
        CreateColorMaterial(XMFLOAT4(0.92f, 0.9f, 0.86f, 1.0f), 0.35f, 0.0f)));
    // Two balloons at each corner of the pedestal.
    for (int corner = 0; corner < 4; ++corner)
    {
        const float x = (corner & 1) ? 0.6f : -0.6f;
        const float z = (corner & 2) ? 0.6f : -0.6f;
        AddBalloons(m_cakeRoot, XMVectorSet(x, 1.0f, z, 1.0f), std::atan2(x, z), 2);
    }
    m_cake = CreateNode("Cake", m_cakeRoot, XMMatrixScaling(g_cakeScale, g_cakeScale, g_cakeScale) * XMMatrixTranslation(0.0f, 1.0f, 0.0f));
    CopyNodes(LoadModel(commandList, g_cakeFile)->GetRootNode(), m_cake);
    m_litScenes.push_back(AddEditorScene("Birthday Cake", m_cakeRoot));

    // Palm trees on both sides of the beach, outside the walkable area.
    auto palms = CreateNode("Palm Trees", nullptr);
    std::shared_ptr<Scene> palmModels[] = { LoadModel(commandList, g_palmFiles[0]), LoadModel(commandList, g_palmFiles[1]) };
    const struct { float x, z, turn, scale; int model; } palmPlaces[] = {
        { -26.0f, -3.0f, 20.0f, 1.6f, 0 }, { -28.5f, 4.0f, 130.0f, 1.4f, 1 }, { -24.5f, 7.0f, 250.0f, 1.2f, 0 },
        { 26.5f, -1.0f, 300.0f, 1.5f, 1 }, { 25.0f, 6.0f, 75.0f, 1.3f, 0 }, { 29.0f, -6.5f, 190.0f, 1.7f, 0 },
        { -31.0f, -12.0f, 45.0f, 1.8f, 1 }, { 31.5f, -14.0f, 160.0f, 1.6f, 0 },
    };
    for (const auto& place : palmPlaces)
    {
        const XMMATRIX transform = XMMatrixScaling(place.scale, place.scale, place.scale) *
            XMMatrixRotationY(XMConvertToRadians(place.turn)) *
            XMMatrixTranslation(place.x, GroundHeight(place.x, place.z) - 0.1f, place.z);
        CopyNodes(palmModels[place.model]->GetRootNode(), CreateNode("Palm", palms, transform));
    }
    m_litScenes.push_back(AddEditorScene("Palm Trees", palms));

    BuildProps(commandList);

    // The rifle is drawn in front of the camera while playing; it is not part of the editable scene.
    m_rifle = LoadModel(commandList, g_rifleFile);
    BoundingBox rifleBounds;
    if (ComputeLocalBounds(m_rifle->GetRootNode(), rifleBounds))
    {
        // The barrel points along +z and ends at the front of the bounds.
        m_rifleMuzzle = XMFLOAT3(rifleBounds.Center.x, 0.03f, rifleBounds.Center.z + rifleBounds.Extents.z);
    }

    UpdateScene(0.0f);
}

BirthdayGame::Screen BirthdayGame::CreateScreen(CommandList& commandList, const std::string& name, float x, float turnDegrees)
{
    Screen screen;
    const float centerY = floorTop + screenBottom + screenHeight * 0.5f;
    screen.root = CreateNode(name, nullptr, XMMatrixRotationY(XMConvertToRadians(turnDegrees)) * XMMatrixTranslation(x, centerY, screenZ));

    // A dark metal frame on two posts.
    auto metal = CreateColorMaterial(XMFLOAT4(0.05f, 0.05f, 0.06f, 1.0f), 0.35f, 0.7f);
    screen.frame = CreateNode(name + " Frame", screen.root);
    screen.frame->AddMesh(CreateMesh(commandList, BuildBox(XMFLOAT3(screenWidth + 0.5f, screenHeight + 0.5f, 0.35f), 0.0f), metal));
    const float postHeight = screenBottom + 0.5f;
    auto postMesh = CreateMesh(commandList, BuildBox(XMFLOAT3(0.3f, postHeight, 0.3f), 0.0f), metal);
    for (float side : { -1.0f, 1.0f })
    {
        auto post = CreateNode("Post", screen.frame,
            XMMatrixTranslation(side * (screenWidth * 0.5f - 1.2f), -screenHeight * 0.5f - postHeight * 0.5f + 0.5f, -0.1f));
        post->AddMesh(postMesh);

        // Balloons tied to the post, leaning out sideways and a little to the front.
        AddBalloons(screen.frame, XMVectorSet(side * (screenWidth * 0.5f - 1.2f), -screenHeight * 0.5f - 0.3f, 0.2f, 1.0f),
            side * (XM_PIDIV2 - 0.5f), 3);
    }

    // The picture area just in front of the frame. Its quads are made by ShowOnScreen.
    screen.display = CreateNode(name + " Display", screen.root, XMMatrixTranslation(0.0f, 0.0f, 0.19f));
    screen.quad = CreateMesh(commandList, BuildScreenQuad(), nullptr);

    screen.frameScene = std::make_shared<Scene>();
    screen.frameScene->SetRootNode(screen.frame);
    screen.displayScene = std::make_shared<Scene>();
    screen.displayScene->SetRootNode(screen.display);
    m_litScenes.push_back(screen.frameScene);

    AddEditorScene(name, screen.root);
    return screen;
}

std::shared_ptr<SceneNode> BirthdayGame::PlaceModel(const std::shared_ptr<Scene>& model, const std::shared_ptr<SceneNode>& parent,
    const std::string& name, float x, float z, float turnDegrees, float size, Fit fit, float y)
{
    BoundingBox bounds;
    ComputeModelBounds(model->GetRootNode(), bounds);
    const float extent = fit == Fit::Height ? bounds.Extents.y * 2.0f :
        std::max({ bounds.Extents.x, bounds.Extents.y, bounds.Extents.z }) * 2.0f;
    const float scale = extent > 0.0f ? size / extent : 1.0f;
    const float bottom = (bounds.Center.y - bounds.Extents.y) * scale;
    const float base = std::isnan(y) ? GroundHeight(x, z) : y;

    auto node = CreateNode(name, parent, XMMatrixScaling(scale, scale, scale) * XMMatrixRotationY(XMConvertToRadians(turnDegrees)) *
        XMMatrixTranslation(x, base - bottom, z));
    CopyNodes(model->GetRootNode(), node);
    return node;
}

void BirthdayGame::AddBalloons(const std::shared_ptr<SceneNode>& parent, FXMVECTOR tiedAt, float lean, int count)
{
    static const XMFLOAT4 colors[] = {
        { 0.9f, 0.08f, 0.1f, 1.0f }, { 1.0f, 0.75f, 0.05f, 1.0f }, { 0.1f, 0.35f, 0.95f, 1.0f }, { 0.95f, 0.3f, 0.65f, 1.0f },
        { 0.15f, 0.75f, 0.25f, 1.0f }, { 1.0f, 0.45f, 0.05f, 1.0f }, { 0.55f, 0.2f, 0.85f, 1.0f },
    };
    for (int i = 0; i < count; ++i)
    {
        const size_t index = m_balloons.size();
        const float spread = (i - (count - 1) * 0.5f) * 0.55f;
        const float tilt = 0.18f + 0.07f * static_cast<float>(index % 3);
        const float length = 1.3f + 0.25f * static_cast<float>((index * 7) % 4);

        // Leaning: tip the string over towards +z, then turn that towards the lean direction.
        Balloon balloon;
        XMStoreFloat4x4(&balloon.rest, XMMatrixRotationX(tilt) * XMMatrixRotationY(lean + spread) * XMMatrixTranslationFromVector(tiedAt));
        balloon.pivot = CreateNode("Balloon", parent, XMLoadFloat4x4(&balloon.rest));
        balloon.phase = static_cast<float>(index) * 1.37f;

        auto string = CreateNode("String", balloon.pivot, XMMatrixScaling(1.0f, length, 1.0f) * XMMatrixTranslation(0.0f, length * 0.5f, 0.0f));
        string->AddMesh(m_stringMesh);
        auto sphere = CreateNode("Ball", balloon.pivot, XMMatrixScaling(0.3f, 0.36f, 0.3f) * XMMatrixTranslation(0.0f, length + 0.34f, 0.0f));
        sphere->AddMesh(CloneMesh(*m_balloonMesh, CreateColorMaterial(colors[index % _countof(colors)], 0.2f, 0.0f)));
        m_balloons.push_back(balloon);
    }
}

void BirthdayGame::BuildProps(CommandList& commandList)
{
    auto model = [&](const wchar_t* file) { return LoadModel(commandList, std::wstring(L"assets/birthday/") + file); };

    // On the beach: a little picnic on the left, a rowboat and some cargo on the right, rocks and grass around.
    auto beach = CreateNode("Beach Props", nullptr);
    auto crate = model(L"pirate/crate.glb");
    PlaceModel(model(L"survival/bedroll.glb"), beach, "Towel", -13.0f, 3.0f, 15.0f, 1.9f, Fit::Size);
    PlaceModel(model(L"survival/tent-canvas.glb"), beach, "Sunshade", -15.2f, 1.0f, 30.0f, 2.2f, Fit::Height);
    PlaceModel(crate, beach, "Table", -11.2f, 2.0f, 10.0f, 0.55f, Fit::Height);
    const float tableTop = GroundHeight(-11.2f, 2.0f) + 0.55f;
    auto cocktail = model(L"food/cocktail.glb");
    PlaceModel(cocktail, beach, "Cocktail", -11.4f, 1.85f, 20.0f, 0.22f, Fit::Height, tableTop);
    PlaceModel(cocktail, beach, "Cocktail", -11.0f, 2.2f, 160.0f, 0.22f, Fit::Height, tableTop);
    PlaceModel(model(L"food/pineapple.glb"), beach, "Pineapple", -11.15f, 1.7f, 0.0f, 0.3f, Fit::Height, tableTop);
    PlaceModel(model(L"food/watermelon.glb"), beach, "Watermelon", -12.2f, 4.4f, 0.0f, 0.3f, Fit::Height);
    auto coconut = model(L"food/coconut.glb");
    PlaceModel(coconut, beach, "Coconut", -13.9f, 4.6f, 0.0f, 0.18f, Fit::Height);
    PlaceModel(coconut, beach, "Coconut", -10.3f, 3.9f, 40.0f, 0.18f, Fit::Height);
    PlaceModel(model(L"survival/campfire-pit.glb"), beach, "Campfire", -5.0f, -3.5f, 0.0f, 1.3f, Fit::Size);

    PlaceModel(model(L"pirate/boat-row-large.glb"), beach, "Rowboat", 14.5f, 8.2f, 35.0f, 3.2f, Fit::Size);
    PlaceModel(model(L"pirate/tool-paddle.glb"), beach, "Paddle", 12.4f, 6.9f, 70.0f, 1.7f, Fit::Height);
    auto barrel = model(L"pirate/barrel.glb");
    PlaceModel(barrel, beach, "Barrel", 17.5f, 1.5f, 0.0f, 0.95f, Fit::Height);
    PlaceModel(barrel, beach, "Barrel", 18.5f, 2.7f, 45.0f, 0.95f, Fit::Height);
    PlaceModel(model(L"pirate/crate-bottles.glb"), beach, "Crate", 16.2f, 2.9f, -15.0f, 0.75f, Fit::Height);
    PlaceModel(model(L"pirate/chest.glb"), beach, "Treasure Chest", 15.5f, -2.5f, -20.0f, 0.75f, Fit::Height);

    std::shared_ptr<Scene> rocks[] = { model(L"pirate/rocks-sand-a.glb"), model(L"pirate/rocks-sand-b.glb"), model(L"pirate/rocks-sand-c.glb") };
    PlaceModel(rocks[0], beach, "Rocks", 19.5f, 7.5f, 40.0f, 1.1f, Fit::Height);
    PlaceModel(rocks[1], beach, "Rocks", -19.5f, 8.0f, 200.0f, 0.9f, Fit::Height);
    PlaceModel(rocks[2], beach, "Rocks", -8.0f, 10.4f, 120.0f, 0.7f, Fit::Height);
    PlaceModel(rocks[0], beach, "Rocks", 9.0f, 10.8f, 300.0f, 0.8f, Fit::Height);
    PlaceModel(rocks[1], beach, "Rocks", 24.0f, -4.0f, 80.0f, 1.4f, Fit::Height);
    auto grass = model(L"pirate/grass-plant.glb");
    const XMFLOAT2 grassPlaces[] = { { -21.0f, -6.5f }, { 21.0f, -6.8f }, { -17.0f, -7.2f }, { 16.0f, -7.0f }, { -6.5f, -7.3f }, { 7.0f, -7.4f },
        { -23.5f, 2.0f }, { 23.0f, -1.0f } };
    for (size_t i = 0; i < _countof(grassPlaces); ++i)
    {
        PlaceModel(grass, beach, "Grass", grassPlaces[i].x, grassPlaces[i].y, i * 47.0f, 0.5f, Fit::Height);
    }

    // A wooden dock into the sea on the right.
    auto dock = model(L"pirate/structure-platform-dock.glb");
    for (int i = 0; i < 4; ++i)
    {
        PlaceModel(dock, beach, "Dock", 27.0f, 8.5f + i * 2.5f, 0.0f, 2.5f, Fit::Size, -0.35f);
    }
    m_litScenes.push_back(AddEditorScene("Beach Props", beach));

    // On the promenade: benches looking at the sea, lanterns with string lights, flags at the corners.
    auto promenade = CreateNode("Promenade Props", nullptr);
    auto bench = model(L"holiday/bench.glb");
    for (float x : { -18.5f, -9.5f, 9.5f, 18.5f })
    {
        PlaceModel(bench, promenade, "Bench", x, floorFront - 2.0f, 180.0f, 1.8f, Fit::Size);
    }
    auto lantern = model(L"holiday/lantern.glb");
    auto lights = model(L"holiday/lights-colored.glb");
    const float lanternX[] = { -23.0f, -14.0f, -5.0f, 5.0f, 14.0f, 23.0f };
    const float lanternZ = floorFront - 0.7f;
    const float lanternHeight = 3.2f;
    for (float x : lanternX)
    {
        PlaceModel(lantern, promenade, "Lantern", x, lanternZ, 0.0f, lanternHeight, Fit::Height);
    }
    BoundingBox lightsBounds;
    ComputeModelBounds(lights->GetRootNode(), lightsBounds);
    for (size_t i = 0; i + 1 < _countof(lanternX); ++i)
    {
        // Stretched from lantern to lantern, hanging a little.
        const float span = lanternX[i + 1] - lanternX[i];
        const float sag = 0.8f;
        auto node = CreateNode("String Lights", promenade,
            XMMatrixScaling(span / (lightsBounds.Extents.x * 2.0f), sag / (lightsBounds.Extents.y * 2.0f), 1.0f) *
            XMMatrixTranslation((lanternX[i] + lanternX[i + 1]) * 0.5f - lightsBounds.Center.x * span / (lightsBounds.Extents.x * 2.0f),
                floorTop + lanternHeight - 0.25f - sag, lanternZ));
        CopyNodes(lights->GetRootNode(), node);
    }
    auto flag = model(L"pirate/flag-pennant.glb");
    for (const XMFLOAT2& corner : { XMFLOAT2(-25.0f, floorFront - 0.6f), XMFLOAT2(25.0f, floorFront - 0.6f), XMFLOAT2(-25.0f, floorBack + 0.6f),
        XMFLOAT2(25.0f, floorBack + 0.6f) })
    {
        PlaceModel(flag, promenade, "Flag", corner.x, corner.y, -60.0f, 3.5f, Fit::Height);
    }
    PlaceModel(model(L"survival/signpost.glb"), promenade, "Signpost", 3.5f, floorFront - 1.0f, 200.0f, 1.9f, Fit::Height);

    // Presents around the cake.
    const struct { const wchar_t* file; float x, z, turn, height; } presents[] = {
        { L"holiday/present-a-cube.glb", 1.0f, -0.6f, 20.0f, 0.45f }, { L"holiday/present-b-round.glb", -0.95f, -0.75f, 0.0f, 0.4f },
        { L"holiday/present-a-rectangle.glb", 0.85f, 0.95f, -30.0f, 0.35f }, { L"holiday/present-a-cube.glb", -0.9f, 0.9f, 50.0f, 0.38f },
    };
    for (const auto& present : presents)
    {
        PlaceModel(model(present.file), promenade, "Present", present.x, cakeZ + present.z, present.turn, present.height, Fit::Height);
    }
    m_litScenes.push_back(AddEditorScene("Promenade Props", promenade));

    // A ship sailing past.
    auto ship = model(L"pirate/ship-medium.glb");
    auto shipRoot = CreateNode("Ship", nullptr);
    m_ship = PlaceModel(ship, shipRoot, "Ship", -45.0f, 95.0f, 70.0f, 22.0f, Fit::Size, -1.6f);
    XMStoreFloat4x4(&m_shipRest, m_ship->GetLocalTransform());
    m_litScenes.push_back(AddEditorScene("Ship", shipRoot));
}

// ------------------------------------------------------------------------------------------------
// The ground

float BirthdayGame::BeachHeight(float x, float z)
{
    float height = sandTop;
    if (z > floorFront)
    {
        // The beach slopes into the sea (the water line is at about z = 10) and gets steeper further out.
        const float distance = z - floorFront;
        const float deep = std::max(0.0f, distance - 18.0f);
        height -= distance * 0.05f + deep * deep * 0.004f;

        // Small dunes and ripples on the dry sand, flat right in front of the promenade.
        const float dunes = 0.18f * std::sin(x * 0.23f + 1.3f) * std::sin(z * 0.19f + 0.4f) + 0.05f * std::sin(x * 0.9f + z * 0.37f);
        height += dunes * SmoothStep(0.0f, 4.0f, distance) * (1.0f - SmoothStep(12.0f, 20.0f, distance));
    }
    else if (z < floorBack)
    {
        // Dunes rise behind the promenade, so the ocean is only in front.
        const float distance = floorBack - z;
        height += std::pow(distance / 40.0f, 1.5f) * 9.0f * SmoothStep(0.0f, 6.0f, distance);
        height += 0.8f * std::sin(x * 0.11f + 0.5f) * std::sin(z * 0.13f) * SmoothStep(4.0f, 20.0f, distance);
    }

    // The land ends in the sea at the sides.
    height -= SmoothStep(52.0f, 78.0f, std::abs(x)) * 6.0f;
    return height;
}

float BirthdayGame::GroundHeight(float x, float z)
{
    if (z <= floorFront && z >= floorBack && std::abs(x) <= floorHalfWidth)
    {
        return floorTop;
    }
    return BeachHeight(x, z);
}

// ------------------------------------------------------------------------------------------------
// Animation and drawing

XMVECTOR BirthdayGame::GetBirdFlightCenter(Side side) const
{
    const Screen& screen = m_screens[static_cast<int>(side)];
    const XMVECTOR local = XMVectorSet(0.0f, screenHeight * 0.5f + birdAboveScreen, 0.8f, 1.0f);
    return XMVector3TransformCoord(local, screen.root->GetWorldTransform());
}

void BirthdayGame::UpdateScene(float deltaTime)
{
    m_time += deltaTime;

    // The birds fly back and forth above their screen and turn around at the ends.
    for (int i = 0; i < 2; ++i)
    {
        Bird& bird = m_birds[i];
        const Side side = static_cast<Side>(i);
        if (!bird.body)
        {
            continue;
        }
        const XMMATRIX screenWorld = m_screens[i].root->GetWorldTransform();
        const XMVECTOR along = XMVector3Normalize(XMVectorSetY(screenWorld.r[0], 0.0f));

        const float angle = m_time * g_birdSpeed + bird.phase;
        const float offset = std::sin(angle) * birdRange;
        const float direction = std::cos(angle) >= 0.0f ? 1.0f : -1.0f;
        const XMVECTOR flight = along * direction;
        const float targetHeading = std::atan2(XMVectorGetX(flight), XMVectorGetZ(flight));
        const float turn = WrapAngle(targetHeading - bird.heading);
        bird.heading = WrapAngle(bird.heading + turn * std::min(1.0f, deltaTime * 4.0f));

        // Shot: two quick spins and a hop.
        float spin = 0.0f;
        float hop = 0.0f;
        if (bird.hitTime >= 0.0f)
        {
            bird.hitTime += deltaTime;
            const float t = std::min(1.0f, bird.hitTime / 0.9f);
            spin = XM_2PI * 2.0f * SmoothStep(0.0f, 1.0f, t);
            hop = std::sin(t * XM_PI) * 1.2f;
            if (bird.hitTime > 0.9f)
            {
                bird.hitTime = -1.0f;
            }
        }

        const float bob = std::sin(m_time * 1.7f + bird.phase) * 0.35f + hop;
        const float bank = std::clamp(turn, -0.6f, 0.6f) * 0.4f;
        const XMVECTOR position = GetBirdFlightCenter(side) + along * offset + XMVectorSet(0.0f, bob, 0.0f, 0.0f);
        bird.body->SetLocalTransform(XMMatrixScaling(g_duckScale, g_duckScale, g_duckScale) * XMMatrixRotationX(bank) *
            XMMatrixRotationY(bird.heading - g_duckForward + spin) * XMMatrixTranslationFromVector(position));
    }

    // Balloons sway on their strings.
    for (const Balloon& balloon : m_balloons)
    {
        const float swayX = 0.09f * std::sin(m_time * 1.3f + balloon.phase);
        const float swayZ = 0.09f * std::sin(m_time * 1.05f + balloon.phase * 1.7f);
        balloon.pivot->SetLocalTransform(XMMatrixRotationX(swayX) * XMMatrixRotationZ(swayZ) * XMLoadFloat4x4(&balloon.rest));
    }

    // The ship rocks on the waves.
    if (m_ship)
    {
        const XMMATRIX rocking = XMMatrixRotationX(0.025f * std::sin(m_time * 0.7f + 1.0f)) * XMMatrixRotationZ(0.04f * std::sin(m_time * 0.5f)) *
            XMMatrixTranslation(0.0f, 0.25f * std::sin(m_time * 0.6f), 0.0f);
        m_ship->SetLocalTransform(rocking * XMLoadFloat4x4(&m_shipRest)); // Around the bottom of the hull.
    }

    // The cake turns slowly and jumps when it is shot.
    if (m_cake)
    {
        // Faster in the start menu.
        const bool menu = m_intro == IntroState::Menu || m_intro == IntroState::FadeOut;
        m_cakeAngle = std::fmod(m_cakeAngle + deltaTime * (menu ? 0.8f : 0.35f), XM_2PI);
        m_cakeBounce = std::max(0.0f, m_cakeBounce - deltaTime * 2.5f);
        const float jump = std::sin(m_cakeBounce * XM_PI) * 0.3f;
        m_cake->SetLocalTransform(XMMatrixScaling(g_cakeScale, g_cakeScale, g_cakeScale) * XMMatrixRotationY(m_cakeAngle) *
            XMMatrixTranslation(0.0f, 1.0f + jump, 0.0f));
    }
}

void BirthdayGame::UpdatePointLights(float time, FXMMATRIX viewMatrix)
{
    // The muzzle flash lights up the rifle and what is close to it.
    m_pointLights.resize(1);
    PointLight& light = m_pointLights[0];
    XMVECTOR muzzle = XMVector3TransformCoord(XMLoadFloat3(&m_rifleMuzzle), GetRifleTransform());
    muzzle = muzzle + GetLookDirection() * 0.3f;
    XMStoreFloat4(&light.positionWS, XMVectorSetW(muzzle, 1.0f));
    XMStoreFloat4(&light.positionVS, XMVector3TransformCoord(muzzle, viewMatrix));
    const float flash = (IsPlaying() && m_muzzleFlash > 0.0f) ? 1.0f : 0.0f;
    light.color = XMFLOAT4(9.0f * flash, 5.5f * flash, 2.2f * flash, 1.0f);

    // The screen shader uses none of these, but EffectPSO binds them all.
    m_screenPSO->SetPointLights(m_pointLights);
    m_screenPSO->SetDirectionalLights(m_directionalLights);
    m_screenPSO->SetIBLTextures(m_diffuseCubemapSRV, m_specularCubemapSRV, m_brdfLUTSRV);
}

void BirthdayGame::DrawShadowCasters(SceneVisitor& visitor)
{
    // The ground only receives shadows.
    for (const auto& scene : m_litScenes)
    {
        if (scene != m_beach && scene != m_promenade)
        {
            scene->Accept(visitor);
        }
    }
    for (const auto& scene : m_editor->GetSpawnedScenes())
    {
        scene->Accept(visitor);
    }
}

void BirthdayGame::DrawObjects(RGPassContext& context, SceneVisitor& visitor)
{
    for (const auto& scene : m_litScenes)
    {
        scene->Accept(visitor);
    }
    // Models dropped into the scene from the editor's Assets panel.
    for (const auto& scene : m_editor->GetSpawnedScenes())
    {
        scene->Accept(visitor);
    }
    // The rifle is put away in the menu and while looking through the scope.
    if (IsPlaying() && m_rifle && m_intro != IntroState::Menu && m_intro != IntroState::FadeOut && m_scope < 0.8f)
    {
        m_rifle->GetRootNode()->SetLocalTransform(GetRifleTransform());
        m_rifle->Accept(visitor);
    }

    // The screens' pictures and text, unlit.
    SceneVisitor screenVisitor(context.GetCommandList(), m_camera, *m_screenPSO, false);
    for (const auto& screen : m_screens)
    {
        if (screen.displayScene)
        {
            screen.displayScene->Accept(screenVisitor);
        }
    }
}

void BirthdayGame::ShowOnScreen(Side side, const std::vector<std::shared_ptr<Texture>>& textures)
{
    Screen& screen = m_screens[static_cast<int>(side)];
    while (!screen.display->GetChildren().empty())
    {
        const auto child = screen.display->GetChildren().back();
        screen.display->RemoveChild(child);
    }

    auto addQuad = [&](const std::shared_ptr<Texture>& texture, float x, float y, float width, float height, float z)
    {
        auto material = std::make_shared<Material>();
        material->SetEmissiveColor(XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f));
        if (texture)
        {
            material->SetTexture(Material::TextureType::Emissive, texture);
        }
        auto node = CreateNode("Picture", screen.display, XMMatrixScaling(width, height, 1.0f) * XMMatrixTranslation(x, y, z));
        node->AddMesh(CloneMesh(*screen.quad, material));
    };

    // Black behind the pictures.
    addQuad(nullptr, 0.0f, 0.0f, screenWidth, screenHeight, 0.0f);
    if (textures.empty())
    {
        return;
    }

    auto aspectOf = [](const std::shared_ptr<Texture>& texture)
    {
        const auto desc = texture->GetD3D12ResourceDesc();
        return desc.Height > 0 ? static_cast<float>(desc.Width) / desc.Height : 1.0f;
    };

    // The grid that shows the pictures biggest: try every number of columns.
    const size_t count = textures.size();
    const float gap = count > 1 ? 0.12f : 0.0f;
    size_t bestColumns = 1;
    float bestArea = -1.0f;
    for (size_t columns = 1; columns <= count; ++columns)
    {
        const size_t rows = (count + columns - 1) / columns;
        const float cellWidth = (screenWidth - gap * (columns - 1)) / columns;
        const float cellHeight = (screenHeight - gap * (rows - 1)) / rows;
        float area = 0.0f;
        for (const auto& texture : textures)
        {
            const float aspect = aspectOf(texture);
            const float width = std::min(cellWidth, cellHeight * aspect);
            area += width * (width / aspect);
        }
        if (area > bestArea)
        {
            bestArea = area;
            bestColumns = columns;
        }
    }

    const size_t columns = bestColumns;
    const size_t rows = (count + columns - 1) / columns;
    const float cellWidth = (screenWidth - gap * (columns - 1)) / columns;
    const float cellHeight = (screenHeight - gap * (rows - 1)) / rows;
    for (size_t i = 0; i < count; ++i)
    {
        const size_t row = i / columns;
        const size_t column = i % columns;
        // The last row is centered when it is not full.
        const size_t inRow = std::min(columns, count - row * columns);
        const float rowWidth = inRow * cellWidth + (inRow - 1) * gap;
        // Seen from the front, +x is on the left: the first picture of a row is at the largest x.
        const float x = rowWidth * 0.5f - cellWidth * 0.5f - column * (cellWidth + gap);
        const float y = screenHeight * 0.5f - cellHeight * 0.5f - row * (cellHeight + gap);

        const float aspect = aspectOf(textures[i]);
        const float width = std::min(cellWidth, cellHeight * aspect);
        addQuad(textures[i], x, y, width, width / aspect, 0.01f);
    }
}
