#include "core/EV.h"

#include "birthday_game.h"

#include <algorithm>
#include <cmath>
#include <cwchar>

#include "geometry.h"
#include "textures.h"

using namespace DirectX;
using namespace EV;
using namespace Birthday;
using namespace Birthday::Layout;

// Willemstad behind the promenade, after photos of the CURACAO letters in Punda and the Handelskade's colorful
// houses. Not accurate, just recognizable: the letters are yellow with a blue front and a white line between, two
// white stars on the C, and the houses have curved or stepped gables, white trim and orange tiled roofs.

namespace
{
    // Material colors are linear; these are picked as sRGB.
    XMFLOAT4 FromSRGB(uint32_t rgb)
    {
        auto channel = [](uint32_t value) { return std::pow(value / 255.0f, 2.2f); };
        return XMFLOAT4(channel((rgb >> 16) & 0xFF), channel((rgb >> 8) & 0xFF), channel(rgb & 0xFF), 1.0f);
    }

    std::shared_ptr<Material> CreatePaint(uint32_t rgb, float roughness)
    {
        auto material = std::make_shared<Material>();
        material->SetDiffuseColor(FromSRGB(rgb));
        material->SetRoughness(roughness);
        material->SetMetallic(0.0f);
        return material;
    }

    // The letters.
    constexpr float g_letterHeight = 4.2f;
    constexpr float g_letterDepth = 0.9f;
    constexpr float g_letterGap = 0.3f;
    constexpr float g_yellowBand = 0.17f; // Yellow around the front, then a white line, then blue.
    constexpr float g_whiteLine = 0.07f;
    constexpr uint32_t g_letterPixels = 420; // How high the letters are drawn for their outlines.
    constexpr uint32_t g_letterMargin = 16;
    constexpr uint32_t g_fieldStep = 4;       // Pixels per grid cell of the outlines.

    constexpr uint32_t g_signYellow = 0xF6C500;
    constexpr uint32_t g_signBlue = 0x1A3C9E;
    constexpr uint32_t g_white = 0xF4F3EE;

    // A five pointed star around a point (pixels, y down), its first point up.
    std::vector<XMFLOAT2> Star(float x, float y, float radius)
    {
        std::vector<XMFLOAT2> points;
        for (int i = 0; i < 10; ++i)
        {
            const float angle = XM_PI * i / 5.0f;
            const float r = (i % 2 == 0) ? radius : radius * 0.42f;
            points.emplace_back(x + std::sin(angle) * r, y - std::cos(angle) * r);
        }
        return points;
    }

    // The houses. Their front wall is at z = 0 facing +z, centered on x = 0, standing on y = 0.
    enum class Gable
    {
        Curved,  // A Curacao gable: curved shoulders and a rounded top.
        Stepped, // A Dutch stepped gable.
        Hipped,  // No gable: a hipped roof along the street, with a small curved gable in the middle.
    };

    struct House
    {
        float    width;
        int      floors;
        uint32_t color;
        Gable    gable;
        bool     shutters;
    };

    constexpr float g_floorHeight = 3.3f;
    constexpr float g_houseDepth = 9.0f;
    constexpr float g_buried = 4.0f;        // The walls go this far into the ground (the terrace is not quite flat).
    constexpr float g_gablePixel = 0.04f;   // Meters per pixel of the gable outlines.
    constexpr float g_gableTrim = 0.16f;    // The white edge around a gable.

