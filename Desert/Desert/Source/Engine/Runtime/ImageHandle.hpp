#pragma once

#include <Common/Core/Core.hpp>
#include <Common/Core/Handle.hpp>

namespace Desert::Runtime
{
    struct ImageHandle
    {
        enum class Type : uint8_t
        {
            Image2D,
            ImageCube,
            // Volume textures. Registering them with the ImageService is what makes them participate in
            // engine-wide image operations — in particular Renderer::RecreateImageSamplers on a texture
            // filter change, which a volume must survive with its LINEAR sampler intact.
            Image3D
        };

        ImageHandle() = default;

        explicit ImageHandle( const Common::Core::Handle& value, Type type ) : Value( value ), ImageType( type )
        {
            DESERT_VERIFY( Value.IsValid(), "ImageHandle created with invalid Handle" );
        }

        bool IsValid() const
        {
            return Value.IsValid();
        }

        Common::Core::Handle Value{};
        Type                 ImageType{};
    };

} // namespace Desert::Runtime