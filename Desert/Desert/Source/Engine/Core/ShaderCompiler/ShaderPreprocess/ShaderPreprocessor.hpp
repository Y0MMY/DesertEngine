#pragma once

#include <Engine/Core/Formats/Shader.hpp>

namespace Desert::Core::Preprocess::Shader
{
    std::unordered_map<Formats::ShaderStage, std::string> PreProcess( const std::string& source );
}