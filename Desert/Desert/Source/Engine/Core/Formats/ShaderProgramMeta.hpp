#pragma once

#include <Engine/Core/Formats/Shader.hpp>

#include <glm/glm.hpp>

#include <optional>
#include <string>
#include <vector>

namespace Desert::Core::Formats
{
    // ---- Program-level metadata declared in a .shader file (data-driven materials) ----
    //
    // Parsed by the ShaderPreprocessor from `#pragma param` / `#pragma state` lines and attached to the
    // compiled Shader. The Graphic layer (pipeline cache + generic Material) consumes it; the editor
    // builds the Details UI from the param list. Kept backend-neutral so Core can produce it and Graphic
    // can interpret it without a layer-inversion.
    //
    // Syntax (in the .shader, alongside `#pragma use_stage`):
    //   #pragma param float   Roughness  "Roughness"  range(0,1)  default(0.5)
    //   #pragma param color   BaseColor  "Base Color"             default(1,1,1,1)
    //   #pragma param vec3    SunDir                              default(0,1,0)
    //   #pragma param texture2D AlbedoTex "Albedo"               default("white")
    //   #pragma state cull back
    //   #pragma state depth less write on
    //   #pragma state blend off
    //   #pragma state topology patches 4

    // How a param is presented/edited. The underlying storage type is ShaderValueType; UiHint refines it
    // (e.g. a vec4 shown as a color picker vs four drag floats).
    enum class ShaderParamWidget : uint8_t
    {
        Auto = 0, // pick from value type
        Color,    // vec3/vec4 as a color swatch
        Slider    // scalar/vector with a range
    };

    struct ShaderParam
    {
        std::string       Name;                                  // UB field / sampler name (the binding key)
        std::string       DisplayName;                           // editor label (defaults to Name)
        std::string       Category;                              // optional Details grouping
        ShaderValueType   Type   = ShaderValueType::Float;       // numeric storage type
        ShaderParamWidget Widget = ShaderParamWidget::Auto;
        bool              IsTexture = false;                     // sampler param (uses DefaultTexture)

        std::optional<float> Min;                                // present => slider/clamped
        std::optional<float> Max;

        glm::vec4   Default        = glm::vec4( 0.0f );          // numeric default (xyzw as needed)
        std::string DefaultTexture;                              // texture param default (e.g. "white")
    };

    // ---- Render state (maps to GraphicsPipelineSpecification in the Graphic layer's pipeline cache) ----
    // std::nullopt = "unspecified" -> the renderer/pipeline-cache falls back to its default.

    enum class StateCull : uint8_t  { None, Front, Back, FrontAndBack };
    enum class StateCompare : uint8_t { Never, Less, Equal, LessOrEqual, Greater, NotEqual, GreaterOrEqual, Always };
    enum class StateTopology : uint8_t { Triangles, Lines, Points, Patches };

    struct ShaderRenderState
    {
        std::optional<StateCull>     Cull;
        std::optional<bool>          DepthTest;
        std::optional<bool>          DepthWrite;
        std::optional<StateCompare>  DepthCompare;
        std::optional<bool>          Blend;
        std::optional<StateTopology> Topology;
        std::optional<uint32_t>      PatchControlPoints; // only meaningful for Patches topology
    };

    // Where a shader may be used (mirrors UE's Material Domain). Drives the editor's material shader
    // picker: only "assignable" domains (Surface, Terrain) are offered for a MaterialComponent; engine
    // shaders (skybox, post-process, shadow…) stay out of the list. A shader with no `#pragma domain` is
    // Unspecified -> treated as internal (not offered).
    enum class ShaderDomain : uint8_t
    {
        Unspecified = 0, // no #pragma domain -> internal, not user-assignable
        Surface,         // lit/unlit surface materials on meshes
        Terrain,         // tessellated terrain materials
        Skybox,
        PostProcess
    };

    struct ShaderProgramMeta
    {
        std::vector<ShaderParam> Params;
        ShaderRenderState        State;
        ShaderDomain             Domain = ShaderDomain::Unspecified;

        bool HasParams() const
        {
            return !Params.empty();
        }

        // Can a user assign this shader to a renderable via a MaterialComponent?
        bool IsUserAssignable() const
        {
            return Domain == ShaderDomain::Surface || Domain == ShaderDomain::Terrain;
        }
    };

} // namespace Desert::Core::Formats
