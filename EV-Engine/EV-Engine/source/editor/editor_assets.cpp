#include "DX12/dx12_includes.h"

#include "editor/editor.h"

#include <algorithm>
#include <cmath>
#include <mutex>

#include "imgui_internal.h"

#include "core/application.h"
#include "core/camera.h"
#include "DX12/command_list.h"
#include "DX12/command_queue.h"
#include "DX12/resource_state_tracker.h"
#include "DX12/scene.h"
#include "DX12/scene_node.h"
#include "resources/material.h"
#include "resources/mesh.h"
#include "resources/texture.h"
#include "UI/GUI.h"

#include "editor_internal.h"

using namespace EV;
using namespace EV::EditorInternal;
using namespace DirectX;
namespace fs = std::filesystem;

namespace
{

    // Images larger than this are not loaded just for a thumbnail.
    constexpr uintmax_t g_maxThumbnailFileSize = 8ull * 1024ull * 1024ull;

    enum class AssetType
    {
        Unknown,
        Model,
        Image,
        Environment,
    };

    std::string GetExtension(const fs::path& path)
    {
        std::string extension = path.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return extension;
    }

    AssetType GetAssetType(const fs::path& path)
    {
        const std::string extension = GetExtension(path);
        if (extension == ".gltf" || extension == ".glb" || extension == ".obj" || extension == ".fbx" || extension == ".dae" ||
            extension == ".3ds" || extension == ".ply" || extension == ".stl" || extension == ".blend")
        {
            return AssetType::Model;
        }
        if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".tga" || extension == ".bmp" ||
            extension == ".dds" || extension == ".tif" || extension == ".tiff")
        {
            return AssetType::Image;
        }
        if (extension == ".hdr" || extension == ".exr")
        {
            return AssetType::Environment;
        }
        return AssetType::Unknown;
    }

    // Background loads run one at a time and only return once the GPU has finished uploading. Loads share
    // textures through the texture cache, so this way a texture handed out by the cache is always completely
    // uploaded; with concurrent loads, a model could be drawn while another load was still uploading (and
    // transitioning) one of its textures.
    std::mutex g_loadMutex;

    // Execute a loading command list and wait until the upload, including the mipmaps that ExecuteCommandList
    // queues on the compute queue afterwards, has finished.
    void ExecuteAndWait(CommandQueue& commandQueue, const std::shared_ptr<CommandList>& commandList)
    {
        commandQueue.WaitForFenceValue(commandQueue.ExecuteCommandList(commandList));

        // The lock keeps fence values in order with the signals that ExecuteCommandList issues from other threads.
        auto& computeQueue = Application::Get().GetCommandQueue(D3D12_COMMAND_LIST_TYPE_COMPUTE);
        ResourceStateTracker::Lock();
        const uint64_t mipsFence = computeQueue.Signal();
        ResourceStateTracker::Unlock();
        computeQueue.WaitForFenceValue(mipsFence);
    }

    // Loading runs on a worker thread using the copy queue, like the applications' own scene loading.
    std::shared_ptr<Scene> LoadModel(const fs::path& path)
    {
        try
        {
            std::lock_guard<std::mutex> lock(g_loadMutex);
            auto& commandQueue = Application::Get().GetCommandQueue(D3D12_COMMAND_LIST_TYPE_COPY);
            auto commandList = commandQueue.GetCommandList();
            auto scene = commandList->LoadSceneFromFile(path.wstring());
            ExecuteAndWait(commandQueue, commandList);
            return scene;
        }
        catch (const std::exception& e)
        {
            OutputDebugStringA((std::string("Failed to load model: ") + e.what() + "\n").c_str());
            return nullptr;
        }
    }

    std::shared_ptr<Texture> LoadImageTexture(const fs::path& path, bool sRGB)
    {
        try
        {
            std::lock_guard<std::mutex> lock(g_loadMutex);
            auto& commandQueue = Application::Get().GetCommandQueue(D3D12_COMMAND_LIST_TYPE_COPY);
            auto commandList = commandQueue.GetCommandList();
            auto texture = commandList->LoadTextureFromFile(path.wstring(), sRGB);
            ExecuteAndWait(commandQueue, commandList);
            return texture;
        }
        catch (const std::exception& e)
        {
            OutputDebugStringA((std::string("Failed to load texture: ") + e.what() + "\n").c_str());
            return nullptr;
        }
    }

    template<typename T>
    bool IsReady(const std::future<T>& future)
    {
        return future.valid() && future.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
    }

    // World space bounds of all meshes below a node.
    bool ComputeWorldBounds(const std::shared_ptr<SceneNode>& root, BoundingBox& bounds)
    {
        bool any = false;
        ForEachNode(root, [&](const std::shared_ptr<SceneNode>& node)
        {
            const XMMATRIX world = node->GetWorldTransform();
            for (const auto& mesh : node->GetMeshes())
            {
                if (!mesh)
                {
                    continue;
                }
                BoundingBox meshBounds;
                mesh->GetAABB().Transform(meshBounds, world);
                if (any)
                {
                    BoundingBox::CreateMerged(bounds, bounds, meshBounds);
                }
                else
                {
                    bounds = meshBounds;
                    any = true;
                }
            }
        });
        return any;
    }
}

