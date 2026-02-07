#pragma once

// Auto-linking - must come first
#include "utility/link_lib.h"

// Core engine includes
#include "core/application.h"
#include "core/game.h"
#include "core/window.h"
#include "core/events.h"
#include "core/clock.h"
#include "core/camera.h"

// DX12 includes
#include "DX12/command_list.h"
#include "DX12/command_queue.h"
#include "DX12/scene.h"
#include "DX12/scene_node.h"
#include "DX12/scene_visitor.h"
#include "DX12/render_target.h"
#include "DX12/swapchain.h"
#include "DX12/light.h"
#include "DX12/effect_pso.h"

// Resource includes
#include "resources/mesh.h"
#include "resources/texture.h"
#include "resources/material.h"
#include "resources/vertex_buffer.h"
#include "resources/index_buffer.h"

// Utility includes
#include "utility/helpers.h"
#include "DX12/dx12_includes.h"

// Thirdparty
#include "imgui.h"
