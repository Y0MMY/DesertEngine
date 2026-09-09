#pragma once

#include <cstdint>

namespace Desert::Core
{
    /**
     * WHERE A SHADER GRAPH'S OWN RESOURCES LIVE IN DESCRIPTOR SET 0 — one window, for every domain.
     *
     * A graph's text is always compiled INTO a program the graph does not own. A Surface or PostProcess
     * graph becomes a generated `.shader` that includes the engine's own lighting headers; a Volume graph
     * becomes the cloud medium that four shipped programs are compiled against
     * (Docs/Clouds/O1_DESIGN.md §12.2). Either way the graph declares GLSL beside declarations it cannot
     * see, and a graph author cannot be asked to know what they are.
     *
     * TWO DECLARATIONS ON ONE DESCRIPTOR SLOT IS NOT A COMPILE ERROR. glslang emits both Binding
     * decorations with no diagnostic at all, `-Werror` included (measured with glslc 2026-09-08, Г17).
     * The engine refuses it after compilation — ShaderReflection::ReflectStage names both resources and
     * VulkanShader::Reflect drops the shader — but that refusal arrives when a material is applied, in
     * front of the person applying it. A reservation is what keeps it from arriving at all.
     *
     * WHY THE NUMBER CANNOT BE DERIVED IN C++, which is the first thing to reach for. The occupancy of a
     * program lives in GLSL text, spread across included headers and macros, and is only a set of numbers
     * after shaderc has run — Г17 established exactly that when it moved the collision check to
     * reflection. So the number here is a RESERVATION and the claim that it is free is a MEASUREMENT:
     * `Desert/Tests/Engine/ShaderCacheKey` compiles every pass of every stage of every shipped `.shader`
     * with shaderc, reflects the SPIR-V with the engine's own reflection, and asserts that not one set-0
     * binding falls at or above this value. It names no engine binding and counts nothing, so a slot
     * added tomorrow reddens it whatever spelled the number.
     *
     * 24 IS ONE ABOVE THE HIGHEST BINDING THE TREE DECLARES TODAY, and that is not comfortable headroom —
     * it is none. The lit shader-graph surface layout reaches 23 (the fourth cascade shadow map in
     * Programs/Graph/MatLitConst.shader), so the next binding that layout grows lands here. That is what
     * the census is for: the collision becomes a red test rather than a wrong picture.
     *
     * MOVING THIS VALUE IS NOT FREE. The DSL spells a texture base as a literal inside the GENERATED
     * `.shader` (`Properties ... TextureBinding(24)`), so a `.shader` generated before a move keeps the
     * old number until its `.dgraph` is compiled again. No generated shader in this repository declares a
     * texture today, so there is no stale artifact to fix; whoever raises this value owns finding out
     * whether that is still true.
     */
    inline constexpr uint32_t kGraphOwnedBindingFirst = 24u;
} // namespace Desert::Core
