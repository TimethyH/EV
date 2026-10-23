#pragma once

/**
 *  @file birthday_game.h
 *
 *  @brief A birthday quiz on a beach: walk around, shoot the cake for a question and answer it by shooting the
 *  bird above the right screen. Built on OceanRenderer's ocean (EV::Ocean); the editor is still there with --editor.
 *
 *  birthday_scene.cpp  the beach, promenade, screens, birds, cake and palm trees
 *  curacao.cpp         Willemstad: the CURACAO letters and colorful houses behind the promenade, the Queen Emma bridge
 *  birthday_player.cpp walking, looking, the rifle, the HUD and the autopilot used by the CI
 *  birthday_quiz.cpp   questions, answers, pictures and the finale
 */

#include <DirectXCollision.h>
#include <DirectXMath.h>

#include <cmath>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "imgui.h"

#include "ocean_scene.h"
#include "quiz_config.h"

namespace EV
{
    class EffectPSO;
    class Material;
    class Mesh;
    class Scene;
    class SceneNode;
    class Texture;
}

namespace Birthday::Layout
{
    // Meters. The ocean is towards +z; the promenade with the screens is behind the beach (towards -z).
    constexpr float floorTop = 1.0f;          // Height of the promenade.
    constexpr float floorFront = -8.0f;       // Where the promenade meets the sand.
    constexpr float floorBack = -34.0f;
    constexpr float floorHalfWidth = 26.0f;
    constexpr float sandTop = floorTop - 0.12f; // The sand right in front of the promenade.

    // The invisible border: the player stays inside this rectangle (the water starts at about z = 10).
    constexpr float borderMinX = -22.0f;
    constexpr float borderMaxX = 22.0f;
    constexpr float borderMinZ = -32.0f;
    constexpr float borderMaxZ = 8.5f;

    constexpr float eyeHeight = 1.7f;
    constexpr float playerStartX = 0.0f;
    constexpr float playerStartZ = 3.0f;
    constexpr float playerStartPitch = 3.0f; // Degrees, looking slightly down at the ocean.

    constexpr float screenWidth = 10.4f;  // The picture area, 16:9.
    constexpr float screenHeight = 5.85f;
    constexpr float screenBottom = 2.4f;  // Above the promenade.
    constexpr float screenX = 9.0f;       // Left screen at +x (left when facing the screens), right screen at -x.
    constexpr float screenZ = -25.0f;
    constexpr float screenTurn = 14.0f;   // Degrees the screens are turned towards the middle.

    constexpr float birdAboveScreen = 2.4f; // Height of the flight above the top of the screen.
    constexpr float birdRange = 4.2f;       // How far the birds fly to each side of the middle of their screen.

    constexpr float cakeZ = -16.0f;

    // Willemstad (Curacao) behind the promenade: a flat terrace in the dunes with the big CURACAO letters on a
    // brick plaza and a row of colorful houses behind them.
    constexpr float townTop = 2.2f;
    constexpr float townFront = -38.0f;
    constexpr float townBack = -66.0f;
    constexpr float townHalfWidth = 56.0f;
    constexpr float lettersZ = -42.0f;  // The front of the letters.
    constexpr float housesZ = -52.0f;   // The houses' front walls.

    // The Queen Emma bridge (the pontoon bridge) out into the sea on the right of the beach, along +z.
    constexpr float bridgeX = 40.0f;
    constexpr float bridgeStartZ = 6.0f;
    constexpr float bridgeLength = 150.0f;
    constexpr float bridgeDeckTop = 1.3f;
}

class BirthdayGame : public EV::Ocean
{
public:
    struct Options
    {
        std::filesystem::path gameFolder;      // questions.json, images, scene.evscene
        bool                  editor = false;  // Start in the editor (F5 plays) instead of playing right away.
        std::filesystem::path autopilotFolder; // Play a fixed script, save screenshots there and quit (used by the CI).
        std::filesystem::path exportFolder;    // Write the models made in code (Curacao) there as OBJ files and quit.
    };

    BirthdayGame(const std::wstring& name, uint32_t width, uint32_t height, const Options& options);
    ~BirthdayGame() override;

    bool LoadContent() override;
    void UnloadContent() override;

protected:
    // Ocean hooks.
    void LoadGameContent(const std::shared_ptr<EV::CommandList>& commandList) override;
    void UpdateGame(UpdateEventArgs& e) override;
    void UpdatePointLights(float time, DirectX::FXMMATRIX viewMatrix) override;
    void DrawObjects(EV::RGPassContext& context, EV::SceneVisitor& visitor) override;
    void DrawGameGUI() override;
    void DrawShadowCasters(EV::SceneVisitor& visitor) override;

