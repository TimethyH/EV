#include "core/EV.h"

#include "birthday_game.h"

#include <algorithm>
#include <chrono>
#include <cfloat>
#include <cmath>
#include <functional>

#include "geometry.h"
#include "textures.h"

using namespace DirectX;
using namespace EV;
using namespace Birthday;
using namespace Birthday::Layout;

namespace
{
    constexpr float g_walkSpeed = 4.0f;    // Meters per second.
    constexpr float g_runSpeed = 7.5f;     // With Shift.
    constexpr float g_mouseSpeed = 0.1f;   // Degrees per pixel (less while looking through the scope).
    constexpr float g_fieldOfView = 65.0f; // Degrees, vertical.
    constexpr float g_scopeFieldOfView = 15.0f;
    constexpr float g_menuFieldOfView = 50.0f;
    constexpr float g_scopeTime = 0.18f;   // Seconds to bring the scope up.
    constexpr float g_fireInterval = 0.35f;
    constexpr float g_recoilKick = 3.0f;   // Degrees the view jumps up when firing.

    // Start menu and waking up.
    constexpr float g_menuFadeIn = 1.2f;   // The menu comes out of black.
    constexpr float g_fadeOutTime = 0.8f;  // After Play, to black.
    constexpr float g_wakeTime = 5.0f;     // Waking up on the beach.
    constexpr float g_wakeControl = 4.0f;  // The player can look and walk from here.
    // The menu camera looks at the cake from behind it, with the beach and the sea behind the cake.
    const XMFLOAT3 g_menuCamera = { 0.0f, floorTop + 2.2f, cakeZ - 3.8f };
    constexpr float g_menuPitch = 13.0f;

    bool IsKeyDown(int key)
    {
        return (GetAsyncKeyState(key) & 0x8000) != 0;
    }

    ImU32 Color(int r, int g, int b, float alpha = 1.0f)
    {
        return IM_COL32(r, g, b, static_cast<int>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f));
    }

    float SmoothStep(float edge0, float edge1, float x)
    {
        const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

    // How far the eyes are open while waking up: they open a little, blink, open more, blink and open.
    float EyeOpenness(float time)
    {
        static const XMFLOAT2 keys[] = {
            { 0.0f, 0.0f }, { 0.6f, 0.0f }, { 1.6f, 0.35f }, { 1.9f, 0.06f }, { 2.1f, 0.06f }, { 3.0f, 0.6f },
            { 3.25f, 0.4f }, { 4.4f, 1.0f },
        };
        if (time >= keys[_countof(keys) - 1].x)
        {
            return 1.0f;
        }
        for (size_t i = 1; i < _countof(keys); ++i)
        {
            if (time < keys[i].x)
            {
                const float t = SmoothStep(keys[i - 1].x, keys[i].x, time);
                return keys[i - 1].y + (keys[i].y - keys[i - 1].y) * t;
            }
        }
        return 1.0f;
    }

    std::chrono::steady_clock::time_point g_autopilotStepStart;
}

bool BirthdayGame::IsPlaying() const
{
    return m_editor && m_editor->IsPlaying();
}

// ------------------------------------------------------------------------------------------------
// Frame

void BirthdayGame::UpdateGame(UpdateEventArgs& e)
{
    const float deltaTime = std::min(static_cast<float>(e.deltaTime), 0.1f);

    if (!m_options.exportFolder.empty())
    {
        Application::Get().Stop(); // The models were written while the scene was built.
        return;
    }
    UpdateAutopilot(deltaTime);
    UpdateScene(deltaTime);
    m_messageTime += deltaTime;

    if (IsPlaying())
    {
        UpdatePlayer(deltaTime);
    }
    else
    {
        CaptureMouse(false);
    }
}

void BirthdayGame::DrawGameGUI()
{
    if (IsPlaying())
    {
        DrawHUD();
    }
}

// ------------------------------------------------------------------------------------------------
// Input: the editor's fly camera while editing; the player controller reads the keyboard and mouse itself.

void BirthdayGame::OnKeyPress(KeyEventArgs& e)
{
    if (!IsPlaying())
    {
        Ocean::OnKeyPress(e);
        return;
    }
    if (e.key == KeyCode::F11 || (e.key == KeyCode::Enter && e.alt))
    {
        m_toggleFullscreen = true;
    }
}

void BirthdayGame::OnKeyRelease(KeyEventArgs& e)
{
    Ocean::OnKeyRelease(e);
}

void BirthdayGame::OnMouseMove(MouseMotionEventArgs& e)
{
    if (!IsPlaying())
    {
        Ocean::OnMouseMove(e);
    }
}

void BirthdayGame::OnMouseWheel(MouseWheelEventArgs& e)
{
    if (!IsPlaying())
    {
        Ocean::OnMouseWheel(e);
    }
}

// ------------------------------------------------------------------------------------------------
// Player

void BirthdayGame::StartGame()
{
    if (m_intro == IntroState::Menu)
    {
        m_intro = IntroState::FadeOut;
        m_introTime = 0.0f;
    }
}

void BirthdayGame::ResetPlayer()
{
    m_playerPosition = XMFLOAT3(playerStartX, GroundHeight(playerStartX, playerStartZ), playerStartZ);
    m_lookYaw = 0.0f;
    m_lookPitch = playerStartPitch;
    m_recoil = 0.0f;
    m_fireCooldown = 0.0f;
    m_muzzleFlash = 0.0f;
    m_walkCycle = 0.0f;
    m_eyeDrop = 0.0f;
    m_pitchOverride = 0.0f;
    m_scoped = false;
    m_scope = 0.0f;
    m_leftButtonWasDown = true; // The click that started playing does not shoot.
    m_rightButtonWasDown = true;

    m_camera.SetTranslation(GetEyePosition());
    m_pitch = m_lookPitch;
    m_yaw = m_lookYaw;
    m_camera.SetRotation(XMQuaternionRotationRollPitchYaw(XMConvertToRadians(m_pitch), XMConvertToRadians(m_yaw), 0.0f));
}

