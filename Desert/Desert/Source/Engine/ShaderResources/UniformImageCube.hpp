#pragma once

#include <Common/Core/ResultStr.hpp>

#include <Engine/Graphic/Shader.hpp>
#include <Engine/Graphic/Image.hpp>

namespace Desert::ShaderResources
{
    class UniformImageCube
    {
    public:
        virtual ~UniformImageCube() = default;

        virtual const uint32_t GetBinding() const = 0;

        virtual void SetImageCube( const Graphic::ImageCube* imageCube ) = 0;

        virtual const Common::UUID GetImageHash() const = 0;

    private:
        static std::shared_ptr<UniformImageCube> Create( const std::string_view debugName, uint32_t binding );

        friend class ShaderResourcesManager;
    };
} // namespace Desert::ShaderResources