    // A gable's outline in meters (x from -width / 2 to width / 2, y up from 0 to height), going around once.
    std::vector<XMFLOAT2> GableOutline(Gable gable, float width, float height)
    {
        const float a = width * 0.5f;
        std::vector<XMFLOAT2> right; // The right half, from the bottom to the top middle.
        if (gable == Gable::Stepped)
        {
            const int steps = 4;
            right.emplace_back(a, 0.0f);
            for (int i = 0; i < steps; ++i)
            {
                const float x = a - (a * 0.78f) * i / steps;
                const float y = height * (i + 1) / (steps + 0.4f);
                right.emplace_back(x, y);
                right.emplace_back(a - (a * 0.78f) * (i + 1) / steps, y);
            }
            right.emplace_back(a * 0.22f, height);
            right.emplace_back(0.0f, height);
        }
        else
        {
            // Out, then curving in to a narrow top part with a rounded cap.
            right.emplace_back(a, 0.0f);
            right.emplace_back(a, height * 0.12f);
            const XMFLOAT2 p0(a, height * 0.12f), p1(a * 1.02f, height * 0.5f), p2(a * 0.38f, height * 0.36f), p3(a * 0.36f, height * 0.72f);
            for (int i = 1; i <= 12; ++i)
            {
                const float t = i / 12.0f;
                const float u = 1.0f - t;
                right.emplace_back(u * u * u * p0.x + 3 * u * u * t * p1.x + 3 * u * t * t * p2.x + t * t * t * p3.x,
                    u * u * u * p0.y + 3 * u * u * t * p1.y + 3 * u * t * t * p2.y + t * t * t * p3.y);
            }
            right.emplace_back(a * 0.36f, height * 0.8f);
            for (int i = 1; i <= 8; ++i)
            {
                const float angle = XM_PIDIV2 * i / 8.0f;
                right.emplace_back(a * 0.36f * std::cos(angle), height * 0.8f + height * 0.2f * std::sin(angle));
            }
        }

        std::vector<XMFLOAT2> outline = right;
        for (auto it = right.rbegin(); it != right.rend(); ++it)
        {
            if (it->x > 0.0f)
            {
                outline.emplace_back(-it->x, it->y);
            }
        }
        return outline;
    }

    // A gable as a white wall with a colored inner part, standing on the cornice. Added to the trim and the wall.
    void AddGable(Gable gable, float width, float height, float y, MeshData& trim, MeshData& wall)
    {
        const uint32_t margin = 4;
        const uint32_t pixelsWide = static_cast<uint32_t>(width / g_gablePixel) + margin * 2;
        const uint32_t pixelsHigh = static_cast<uint32_t>(height / g_gablePixel) + margin * 2;
        std::vector<XMFLOAT2> outline = GableOutline(gable, width, height);
        for (XMFLOAT2& point : outline)
        {
            // Meters (y up, x right seen from the front) to pixels (y down).
            point = XMFLOAT2(margin + (point.x + width * 0.5f) / g_gablePixel, margin + (height - point.y) / g_gablePixel);
        }
        const DistanceField field = BuildDistanceField(BuildPolygonMask(pixelsWide, pixelsHigh, { outline }), g_gablePixel, 2);
        // Grid point (0, 0) is at the top left of the mask, seen from the front.
        const XMMATRIX place = XMMatrixTranslation(width * 0.5f + margin * g_gablePixel, y + height + margin * g_gablePixel, 0.0f);
        trim.Append(ExtrudeShape(field, 0.0f, 0.06f, -0.5f), place);
        wall.Append(ExtrudeShape(field, g_gableTrim, 0.09f, 0.0f), place);
    }

    struct HouseMeshes
    {
        MeshData trim, glass, roof, door, shutter;
    };

    // A window with a white frame (and shutters) on the front wall, its center at (x, y).
    void AddWindow(HouseMeshes& meshes, float x, float y, float width, float height, bool shutters)
    {
        meshes.trim.AddBox(XMFLOAT3(x, y, 0.05f), XMFLOAT3(width + 0.24f, height + 0.24f, 0.1f));
        meshes.glass.AddBox(XMFLOAT3(x, y, 0.06f), XMFLOAT3(width, height, 0.1f));
        meshes.trim.AddBox(XMFLOAT3(x, y - height * 0.5f - 0.16f, 0.1f), XMFLOAT3(width + 0.4f, 0.1f, 0.2f)); // The sill.
        if (shutters)
        {
            for (float side : { -1.0f, 1.0f })
            {
                meshes.shutter.AddBox(XMFLOAT3(x + side * (width * 0.5f + 0.32f), y, 0.06f), XMFLOAT3(width * 0.48f, height + 0.1f, 0.08f));
            }
        }
    }