void BirthdayGame::CaptureMouse(bool capture)
{
    if (capture == m_mouseCaptured)
    {
        return;
    }
    m_mouseCaptured = capture;

    ImGuiIO& io = ImGui::GetIO();
    if (capture)
    {
        // The cursor is hidden and kept in the middle of the window; its movement turns the view.
        ShowCursor(FALSE);
        io.ConfigFlags |= ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoMouseCursorChange;
        HWND window = m_pWindow->GetWindowHandle();
        RECT client;
        GetClientRect(window, &client);
        POINT center = { (client.right - client.left) / 2, (client.bottom - client.top) / 2 };
        ClientToScreen(window, &center);
        SetCursorPos(center.x, center.y);
    }
    else
    {
        ShowCursor(TRUE);
        io.ConfigFlags &= ~(ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoMouseCursorChange);
    }
}

void BirthdayGame::UpdatePlayer(float deltaTime)
{
    HWND window = m_pWindow->GetWindowHandle();
    const bool focused = GetForegroundWindow() == window;
    const bool autopilot = !m_options.autopilotFolder.empty();

    // Start menu -> fade to black -> waking up on the beach -> playing.
    m_introTime += deltaTime;
    if (m_intro == IntroState::FadeOut && m_introTime >= g_fadeOutTime)
    {
        ResetPlayer();
        m_intro = IntroState::Waking;
        m_introTime = 0.0f;
    }
    if (m_intro == IntroState::Waking && m_introTime >= g_wakeTime)
    {
        m_intro = IntroState::Playing;
        ShowMessage(m_quiz.intro);
    }

    if (m_intro == IntroState::Menu || m_intro == IntroState::FadeOut)
    {
        // The cursor is free to click Play.
        CaptureMouse(false);
        m_camera.SetTranslation(XMLoadFloat3(&g_menuCamera));
        m_camera.SetFov(g_menuFieldOfView);
        m_yaw = 0.0f;
        m_pitch = g_menuPitch;
        m_leftButtonWasDown = true;
        m_rightButtonWasDown = true;
        return;
    }

    const bool inGame = m_intro == IntroState::Playing || m_introTime >= g_wakeControl;
    const bool control = focused && !m_paused && !autopilot && inGame;
    CaptureMouse(control);

    // Look around (slower through the scope, so aiming stays steady).
    const float fieldOfView = g_fieldOfView + (g_scopeFieldOfView - g_fieldOfView) * SmoothStep(0.0f, 1.0f, m_scope);
    if (m_mouseCaptured)
    {
        RECT client;
        GetClientRect(window, &client);
        POINT center = { (client.right - client.left) / 2, (client.bottom - client.top) / 2 };
        ClientToScreen(window, &center);
        POINT cursor;
        if (GetCursorPos(&cursor))
        {
            const float speed = g_mouseSpeed * fieldOfView / g_fieldOfView;
            m_lookYaw += (cursor.x - center.x) * speed;
            m_lookPitch = std::clamp(m_lookPitch + (cursor.y - center.y) * speed, -80.0f, 80.0f);
            SetCursorPos(center.x, center.y);
        }
        m_lookYaw = std::fmod(m_lookYaw, 360.0f);
    }

    // Walk (along the ground, whichever way the view is tilted).
    bool walking = false;
    if (control)
    {
        const float forward = (IsKeyDown('W') || IsKeyDown(VK_UP) ? 1.0f : 0.0f) - (IsKeyDown('S') || IsKeyDown(VK_DOWN) ? 1.0f : 0.0f);
        const float right = (IsKeyDown('D') || IsKeyDown(VK_RIGHT) ? 1.0f : 0.0f) - (IsKeyDown('A') || IsKeyDown(VK_LEFT) ? 1.0f : 0.0f);
        if (forward != 0.0f || right != 0.0f)
        {
            const float yaw = XMConvertToRadians(m_lookYaw);
            const XMVECTOR forwardAxis = XMVectorSet(std::sin(yaw), 0.0f, std::cos(yaw), 0.0f);
            const XMVECTOR rightAxis = XMVectorSet(std::cos(yaw), 0.0f, -std::sin(yaw), 0.0f);
            // Slower while looking through the scope.
            const float speed = (IsKeyDown(VK_SHIFT) && !m_scoped ? g_runSpeed : g_walkSpeed) * (m_scoped ? 0.5f : 1.0f);
            const XMVECTOR move = XMVector3Normalize(forwardAxis * forward + rightAxis * right) * speed * deltaTime;
            m_playerPosition.x += XMVectorGetX(move);
            m_playerPosition.z += XMVectorGetZ(move);
            m_walkCycle += deltaTime * speed;
            walking = true;
        }
    }

    // The invisible border around the beach and the promenade.
    m_playerPosition.x = std::clamp(m_playerPosition.x, borderMinX, borderMaxX);
    m_playerPosition.z = std::clamp(m_playerPosition.z, borderMinZ, borderMaxZ);
    m_playerPosition.y = GroundHeight(m_playerPosition.x, m_playerPosition.z);
    if (!walking)
    {
        // Let the head bob settle.
        m_walkCycle = std::round(m_walkCycle / XM_PI) * XM_PI;
    }

    // Left click: resume after pausing, otherwise shoot (also while a message is shown, it does not stop the game;
    // the next message replaces it). Space closes the message. Right click: scope in and out.
    const bool leftDown = focused && IsKeyDown(VK_LBUTTON);
    const bool rightDown = focused && IsKeyDown(VK_RBUTTON);
    const bool spaceDown = focused && IsKeyDown(VK_SPACE);
    if (leftDown && !m_leftButtonWasDown && !autopilot)
    {
        if (m_paused)
        {
            m_paused = false;
        }
        else if (inGame)
        {
            Fire();
        }
    }
    if (spaceDown && !m_spaceWasDown && !autopilot && m_messageVisible && m_messageTime > 0.3f)
    {
        m_messageVisible = false;
    }
    m_spaceWasDown = spaceDown;
    if (rightDown && !m_rightButtonWasDown && !autopilot && inGame && !m_paused)
    {
        m_scoped = !m_scoped;
    }
    m_leftButtonWasDown = leftDown;
    m_rightButtonWasDown = rightDown;

    m_scope = std::clamp(m_scope + (m_scoped ? 1.0f : -1.0f) * deltaTime / g_scopeTime, 0.0f, 1.0f);
    m_fireCooldown = std::max(0.0f, m_fireCooldown - deltaTime);
    m_muzzleFlash = std::max(0.0f, m_muzzleFlash - deltaTime);
    m_hitMarker = std::max(0.0f, m_hitMarker - deltaTime);
    m_recoil = std::max(0.0f, m_recoil - deltaTime * 4.0f);

    // Waking up: lying on the sand looking at the sky, then sitting up and looking at the sea.
    if (m_intro == IntroState::Waking)
    {
        m_eyeDrop = (eyeHeight - 0.35f) * (1.0f - SmoothStep(1.8f, 4.6f, m_introTime));
        m_pitchOverride = 1.0f - SmoothStep(1.2f, 4.4f, m_introTime);
    }
    else
    {
        m_eyeDrop = 0.0f;
        m_pitchOverride = 0.0f;
    }

    // The ocean's OnUpdate builds the camera rotation from m_pitch and m_yaw after this.
    m_camera.SetTranslation(GetEyePosition());
    m_camera.SetFov(fieldOfView);
    m_yaw = m_lookYaw;
    const float pitch = m_lookPitch - g_recoilKick * m_recoil * m_recoil * (m_scoped ? 0.5f : 1.0f);
    m_pitch = pitch + (-55.0f - pitch) * m_pitchOverride;
}

