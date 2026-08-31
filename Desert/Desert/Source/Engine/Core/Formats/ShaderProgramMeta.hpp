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
    enum class StateBlendFactor : uint8_t
    {
        Zero, One, SrcColor, OneMinusSrcColor, DstColor, OneMinusDstColor,
        SrcAlpha, OneMinusSrcAlpha, DstAlpha, OneMinusDstAlpha
    };
    enum class StateStencilOp : uint8_t
    {
        Keep, Zero, Replace, IncrementClamp, DecrementClamp, Invert, IncrementWrap, DecrementWrap
    };

    struct ShaderRenderState
    {
        std::optional<StateCull>     Cull;
        std::optional<bool>          DepthTest;
        std::optional<bool>          DepthWrite;
        std::optional<StateCompare>  DepthCompare;
        std::optional<bool>          Blend;
        std::optional<StateTopology> Topology;
        std::optional<uint32_t>      PatchControlPoints; // only meaningful for Patches topology

        // Custom blend factors (only meaningful when Blend is on). Both set => custom; otherwise the
        // renderer's standard src-alpha / one-minus-src-alpha blend. DSL: `Blend SrcAlpha OneMinusSrcAlpha`.
        std::optional<StateBlendFactor> BlendSrc;
        std::optional<StateBlendFactor> BlendDst;

        // Stencil test. DSL: `Stencil <compare> <ref> [<fail> <pass> <depthFail>]`
        // (default ops Keep/Replace/Keep; read/write masks 0xFF). std::nullopt = no stencil (default).
        std::optional<bool>           StencilTest;
        std::optional<StateCompare>   StencilCompare;
        std::optional<uint32_t>       StencilRef;
        std::optional<StateStencilOp> StencilFail;
        std::optional<StateStencilOp> StencilPass;
        std::optional<StateStencilOp> StencilDepthFail;
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

    // The enum's own spelling, for diagnostics. It lives beside the enum so a domain added above cannot
    // leave a log line reading "3": this switch has no default, so growing the enum is a -Wswitch warning
    // and the build is warning-clean.
    //
    // NOT the same function as the editor's DomainName() (MaterialEditorPanel.cpp), which is deliberately
    // separate artist-facing copy -- "engine-internal" reads better than "Unspecified" beside a material's
    // name in a window, whereas a log wants the token the `.shader` file actually writes so the message can
    // be grepped straight back to the file that caused it.
    constexpr const char* ShaderDomainName( ShaderDomain domain )
    {
        switch ( domain )
        {
            case ShaderDomain::Unspecified:
                return "Unspecified";
            case ShaderDomain::Surface:
                return "Surface";
            case ShaderDomain::Terrain:
                return "Terrain";
            case ShaderDomain::Skybox:
                return "Skybox";
            case ShaderDomain::PostProcess:
                return "PostProcess";
        }
        return "Unspecified";
    }

    // ---- Which draw path may execute a domain ----
    //
    // These two are the domain's actual MEANING, and they are separate on purpose. A domain is not a label
    // a material carries around; it is the name of the one renderer that knows how to feed that shader.
    //
    // THE TRAP THEY EXIST TO CLOSE. IsUserAssignable() below is their UNION, and a draw path that asks the
    // union instead of its own half accepts a material it cannot execute. That shipped: a `.demat` naming
    // the `Terrain` shader, placed in a StaticMeshComponent slot, drew with a pipeline belonging to a
    // different renderer and produced nothing at all -- no log, no refusal, no validation error -- because
    // the geometry and the uniform blocks the mesh path supplies are not the ones a terrain shader reads.
    // MeshRenderer::DrawGenericMeshes asks DrawnByMeshPath() and refuses by name.
    //
    // So: when a new domain is added, it gets a predicate here and a path that answers to it, or it is not
    // user-assignable. There is no third option in which a user may pick it and nothing draws it.

    // The domain each path rasterizes, named rather than spelled inside the predicates: a refusal has to
    // print the domain it drew AND the domain it wanted, and those two strings must come from the same
    // place the comparison does or the message can describe a rule the code is not applying.
    inline constexpr ShaderDomain kMeshPathDomain    = ShaderDomain::Surface;
    inline constexpr ShaderDomain kTerrainPathDomain = ShaderDomain::Terrain;

    constexpr bool DrawnByMeshPath( ShaderDomain domain )
    {
        return domain == kMeshPathDomain;
    }

    constexpr bool DrawnByTerrainPath( ShaderDomain domain )
    {
        return domain == kTerrainPathDomain;
    }

    struct ShaderProgramMeta
    {
        std::vector<ShaderParam> Params;
        ShaderRenderState        State;
        ShaderDomain             Domain = ShaderDomain::Unspecified;

        // Additional named passes declared by the shader (DSL `Pass "Name" { ... }` blocks).
        // Each is a separate program registered in the ShaderService as "<Shader>/<Pass>";
        // empty for legacy single-program shaders.
        std::vector<std::string> PassNames;

        bool HasParams() const
        {
            return !Params.empty();
        }

        // Can a user assign this shader to a renderable via a MaterialComponent?
        //
        // DERIVED from the per-path predicates rather than restating the list, so the two cannot drift:
        // "a user may assign it" means exactly "some path draws it". Restated by hand, this is the line
        // that would keep offering a domain after its renderer stopped answering for it -- and a draw path
        // must still ask its OWN predicate, never this one (see the note above DrawnByMeshPath).
        bool IsUserAssignable() const
        {
            return DrawnByMeshPath( Domain ) || DrawnByTerrainPath( Domain );
        }
    };

} // namespace Desert::Core::Formats