    // Two sloped roof faces from the eaves (y = 0, x = +-halfWidth) up to a ridge (y = rise, x = 0), along z, closed at
    // the back by a triangle in the wall color.
    void AddGableRoof(MeshData& roof, MeshData& wall, float halfWidth, float rise, float y, float front, float back)
    {
        for (float side : { -1.0f, 1.0f })
        {
            const XMVECTOR eave = XMVectorSet(side * (halfWidth + 0.25f), y - 0.15f, front, 1.0f);
            const XMVECTOR ridge = XMVectorSet(0.0f, y + rise, front, 1.0f);
            const XMVECTOR slope = ridge - eave;
            const XMVECTOR along = XMVectorSet(0.0f, 0.0f, back - front, 0.0f);
            roof.AddQuad(eave, slope, along, XMVector3Cross(along, slope) * side, 1.0f, 1.0f);
        }
        wall.AddPolygon({ XMFLOAT3(-halfWidth, y, back), XMFLOAT3(halfWidth, y, back), XMFLOAT3(0.0f, y + rise, back) },
            XMVectorSet(0.0f, 0.0f, -1.0f, 0.0f));
    }

    // A hipped roof over the whole house: slopes on all four sides up to a ridge along x.
    void AddHippedRoof(MeshData& roof, float width, float rise, float y)
    {
        const float a = width * 0.5f + 0.3f;
        const float front = 0.35f;
        const float back = -g_houseDepth - 0.3f;
        const float middle = (front + back) * 0.5f;
        const float inset = (front - back) * 0.5f;
        const XMFLOAT3 frontLeft(a, y, front), frontRight(-a, y, front), backLeft(a, y, back), backRight(-a, y, back);
        const float ridge = std::max(a - inset, 0.6f); // Half the ridge's length.
        const XMFLOAT3 ridgeLeft(ridge, y + rise, middle), ridgeRight(-ridge, y + rise, middle);
        roof.AddPolygon({ frontLeft, frontRight, ridgeRight, ridgeLeft }, XMVectorSet(0.0f, inset, rise, 0.0f));
        roof.AddPolygon({ backLeft, backRight, ridgeRight, ridgeLeft }, XMVectorSet(0.0f, inset, -rise, 0.0f));
        roof.AddPolygon({ frontLeft, backLeft, ridgeLeft }, XMVectorSet(rise, inset, 0.0f, 0.0f));
        roof.AddPolygon({ frontRight, backRight, ridgeRight }, XMVectorSet(-rise, inset, 0.0f, 0.0f));
    }