// ------------------------------------------------------------------------------------------------
// Public

std::vector<std::shared_ptr<Scene>> Editor::GetSpawnedScenes() const
{
    std::vector<std::shared_ptr<Scene>> scenes;
    for (const auto& entry : m_scenes)
    {
        if (entry.spawned)
        {
            scenes.push_back(entry.scene);
        }
    }
    return scenes;
}

void Editor::SetAssetRoot(const fs::path& root)
{
    m_assetRoot = root;
    m_assetDirectory = root;
    m_assetThumbnails.clear();
}

// ------------------------------------------------------------------------------------------------
// Assets panel

void Editor::DrawAssetIcon(const fs::path& path, bool isDirectory, float size)
{
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 position = ImGui::GetCursorScreenPos();
    const ImVec2 max(position.x + size, position.y + size);

    if (isDirectory)
    {
        // Simple folder shape.
        const ImU32 color = IM_COL32(214, 170, 72, 255);
        drawList->AddRectFilled(ImVec2(position.x + size * 0.1f, position.y + size * 0.22f),
            ImVec2(position.x + size * 0.45f, position.y + size * 0.32f), color, 3.0f);
        drawList->AddRectFilled(ImVec2(position.x + size * 0.1f, position.y + size * 0.28f),
            ImVec2(position.x + size * 0.9f, position.y + size * 0.82f), color, 4.0f);
        ImGui::Dummy(ImVec2(size, size));
        return;
    }

    const AssetType type = GetAssetType(path);

    // Images get a real thumbnail, loaded in the background.
    if (type == AssetType::Image)
    {
        auto& thumbnail = m_assetThumbnails[path.wstring()];
        if (!thumbnail.texture && !thumbnail.failed && !thumbnail.loading.valid())
        {
            std::error_code error;
            const auto fileSize = fs::file_size(path, error);
            if (!error && fileSize <= g_maxThumbnailFileSize)
            {
                thumbnail.loading = std::async(std::launch::async, LoadImageTexture, path, true);
            }
            else
            {
                thumbnail.failed = true;
            }
        }
        if (IsReady(thumbnail.loading))
        {
            thumbnail.texture = thumbnail.loading.get();
            thumbnail.failed = thumbnail.texture == nullptr;
        }
        if (thumbnail.texture)
        {
            TextureThumbnail(thumbnail.texture, size);
            return;
        }
    }

    // Colored tile with the file extension.
    ImU32 color = IM_COL32(90, 90, 90, 255);
    switch (type)
    {
    case AssetType::Model: color = IM_COL32(64, 128, 196, 255); break;
    case AssetType::Image: color = IM_COL32(96, 160, 96, 255); break;
    case AssetType::Environment: color = IM_COL32(170, 110, 200, 255); break;
    default: break;
    }
    drawList->AddRectFilled(ImVec2(position.x + size * 0.12f, position.y + size * 0.08f),
        ImVec2(position.x + size * 0.88f, position.y + size * 0.92f), color, 4.0f);

    std::string extension = GetExtension(path);
    if (!extension.empty())
    {
        extension = extension.substr(1);
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    }
    const ImVec2 textSize = ImGui::CalcTextSize(extension.c_str());
    drawList->AddText(ImVec2(position.x + (size - textSize.x) * 0.5f, position.y + (size - textSize.y) * 0.5f),
        IM_COL32(255, 255, 255, 230), extension.c_str());
    ImGui::Dummy(ImVec2(size, size));
}