float BirthdayGame::GetTextSize() const
{
    const ImFont* largeFont = m_GUI->GetLargeFont();
    return largeFont ? largeFont->LegacySize * 0.6f : ImGui::GetFontSize() * 1.6f;
}

XMVECTOR BirthdayGame::GetEyePosition() const
{
    const float bob = std::abs(std::sin(m_walkCycle * 1.1f)) * 0.05f * (1.0f - m_scope);
    return XMVectorSet(m_playerPosition.x, m_playerPosition.y + eyeHeight + bob - m_eyeDrop, m_playerPosition.z, 1.0f);
}

XMVECTOR BirthdayGame::GetLookDirection() const
{
    const float yaw = XMConvertToRadians(m_lookYaw);
    const float pitch = XMConvertToRadians(m_lookPitch);
    return XMVectorSet(std::cos(pitch) * std::sin(yaw), -std::sin(pitch), std::cos(pitch) * std::cos(yaw), 0.0f);
}

XMMATRIX BirthdayGame::GetRifleTransform() const
{
    // Held to the right of and below the eye, the muzzle about a meter ahead, turned a little towards the
    // crosshair. Firing kicks it back and up; scoping in brings it to the middle, the scope in front of the eye.
    const float aim = SmoothStep(0.0f, 1.0f, m_scope);
    const float sway = std::sin(m_walkCycle * 0.55f) * 0.012f * (1.0f - aim);
    const float x = 0.25f + (0.045f - 0.25f) * aim + sway;
    const float y = -0.23f + (-0.14f + 0.23f) * aim + std::abs(sway);
    const XMMATRIX inView = XMMatrixRotationX(-0.15f * m_recoil) * XMMatrixRotationY(-0.045f * (1.0f - aim)) *
        XMMatrixTranslation(x, y, 0.95f - 0.1f * m_recoil);
    return inView * XMMatrixInverse(nullptr, m_camera.GetViewMatrix());
}

void BirthdayGame::AimAt(FXMVECTOR target)
{
    const XMVECTOR direction = XMVector3Normalize(target - GetEyePosition());
    m_lookYaw = XMConvertToDegrees(std::atan2(XMVectorGetX(direction), XMVectorGetZ(direction)));
    m_lookPitch = XMConvertToDegrees(std::asin(std::clamp(-XMVectorGetY(direction), -1.0f, 1.0f)));
}

void BirthdayGame::Fire()
{
    if (m_fireCooldown > 0.0f)
    {
        return;
    }
    m_fireCooldown = g_fireInterval;
    m_recoil = 1.0f;
    m_muzzleFlash = 0.07f;

    // The bullet goes where the crosshair is, instantly.
    const XMVECTOR origin = GetEyePosition();
    const XMVECTOR direction = GetLookDirection();

    enum class Target { None, Cake, Redo, LeftBird, RightBird } hit = Target::None;
    float nearest = FLT_MAX;
    BoundingBox bounds;
    float distance = 0.0f;
    // The cake and its pedestal, which has the "try again" signs (not the balloons tied to it).
    const std::pair<std::shared_ptr<SceneNode>, Target> cakeParts[] = { { m_cake, Target::Cake }, { m_pedestal, Target::Redo } };
    for (const auto& [part, target] : cakeParts)
    {
        if (part && ComputeWorldBounds(part, bounds) && bounds.Intersects(origin, direction, distance) && distance < nearest)
        {
            nearest = distance;
            hit = target;
        }
    }
    for (int i = 0; i < 2; ++i)
    {
        if (m_birds[i].body && ComputeWorldBounds(m_birds[i].body, bounds))
        {
            // A little bigger than the duck, it is flying after all.
            bounds.Extents = XMFLOAT3(bounds.Extents.x * 1.35f, bounds.Extents.y * 1.35f, bounds.Extents.z * 1.35f);
            if (bounds.Intersects(origin, direction, distance) && distance < nearest)
            {
                nearest = distance;
                hit = i == 0 ? Target::LeftBird : Target::RightBird;
            }
        }
    }

    switch (hit)
    {
    case Target::Cake:
        m_hitMarker = 0.25f;
        m_cakeBounce = 1.0f;
        OnCakeShot();
        break;
    case Target::Redo:
        m_hitMarker = 0.25f;
        OnRedoShot();
        break;
    case Target::LeftBird:
    case Target::RightBird:
    {
        const Side side = hit == Target::LeftBird ? Side::Left : Side::Right;
        m_hitMarker = 0.25f;
        m_birds[static_cast<int>(side)].hitTime = 0.0f;
        OnBirdShot(side);
        break;
    }
    default:
        break;
    }
}

