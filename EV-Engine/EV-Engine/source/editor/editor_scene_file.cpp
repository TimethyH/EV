#include "DX12/dx12_includes.h"

#include "editor/editor.h"

#include <commdlg.h> // GetOpenFileNameW, GetSaveFileNameW
#include <objbase.h> // CoInitializeEx

#include <fstream>
#include <map>

#include "nlohmann/json.hpp"

#include "core/camera.h"
#include "DX12/scene.h"
#include "DX12/scene_node.h"
#include "resources/material.h"
#include "resources/mesh.h"
#include "resources/texture.h"

#include "editor_internal.h"

#pragma comment(lib, "Comdlg32.lib")
#pragma comment(lib, "Ole32.lib")

using namespace EV;
using namespace EV::EditorInternal;
using namespace DirectX;
namespace fs = std::filesystem;
using json = nlohmann::json;

/*
 * Scene file layout (JSON, .evscene):
 *
 *  {
 *    "format": "EV Scene", "version": 1,
 *    "camera": { "position": [x, y, z], "rotation": [x, y, z, w], "fov": 45 },
 *    "environment": "assets/sky4k.hdr",
 *    "settings": { "ocean.windSpeed": 0.5, ... },   (the application's, see Editor::SetSceneSettingsCallbacks)
 *    "models": [ "assets/kenny/ship-large.obj", ... ],      One entry per loaded copy of a model file.
 *    "materials": [ { "diffuse": [r, g, b, a], ..., "textures": { "Diffuse": "assets/...png", "Normal": null } } ],
 *    "objects": [ { "name": "ship-large", "root": <node> } ], Models placed in the editor.
 *    "applicationScenes": [ { "name": "Ocean", "transforms": [ [16 floats], ... ] } ]
 *  }
 *
 *  <node> = { "name": "...", "transform": [16 floats], "meshes": [ { "model": 0, "mesh": 3, "material": 0 } ],
 *             "children": [ <node>, ... ] }
 *
 * Meshes are found again by their index in the model file (Mesh::Source), so the node hierarchy can be edited
 * freely. Paths are relative to the working directory and use forward slashes.
 */

namespace
{
    constexpr const char* g_sceneFormat = "EV Scene";
    constexpr int         g_sceneVersion = 1;

    // Texture slot names in scene files, in Material::TextureType order.
    constexpr const char* g_textureSlotKeys[] = {
        "Ambient", "Emissive", "Diffuse", "Specular", "SpecularPower", "Normal", "Bump", "Opacity", "MetallicRoughness", "H0",
    };
    static_assert(_countof(g_textureSlotKeys) == static_cast<size_t>(Material::TextureType::NumTypes));

    // Paths are stored relative to the working directory when possible, with forward slashes.
    std::string ToScenePath(const fs::path& path)
    {
        fs::path stored = path;
        if (path.is_absolute())
        {
            std::error_code error;
            const fs::path relative = fs::relative(path, fs::current_path(), error);
            if (!error && !relative.empty() && *relative.begin() != "..")
            {
                stored = relative;
            }
        }
        return ToUTF8(stored.lexically_normal().generic_wstring());
    }

    json WriteMatrix(FXMMATRIX matrix)
    {
        XMFLOAT4X4 m;
        XMStoreFloat4x4(&m, matrix);
        json values = json::array();
        for (int row = 0; row < 4; ++row)
        {
            for (int column = 0; column < 4; ++column)
            {
                values.push_back(m.m[row][column]);
            }
        }
        return values;
    }

    XMFLOAT4X4 ReadMatrix(const json& values)
    {
        XMFLOAT4X4 m;
        XMStoreFloat4x4(&m, XMMatrixIdentity());
        if (values.is_array() && values.size() == 16)
        {
            for (int i = 0; i < 16; ++i)
            {
                m.m[i / 4][i % 4] = values[i].get<float>();
            }
        }
        return m;
    }

    json WriteFloat4(const XMFLOAT4& value)
    {
        return json::array({ value.x, value.y, value.z, value.w });
    }

