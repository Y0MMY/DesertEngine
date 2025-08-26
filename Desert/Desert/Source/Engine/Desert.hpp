#pragma once

#include <cstdint>
#include <variant>
#include <optional>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>
#include <memory>

// =================== Common =================== //

#include <Common/Core/LayerStack.hpp>
#include <Common/Core/Logger.hpp>
#include <Common/Core/Math/Ray.hpp>
#include <Common/Core/Input.hpp>

// =================== Engine =================== //

#include <Engine/Core/Application.hpp>
#include <Engine/Core/Scene.hpp>

// =================== Graphic =================== //

#include <Engine/Graphic/Framebuffer.hpp>
#include <Engine/Graphic/VertexBuffer.hpp>
#include <Engine/Graphic/IndexBuffer.hpp>
#include <Engine/Graphic/Pipeline.hpp>
#include <Engine/Graphic/RenderPass.hpp>
#include <Engine/Graphic/Shader.hpp>
#include <Engine/Graphic/Renderer.hpp>
#include <Engine/Graphic/Texture.hpp>
#include <Engine/Graphic/Image.hpp>
#include <Engine/Graphic/SceneRenderer.hpp>
#include <Engine/Graphic/Geometry/Mesh.hpp>
#include <Engine/Graphic/UICacheTexture.hpp>
#include <Engine/Graphic/Systems/RenderSystem.hpp>

// =================== ECS =================== //

#include <Engine/ECS/Entity.hpp>
#include <Engine/ECS/Components.hpp>

// =================== Assets =================== //

#include <Engine/Assets/AssetManager.hpp>
#include <Engine/Assets/AssetPreloader.hpp>
