#pragma once

#include <Common/Core/Result.hpp>

#include <Engine/Graphic/Shader.hpp>
#include <Engine/Graphic/Image.hpp>

namespace Desert::Uniforms
{
    class UniformImageCube
    {
    public:
        virtual ~UniformImageCube() = default;

        virtual const uint32_t GetBinding() const = 0;

        virtual void SetImageCube( const std::shared_ptr<Graphic::ImageCube>& imageCube ) = 0;

    private:
        static std::shared_ptr<UniformImageCube> Create( const std::string_view debugName, uint32_t binding );

        friend class UniformManager;
    };
} // namespace Desert::Uniforms