    void ReadFloat4(const json& object, const char* key, XMFLOAT4& value)
    {
        const auto it = object.find(key);
        if (it != object.end() && it->is_array() && it->size() == 4)
        {
            value = XMFLOAT4((*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>(), (*it)[3].get<float>());
        }
    }

    void ReadFloat(const json& object, const char* key, float& value)
    {
        const auto it = object.find(key);
        if (it != object.end() && it->is_number())
        {
            value = it->get<float>();
        }
    }

    json WriteMaterial(const Material& material)
    {
        const MaterialProperties& properties = material.GetMaterialProperties();
        json result;
        result["diffuse"] = WriteFloat4(properties.diffuse);
        result["specular"] = WriteFloat4(properties.specular);
        result["emissive"] = WriteFloat4(properties.emissive);
        result["ambient"] = WriteFloat4(properties.ambient);
        result["reflectance"] = WriteFloat4(properties.reflectance);
        result["metallic"] = properties.metallic;
        result["roughness"] = properties.roughness;
        result["opacity"] = properties.opacity;
        result["specularPower"] = properties.specularPower;
        result["indexOfRefraction"] = properties.indexOfRefraction;
        result["bumpIntensity"] = properties.bumpIntensity;

        // Textures loaded from a file are stored by path, empty slots as null. Other textures (created in code)
        // are left out, the model's own texture is kept for those.
        json textures = json::object();
        for (int slot = 0; slot < static_cast<int>(Material::TextureType::NumTypes); ++slot)
        {
            const auto texture = material.GetTexture(static_cast<Material::TextureType>(slot));
            if (!texture)
            {
                textures[g_textureSlotKeys[slot]] = nullptr;
                continue;
            }
            std::error_code error;
            if (fs::is_regular_file(texture->GetName(), error))
            {
                textures[g_textureSlotKeys[slot]] = ToScenePath(texture->GetName());
            }
        }
        result["textures"] = textures;
        return result;
    }

    // Runs on the loading thread, before the material is shown.
    void ReadMaterial(const json& values, Material& material, std::vector<std::string>& warnings)
    {
        // Start from the current properties: they also hold the has-texture flags that SetTexture maintains.
        MaterialProperties properties = material.GetMaterialProperties();
        ReadFloat4(values, "diffuse", properties.diffuse);
        ReadFloat4(values, "specular", properties.specular);
        ReadFloat4(values, "emissive", properties.emissive);
        ReadFloat4(values, "ambient", properties.ambient);
        ReadFloat4(values, "reflectance", properties.reflectance);
        ReadFloat(values, "metallic", properties.metallic);
        ReadFloat(values, "roughness", properties.roughness);
        ReadFloat(values, "opacity", properties.opacity);
        ReadFloat(values, "specularPower", properties.specularPower);
        ReadFloat(values, "indexOfRefraction", properties.indexOfRefraction);
        ReadFloat(values, "bumpIntensity", properties.bumpIntensity);
        material.SetMaterialProperties(properties);

        const auto textures = values.find("textures");
        if (textures == values.end() || !textures->is_object())
        {
            return;
        }
        for (int slot = 0; slot < static_cast<int>(Material::TextureType::NumTypes); ++slot)
        {
            const auto type = static_cast<Material::TextureType>(slot);
            const auto it = textures->find(g_textureSlotKeys[slot]);
            if (it == textures->end())
            {
                continue;
            }
            if (it->is_null())
            {
                material.SetTexture(type, nullptr);
                continue;
            }
            if (!it->is_string())
            {
                continue;
            }

            // Only load textures that were changed in the editor; the model already has its own.
            const fs::path path = PathFromUTF8(it->get<std::string>().c_str());
            const auto current = material.GetTexture(type);
            if (current && fs::path(current->GetName()).lexically_normal() == path.lexically_normal())
            {
                continue;
            }
            if (auto texture = LoadImageTexture(path, IsColorTexture(type)))
            {
                material.SetTexture(type, texture);
            }
            else
            {
                warnings.push_back("Could not load texture " + it->get<std::string>());
            }
        }
    }

    // Shows a file dialog on the calling thread. Returns an empty path when cancelled.
    fs::path RunFileDialog(bool save, const fs::path& initialDirectory)
    {
        // The Explorer style dialog uses COM.
        const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

        wchar_t file[MAX_PATH] = {};
        const std::wstring directory = initialDirectory.wstring();

        OPENFILENAMEW dialog = {};
        dialog.lStructSize = sizeof(dialog);
        dialog.lpstrFilter = L"EV Scene (*.evscene)\0*.evscene\0All Files (*.*)\0*.*\0";
        dialog.lpstrFile = file;
        dialog.nMaxFile = MAX_PATH;
        dialog.lpstrInitialDir = directory.c_str();
        dialog.lpstrDefExt = L"evscene";
        dialog.lpstrTitle = save ? L"Save Scene" : L"Open Scene";
        // Asset paths are relative to the working directory, so the dialog must not change it.
        dialog.Flags = OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);

        const BOOL chosen = save ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog);