    // In the editor the ocean's fly camera gets the input; while playing the player controller reads it itself.
    void OnKeyPress(KeyEventArgs& e) override;
    void OnKeyRelease(KeyEventArgs& e) override;
    void OnMouseMove(MouseMotionEventArgs& e) override;
    void OnMouseWheel(MouseWheelEventArgs& e) override;

private:
    struct Screen
    {
        std::shared_ptr<EV::SceneNode> root;    // Moved in the editor.
        std::shared_ptr<EV::SceneNode> frame;   // Frame and posts, lit like everything else.
        std::shared_ptr<EV::SceneNode> display; // Quads with pictures or text, drawn unlit with the screen shader.
        std::shared_ptr<EV::Scene>     frameScene;
        std::shared_ptr<EV::Scene>     displayScene;
        std::shared_ptr<EV::Mesh>      quad;    // Shared by the display's quads.
    };

    // A picture scrolling past a screen (ShowScrolling): two quads, as it can be seen twice when the list wraps around.
    struct ScrollingPicture
    {
        float                          width = 0.0f;
        float                          height = 0.0f;
        std::shared_ptr<EV::SceneNode> quads[2];
        std::shared_ptr<EV::Material>  materials[2];
    };

    struct Bird
    {
        std::shared_ptr<EV::SceneNode> body; // Animated every frame.
        std::shared_ptr<EV::Scene>     scene;
        float phase = 0.0f;
        float heading = 0.0f;   // Radians, the direction it flies.
        float hitTime = -1.0f;  // Seconds since it was shot; < 0 when it was not.
    };

    // A balloon on a string, swaying around where the string is tied.
    struct Balloon
    {
        std::shared_ptr<EV::SceneNode> pivot;
        DirectX::XMFLOAT4X4            rest;  // The pivot's transform without swaying.
        float                          phase = 0.0f;
    };

    // How PlaceModel sizes a model: by its height or by its largest side.
    enum class Fit
    {
        Height,
        Size,
    };

    struct Particle
    {
        DirectX::XMFLOAT2 position;
        DirectX::XMFLOAT2 velocity;
        float             angle;
        float             spin;
        float             size;
        uint32_t          color;
    };

    // Playing starts in a menu; Play fades to black and the player wakes up on the beach.
    enum class IntroState
    {
        Menu,
        FadeOut,
        Waking,
        Playing,
    };

    enum class QuizState
    {
        Intro,    // Waiting for the cake to be shot.
        Question, // Waiting for a bird to be shot.
        Answered, // The picture is shown; waiting for the cake again.
        Finale,
    };

    bool IsPlaying() const;

    // Scene (birthday_scene.cpp)
    void BuildScene(EV::CommandList& commandList);
    std::shared_ptr<EV::Material> LoadSurface(EV::CommandList& commandList, const std::wstring& color, const std::wstring& normal,
        const std::wstring& ambientRoughnessMetal);
    std::shared_ptr<EV::Scene> LoadModel(EV::CommandList& commandList, const std::wstring& file);
    Screen CreateScreen(EV::CommandList& commandList, const std::string& name, float x, float turnDegrees);
    void BuildProps(EV::CommandList& commandList);
    // Curacao (curacao.cpp)
    void BuildCuracao(EV::CommandList& commandList);
    void BuildBridge(EV::CommandList& commandList);
    void UpdateBridgeLights(); // The colors of the bridge's arches and lights change slowly.
    // A copy of a model below parent, made size meters high (or long), turned and standing on the ground at (x, z)
    // (or with its bottom at height y when y is given).
    std::shared_ptr<EV::SceneNode> PlaceModel(const std::shared_ptr<EV::Scene>& model, const std::shared_ptr<EV::SceneNode>& parent,
        const std::string& name, float x, float z, float turnDegrees, float size, Fit fit, float y = NAN);
    // Balloons tied at a point (in parent's space), leaning outwards in the given direction (radians around y).
    void AddBalloons(const std::shared_ptr<EV::SceneNode>& parent, DirectX::FXMVECTOR tiedAt, float lean, int count);
    // Shows a node tree in the editor's hierarchy (so it can be moved and saved in scene files).
    std::shared_ptr<EV::Scene> AddEditorScene(const std::string& name, const std::shared_ptr<EV::SceneNode>& root);
    void UpdateScene(float deltaTime);
    DirectX::XMVECTOR GetBirdFlightCenter(Birthday::Side side) const;
    void ShowOnScreen(Birthday::Side side, const std::vector<std::shared_ptr<EV::Texture>>& textures);
    // Shows the pictures one after the other, each as big as on its own, scrolling up from one to the next and
    // starting over after the last one.
    void ShowScrolling(Birthday::Side side, const std::vector<std::shared_ptr<EV::Texture>>& textures);
    void UpdateScrolling(float deltaTime);
    static float GroundHeight(float x, float z);
    static float BeachHeight(float x, float z);