// ------------------------------------------------------------------------------------------------
// HUD

void BirthdayGame::DrawHUD()
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 position = viewport->Pos;
    const ImVec2 size = viewport->Size;
    const ImVec2 center(position.x + size.x * 0.5f, position.y + size.y * 0.5f);
    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    const float unit = std::max(1.0f, size.y / 1080.0f); // Scales the HUD with the window.

    ImFont* largeFont = m_GUI->GetLargeFont();
    ImFont* font = largeFont ? largeFont : ImGui::GetFont();
    const float fontSize = GetTextSize();

    if (m_intro == IntroState::Menu || m_intro == IntroState::FadeOut)
    {
        DrawMenu(drawList, position, size);
    }
    else
    {
        // Muzzle flash: a bright star at the end of the barrel (the rifle is not seen through the scope).
        if (m_muzzleFlash > 0.0f && m_scope < 0.8f)
        {
            const XMVECTOR muzzle = XMVector3TransformCoord(XMLoadFloat3(&m_rifleMuzzle), GetRifleTransform());
            const XMVECTOR clip = XMVector3TransformCoord(muzzle, m_camera.GetViewMatrix() * m_camera.GetProjectionMatrix());
            const ImVec2 flash(position.x + (XMVectorGetX(clip) * 0.5f + 0.5f) * size.x, position.y + (0.5f - XMVectorGetY(clip) * 0.5f) * size.y);
            const float alpha = m_muzzleFlash / 0.07f;
            const int points = 12;
            for (int layer = 0; layer < 2; ++layer)
            {
                const float outer = (layer == 0 ? 90.0f : 45.0f) * unit;
                const float inner = outer * 0.35f;
                ImVec2 star[points * 2];
                for (int i = 0; i < points * 2; ++i)
                {
                    const float angle = i * XM_PI / points + m_time * 7.0f;
                    const float radius = (i % 2 == 0) ? outer * (0.75f + 0.25f * std::sin(i * 2.3f)) : inner;
                    star[i] = ImVec2(flash.x + std::cos(angle) * radius, flash.y + std::sin(angle) * radius);
                }
                // Concave, so draw it as triangles around the middle.
                const ImU32 color = layer == 0 ? Color(255, 170, 60, 0.85f * alpha) : Color(255, 250, 220, alpha);
                for (int i = 0; i < points * 2; ++i)
                {
                    drawList->AddTriangleFilled(flash, star[i], star[(i + 1) % (points * 2)], color);
                }
            }
        }

        if (m_scope >= 0.8f)
        {
            DrawScope(drawList, position, size);
        }
        else if (m_intro == IntroState::Playing)
        {
            // Crosshair.
            const float gap = 6.0f * unit;
            const float length = 9.0f * unit;
            for (int pass = 0; pass < 2; ++pass)
            {
                const ImU32 color = pass == 0 ? Color(0, 0, 0, 0.6f) : Color(255, 255, 255);
                const float thickness = (pass == 0 ? 4.0f : 2.0f) * unit;
                drawList->AddLine(ImVec2(center.x - gap - length, center.y), ImVec2(center.x - gap, center.y), color, thickness);
                drawList->AddLine(ImVec2(center.x + gap, center.y), ImVec2(center.x + gap + length, center.y), color, thickness);
                drawList->AddLine(ImVec2(center.x, center.y - gap - length), ImVec2(center.x, center.y - gap), color, thickness);
                drawList->AddLine(ImVec2(center.x, center.y + gap), ImVec2(center.x, center.y + gap + length), color, thickness);
            }
            drawList->AddCircleFilled(center, 1.5f * unit, Color(255, 255, 255));
        }

        // Hit marker after hitting something.
        if (m_hitMarker > 0.0f)
        {
            const float alpha = m_hitMarker / 0.25f;
            const float inner = 10.0f * unit;
            const float outer = 20.0f * unit;
            for (float sx : { -1.0f, 1.0f })
            {
                for (float sy : { -1.0f, 1.0f })
                {
                    drawList->AddLine(ImVec2(center.x + sx * inner, center.y + sy * inner), ImVec2(center.x + sx * outer, center.y + sy * outer),
                        Color(255, 80, 80, alpha), 3.0f * unit);
                }
            }
        }

        // The question at the top while answering it.
        if (m_state == QuizState::Question && !m_messageVisible && !m_banner.empty())
        {
            const float wrap = std::min(size.x * 0.7f, fontSize * 30.0f);
            const float bannerSize = fontSize * 0.75f;
            const ImVec2 textSize = font->CalcTextSizeA(bannerSize, FLT_MAX, wrap, m_banner.c_str());
            const ImVec2 padding(bannerSize * 0.8f, bannerSize * 0.45f);
            const ImVec2 topLeft(center.x - textSize.x * 0.5f - padding.x, position.y + 20.0f * unit);
            const ImVec2 bottomRight(center.x + textSize.x * 0.5f + padding.x, topLeft.y + textSize.y + padding.y * 2.0f);
            drawList->AddRectFilled(topLeft, bottomRight, Color(10, 20, 45, 0.78f), bannerSize * 0.4f);
            drawList->AddText(font, bannerSize, ImVec2(topLeft.x + padding.x, topLeft.y + padding.y), Color(255, 255, 255), m_banner.c_str(),
                nullptr, wrap);
        }

        if (m_messageVisible)
        {
            DrawTextBox(drawList, position, size);
        }

        UpdateConfetti(ImGui::GetIO().DeltaTime, size);
        for (const Particle& particle : m_confetti)
        {
            const ImVec2 middle(position.x + particle.position.x * size.x, position.y + particle.position.y * size.y);
            const float halfWidth = particle.size * unit;
            const float halfHeight = particle.size * 0.55f * unit * std::abs(std::cos(particle.angle * 1.7f)); // Flutters.
            const float c = std::cos(particle.angle);
            const float s = std::sin(particle.angle);
            auto corner = [&](float x, float y) { return ImVec2(middle.x + x * c - y * s, middle.y + x * s + y * c); };
            drawList->AddQuadFilled(corner(-halfWidth, -halfHeight), corner(halfWidth, -halfHeight), corner(halfWidth, halfHeight),
                corner(-halfWidth, halfHeight), particle.color);
        }

        if (m_intro == IntroState::Waking)
        {
            DrawWakeUp(drawList, position, size);
        }
    }

    if (m_paused)
    {
        drawList->AddRectFilled(position, ImVec2(position.x + size.x, position.y + size.y), Color(0, 0, 0, 0.55f));
        const char* title = "Paused";
        const float titleSize = fontSize * 1.5f;
        const ImVec2 titleExtent = font->CalcTextSizeA(titleSize, FLT_MAX, 0.0f, title);
        drawList->AddText(font, titleSize, ImVec2(center.x - titleExtent.x * 0.5f, center.y - titleExtent.y), Color(255, 255, 255), title);
        const char* help = "Click to keep playing.   Press Esc again to quit.";
        const ImVec2 helpSize = font->CalcTextSizeA(fontSize * 0.7f, FLT_MAX, 0.0f, help);
        drawList->AddText(font, fontSize * 0.7f, ImVec2(center.x - helpSize.x * 0.5f, center.y + fontSize * 0.3f), Color(230, 230, 230), help);
    }
}