    // One house: its wall (in its own color) and its parts in the shared meshes, moved to its place.
    void BuildHouse(const House& house, FXMMATRIX place, HouseMeshes& shared, MeshData& wall)
    {
        HouseMeshes meshes;
        MeshData walls;
        const float w = house.width;
        const float height = house.floors * g_floorHeight + 0.5f;

        walls.AddBox(XMFLOAT3(0.0f, (height - g_buried) * 0.5f, -g_houseDepth * 0.5f), XMFLOAT3(w, height + g_buried, g_houseDepth));

        // White corners, a band between the floors and a cornice at the top.
        for (float side : { -1.0f, 1.0f })
        {
            meshes.trim.AddBox(XMFLOAT3(side * (w * 0.5f - 0.2f), height * 0.5f, 0.07f), XMFLOAT3(0.4f, height, 0.14f));
        }
        for (int floor = 1; floor < house.floors; ++floor)
        {
            meshes.trim.AddBox(XMFLOAT3(0.0f, floor * g_floorHeight + 0.5f, 0.08f), XMFLOAT3(w, 0.2f, 0.16f));
        }
        meshes.trim.AddBox(XMFLOAT3(0.0f, height - 0.15f, 0.12f), XMFLOAT3(w + 0.3f, 0.45f, 0.24f));
        meshes.trim.AddBox(XMFLOAT3(0.0f, 0.2f, 0.05f), XMFLOAT3(w, 0.4f, 0.1f)); // A plinth.

        // Windows on every floor; a door in the middle of the ground floor.
        const int columns = std::max(2, static_cast<int>(w / 1.75f));
        const float spacing = (w - 0.8f) / columns;
        for (int floor = 0; floor < house.floors; ++floor)
        {
            const float y = floor * g_floorHeight + 0.5f + (floor == 0 ? 1.45f : 1.6f);
            for (int column = 0; column < columns; ++column)
            {
                const float x = -w * 0.5f + 0.4f + spacing * (column + 0.5f);
                const bool middle = floor == 0 && column == columns / 2;
                if (middle)
                {
                    meshes.trim.AddBox(XMFLOAT3(x, 1.75f, 0.05f), XMFLOAT3(1.5f, 2.7f, 0.1f));
                    meshes.door.AddBox(XMFLOAT3(x, 1.65f, 0.07f), XMFLOAT3(1.15f, 2.5f, 0.1f));
                    continue;
                }
                AddWindow(meshes, x, y, std::min(0.85f, spacing * 0.48f), 1.45f, house.shutters);
            }
        }

        // The top: a gable in front of a roof running back, or a hipped roof.
        if (house.gable == Gable::Hipped)
        {
            AddHippedRoof(meshes.roof, w, 2.6f, height);
            const float gableWidth = std::min(w * 0.42f, 3.6f);
            AddGable(Gable::Curved, gableWidth, gableWidth * 0.9f, height, meshes.trim, walls);
            AddWindow(meshes, 0.0f, height + gableWidth * 0.32f, 0.5f, 0.75f, false);
        }
        else
        {
            const float gableHeight = w * (house.gable == Gable::Stepped ? 0.75f : 0.7f);
            AddGableRoof(meshes.roof, walls, w * 0.5f, gableHeight * 0.85f, height, -0.4f, -g_houseDepth);
            AddGable(house.gable, w, gableHeight, height, meshes.trim, walls);
            AddWindow(meshes, 0.0f, height + gableHeight * 0.38f, 0.65f, 1.0f, false);
        }

        wall.Append(walls, place);
        shared.trim.Append(meshes.trim, place);
        shared.glass.Append(meshes.glass, place);
        shared.roof.Append(meshes.roof, place);
        shared.door.Append(meshes.door, place);
        shared.shutter.Append(meshes.shutter, place);
    }
}