        if (SUCCEEDED(com))
        {
            CoUninitialize();
        }
        return chosen ? fs::path(file) : fs::path();
    }
}

// ------------------------------------------------------------------------------------------------
// Public

void Editor::NewScene()
{
    CancelModelDrag();
    std::erase_if(m_scenes, [](const SceneEntry& entry) { return entry.spawned; });
    m_selectedNode.reset();
    m_scenePath.clear();
    m_statusMessage = "New scene";
    m_statusMessageTime = 3.0f;
}

bool Editor::SaveScene(const fs::path& path)
{
    json file;
    file["format"] = g_sceneFormat;
    file["version"] = g_sceneVersion;

    if (m_camera)
    {
        XMFLOAT3 position;
        XMFLOAT4 rotation;
        XMStoreFloat3(&position, m_camera->GetTranslation());
        XMStoreFloat4(&rotation, m_camera->GetRotation());
        file["camera"] = {
            { "position", json::array({ position.x, position.y, position.z }) },
            { "rotation", WriteFloat4(rotation) },
            { "fov", m_camera->GetFov() },
        };
    }
    if (!m_environmentPath.empty())
    {
        file["environment"] = ToScenePath(m_environmentPath);
    }
    if (m_saveSettingsCallback)
    {
        std::map<std::string, float> settings;
        m_saveSettingsCallback(settings);
        if (!settings.empty())
        {
            file["settings"] = settings;
        }
    }

    // Every loaded copy of a model file gets an entry; meshes and materials refer to them by index.
    std::map<std::pair<std::wstring, uint64_t>, size_t> modelIndices;
    std::map<const Material*, size_t> materialIndices;
    json models = json::array();
    json materials = json::array();
    size_t skippedMeshes = 0;

    std::function<json(const std::shared_ptr<SceneNode>&)> writeNode = [&](const std::shared_ptr<SceneNode>& node)
    {
        json result;
        result["name"] = node->GetName();
        result["transform"] = WriteMatrix(node->GetLocalTransform());

        json meshes = json::array();
        for (const auto& mesh : node->GetMeshes())
        {
            if (!mesh)
            {
                continue;
            }
            const Mesh::Source& source = mesh->GetSource();
            if (source.file.empty())
            {
                // Created in code, it cannot be loaded again.
                ++skippedMeshes;
                continue;
            }

            auto model = modelIndices.find({ source.file, source.loadID });
            if (model == modelIndices.end())
            {
                model = modelIndices.emplace(std::make_pair(source.file, source.loadID), models.size()).first;
                models.push_back(ToScenePath(source.file));
            }

            json meshReference = { { "model", model->second }, { "mesh", source.index } };
            if (const auto material = mesh->GetMaterial())
            {
                auto materialIndex = materialIndices.find(material.get());
                if (materialIndex == materialIndices.end())
                {
                    materialIndex = materialIndices.emplace(material.get(), materials.size()).first;
                    materials.push_back(WriteMaterial(*material));
                }
                meshReference["material"] = materialIndex->second;
            }
            meshes.push_back(meshReference);
        }
        if (!meshes.empty())
        {
            result["meshes"] = meshes;
        }

        json children = json::array();
        for (const auto& child : node->GetChildren())
        {
            children.push_back(writeNode(child));
        }
        if (!children.empty())
        {
            result["children"] = children;
        }
        return result;
    };

    json objects = json::array();
    json applicationScenes = json::array();
    for (const auto& entry : m_scenes)
    {
        const auto root = entry.scene->GetRootNode();
        if (!root)
        {
            continue;
        }
        if (entry.spawned)
        {
            objects.push_back({ { "name", entry.name }, { "root", writeNode(root) } });
        }
        else
        {
            // The application creates these itself; only where their nodes are is saved.
            json transforms = json::array();
            ForEachNode(root, [&](const std::shared_ptr<SceneNode>& node) { transforms.push_back(WriteMatrix(node->GetLocalTransform())); });
            applicationScenes.push_back({ { "name", entry.name }, { "transforms", transforms } });
        }
    }

    file["models"] = models;
    file["materials"] = materials;
    file["objects"] = objects;
    file["applicationScenes"] = applicationScenes;

    std::error_code error;
    if (path.has_parent_path())
    {
        fs::create_directories(path.parent_path(), error);
    }
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (stream)
    {
        stream << file.dump(2) << '\n';
    }
    if (!stream)
    {
        m_statusMessage = "Could not write " + ToUTF8(path.wstring());
        m_statusMessageTime = 6.0f;
        return false;
    }

    m_scenePath = path;
    m_statusMessage = "Saved " + ToUTF8(path.filename().wstring());
    if (skippedMeshes > 0)
    {
        m_statusMessage += " (" + std::to_string(skippedMeshes) + " meshes created in code were not saved)";
    }
    m_statusMessageTime = 4.0f;
    return true;
}

