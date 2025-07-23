#pragma once

#include <Common/Core/Result.hpp>

#include <Engine/Graphic/Shader.hpp>
#include <Engine/Graphic/Image.hpp>

namespace Desert::Uniforms
{
    class UniformImage2D
    {
    public:
        virtual ~UniformImage2D() = default;

        virtual const uint32_t GetBinding() const = 0;

        virtual void SetImage2D( const std::shared_ptr<Graphic::Image2D>& image2D ) = 0;

        virtual const Common::UUID GetImageHash() const = 0;

    private:
        static std::shared_ptr<UniformImage2D> Create( const std::string_view debugName, uint32_t binding );

        friend class UniformManager;
    };
} // namespace Desert::Uniforms