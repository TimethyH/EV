#include <scene_visitor.h>

#include <command_list.h>
#include <index_buffer.h>
#include <mesh.h>

// using namespace dx12lib;

SceneVisitor::SceneVisitor(CommandList& commandList)
    : m_CommandList(commandList)
{
}

void SceneVisitor::Visit(Mesh& mesh)
{
    mesh.Draw(m_CommandList);
}