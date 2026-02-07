#include <DX12/scene_visitor.h>

#include <DX12/command_list.h>
#include <resources/index_buffer.h>
#include <resources/mesh.h>

#include "core/camera.h"
#include "DX12/effect_pso.h"
#include "resources/material.h"
#include "DX12/scene_node.h"


using namespace EV;
using namespace DirectX;

SceneVisitor::SceneVisitor(CommandList& commandList, const Camera& camera, EffectPSO& pso, bool transparent)
    : m_commandList(commandList)
    , m_camera(camera)
    , m_lightingPSO(pso)
    , m_transparentPass(transparent)
{
}

void SceneVisitor::Visit(Scene& scene)
{
    m_lightingPSO.SetViewMatrix(m_camera.GetViewMatrix());
    m_lightingPSO.SetProjectionMatrix(m_camera.GetProjectionMatrix());
}

void SceneVisitor::Visit(SceneNode& sceneNode)
{
    auto world = sceneNode.GetWorldTransform();
    m_lightingPSO.SetWorldMatrix(world);
}

void SceneVisitor::Visit(Mesh& mesh)
{
    auto material = mesh.GetMaterial();
    // if (material->IsTransparent() == m_transparentPass) // TODO: need to account for transparant objects.
    {
        m_lightingPSO.SetMaterial(material);

        m_lightingPSO.Apply(m_commandList);
        mesh.Draw(m_commandList);
    }
}