void BirthdayGame::BuildCuracao(CommandList& commandList)
{
    // The brick plaza the letters stand on, a step up from the terrace.
    auto bricks = LoadSurface(commandList, L"assets/birthday/bricks/brick_floor_003_diffuse_2k.jpg",
        L"assets/birthday/bricks/brick_floor_003_nor_dx_2k.jpg", L"assets/birthday/bricks/brick_floor_003_arm_2k.jpg");
    const float plazaTop = townTop + 0.15f;
    const float plazaFront = lettersZ + 4.5f;
    const float plazaBack = lettersZ - 5.5f;
    auto sign = CreateNode("Curacao Sign", nullptr);
    auto plaza = CreateNode("Plaza", sign, XMMatrixTranslation(0.0f, plazaTop - 1.0f, (plazaFront + plazaBack) * 0.5f));
    plaza->AddMesh(CreateMesh(commandList, BuildBox(XMFLOAT3(38.0f, 2.0f, plazaFront - plazaBack), 0.4f), bricks));

    // The letters: each one's outline as a distance field, so the yellow body, the white line and the blue front are
    // the same shape, smaller and smaller.
    const wchar_t* word = L"CURACAO";
    const size_t letterCount = wcslen(word);
    std::vector<ShapeMask> masks;
    float wordWidth = -g_letterGap;
    for (size_t i = 0; i < letterCount; ++i)
    {
        masks.push_back(RenderLetterMask(word[i], g_letterPixels, g_letterMargin));
        const float pixelSize = g_letterHeight / (masks.back().height - g_letterMargin * 2);
        wordWidth += (masks.back().width - g_letterMargin * 2) * pixelSize + g_letterGap;
    }

    MeshData yellow, white, blue;
    float left = wordWidth * 0.5f; // Seen from the front, +x is on the left.
    for (size_t i = 0; i < letterCount; ++i)
    {
        const ShapeMask& mask = masks[i];
        const float pixelSize = g_letterHeight / (mask.height - g_letterMargin * 2);
        const float inkWidth = (mask.width - g_letterMargin * 2) * pixelSize;
        const DistanceField field = BuildDistanceField(mask, pixelSize, g_fieldStep);
        const float margin = g_letterMargin * pixelSize;
        const XMMATRIX place = XMMatrixTranslation(left + margin, plazaTop + g_letterHeight + margin, lettersZ);

        yellow.Append(ExtrudeShape(field, 0.0f, 0.0f, -g_letterDepth), place);
        white.Append(ExtrudeShape(field, g_yellowBand, 0.025f, 0.0f), place);
        blue.Append(ExtrudeShape(field, g_yellowBand + g_whiteLine, 0.05f, 0.0f), place);

        // Two white stars on the C's upper arm, like on the flag.
        if (i == 0)
        {
            const float w = static_cast<float>(mask.width - g_letterMargin * 2);
            const float h = static_cast<float>(mask.height - g_letterMargin * 2);
            const float m = static_cast<float>(g_letterMargin);
            const ShapeMask stars = BuildPolygonMask(mask.width, mask.height,
                { Star(m + w * 0.5f, m + h * 0.105f, h * 0.06f), Star(m + w * 0.69f, m + h * 0.15f, h * 0.068f) });
            white.Append(ExtrudeShape(BuildDistanceField(stars, pixelSize, 2), 0.0f, 0.075f, 0.0f), place);
        }
        left -= inkWidth + g_letterGap;
    }
    auto letters = CreateNode("Letters", sign);
    letters->AddMesh(CreateMesh(commandList, yellow, CreatePaint(g_signYellow, 0.45f)));
    letters->AddMesh(CreateMesh(commandList, white, CreatePaint(g_white, 0.45f)));
    letters->AddMesh(CreateMesh(commandList, blue, CreatePaint(g_signBlue, 0.4f)));

    // Palm trees at both ends of the letters.
    std::shared_ptr<Scene> palm = LoadModel(commandList, L"assets/birthday/palms/palm-detailed-straight.glb");
    for (float side : { -1.0f, 1.0f })
    {
        PlaceModel(palm, sign, "Palm", side * (wordWidth * 0.5f + 2.5f), lettersZ - 1.5f, side * 70.0f, 7.5f, Fit::Height, plazaTop);
    }
    m_litScenes.push_back(AddEditorScene("Curacao Sign", sign));

    // The Handelskade: a row of houses side by side, each in its own color.
    const House houses[] = {
        { 6.5f, 3, 0x7EC8E3, Gable::Curved, true },   // Light blue.
        { 7.0f, 3, 0xF3A43B, Gable::Stepped, false }, // Orange.
        { 5.8f, 4, 0xE98AB8, Gable::Curved, false },  // Pink.
        { 9.5f, 3, 0x9ED9B8, Gable::Hipped, true },   // Mint green.
        { 6.2f, 4, 0xF2C230, Gable::Curved, false },  // Yellow.
        { 6.8f, 3, 0x3767C2, Gable::Stepped, false }, // Blue.
        { 6.0f, 3, 0xE0574A, Gable::Curved, true },   // Red.
        { 10.5f, 4, 0xF5CF45, Gable::Hipped, false }, // The big yellow one.
        { 6.4f, 3, 0x45B9AE, Gable::Curved, false },  // Turquoise.
        { 6.0f, 4, 0xF08F5A, Gable::Stepped, true },  // Salmon.
        { 7.2f, 3, 0xB7A2DB, Gable::Curved, false },  // Lilac.
        { 6.6f, 3, 0xA7CF5E, Gable::Stepped, false }, // Lime.
        { 8.8f, 3, 0xEE7FA8, Gable::Hipped, true },   // Rose.
        { 6.2f, 4, 0x6FB7EE, Gable::Curved, false },  // Sky blue.
        { 6.8f, 3, 0xF7D774, Gable::Curved, true },   // Pale yellow.
    };
    float streetWidth = 0.0f;
    for (const House& house : houses)
    {
        streetWidth += house.width;
    }

    HouseMeshes shared;
    auto town = CreateNode("Willemstad", nullptr);
    float x = streetWidth * 0.5f;
    for (size_t i = 0; i < _countof(houses); ++i)
    {
        const House& house = houses[i];
        // Not quite in a straight line.
        const float z = housesZ + 0.35f * std::sin(i * 2.3f);
        MeshData wall;
        BuildHouse(house, XMMatrixTranslation(x - house.width * 0.5f, townTop, z), shared, wall);
        town->AddMesh(CreateMesh(commandList, wall, CreatePaint(house.color, 0.8f)));
        x -= house.width;
    }
    town->AddMesh(CreateMesh(commandList, shared.trim, CreatePaint(g_white, 0.7f)));
    town->AddMesh(CreateMesh(commandList, shared.glass, CreatePaint(0x22343C, 0.25f)));
    town->AddMesh(CreateMesh(commandList, shared.roof, CreatePaint(0xC9542C, 0.75f)));
    town->AddMesh(CreateMesh(commandList, shared.door, CreatePaint(0x5A3A26, 0.6f)));
    town->AddMesh(CreateMesh(commandList, shared.shutter, CreatePaint(0x2E7D4F, 0.6f)));
    m_litScenes.push_back(AddEditorScene("Willemstad", town));
}