void Editor::LoadScene(const fs::path& path)
{
    if (IsLoadingScene())
    {
        m_statusMessage = "Still loading " + ToUTF8(m_sceneLoadPath.filename().wstring());
        m_statusMessageTime = 3.0f;
        return;
    }

    m_sceneLoadPath = path;
    m_sceneLoad = std::async(std::launch::async, &Editor::ReadSceneFile, path);
    m_statusMessage = "Loading " + ToUTF8(path.filename().wstring()) + "...";
    m_statusMessageTime = 60.0f;
}

// ------------------------------------------------------------------------------------------------
// Loading

std::unique_ptr<Editor::SceneFileContents> Editor::ReadSceneFile(const fs::path& path)
{
    auto contents = std::make_unique<SceneFileContents>();
    contents->path = path;

    try
    {
        std::ifstream stream(path, std::ios::binary);
        if (!stream)
        {
            contents->error = "Could not open " + ToUTF8(path.wstring());
            return contents;
        }
        const json file = json::parse(stream);
        if (!file.is_object() || file.value("format", "") != g_sceneFormat)
        {
            contents->error = ToUTF8(path.filename().wstring()) + " is not an EV scene file";
            return contents;
        }
        if (file.value("version", 0) > g_sceneVersion)
        {
            contents->warnings.push_back("The file was saved by a newer version of the engine");
        }

        if (const auto camera = file.find("camera"); camera != file.end() && camera->is_object())
        {
            XMFLOAT4 position(0.0f, 0.0f, 0.0f, 0.0f);
            if (const auto values = camera->find("position"); values != camera->end() && values->is_array() && values->size() == 3)
            {
                position = XMFLOAT4((*values)[0].get<float>(), (*values)[1].get<float>(), (*values)[2].get<float>(), 0.0f);
            }
            contents->cameraPosition = XMFLOAT3(position.x, position.y, position.z);
            ReadFloat4(*camera, "rotation", contents->cameraRotation);
            ReadFloat(*camera, "fov", contents->cameraFov);
            contents->hasCamera = true;
        }
        if (const auto environment = file.find("environment"); environment != file.end() && environment->is_string())
        {
            contents->environment = PathFromUTF8(environment->get<std::string>().c_str());
        }
        if (const auto settings = file.find("settings"); settings != file.end() && settings->is_object())
        {
            for (const auto& [name, value] : settings->items())
            {
                if (value.is_number())
                {
                    contents->settings[name] = value.get<float>();
                }
            }
        }

        // Load every model once; meshes are looked up by their index in the file.
        std::vector<std::vector<std::shared_ptr<Mesh>>> modelMeshes;
        for (const auto& model : file.value("models", json::array()))
        {
            std::vector<std::shared_ptr<Mesh>> meshes;
            const std::string modelPath = model.is_string() ? model.get<std::string>() : std::string();
            if (auto scene = modelPath.empty() ? nullptr : LoadModel(PathFromUTF8(modelPath.c_str())))
            {
                ForEachNode(scene->GetRootNode(), [&](const std::shared_ptr<SceneNode>& node)
                {
                    for (const auto& mesh : node->GetMeshes())
                    {
                        if (!mesh)
                        {
                            continue;
                        }
                        const uint32_t index = mesh->GetSource().index;
                        if (index >= meshes.size())
                        {
                            meshes.resize(index + 1);
                        }
                        meshes[index] = mesh;
                    }
                });
            }
            else
            {
                contents->warnings.push_back("Could not load model " + modelPath);
            }
            modelMeshes.push_back(std::move(meshes));
        }

        // The saved values of a material go to the material of the first mesh that uses it.
        const json materialValues = file.value("materials", json::array());
        std::vector<std::shared_ptr<Material>> materials(materialValues.size());
        size_t missingMeshes = 0;

        std::function<std::shared_ptr<SceneNode>(const json&, const std::shared_ptr<SceneNode>&)> readNode =
            [&](const json& values, const std::shared_ptr<SceneNode>& parent)
        {
            auto node = std::make_shared<SceneNode>();
            node->SetName(values.value("name", ""));
            if (parent)
            {
                // AddChild keeps the world transform, so the local transform is set afterwards.
                parent->AddChild(node);
            }
            const XMFLOAT4X4 transform = ReadMatrix(values.value("transform", json()));
            node->SetLocalTransform(XMLoadFloat4x4(&transform));

            for (const auto& meshReference : values.value("meshes", json::array()))
            {
                const size_t model = meshReference.value("model", SIZE_MAX);
                const size_t index = meshReference.value("mesh", SIZE_MAX);
                if (model >= modelMeshes.size() || index >= modelMeshes[model].size() || !modelMeshes[model][index])
                {
                    ++missingMeshes;
                    continue;
                }
                const auto& mesh = modelMeshes[model][index];

                const size_t material = meshReference.value("material", SIZE_MAX);
                if (material < materials.size())
                {
                    if (!materials[material])
                    {
                        materials[material] = mesh->GetMaterial();
                    }
                    else
                    {
                        mesh->SetMaterial(materials[material]);
                    }
                }
                node->AddMesh(mesh);
            }

            for (const auto& child : values.value("children", json::array()))
            {
                readNode(child, node);
            }
            return node;
        };

        for (const auto& object : file.value("objects", json::array()))
        {
            const auto root = object.find("root");
            if (root == object.end() || !root->is_object())
            {
                continue;
            }
            auto scene = std::make_shared<Scene>();
            scene->SetRootNode(readNode(*root, nullptr));
            contents->scenes.push_back({ object.value("name", "Model"), scene, true });
        }

        for (size_t i = 0; i < materials.size(); ++i)
        {
            if (materials[i])
            {
                ReadMaterial(materialValues[i], *materials[i], contents->warnings);
            }
        }

        if (missingMeshes > 0)
        {
            contents->warnings.push_back(std::to_string(missingMeshes) + " meshes could not be found");
        }

        for (const auto& scene : file.value("applicationScenes", json::array()))
        {
            SceneFileContents::ApplicationScene applicationScene;
            applicationScene.name = scene.value("name", "");
            for (const auto& transform : scene.value("transforms", json::array()))
            {
                applicationScene.transforms.push_back(ReadMatrix(transform));
            }
            contents->applicationScenes.push_back(std::move(applicationScene));
        }
    }
    catch (const std::exception& e)
    {
        contents->error = "Could not read " + ToUTF8(path.filename().wstring()) + ": " + e.what();
        contents->scenes.clear();
    }

    return contents;
}

