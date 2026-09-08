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

        virtual uint32_t GetBinding() const = 0;

        virtual void SetImageCube( const Graphic::ImageCube* imageCube ) = 0;

        // GetImageHash() was here with no caller — same as UniformImage2D. Г12.

    private:
        static std::shared_ptr<UniformImageCube> Create( const std::string_view debugName, uint32_t binding );

        friend class ShaderResourcesManager;
    };
} // namespace Desert::ShaderResources