void Editor::DrawAssetBrowser()
{
    if (!m_showAssetBrowser)
    {
        return;
    }
    if (!ImGui::Begin("Assets", &m_showAssetBrowser))
    {
        ImGui::End();
        return;
    }

    std::error_code error;
    if (!fs::is_directory(m_assetDirectory, error))
    {
        m_assetDirectory = m_assetRoot;
    }
    if (!fs::is_directory(m_assetRoot, error))
    {
        ImGui::TextColored(g_colorTextDim, "Asset folder '%s' not found (relative to %s).", m_assetRoot.string().c_str(),
            fs::current_path(error).string().c_str());
        ImGui::End();
        return;
    }

    // Breadcrumb: assets > folder > subfolder
    {
        const fs::path relative = fs::relative(m_assetDirectory, m_assetRoot, error);
        fs::path current = m_assetRoot;
        if (ImGui::Button(m_assetRoot.filename().string().c_str()))
        {
            m_assetDirectory = m_assetRoot;
        }
        if (!error && relative != ".")
        {
            for (const auto& part : relative)
            {
                current /= part;
                ImGui::SameLine(0.0f, 2.0f);
                ImGui::TextColored(g_colorTextDim, ">");
                ImGui::SameLine(0.0f, 2.0f);
                ImGui::PushID(current.string().c_str());
                if (ImGui::Button(part.string().c_str()))
                {
                    m_assetDirectory = current;
                }
                ImGui::PopID();
            }
        }
        ImGui::SameLine(0.0f, 20.0f);
        ImGui::SetNextItemWidth(180.0f);
        ImGui::InputTextWithHint("##assetfilter", "Filter...", m_assetFilter, sizeof(m_assetFilter));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100.0f);
        ImGui::SliderFloat("##iconsize", &m_assetIconSize, 40.0f, 160.0f, "Size %.0f");
    }
    ImGui::Separator();

    // Folders first, then files, both sorted by name. Listing a large folder every frame is slow, so the
    // listing is kept until the folder or filter changes, and refreshed every second to pick up new files.
    const double now = ImGui::GetTime();
    if (m_assetListing.directory != m_assetDirectory || m_assetListing.filter != m_assetFilter || now - m_assetListing.time > 1.0)
    {
        m_assetListing.directory = m_assetDirectory;
        m_assetListing.filter = m_assetFilter;
        m_assetListing.time = now;
        m_assetListing.entries.clear();
        for (const auto& entry : fs::directory_iterator(m_assetDirectory, error))
        {
            std::string name = entry.path().filename().string();
            // Preprocessed assimp caches are not useful to show.
            if (GetExtension(entry.path()) == ".assbin" || !ContainsCaseInsensitive(name, m_assetFilter))
            {
                continue;
            }
            m_assetListing.entries.push_back({ entry.path(), std::move(name), entry.is_directory(error) });
        }
        std::sort(m_assetListing.entries.begin(), m_assetListing.entries.end(), [](const AssetEntry& a, const AssetEntry& b)
        {
            if (a.isDirectory != b.isDirectory)
            {
                return a.isDirectory;
            }
            return a.name < b.name;
        });
    }
    const auto& entries = m_assetListing.entries;

    ImGui::BeginChild("##assetgrid");
    const float cellWidth = m_assetIconSize + ImGui::GetStyle().ItemSpacing.x * 2.0f;
    const int columns = std::max(1, static_cast<int>(ImGui::GetContentRegionAvail().x / cellWidth));
    const ImVec2 cellSize(m_assetIconSize, m_assetIconSize + ImGui::GetStyle().ItemSpacing.y + ImGui::GetTextLineHeight());

    fs::path openDirectory;
    int column = 0;
    for (const auto& entry : entries)
    {
        // Cells scrolled out of view only take up their space.
        if (!ImGui::IsRectVisible(cellSize))
        {
            ImGui::Dummy(cellSize);
            if (++column < columns)
            {
                ImGui::SameLine();
            }
            else
            {
                column = 0;
            }
            continue;
        }

        const fs::path& path = entry.path;
        const bool isDirectory = entry.isDirectory;
        const std::string& name = entry.name;

        ImGui::PushID(name.c_str());
        ImGui::BeginGroup();

        const ImVec2 cellStart = ImGui::GetCursorScreenPos();
        DrawAssetIcon(path, isDirectory, m_assetIconSize);

        // Name, cut to the cell width.
        std::string label = name;
        while (label.size() > 3 && ImGui::CalcTextSize(label.c_str()).x > m_assetIconSize)
        {
            label = label.substr(0, label.size() - 4) + "..";
        }
        ImGui::TextUnformatted(label.c_str());
        ImGui::EndGroup();

        // The whole cell is the interactive item.
        const ImVec2 cellEnd = ImGui::GetItemRectMax();
        ImGui::SetCursorScreenPos(cellStart);
        ImGui::InvisibleButton("##cell", ImVec2(cellEnd.x - cellStart.x, cellEnd.y - cellStart.y));
        if (ImGui::IsItemHovered())
        {
            ImGui::GetWindowDrawList()->AddRect(cellStart, cellEnd, ImGui::GetColorU32(g_colorAccent), 3.0f);
            ImGui::SetTooltip("%s%s", path.string().c_str(),
                isDirectory ? "" : (GetAssetType(path) == AssetType::Model ? "\nDrag into the Scene to place it" :
                    GetAssetType(path) == AssetType::Image ? "\nDrag onto an object or a material slot" : ""));
        }
        if (isDirectory && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            openDirectory = path;
        }

        if (!isDirectory && GetAssetType(path) != AssetType::Unknown && ImGui::BeginDragDropSource())
        {
            const std::string utf8 = ToUTF8(path.wstring());
            ImGui::SetDragDropPayload(g_assetPayload, utf8.c_str(), utf8.size() + 1);
            ImGui::Text("%s", name.c_str());
            ImGui::EndDragDropSource();
        }

        ImGui::PopID();

        if (++column < columns)
        {
            ImGui::SameLine();
        }
        else
        {
            column = 0;
        }
    }
    if (entries.empty())
    {
        ImGui::TextColored(g_colorTextDim, "Empty folder.");
    }
    ImGui::EndChild();

    if (!openDirectory.empty())
    {
        m_assetDirectory = openDirectory;
    }

    ImGui::End();
}