// ------------------------------------------------------------------------------------------------
// The Queen Emma bridge ("Pontjesbrug"): a deck on a row of floating pontoons, with white railings, lamp posts
// and arches of colored lights over the deck. Built along +z from the beach, x across the deck.

namespace
{
    constexpr float g_deckWidth = 6.4f;
    constexpr float g_sectionLength = 10.0f; // One pontoon and one arch per section.
    constexpr float g_archHeight = 4.6f;     // Above the deck.
    constexpr float g_archRadius = 0.09f;

    // A tube along a half ellipse from (-halfWidth, 0) over (0, height) to (halfWidth, 0), in the x-y plane.
    MeshData BuildArch(float halfWidth, float height, float radius)
    {
        MeshData data;
        const int segments = 32;
        const int sides = 8;
        const XMVECTOR across = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
        for (int s = 0; s <= segments; ++s)
        {
            const float t = XM_PI * s / segments;
            const XMVECTOR center = XMVectorSet(-halfWidth * std::cos(t), height * std::sin(t), 0.0f, 1.0f);
            const XMVECTOR along = XMVector3Normalize(XMVectorSet(halfWidth * std::sin(t), height * std::cos(t), 0.0f, 0.0f));
            const XMVECTOR side = XMVector3Cross(across, along);
            for (int k = 0; k <= sides; ++k)
            {
                const float angle = XM_2PI * k / sides;
                const XMVECTOR normal = side * std::cos(angle) + across * std::sin(angle);
                data.vertices.emplace_back(center + normal * radius, normal, XMVectorSet(static_cast<float>(k) / sides, t, 0.0f, 0.0f),
                    along, XMVector3Cross(normal, along));
            }
        }
        for (int s = 0; s < segments; ++s)
        {
            for (int k = 0; k < sides; ++k)
            {
                const uint32_t a = s * (sides + 1) + k;
                const uint32_t b = a + sides + 1;
                data.AddTriangle(a, a + 1, b);
                data.AddTriangle(a + 1, b + 1, b);
            }
        }
        return data;
    }

    // A fully saturated color around the color wheel (0 red, 1/3 green, 2/3 blue).
    XMFLOAT3 HueToColor(float hue)
    {
        auto channel = [hue](float offset)
        {
            const float h = std::fmod(hue + offset, 1.0f) * 6.0f;
            return std::clamp(std::abs(h - 3.0f) - 1.0f, 0.0f, 1.0f);
        };
        return XMFLOAT3(channel(0.0f), channel(2.0f / 3.0f), channel(1.0f / 3.0f));
    }
}

