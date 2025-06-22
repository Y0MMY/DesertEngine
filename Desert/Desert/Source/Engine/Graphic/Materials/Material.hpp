#pragma once

#include <Engine/Graphic/Shader.hpp>
#include <Engine/Graphic/Image.hpp>
#include <Engine/Uniforms/UniformBuffer.hpp>
#include <Engine/Uniforms/UniformImageCube.hpp>
#include <Engine/Uniforms/UniformImage2D.hpp>
#include <Engine/Graphic/Pipeline.hpp>

#include <glm/glm.hpp>

namespace Desert::Graphic
{
    class Material
    {
    public:
        virtual ~Material() = default;

        virtual Common::BoolResult
        AddUniformBufferToOverride( const std::shared_ptr<Uniforms::UniformBuffer>& uniformBuffer ) = 0;
        virtual Common::BoolResult
        AddUniformCubeToOverride( const std::shared_ptr<Uniforms::UniformImageCube>& uniformCube ) = 0;
        virtual Common::BoolResult
        AddUniform2DToOverride( const std::shared_ptr<Uniforms::UniformImage2D>& uniform2D ) = 0;
        virtual Common::BoolResult Invalidate()                                              = 0;
        virtual Common::BoolResult ApplyMaterial()                                           = 0;
        virtual void               Clear() = 0; // TODO: better func name

        virtual std::shared_ptr<Shader> GetShader() const = 0;

        virtual Common::BoolResult PushConstant( const void* buffer, const uint32_t bufferSize ) = 0;

        static std::shared_ptr<Material> Create( const std::string&             debugName,
                                                 const std::shared_ptr<Shader>& shader );
    };
} // namespace Desert::Graphic