#pragma once

#include <Engine/Core/Traits/is_texture.hpp>
#include <Engine/Graphic/Texture.hpp>

template <>
struct is_texture<Desert::Graphic::Texture2DRef> : std::true_type
{
};

template <>
struct is_texture<Desert::Graphic::TextureCubeRef> : std::true_type
{
};
