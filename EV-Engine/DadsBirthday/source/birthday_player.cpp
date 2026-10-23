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
    constexpr float g_mouseSpeed = 0.1f;   // Degrees per pixel.
    constexpr float g_fieldOfView = 65.0f; // Degrees, vertical.
    constexpr float g_fireInterval = 0.35f;
    constexpr float g_recoilKick = 3.0f;   // Degrees the view jumps up when firing.

    bool IsKeyDown(int key)
    {
        return (GetAsyncKeyState(key) & 0x8000) != 0;
    }

    ImU32 Color(int r, int g, int b, float alpha = 1.0f)
    {
        return IM_COL32(r, g, b, static_cast<int>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f));
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

void BirthdayGame::ResetPlayer()
{
    m_playerPosition = XMFLOAT3(playerStartX, GroundHeight(playerStartX, playerStartZ), playerStartZ);
    m_lookYaw = 0.0f;
    m_lookPitch = playerStartPitch;
    m_recoil = 0.0f;
    m_fireCooldown = 0.0f;
    m_muzzleFlash = 0.0f;
    m_walkCycle = 0.0f;
    m_leftButtonWasDown = true; // The click that started playing does not shoot.

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
    const bool control = focused && !m_paused && !autopilot;
    CaptureMouse(control);

    // Look around.
    if (m_mouseCaptured)
    {
        RECT client;
        GetClientRect(window, &client);
        POINT center = { (client.right - client.left) / 2, (client.bottom - client.top) / 2 };
        ClientToScreen(window, &center);
        POINT cursor;
        if (GetCursorPos(&cursor))
        {
            m_lookYaw += (cursor.x - center.x) * g_mouseSpeed;
            m_lookPitch = std::clamp(m_lookPitch + (cursor.y - center.y) * g_mouseSpeed, -80.0f, 80.0f);
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
            const float speed = IsKeyDown(VK_SHIFT) ? g_runSpeed : g_walkSpeed;
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

    // Left click: continue after a message, resume after pausing, otherwise shoot.
    const bool buttonDown = focused && IsKeyDown(VK_LBUTTON);
    if (buttonDown && !m_leftButtonWasDown && !autopilot)
    {
        if (m_paused)
        {
            m_paused = false;
        }
        else if (m_messageVisible)
        {
            if (m_messageTime > 0.3f)
            {
                m_messageVisible = false;
            }
        }
        else
        {
            Fire();
        }
    }
    m_leftButtonWasDown = buttonDown;

    m_fireCooldown = std::max(0.0f, m_fireCooldown - deltaTime);
    m_muzzleFlash = std::max(0.0f, m_muzzleFlash - deltaTime);
    m_hitMarker = std::max(0.0f, m_hitMarker - deltaTime);
    m_recoil = std::max(0.0f, m_recoil - deltaTime * 4.0f);

    // The ocean's OnUpdate builds the camera rotation from m_pitch and m_yaw after this.
    m_camera.SetTranslation(GetEyePosition());
    m_camera.SetFov(g_fieldOfView);
    m_yaw = m_lookYaw;
    m_pitch = m_lookPitch - g_recoilKick * m_recoil * m_recoil;
}

XMVECTOR BirthdayGame::GetEyePosition() const
{
    const float bob = std::abs(std::sin(m_walkCycle * 1.1f)) * 0.05f;
    return XMVectorSet(m_playerPosition.x, m_playerPosition.y + eyeHeight + bob, m_playerPosition.z, 1.0f);
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
    // crosshair. Firing kicks it back and up.
    const float sway = std::sin(m_walkCycle * 0.55f) * 0.012f;
    const XMMATRIX inView = XMMatrixRotationX(-0.15f * m_recoil) * XMMatrixRotationY(-0.045f) *
        XMMatrixTranslation(0.2f + sway, -0.19f + std::abs(sway), 0.95f - 0.1f * m_recoil);
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

    enum class Target { None, Cake, LeftBird, RightBird } hit = Target::None;
    float nearest = FLT_MAX;
    BoundingBox bounds;
    float distance = 0.0f;
    if (m_cakeRoot && ComputeWorldBounds(m_cakeRoot, bounds) && bounds.Intersects(origin, direction, distance) && distance < nearest)
    {
        nearest = distance;
        hit = Target::Cake;
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

    // Muzzle flash: a bright star at the end of the barrel.
    if (m_muzzleFlash > 0.0f)
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

    // Crosshair, with a hit marker after hitting something.
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

    ImFont* largeFont = m_GUI->GetLargeFont();
    ImFont* font = largeFont ? largeFont : ImGui::GetFont();
    const float fontSize = largeFont ? largeFont->LegacySize : ImGui::GetFontSize() * 1.6f;

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

    if (m_paused)
    {
        drawList->AddRectFilled(position, ImVec2(position.x + size.x, position.y + size.y), Color(0, 0, 0, 0.55f));
        const char* title = "Paused";
        const ImVec2 titleSize = font->CalcTextSizeA(fontSize * 1.5f, FLT_MAX, 0.0f, title);
        drawList->AddText(font, fontSize * 1.5f, ImVec2(center.x - titleSize.x * 0.5f, center.y - titleSize.y), Color(255, 255, 255), title);
        const char* help = "Click to keep playing.   Press Esc again to quit.";
        const ImVec2 helpSize = font->CalcTextSizeA(fontSize * 0.7f, FLT_MAX, 0.0f, help);
        drawList->AddText(font, fontSize * 0.7f, ImVec2(center.x - helpSize.x * 0.5f, center.y + fontSize * 0.3f), Color(230, 230, 230), help);
    }
}

void BirthdayGame::DrawTextBox(ImDrawList* drawList, const ImVec2& position, const ImVec2& size)
{
    ImFont* largeFont = m_GUI->GetLargeFont();
    ImFont* font = largeFont ? largeFont : ImGui::GetFont();
    const float fontSize = largeFont ? largeFont->LegacySize : ImGui::GetFontSize() * 1.6f;

    // A box along the bottom, the text on the left and a "click to continue" arrow on the right side.
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
    const char* hint = "click to continue";
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
        ComputeWorldBounds(m_cakeRoot, bounds);
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

    struct Step
    {
        const char*           name;
        std::function<void()> start;
    };
    const Step steps[] = {
        { "1_start", [this]() { ResetPlayer(); } },
        { "2_screens", [&]() { m_messageVisible = false; lookAtScreens(); } },
        { "3_question", [&]() { lookAtScreens(); shootCake(); lookAtScreens(); } },
        { "4_answer_left", [&]() { m_messageVisible = false; lookAtScreens(); shootBird(Side::Left); lookAtScreens(); } },
        { "5_second_question", [&]() { lookAtScreens(); shootCake(); m_messageVisible = false; lookAtScreens(); } },
        { "6_answer_right", [&]() { lookAtScreens(); shootBird(Side::Right); lookAtScreens(); } },
        { "7_finale", [&]()
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
            } },
        { "8_beach_side", [&]()
            {
                m_messageVisible = false;
                m_playerPosition = XMFLOAT3(-12.0f, 0.0f, 6.0f);
                m_lookYaw = 60.0f;
                m_lookPitch = 2.0f;
            } },
        { "9_editor", [this]() { m_editor->StopPlaying(); } },
    };
    constexpr size_t stepCount = sizeof(steps) / sizeof(steps[0]);

    const float elapsed = std::chrono::duration<float>(std::chrono::steady_clock::now() - g_autopilotStepStart).count();
    if (m_autopilotFrames < 0)
    {
        steps[m_autopilotStep].start();
        m_autopilotFrames = 0;
        g_autopilotStepStart = std::chrono::steady_clock::now();
        return;
    }
    ++m_autopilotFrames;

    // Let the step settle for a few frames (and seconds: the CI renders slowly), then take its screenshot.
    if (m_autopilotFrames < 8 || elapsed < 3.0f)
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
