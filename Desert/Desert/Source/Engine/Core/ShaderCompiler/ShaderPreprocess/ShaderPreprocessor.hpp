#pragma once

#include <Engine/Core/Formats/Shader.hpp>
#include <Engine/Core/Formats/ShaderProgramMeta.hpp>

namespace Desert::Core::Preprocess
{
    class ShaderPreprocess
    {
    public:
        static std::unordered_map<Core::Formats::ShaderStage, std::string>
        PreProcessProgram( const std::string& source, const std::filesystem::path& basePath );

        // Parses program-level `#pragma param` / `#pragma state` lines from the .shader source into the
        // data-driven material metadata. Additive: a shader with no such pragmas yields an empty meta.
        static Core::Formats::ShaderProgramMeta ParseProgramMeta( const std::string& source );
    };
} // namespace Desert::Core::Preprocess