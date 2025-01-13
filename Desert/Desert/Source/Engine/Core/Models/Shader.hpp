#pragma once

#include <Engine/Core/Formats/Shader.hpp>

namespace Desert::Core::Models
{
    struct UniformBuffer
    {
        uint32_t    Size         = 0;
        uint32_t    BindingPoint = 0;
        std::string Name;

        Core::Formats::ShaderStage ShaderStage = Core::Formats::ShaderStage::None;
    };

} // namespace Desert::Core::Models