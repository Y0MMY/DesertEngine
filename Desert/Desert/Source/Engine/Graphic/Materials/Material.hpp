#pragma once

#include <Engine/Graphic/Shader.hpp>
#include <Engine/Graphic/Image.hpp>
#include <Engine/Graphic/UniformBuffer.hpp>

#include <glm/glm.hpp>

namespace Desert::Graphic
{
    class Material
    {
    public:
        virtual ~Material() = default;

        /*virtual Common::BoolResult SetVec3( const std::string& name, const glm::vec3& data )                 = 0;
        virtual Common::BoolResult SetMat4( const std::string& name, const glm::mat4& data )                 = 0;*/

        virtual Common::BoolResult AddUniformToOverride( const std::shared_ptr<UniformBuffer>& uniformBuffer ) = 0;

        virtual Common::BoolResult SetImage2D( const std::string&              name,
                                               const std::shared_ptr<Image2D>& image2D )     = 0;
        virtual Common::BoolResult SetImageCube( const std::string&                name,
                                                 const std::shared_ptr<ImageCube>& imageCube ) = 0;

        virtual Common::BoolResult Invalidate()    = 0;
        virtual Common::BoolResult ApplyMaterial() = 0;

        static std::shared_ptr<Material> Create( const std::string&             debugName,
                                                 const std::shared_ptr<Shader>& shader );
    };
} // namespace Desert::Graphic