void Editor::UpdateSceneLoad()
{
    if (!IsReady(m_sceneLoad))
    {
        return;
    }

    auto contents = m_sceneLoad.get();
    if (!contents->error.empty())
    {
        OutputDebugStringA((contents->error + "\n").c_str());
        m_statusMessage = contents->error;
        m_statusMessageTime = 6.0f;
        return;
    }
    ApplySceneFile(*contents);
}

void Editor::ApplySceneFile(SceneFileContents& contents)
{
    // The loaded models replace the ones placed before.
    CancelModelDrag();
    std::erase_if(m_scenes, [](const SceneEntry& entry) { return entry.spawned; });
    m_selectedNode.reset();
    for (auto& entry : contents.scenes)
    {
        m_scenes.push_back(std::move(entry));
    }

    // The application's own scenes are matched by name; the transforms only fit while the hierarchy is the same.
    for (const auto& saved : contents.applicationScenes)
    {
        for (const auto& entry : m_scenes)
        {
            if (entry.spawned || entry.name != saved.name || !entry.scene->GetRootNode() || saved.transforms.empty())
            {
                continue;
            }
            std::vector<std::shared_ptr<SceneNode>> nodes;
            ForEachNode(entry.scene->GetRootNode(), [&](const std::shared_ptr<SceneNode>& node) { nodes.push_back(node); });
            if (nodes.size() != saved.transforms.size())
            {
                contents.warnings.push_back("The nodes of " + saved.name + " changed, only its position was loaded");
            }
            const size_t count = nodes.size() == saved.transforms.size() ? nodes.size() : 1;
            for (size_t i = 0; i < count; ++i)
            {
                nodes[i]->SetLocalTransform(XMLoadFloat4x4(&saved.transforms[i]));
            }
            break;
        }
    }

    if (!contents.settings.empty() && m_loadSettingsCallback)
    {
        m_loadSettingsCallback(contents.settings);
    }

    if (contents.hasCamera && m_camera)
    {
        m_camera->SetTranslation(XMLoadFloat3(&contents.cameraPosition));
        m_camera->SetRotation(XMQuaternionNormalize(XMLoadFloat4(&contents.cameraRotation)));
        m_camera->SetFov(contents.cameraFov);
        if (m_cameraChangedCallback)
        {
            m_cameraChangedCallback();
        }
    }

    m_scenePath = contents.path;
    m_statusMessage = "Opened " + ToUTF8(contents.path.filename().wstring());
    m_statusMessageTime = 4.0f;

    for (const auto& warning : contents.warnings)
    {
        OutputDebugStringA((warning + "\n").c_str());
    }
    if (!contents.warnings.empty())
    {
        m_statusMessage += " (" + contents.warnings.front() + (contents.warnings.size() > 1 ? ", ..." : "") + ")";
        m_statusMessageTime = 8.0f;
    }

    // Loads in the background and reports in the status bar itself.
    if (!contents.environment.empty() && m_environmentCallback && fs::path(contents.environment) != m_environmentPath)
    {
        LoadEnvironment(contents.environment);
    }
}