// ------------------------------------------------------------------------------------------------
// Viewport drops

bool Editor::GetMouseRay(XMVECTOR& origin, XMVECTOR& direction) const
{
    if (!m_camera || m_viewportWidth == 0 || m_viewportHeight == 0)
    {
        return false;
    }

    const float ndcX = 2.0f * m_viewportMousePosition.x / m_viewportWidth - 1.0f;
    const float ndcY = 1.0f - 2.0f * m_viewportMousePosition.y / m_viewportHeight;
    const XMMATRIX inverseViewProjection = XMMatrixInverse(nullptr, m_camera->GetViewMatrix() * m_camera->GetProjectionMatrix());
    origin = XMVector3TransformCoord(XMVectorSet(ndcX, ndcY, 0.0f, 1.0f), inverseViewProjection);
    const XMVECTOR farPoint = XMVector3TransformCoord(XMVectorSet(ndcX, ndcY, 1.0f, 1.0f), inverseViewProjection);
    direction = XMVector3Normalize(farPoint - origin);
    return true;
}

bool Editor::RayCastScenes(FXMVECTOR origin, FXMVECTOR direction, const Scene* ignore, float& distance) const
{
    bool hit = false;
    distance = FLT_MAX;
    for (const auto& entry : m_scenes)
    {
        if (entry.scene.get() == ignore)
        {
            continue;
        }
        ForEachNode(entry.scene->GetRootNode(), [&](const std::shared_ptr<SceneNode>& node)
        {
            const XMMATRIX world = node->GetWorldTransform();
            for (const auto& mesh : node->GetMeshes())
            {
                if (!mesh)
                {
                    continue;
                }
                BoundingBox worldBox;
                mesh->GetAABB().Transform(worldBox, world);
                float hitDistance = 0.0f;
                if (worldBox.Intersects(origin, direction, hitDistance) && hitDistance < distance)
                {
                    distance = hitDistance;
                    hit = true;
                }
            }
        });
    }
    return hit;
}