void BirthdayGame::DrawMenu(ImDrawList* drawList, const ImVec2& position, const ImVec2& size)
{
    ImFont* largeFont = m_GUI->GetLargeFont();
    ImFont* font = largeFont ? largeFont : ImGui::GetFont();
    const float fontSize = GetTextSize();
    const ImVec2 center(position.x + size.x * 0.5f, position.y + size.y * 0.5f);

    // The menu comes out of black, and goes back to black after Play.
    const bool fading = m_intro == IntroState::FadeOut;
    const float alpha = fading ? 1.0f - SmoothStep(0.0f, g_fadeOutTime * 0.6f, m_introTime) : SmoothStep(0.3f, g_menuFadeIn, m_introTime);
    const float black = fading ? SmoothStep(0.0f, g_fadeOutTime, m_introTime) : 1.0f - SmoothStep(0.0f, g_menuFadeIn, m_introTime);

    // A soft dark band behind the title, so it reads on the bright sky.
    const float titleSize = largeFont ? largeFont->LegacySize * 1.25f : fontSize * 2.0f;
    const std::string& title = m_quiz.title;
    const ImVec2 titleExtent = font->CalcTextSizeA(titleSize, FLT_MAX, 0.0f, title.c_str());
    const float titleY = position.y + size.y * 0.13f;
    const float bandTop = titleY - titleSize * 0.5f;
    const float bandBottom = titleY + titleExtent.y + titleSize * 0.5f;
    const float bandMiddle = (bandTop + bandBottom) * 0.5f;
    drawList->AddRectFilledMultiColor(ImVec2(position.x, bandTop), ImVec2(position.x + size.x, bandMiddle), Color(0, 0, 0, 0.0f),
        Color(0, 0, 0, 0.0f), Color(0, 0, 0, 0.35f * alpha), Color(0, 0, 0, 0.35f * alpha));
    drawList->AddRectFilledMultiColor(ImVec2(position.x, bandMiddle), ImVec2(position.x + size.x, bandBottom), Color(0, 0, 0, 0.35f * alpha),
        Color(0, 0, 0, 0.35f * alpha), Color(0, 0, 0, 0.0f), Color(0, 0, 0, 0.0f));
    const ImVec2 titlePosition(center.x - titleExtent.x * 0.5f, titleY);
    drawList->AddText(font, titleSize, ImVec2(titlePosition.x + 4.0f, titlePosition.y + 5.0f), Color(0, 0, 0, 0.5f * alpha), title.c_str());
    drawList->AddText(font, titleSize, titlePosition, Color(255, 222, 130, alpha), title.c_str());

    // Play (and Quit) under the cake.
    ImGui::SetNextWindowPos(ImVec2(center.x, position.y + size.y * 0.8f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, std::max(alpha, 0.01f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, fontSize * 0.5f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(fontSize * 1.2f, fontSize * 0.35f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, fontSize * 0.5f));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.93f, 0.33f, 0.45f, 0.92f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.45f, 0.55f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.8f, 0.25f, 0.38f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDocking;
    if (ImGui::Begin("##StartMenu", nullptr, flags))
    {
        ImGui::PushFont(font, fontSize * 1.25f);
        const float buttonWidth = fontSize * 7.0f;
        if (ImGui::Button("Play", ImVec2(buttonWidth, 0.0f)))
        {
            StartGame();
        }
        ImGui::PopFont();
        ImGui::PushFont(font, fontSize * 0.6f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.05f, 0.08f, 0.15f, 0.6f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.1f, 0.15f, 0.28f, 0.8f));
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + buttonWidth * 0.3f);
        if (ImGui::Button("Quit", ImVec2(buttonWidth * 0.4f, 0.0f)) && m_intro == IntroState::Menu)
        {
            if (m_editor->IsStandalonePlay())
            {
                Application::Get().Stop();
            }
            else
            {
                m_editor->StopPlaying();
            }
        }
        ImGui::PopStyleColor(2);
        ImGui::PopFont();
    }
    ImGui::End();
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar(4);

    if (black > 0.0f)
    {
        drawList->AddRectFilled(position, ImVec2(position.x + size.x, position.y + size.y), Color(0, 0, 0, black));
    }
}

