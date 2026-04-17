#pragma once

#include <string>
#include <array>
#include <optional>

#include <Common/Core/UUID.hpp>

#include <glm/glm.hpp>

namespace Desert::Assets::Serialization
{
    struct TextureRef
    {
        std::string Path;
    };

    template <typename T>
    struct MaterialParam
    {
        std::optional<TextureRef> Texture;
        T                         Value;
    };

    struct MaterialAssetData
    {
        std::string  Name;
        Common::UUID MaterialHandle;

        MaterialParam<glm::vec4> Albedo; // base color
        MaterialParam<float>     Metallic;
        MaterialParam<float>     Roughness;
        MaterialParam<float>     AO;
        MaterialParam<glm::vec3> Emissive;
    };
} // namespace Desert::Assets::Serialization