std::shared_ptr<SceneNode> Editor::FindNodeUnderMouse() const
{
    XMVECTOR origin, direction;
    if (!GetMouseRay(origin, direction))
    {
        return nullptr;
    }

    std::shared_ptr<SceneNode> closest;
    float closestDistance = FLT_MAX;
    for (const auto& entry : m_scenes)
    {
        ForEachNode(entry.scene->GetRootNode(), [&](const std::shared_ptr<SceneNode>& node)
        {
            const XMMATRIX world = node->GetWorldTransform();
            for (const auto& mesh : node->GetMeshes())
            {
                BoundingBox worldBox;
                mesh->GetAABB().Transform(worldBox, world);
                float distance = 0.0f;
                if (worldBox.Intersects(origin, direction, distance) && distance < closestDistance)
                {
                    closestDistance = distance;
                    closest = node;
                }
            }
        });
    }
    return closest;
}

void Editor::AcceptViewportDrop()
{
    if (!ImGui::BeginDragDropTarget())
    {
        return;
    }

    // Accept before delivery so a model can follow the mouse while it is being dragged.
    const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(g_assetPayload,
        ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
    if (payload)
    {
        const fs::path path = PathFromUTF8(static_cast<const char*>(payload->Data));
        const AssetType type = GetAssetType(path);

        if (type == AssetType::Model)
        {
            if (!m_modelDrag || m_modelDrag->path != path)
            {
                // Start loading as soon as the model enters the viewport (like Kudzu).
                if (m_modelDrag && m_modelDrag->scene)
                {
                    RemoveScene(m_modelDrag->scene);
                }
                m_modelDrag = std::make_unique<ModelDrag>();
                m_modelDrag->path = path;
                m_modelDrag->loading = std::async(std::launch::async, LoadModel, path);
            }

            // Mouse wheel moves the model closer/further when it is not placed on a surface.
            const float wheel = ImGui::GetIO().MouseWheel;
            if (wheel != 0.0f)
            {
                m_dropDistance = std::clamp(m_dropDistance * std::pow(1.15f, wheel), 0.5f, 500.0f);
            }

            PlaceDraggedModel();

            if (payload->IsDelivery())
            {
                m_modelDrag->delivered = true;
            }
            else
            {
                ImGui::SetTooltip("%s\n%s", path.filename().string().c_str(),
                    m_modelDrag->scene ? "Release to place. Mouse wheel: distance" : "Loading...");
            }
        }
        else if (type == AssetType::Image)
        {
            // Dropping an image on an object assigns it as the diffuse texture of its meshes.
            auto node = FindNodeUnderMouse();
            if (payload->IsDelivery() && node)
            {
                for (const auto& mesh : node->GetMeshes())
                {
                    if (auto material = mesh->GetMaterial())
                    {
                        DropTextureOnMaterial(path, material, static_cast<int>(Material::TextureType::Diffuse));
                    }
                }
                m_selectedNode = node;
            }
            else if (!payload->IsDelivery())
            {
                ImGui::SetTooltip(node ? "Set as diffuse texture of '%s'" : "Drop onto an object", node ? node->GetName().c_str() : "");
            }
        }
        else if (!payload->IsDelivery())
        {
            ImGui::SetTooltip("This asset type cannot be dropped into the scene");
        }
    }
    ImGui::EndDragDropTarget();
}

void Editor::PlaceDraggedModel()
{
    if (!m_modelDrag)
    {
        return;
    }

    XMVECTOR origin, direction;
    if (!GetMouseRay(origin, direction))
    {
        return;
    }

    // Closest of: an object under the mouse, the ground plane (y = 0), otherwise a fixed distance.
    float distance = 0.0f;
    bool hit = RayCastScenes(origin, direction, m_modelDrag->scene.get(), distance);

    const float directionY = XMVectorGetY(direction);
    if (std::abs(directionY) > 1e-4f)
    {
        const float groundDistance = -XMVectorGetY(origin) / directionY;
        if (groundDistance > 0.0f && (!hit || groundDistance < distance))
        {
            distance = groundDistance;
            hit = true;
        }
    }
    if (!hit || distance > m_farZ)
    {
        distance = m_dropDistance;
    }

    XMStoreFloat3(&m_modelDrag->position, origin + direction * distance);
    m_modelDrag->hasPosition = true;

    if (m_modelDrag->scene)
    {
        // Put the model's bottom center on the target point.
        const XMVECTOR offset = XMLoadFloat3(&m_modelDrag->position) - XMLoadFloat3(&m_modelDrag->bottomOffset);
        m_modelDrag->scene->GetRootNode()->SetLocalTransform(XMLoadFloat4x4(&m_modelDrag->rootTransform) * XMMatrixTranslationFromVector(offset));
    }
}

void Editor::UpdateModelDrag()
{
    std::erase_if(m_discardedLoads, [](const auto& loading) { return IsReady(loading); });

    if (!m_modelDrag)
    {
        return;
    }

    if (!m_modelDrag->scene && IsReady(m_modelDrag->loading))
    {
        auto scene = m_modelDrag->loading.get();
        if (!scene || !scene->GetRootNode())
        {
            m_statusMessage = "Failed to load " + m_modelDrag->path.filename().string();
            m_statusMessageTime = 5.0f;
            m_modelDrag.reset();
            return;
        }

        // Measure the model with its loaded root transform, relative to the root position.
        auto root = scene->GetRootNode();
        XMStoreFloat4x4(&m_modelDrag->rootTransform, root->GetLocalTransform());
        BoundingBox bounds;
        if (ComputeWorldBounds(root, bounds))
        {
            m_modelDrag->bottomOffset = XMFLOAT3(bounds.Center.x, bounds.Center.y - bounds.Extents.y, bounds.Center.z);
        }
        else
        {
            m_modelDrag->bottomOffset = XMFLOAT3(0.0f, 0.0f, 0.0f);
        }

        m_modelDrag->scene = scene;
        const std::string name = m_modelDrag->path.stem().string();
        if (root->GetName().empty())
        {
            root->SetName(name);
        }
        m_scenes.push_back({ name, scene, true });
        m_selectedNode = root;

        if (m_modelDrag->hasPosition)
        {
            const XMVECTOR offset = XMLoadFloat3(&m_modelDrag->position) - XMLoadFloat3(&m_modelDrag->bottomOffset);
            root->SetLocalTransform(XMLoadFloat4x4(&m_modelDrag->rootTransform) * XMMatrixTranslationFromVector(offset));
        }
    }

    // Dropped somewhere else (or cancelled): remove the preview unless it was released over the viewport.
    const bool dragging = ImGui::GetDragDropPayload() != nullptr;
    if (!dragging && !m_modelDrag->delivered)
    {
        if (m_modelDrag->scene)
        {
            RemoveScene(m_modelDrag->scene);
        }
        else if (m_modelDrag->loading.valid())
        {
            // Destroying a std::async future waits for it, so let the load finish in the background.
            m_discardedLoads.push_back(std::move(m_modelDrag->loading));
        }
        m_modelDrag.reset();
        return;
    }

    if (m_modelDrag->delivered && m_modelDrag->scene)
    {
        m_statusMessage = "Placed " + m_modelDrag->path.filename().string();
        m_statusMessageTime = 3.0f;
        m_modelDrag.reset();
    }
}

// ------------------------------------------------------------------------------------------------
// Texture drops

void Editor::DropTextureOnMaterial(const fs::path& path, const std::shared_ptr<Material>& material, int slot)
{
    // Color textures are sampled as sRGB, data textures (normals, roughness...) as linear.
    const auto type = static_cast<Material::TextureType>(slot);
    const bool sRGB = type == Material::TextureType::Diffuse || type == Material::TextureType::Emissive ||
        type == Material::TextureType::Ambient || type == Material::TextureType::Specular;

    TextureDrop drop;
    drop.loading = std::async(std::launch::async, LoadImageTexture, path, sRGB);
    drop.material = material;
    drop.slot = slot;
    m_textureDrops.push_back(std::move(drop));
}

void Editor::UpdateTextureDrops()
{
    for (auto it = m_textureDrops.begin(); it != m_textureDrops.end();)
    {
        if (!IsReady(it->loading))
        {
            ++it;
            continue;
        }

        auto texture = it->loading.get();
        if (auto material = it->material.lock(); material && texture)
        {
            material->SetTexture(static_cast<Material::TextureType>(it->slot), texture);
        }
        else if (!texture)
        {
            m_statusMessage = "Failed to load texture";
            m_statusMessageTime = 5.0f;
        }
        it = m_textureDrops.erase(it);
    }
}

// ------------------------------------------------------------------------------------------------
// Background loads

bool Editor::IsLoadingInBackground() const
{
    if ((m_modelDrag && !m_modelDrag->scene && m_modelDrag->loading.valid()) || !m_textureDrops.empty() || !m_discardedLoads.empty())
    {
        return true;
    }
    for (const auto& [path, thumbnail] : m_assetThumbnails)
    {
        if (thumbnail.loading.valid())
        {
            return true;
        }
    }
    return false;
}

void Editor::UpdateSettledTextures()
{
    // Only refresh while nothing is loading: everything alive then has finished uploading.
    if (IsLoadingInBackground())
    {
        return;
    }
    m_settledTextures.clear();
    for (const auto& texture : Application::Get().GetLiveTextures())
    {
        m_settledTextures.insert(texture);
    }
}

bool Editor::IsTextureSettled(const std::shared_ptr<Texture>& texture) const
{
    return m_settledTextures.count(texture) > 0;
}

// ------------------------------------------------------------------------------------------------
// Editing

void Editor::SpawnModelInFrontOfCamera(const fs::path& path)
{
    if (m_modelDrag && m_modelDrag->scene)
    {
        RemoveScene(m_modelDrag->scene);
    }

    m_modelDrag = std::make_unique<ModelDrag>();
    m_modelDrag->path = path;
    m_modelDrag->loading = std::async(std::launch::async, LoadModel, path);
    m_modelDrag->delivered = true;

    if (m_camera)
    {
        // Camera forward is the third row of the inverse view matrix.
        const XMMATRIX cameraWorld = XMMatrixInverse(nullptr, m_camera->GetViewMatrix());
        XMStoreFloat3(&m_modelDrag->position, cameraWorld.r[3] + XMVector3Normalize(cameraWorld.r[2]) * m_dropDistance);
        m_modelDrag->hasPosition = true;
    }
}

void Editor::DeleteSelectedNode()
{
    auto node = m_selectedNode.lock();
    if (!node)
    {
        return;
    }

    if (auto parent = node->GetParent())
    {
        parent->RemoveChild(node);
        m_selectedNode.reset();
        return;
    }

    // A scene root: only scenes added by dropping assets can be removed.
    for (const auto& entry : m_scenes)
    {
        if (entry.scene->GetRootNode() == node)
        {
            if (entry.spawned)
            {
                RemoveScene(entry.scene);
                m_selectedNode.reset();
            }
            else
            {
                m_statusMessage = "Scenes created by the application cannot be deleted";
                m_statusMessageTime = 3.0f;
            }
            return;
        }
    }
}