void BirthdayGame::DrawWakeUp(ImDrawList* drawList, const ImVec2& position, const ImVec2& size)
{
    // Eyelids that open, blink and open again, over a darkness that slowly lifts.
    const float time = m_introTime;
    const float openness = EyeOpenness(time);
    const float centerY = position.y + size.y * 0.5f;
    const float centerX = position.x + size.x * 0.5f;
    const float gap = openness * size.y * 0.85f;
    const float soft = size.y * 0.09f; // The blurry edge of the lids.

    // Vertical slices on whole pixels, side by side: overlapping half transparent edges would show as stripes.
    const int slices = 64;
    const float sliceWidth = size.x / slices;
    for (int i = 0; i < slices; ++i)
    {
        const float x0 = std::floor(position.x + i * sliceWidth);
        const float x1 = i + 1 == slices ? position.x + size.x : std::floor(position.x + (i + 1) * sliceWidth);
        const float u = ((x0 + sliceWidth * 0.5f) - centerX) / (size.x * 0.5f);
        const float halfGap = gap * 0.5f * (1.0f - 0.35f * u * u); // Curved like an eye.
        const float topEdge = centerY - halfGap;
        const float bottomEdge = centerY + halfGap;
        drawList->AddRectFilled(ImVec2(x0, position.y), ImVec2(x1, topEdge), Color(0, 0, 0));
        drawList->AddRectFilledMultiColor(ImVec2(x0, topEdge), ImVec2(x1, topEdge + soft), Color(0, 0, 0), Color(0, 0, 0),
            Color(0, 0, 0, 0.0f), Color(0, 0, 0, 0.0f));
        drawList->AddRectFilled(ImVec2(x0, bottomEdge), ImVec2(x1, position.y + size.y), Color(0, 0, 0));
        drawList->AddRectFilledMultiColor(ImVec2(x0, bottomEdge - soft), ImVec2(x1, bottomEdge), Color(0, 0, 0, 0.0f), Color(0, 0, 0, 0.0f),
            Color(0, 0, 0), Color(0, 0, 0));
    }

    // Still sleepy: the whole picture is darker at first, with a warm dreamy haze.
    const float darkness = 0.75f * (1.0f - SmoothStep(0.5f, 4.2f, time));
    const float haze = 0.25f * std::sin(XM_PI * std::clamp(time / g_wakeTime, 0.0f, 1.0f));
    drawList->AddRectFilled(position, ImVec2(position.x + size.x, position.y + size.y), Color(255, 236, 205, haze));
    drawList->AddRectFilled(position, ImVec2(position.x + size.x, position.y + size.y), Color(0, 0, 0, darkness));
}

void BirthdayGame::DrawScope(ImDrawList* drawList, const ImVec2& position, const ImVec2& size)
{
    // Black around a round view, with a thin reticle.
    const ImVec2 center(position.x + size.x * 0.5f, position.y + size.y * 0.5f);
    const float radius = std::min(size.x, size.y) * 0.46f;
    const float outer = std::sqrt(size.x * size.x + size.y * size.y);
    const int segments = 96;
    for (int i = 0; i < segments; ++i)
    {
        const float a0 = XM_2PI * i / segments;
        const float a1 = XM_2PI * (i + 1) / segments;
        const ImVec2 inner0(center.x + std::cos(a0) * radius, center.y + std::sin(a0) * radius);
        const ImVec2 inner1(center.x + std::cos(a1) * radius, center.y + std::sin(a1) * radius);
        const ImVec2 outer0(center.x + std::cos(a0) * outer, center.y + std::sin(a0) * outer);
        const ImVec2 outer1(center.x + std::cos(a1) * outer, center.y + std::sin(a1) * outer);
        drawList->AddQuadFilled(inner0, outer0, outer1, inner1, Color(0, 0, 0));
    }
    // A dark rim inside the lens.
    for (int ring = 0; ring < 6; ++ring)
    {
        drawList->AddCircle(center, radius - ring * 3.0f, Color(0, 0, 0, 0.55f - ring * 0.09f), segments, 3.5f);
    }

    // Reticle: thick lines from the edge, thin near the middle, and a red dot.
    const float thin = std::max(1.0f, size.y / 900.0f);
    const float thick = thin * 4.0f;
    const float inner = radius * 0.35f;
    const ImU32 black = Color(0, 0, 0, 0.9f);
    drawList->AddLine(ImVec2(center.x - radius, center.y), ImVec2(center.x - inner, center.y), black, thick);
    drawList->AddLine(ImVec2(center.x + inner, center.y), ImVec2(center.x + radius, center.y), black, thick);
    drawList->AddLine(ImVec2(center.x, center.y + inner), ImVec2(center.x, center.y + radius), black, thick);
    drawList->AddLine(ImVec2(center.x - inner, center.y), ImVec2(center.x + inner, center.y), black, thin);
    drawList->AddLine(ImVec2(center.x, center.y - radius), ImVec2(center.x, center.y + inner), black, thin);
    for (int mark = 1; mark <= 4; ++mark)
    {
        const float offset = inner * mark / 5.0f;
        const float half = radius * 0.012f;
        drawList->AddLine(ImVec2(center.x - offset, center.y - half), ImVec2(center.x - offset, center.y + half), black, thin);
        drawList->AddLine(ImVec2(center.x + offset, center.y - half), ImVec2(center.x + offset, center.y + half), black, thin);
        drawList->AddLine(ImVec2(center.x - half, center.y + offset), ImVec2(center.x + half, center.y + offset), black, thin);
    }
    drawList->AddCircleFilled(center, thin * 2.2f, Color(255, 40, 40));
}