    // Player (birthday_player.cpp)
    void StartGame(); // Play in the menu.
    void ResetPlayer();
    void UpdatePlayer(float deltaTime);
    void CaptureMouse(bool capture);
    void Fire();
    DirectX::XMVECTOR GetEyePosition() const;
    DirectX::XMVECTOR GetLookDirection() const;
    DirectX::XMMATRIX GetRifleTransform() const;
    void AimAt(DirectX::FXMVECTOR target);
    void DrawHUD();
    void DrawMenu(ImDrawList* drawList, const ImVec2& viewportPosition, const ImVec2& viewportSize);
    void DrawWakeUp(ImDrawList* drawList, const ImVec2& viewportPosition, const ImVec2& viewportSize);
    void DrawScope(ImDrawList* drawList, const ImVec2& viewportPosition, const ImVec2& viewportSize);
    float GetTextSize() const; // The HUD's text size in pixels.
    void DrawTextBox(ImDrawList* drawList, const ImVec2& viewportPosition, const ImVec2& viewportSize);
    void UpdateConfetti(float deltaTime, const ImVec2& viewportSize);
    void UpdateAutopilot(float deltaTime);

    // Quiz (birthday_quiz.cpp)
    void LoadQuiz(EV::CommandList& commandList);
    void ResetQuiz(); // Back to the start: idle screens and the welcome message.
    std::shared_ptr<EV::Texture> GetPicture(const std::filesystem::path& path) const;
    void OnCakeShot();
    void OnBirdShot(Birthday::Side side);
    void StartQuestion(size_t index);
    void Answer(Birthday::Side side);
    void StartFinale();
    void ShowIdleScreens();
    void ShowMessage(const std::string& text);

    Options m_options;

    // Scene
    std::shared_ptr<EV::EffectPSO>            m_screenPSO;  // Unlit pictures that come out of the tonemapper unchanged.
    std::vector<std::shared_ptr<EV::Scene>>   m_litScenes;  // Everything drawn with the lighting shader.
    std::shared_ptr<EV::Scene>                m_beach;
    std::shared_ptr<EV::Scene>                m_promenade;
    std::shared_ptr<EV::SceneNode>            m_pedestal;
    std::shared_ptr<EV::SceneNode>            m_ship;
    DirectX::XMFLOAT4X4                       m_shipRest = {};
    std::vector<std::shared_ptr<EV::Material>> m_bridgeLights; // One per arch, along the bridge.
    std::vector<Balloon>                      m_balloons;
    std::shared_ptr<EV::Mesh>                 m_balloonMesh;
    std::shared_ptr<EV::Mesh>                 m_stringMesh;
    std::shared_ptr<EV::Scene>                m_rifle;
    std::shared_ptr<EV::SceneNode>            m_cake;       // The part that spins.
    std::shared_ptr<EV::SceneNode>            m_cakeRoot;   // Moved in the editor.
    Screen                                    m_screens[2];
    Bird                                      m_birds[2];
    DirectX::XMFLOAT3                         m_rifleMuzzle = {}; // In the rifle's space.
    float                                     m_time = 0.0f;
    float                                     m_cakeBounce = 0.0f;
    float                                     m_cakeAngle = 0.0f;
    std::vector<ScrollingPicture>             m_scrolling;  // On m_scrollingSide; empty when nothing scrolls.
    Birthday::Side                            m_scrollingSide = Birthday::Side::Right;
    float                                     m_scrollTime = 0.0f;

    // Player
    DirectX::XMFLOAT3 m_playerPosition = {}; // Feet.
    float             m_lookYaw = 0.0f;      // Degrees.
    float             m_lookPitch = 0.0f;
    float             m_recoil = 0.0f;       // 1 right after a shot, back to 0 quickly.
    float             m_fireCooldown = 0.0f;
    float             m_muzzleFlash = 0.0f;  // Seconds left.
    float             m_hitMarker = 0.0f;
    float             m_walkCycle = 0.0f;
    bool              m_mouseCaptured = false;
    bool              m_leftButtonWasDown = true;
    bool              m_paused = false;
    bool              m_scoped = false;      // Right mouse button toggles the scope.
    float             m_scope = 0.0f;        // 0 = rifle at the hip, 1 = looking through the scope.
    bool              m_rightButtonWasDown = true;
    float             m_eyeDrop = 0.0f;      // How far below standing height the eye is (lying down while waking up).
    float             m_pitchOverride = 0.0f; // Waking up: how much the view is still turned to the sky (0..1).
    IntroState        m_intro = IntroState::Playing;
    float             m_introTime = 0.0f;

    // Quiz
    Birthday::QuizConfig                                     m_quiz;
    QuizState                                                m_state = QuizState::Intro;
    size_t                                                   m_questionIndex = 0;
    int                                                      m_correctAnswers = 0;
    std::vector<std::shared_ptr<EV::Texture>>                m_answerCards; // Left and right card per question.
    std::shared_ptr<EV::Texture>                             m_idleCards[2];
    std::shared_ptr<EV::Texture>                             m_finaleCard;
    std::map<std::filesystem::path, std::shared_ptr<EV::Texture>> m_pictures; // Missing files get a card that says so.
    std::string                                              m_message;
    bool                                                     m_messageVisible = false;
    float                                                    m_messageTime = 0.0f;
    std::string                                              m_banner; // The question, at the top while answering.
    std::vector<Particle>                                    m_confetti;

    // Autopilot (CI screenshots)
    size_t m_autopilotStep = 0;
    float  m_autopilotTime = 0.0f;
    int    m_autopilotFrames = 0;
    bool   m_autopilotStarted = false;
};
