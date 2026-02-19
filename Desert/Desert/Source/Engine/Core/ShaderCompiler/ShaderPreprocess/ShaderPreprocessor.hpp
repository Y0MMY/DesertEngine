#pragma once

#include <Engine/Core/Formats/Shader.hpp>

namespace Desert::Core::Preprocess
{
    class ShaderPreprocess
    {
    public:
        static std::unordered_map<Core::Formats::ShaderStage, std::string>
        PreProcessProgram( const std::string& source, const std::filesystem::path& basePath );
    };
} // namespace Desert::Core::Preprocess