void BirthdayGame::DrawTextBox(ImDrawList* drawList, const ImVec2& position, const ImVec2& size)
{
    ImFont* largeFont = m_GUI->GetLargeFont();
    ImFont* font = largeFont ? largeFont : ImGui::GetFont();
    const float fontSize = GetTextSize();

    // A box along the bottom, the text on the left and a "space to close" arrow on the right side.
    const float width = std::min(size.x * 0.86f, fontSize * 36.0f);
    const float padding = fontSize * 0.7f;
    const float arrowArea = fontSize * 4.2f;
    const float wrap = width - padding * 2.0f - arrowArea;
    const ImVec2 textSize = font->CalcTextSizeA(fontSize, FLT_MAX, wrap, m_message.c_str());
    const float height = std::max(textSize.y, fontSize * 2.0f) + padding * 2.0f;

    // Slides up when it appears.
    const float appear = std::min(1.0f, m_messageTime / 0.25f);
    const float slide = (1.0f - appear) * (1.0f - appear) * fontSize * 2.0f;
    const ImVec2 topLeft(position.x + (size.x - width) * 0.5f, position.y + size.y - height - fontSize * 1.2f + slide);
    const ImVec2 bottomRight(topLeft.x + width, topLeft.y + height);

    drawList->AddRectFilled(ImVec2(topLeft.x + 6.0f, topLeft.y + 8.0f), ImVec2(bottomRight.x + 6.0f, bottomRight.y + 8.0f),
        Color(0, 0, 0, 0.35f * appear), fontSize * 0.5f);
    drawList->AddRectFilledMultiColor(topLeft, bottomRight, Color(18, 40, 92, 0.94f * appear), Color(18, 40, 92, 0.94f * appear),
        Color(8, 18, 48, 0.94f * appear), Color(8, 18, 48, 0.94f * appear));
    drawList->AddRect(topLeft, bottomRight, Color(255, 215, 120, appear), fontSize * 0.25f, 0, 3.0f);
    drawList->AddText(font, fontSize, ImVec2(topLeft.x + padding, topLeft.y + (height - textSize.y) * 0.5f), Color(255, 255, 255, appear),
        m_message.c_str(), nullptr, wrap);

    // The arrow bobs to the right; the hint is under it.
    const float bob = std::sin(m_messageTime * 6.0f) * fontSize * 0.15f;
    const ImVec2 arrowCenter(bottomRight.x - arrowArea * 0.5f + bob, topLeft.y + height * 0.42f);
    const float arrowSize = fontSize * 0.55f;
    drawList->AddTriangleFilled(ImVec2(arrowCenter.x - arrowSize * 0.6f, arrowCenter.y - arrowSize),
        ImVec2(arrowCenter.x + arrowSize * 0.8f, arrowCenter.y), ImVec2(arrowCenter.x - arrowSize * 0.6f, arrowCenter.y + arrowSize),
        Color(255, 215, 120, appear));
    const char* hint = "spatie pa sera";
    const float hintSize = fontSize * 0.42f;
    const ImVec2 hintTextSize = font->CalcTextSizeA(hintSize, FLT_MAX, 0.0f, hint);
    drawList->AddText(font, hintSize, ImVec2(bottomRight.x - arrowArea * 0.5f - hintTextSize.x * 0.5f, arrowCenter.y + arrowSize * 1.3f),
        Color(255, 235, 190, appear), hint);
}

void BirthdayGame::UpdateConfetti(float deltaTime, const ImVec2& viewportSize)
{
    const float aspect = viewportSize.y > 0.0f ? viewportSize.x / viewportSize.y : 1.0f;
    for (Particle& particle : m_confetti)
    {
        particle.velocity.y = std::min(particle.velocity.y + deltaTime * 0.25f, 0.18f);
        particle.velocity.x *= 1.0f - deltaTime * 0.5f;
        particle.position.x += (particle.velocity.x + 0.02f * std::sin(particle.angle * 2.0f)) * deltaTime / aspect;
        particle.position.y += particle.velocity.y * deltaTime;
        particle.angle += particle.spin * deltaTime;
    }
    std::erase_if(m_confetti, [](const Particle& particle) { return particle.position.y > 1.1f; });
}

// ------------------------------------------------------------------------------------------------
// Autopilot: a fixed tour that takes screenshots, so the CI can show what the game looks like.

