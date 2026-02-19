#pragma once

#include <Engine/Core/Traits/is_image.hpp>
#include <Engine/Graphic/Image.hpp>

template <>
struct is_image<Desert::Graphic::Image2DRef> : std::true_type
{
};

template <>
struct is_image<Desert::Graphic::ImageCubeRef> : std::true_type
{
};