void BirthdayGame::BuildBridge(CommandList& commandList)
{
    const int sections = static_cast<int>(bridgeLength / g_sectionLength);
    const float halfDeck = g_deckWidth * 0.5f;
    auto bridge = CreateNode("Queen Emma Bridge", nullptr, XMMatrixTranslation(bridgeX, 0.0f, bridgeStartZ));

    // Where it starts: a stone landing on the beach. Then the deck on the pontoons: dark hulls, a little wider than
    // the deck, with a light edge along the top.
    MeshData landing, pontoons, rims, railings;
    landing.AddBox(XMFLOAT3(0.0f, bridgeDeckTop - 1.5f, -3.0f), XMFLOAT3(g_deckWidth + 1.2f, 3.0f, 6.0f));
    for (int i = 0; i < sections; ++i)
    {
        const float z = (i + 0.5f) * g_sectionLength;
        pontoons.AddBox(XMFLOAT3(0.0f, -0.2f, z), XMFLOAT3(g_deckWidth + 1.4f, 2.0f, g_sectionLength - 0.7f));
        for (float side : { -1.0f, 1.0f })
        {
            rims.AddBox(XMFLOAT3(side * (halfDeck + 0.72f), 0.85f, z), XMFLOAT3(0.1f, 0.12f, g_sectionLength - 0.7f));
        }
    }

    // White railings (posts and two rails) on both sides, and lamp posts on both sides in turn.
    for (float side : { -1.0f, 1.0f })
    {
        const float x = side * (halfDeck - 0.1f);
        railings.AddBox(XMFLOAT3(x, bridgeDeckTop + 1.05f, bridgeLength * 0.5f), XMFLOAT3(0.08f, 0.08f, bridgeLength));
        railings.AddBox(XMFLOAT3(x, bridgeDeckTop + 0.55f, bridgeLength * 0.5f), XMFLOAT3(0.05f, 0.05f, bridgeLength));
        for (float z = 0.5f; z < bridgeLength; z += 2.0f)
        {
            railings.AddBox(XMFLOAT3(x, bridgeDeckTop + 0.55f, z), XMFLOAT3(0.07f, 1.1f, 0.07f));
        }
        for (int i = (side > 0.0f ? 0 : 1); i < sections; i += 2)
        {
            const float z = i * g_sectionLength + 0.4f;
            railings.AddBox(XMFLOAT3(side * (halfDeck + 0.25f), bridgeDeckTop + 3.4f, z), XMFLOAT3(0.14f, 6.8f, 0.14f));
            railings.AddBox(XMFLOAT3(side * (halfDeck - 0.15f), bridgeDeckTop + 6.75f, z), XMFLOAT3(0.9f, 0.12f, 0.2f));
        }
    }

    // The bridge keeper's little yellow house with windows all around, beside the deck on its own pontoon.
    MeshData house, houseTrim, houseGlass;
    const float houseX = -(halfDeck + 2.0f);
    const float houseZ = 9.0f;
    pontoons.AddBox(XMFLOAT3(houseX, -0.2f, houseZ), XMFLOAT3(4.2f, 2.0f, 5.0f));
    railings.AddBox(XMFLOAT3(houseX, bridgeDeckTop - 0.35f, houseZ), XMFLOAT3(4.0f, 0.3f, 4.8f)); // Its floor.
    house.AddBox(XMFLOAT3(houseX, bridgeDeckTop + 1.4f, houseZ), XMFLOAT3(3.2f, 2.8f, 3.6f));
    houseTrim.AddBox(XMFLOAT3(houseX, bridgeDeckTop + 2.92f, houseZ), XMFLOAT3(3.8f, 0.25f, 4.2f)); // The roof.
    houseTrim.AddBox(XMFLOAT3(houseX, bridgeDeckTop + 1.3f, houseZ), XMFLOAT3(3.3f, 0.1f, 3.7f));   // Below the windows.
    for (float side : { -1.0f, 1.0f })
    {
        for (float offset : { -1.0f, 0.0f, 1.0f })
        {
            houseGlass.AddBox(XMFLOAT3(houseX + side * 1.6f, bridgeDeckTop + 1.9f, houseZ + offset * 1.05f), XMFLOAT3(0.06f, 1.0f, 0.8f));
        }
        for (float offset : { -0.75f, 0.75f })
        {
            houseGlass.AddBox(XMFLOAT3(houseX + offset, bridgeDeckTop + 1.9f, houseZ + side * 1.8f), XMFLOAT3(0.9f, 1.0f, 0.06f));
        }
    }

    auto planks = LoadSurface(commandList, L"assets/birthday/boardwalk/wood_planks_diff_2k.jpg",
        L"assets/birthday/boardwalk/wood_planks_nor_dx_2k.jpg", L"assets/birthday/boardwalk/wood_planks_arm_2k.jpg");
    auto deck = CreateNode("Deck", bridge, XMMatrixTranslation(0.0f, bridgeDeckTop - 0.2f, bridgeLength * 0.5f));
    deck->AddMesh(CreateMesh(commandList, BuildBox(XMFLOAT3(g_deckWidth, 0.4f, bridgeLength), 0.35f), planks));
    bridge->AddMesh(CreateMesh(commandList, landing, CreatePaint(0xB9B2A3, 0.9f)));
    bridge->AddMesh(CreateMesh(commandList, pontoons, CreatePaint(0x2A3036, 0.6f)));
    bridge->AddMesh(CreateMesh(commandList, rims, CreatePaint(0xD9CDAA, 0.7f)));
    bridge->AddMesh(CreateMesh(commandList, railings, CreatePaint(g_white, 0.5f)));
    bridge->AddMesh(CreateMesh(commandList, house, CreatePaint(0xF1CE5A, 0.8f)));
    bridge->AddMesh(CreateMesh(commandList, houseTrim, CreatePaint(g_white, 0.6f)));
    bridge->AddMesh(CreateMesh(commandList, houseGlass, CreatePaint(0x2A3A40, 0.2f)));

    // An arch and a light strip along the pontoon at the water per section, each section in its own color.
    auto arch = CreateMesh(commandList, BuildArch(halfDeck - 0.1f, g_archHeight, g_archRadius), nullptr);
    MeshData strip;
    for (float side : { -1.0f, 1.0f })
    {
        strip.AddBox(XMFLOAT3(side * (halfDeck + 0.72f), 0.45f, 0.0f), XMFLOAT3(0.06f, 0.12f, g_sectionLength - 1.2f));
    }
    auto stripMesh = CreateMesh(commandList, strip, nullptr);
    m_bridgeLights.clear();
    for (int i = 0; i < sections; ++i)
    {
        auto light = CreatePaint(0xFFFFFF, 0.3f);
        m_bridgeLights.push_back(light);
        auto section = CreateNode("Arch", bridge, XMMatrixTranslation(0.0f, bridgeDeckTop, (i + 0.5f) * g_sectionLength));
        section->AddMesh(CloneMesh(*arch, light));
        auto lights = CreateNode("Lights", bridge, XMMatrixTranslation(0.0f, 0.0f, (i + 0.5f) * g_sectionLength));
        lights->AddMesh(CloneMesh(*stripMesh, light));
    }
    UpdateBridgeLights();
    m_litScenes.push_back(AddEditorScene("Queen Emma Bridge", bridge));
}

void BirthdayGame::UpdateBridgeLights()
{
    // Pink, green, purple, blue... along the bridge, slowly moving on.
    for (size_t i = 0; i < m_bridgeLights.size(); ++i)
    {
        const XMFLOAT3 color = HueToColor(std::fmod(0.83f + i * 0.29f + m_time * 0.04f, 1.0f));
        const float glow = 1.6f;
        m_bridgeLights[i]->SetDiffuseColor(XMFLOAT4(0.2f + color.x * 0.6f, 0.2f + color.y * 0.6f, 0.2f + color.z * 0.6f, 1.0f));
        m_bridgeLights[i]->SetEmissiveColor(XMFLOAT4(color.x * glow, color.y * glow, color.z * glow, 1.0f));
    }
}