void BirthdayGame::UpdateAutopilot(float deltaTime)
{
    if (m_options.autopilotFolder.empty())
    {
        return;
    }
    if (!m_autopilotStarted)
    {
        m_autopilotStarted = true;
        m_editor->Play();
        g_autopilotStepStart = std::chrono::steady_clock::now();
        m_autopilotStep = 0;
        m_autopilotFrames = -1; // The first step starts below.
    }

    auto lookAtScreens = [this]()
    {
        m_playerPosition = XMFLOAT3(0.0f, 0.0f, -3.0f);
        m_lookYaw = 180.0f;
        m_lookPitch = -13.0f;
    };
    auto shootCake = [this]()
    {
        BoundingBox bounds;
        ComputeWorldBounds(m_cake, bounds);
        AimAt(XMLoadFloat3(&bounds.Center));
        m_fireCooldown = 0.0f;
        Fire();
    };
    auto shootBird = [this](Side side)
    {
        BoundingBox bounds;
        ComputeWorldBounds(m_birds[static_cast<int>(side)].body, bounds);
        AimAt(XMLoadFloat3(&bounds.Center));
        m_fireCooldown = 0.0f;
        Fire();
    };
    auto shootRedo = [this]()
    {
        BoundingBox bounds;
        ComputeWorldBounds(m_pedestal, bounds);
        AimAt(XMLoadFloat3(&bounds.Center));
        m_fireCooldown = 0.0f;
        Fire();
    };
    auto always = []() { return true; };

    struct Step
    {
        const char*           name;
        std::function<void()> start;
        std::function<bool()> ready; // The screenshot is taken once this is true (and the step had a few frames).
    };
    const Step steps[] = {
        { "01_menu", []() {}, [this]() { return m_introTime > g_menuFadeIn; } },
        { "02_waking", [this]() { StartGame(); }, [this]() { return m_intro == IntroState::Waking && m_introTime > 2.4f; } },
        { "03_start", []() {}, [this]() { return m_intro == IntroState::Playing && m_messageTime > 0.5f; } },
        { "04_scope", [&]()
            {
                m_messageVisible = false;
                lookAtScreens();
                BoundingBox bounds;
                ComputeWorldBounds(m_birds[static_cast<int>(Side::Left)].body, bounds);
                AimAt(XMLoadFloat3(&bounds.Center));
                m_scoped = true;
            }, [this]() { return m_scope >= 1.0f; } },
        { "05_screens", [&]() { m_scoped = false; m_scope = 0.0f; lookAtScreens(); }, always },
        { "06_question", [&]() { lookAtScreens(); shootCake(); lookAtScreens(); }, always },
        { "07_answer_left", [&]() { m_messageVisible = false; lookAtScreens(); shootBird(Side::Left); lookAtScreens(); }, always },
        // The same question again, from where the "try again" signs on the pedestal can be seen.
        { "07_redo", [&]() { lookAtScreens(); shootRedo(); }, always },
        { "07_answer_again", [&]() { lookAtScreens(); shootBird(Side::Right); lookAtScreens(); }, always },
        { "08_second_question", [&]() { lookAtScreens(); shootCake(); m_messageVisible = false; lookAtScreens(); }, always },
        { "09_answer_right", [&]() { lookAtScreens(); shootBird(Side::Right); lookAtScreens(); }, always },
        { "10_finale", [&]()
            {
                // Answer the remaining questions, then the cake once more.
                for (int i = 0; i < 100 && m_state != QuizState::Finale; ++i)
                {
                    if (m_state == QuizState::Question)
                    {
                        Answer(Side::Left);
                    }
                    else
                    {
                        OnCakeShot();
                    }
                }
                lookAtScreens();
            }, always },
        { "11_beach_side", [&]()
            {
                m_messageVisible = false;
                m_playerPosition = XMFLOAT3(-12.0f, 0.0f, 6.0f);
                m_lookYaw = 60.0f;
                m_lookPitch = 2.0f;
            }, always },
        { "12_shadows", [&]()
            {
                // Behind the cake, looking towards the sun: the shadows fall towards the camera.
                m_playerPosition = XMFLOAT3(-3.4f, 0.0f, cakeZ - 5.0f);
                m_lookYaw = 34.0f;
                m_lookPitch = 18.0f;
            }, always },
        { "12_shadow_lookup", [&]() { m_shadowDebug = true; }, always },
        { "13_promenade", [&]()
            {
                m_shadowDebug = false;
                m_playerPosition = XMFLOAT3(13.0f, 0.0f, -3.0f);
                m_lookYaw = 235.0f;
                m_lookPitch = 8.0f;
            }, always },
        { "14_curacao", [&]()
            {
                // Behind the screens: the CURACAO letters and the houses of Willemstad.
                m_playerPosition = XMFLOAT3(0.0f, 0.0f, borderMinZ);
                m_lookYaw = 180.0f;
                m_lookPitch = -6.0f;
            }, always },
        { "15_willemstad", [&]()
            {
                m_playerPosition = XMFLOAT3(borderMaxX, 0.0f, -28.0f);
                m_lookYaw = 215.0f;
                m_lookPitch = -4.0f;
            }, always },
        { "16_bridge", [&]()
            {
                // The Queen Emma bridge, on the right of the beach.
                m_playerPosition = XMFLOAT3(borderMaxX, 0.0f, 3.0f);
                m_lookYaw = 55.0f;
                m_lookPitch = 2.0f;
            }, always },
        { "17_editor", [this]() { m_editor->StopPlaying(); }, always },
    };
    constexpr size_t stepCount = sizeof(steps) / sizeof(steps[0]);

    if (m_autopilotFrames < 0)
    {
        steps[m_autopilotStep].start();
        m_autopilotFrames = 0;
        g_autopilotStepStart = std::chrono::steady_clock::now();
        return;
    }
    ++m_autopilotFrames;

    // Let the step settle for a few frames (and seconds: the CI renders slowly), then take its screenshot. A step
    // that never gets ready is captured anyway after two minutes.
    const float elapsed = std::chrono::duration<float>(std::chrono::steady_clock::now() - g_autopilotStepStart).count();
    const bool ready = steps[m_autopilotStep].ready() || elapsed > 120.0f;
    if (m_autopilotFrames < 8 || elapsed < 3.0f || !ready)
    {
        return;
    }
    SaveWindowScreenshot(m_pWindow->GetWindowHandle(), m_options.autopilotFolder / (std::string(steps[m_autopilotStep].name) + ".bmp"));

    if (++m_autopilotStep >= stepCount)
    {
        Application::Get().Stop();
        return;
    }
    m_autopilotFrames = -1;
}
