#pragma once

#include <Engine/Core/Formats/Shader.hpp>
#include <Common/Core/ResultStr.hpp>
#include <vector>
#include <cstdint>
#include <string>

namespace Desert::Core
{
    /**
     * @brief Specialized class for compiling GLSL shader source to SPIR-V.
     */
    class ShaderCompiler
    {
    public:
        /**
         * @brief Compiles GLSL source to SPIR-V.
         * 
         * @param stage The shader stage (Vertex, Fragment, Compute).
         * @param source The GLSL source code.
         * @param shaderPath The path to the shader file (for error reporting and includes).
         * @return Common::ResultStr<std::vector<uint32_t>> The compiled SPIR-V binary or an error message.
         */
        static Common::ResultStr<std::vector<uint32_t>> CompileGLSLToSPIRV( 
            Formats::ShaderStage stage, 
            const std::string& source, 
            const std::string& shaderPath );
    };

} // namespace Desert::Core
