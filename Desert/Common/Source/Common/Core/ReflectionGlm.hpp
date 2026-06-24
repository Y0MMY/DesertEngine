#pragma once

//  Include this header in .cpp files that register GLM-typed fields with TypeDescriptorBuilder.
//  It provides DeduceTypeTag<> specialisations for glm::vec2 / vec3 / vec4.
//
//  Never include from other .hpp files — it pulls in all of GLM.

#include <Common/Core/Reflection.hpp>
#include <glm/glm.hpp>

namespace Desert::Reflect
{
    template <>
    constexpr FieldTypeTag DeduceTypeTag<glm::vec2>() { return FieldTypeTag::Vec2; }

    template <>
    constexpr FieldTypeTag DeduceTypeTag<glm::vec3>() { return FieldTypeTag::Vec3; }

    template <>
    constexpr FieldTypeTag DeduceTypeTag<glm::vec4>() { return FieldTypeTag::Vec4; }

} // namespace Desert::Reflect