void Editor::CancelModelDrag()
{
    if (!m_modelDrag)
    {
        return;
    }
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
}

// ------------------------------------------------------------------------------------------------
// Menu and dialogs

void Editor::DrawFileMenu()
{
    if (!ImGui::BeginMenu("File"))
    {
        return;
    }

    const bool busy = m_fileDialog || IsLoadingScene();
    if (ImGui::MenuItem("New Scene", "Ctrl+N", false, !busy))
    {
        NewScene();
    }
    if (ImGui::MenuItem("Open Scene...", "Ctrl+O", false, !busy))
    {
        ShowFileDialog(false);
    }
    if (ImGui::MenuItem("Save Scene", "Ctrl+S", false, !busy))
    {
        SaveCurrentScene();
    }
    if (ImGui::MenuItem("Save Scene As...", "Ctrl+Shift+S", false, !busy))
    {
        ShowFileDialog(true);
    }
    // The application's File menu items follow (ImGui appends menus with the same name).
    ImGui::Separator();
    ImGui::EndMenu();
}

void Editor::SaveCurrentScene()
{
    if (m_scenePath.empty())
    {
        ShowFileDialog(true);
    }
    else
    {
        SaveScene(m_scenePath);
    }
}

void Editor::ShowFileDialog(bool save)
{
    if (m_fileDialog || IsLoadingScene())
    {
        return;
    }

    // Start in the folder of the current scene, otherwise in "scenes" in the working directory.
    std::error_code error;
    fs::path directory = m_scenePath.has_parent_path() ? m_scenePath.parent_path() : fs::current_path() / "scenes";
    fs::create_directories(directory, error);
    directory = fs::absolute(directory, error);

    // A modal dialog on this thread would run its own message loop, which paints (renders) the window from
    // inside the current frame.
    m_fileDialog = std::make_unique<FileDialog>();
    m_fileDialog->save = save;
    m_fileDialog->result = std::async(std::launch::async, RunFileDialog, save, directory);
}

void Editor::UpdateFileDialog()
{
    if (!m_fileDialog || !IsReady(m_fileDialog->result))
    {
        return;
    }

    const fs::path path = m_fileDialog->result.get();
    const bool save = m_fileDialog->save;
    m_fileDialog.reset();

    if (path.empty())
    {
        return;
    }
    if (save)
    {
        SaveScene(path);
    }
    else
    {
        LoadScene(path);
    }
}
