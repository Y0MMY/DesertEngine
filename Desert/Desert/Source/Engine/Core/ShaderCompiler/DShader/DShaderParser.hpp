#pragma once

// Desert Shader Language (DSL) — a single-file, ShaderLab-style shader format.
//
// A .shader file in this format holds everything a shader program needs: editor-facing
// properties, render state, and the GLSL for every stage. Example:
//
//     Shader "Unlit"
//     {
//         Domain Surface
//
//         // Binding(n) opts into auto-generation of the MaterialUB uniform block from the
//         // numeric properties; TextureBinding(m) assigns sequential bindings to the texture
//         // properties. Omit both to declare GLSL resources by hand (metadata-only mode).
//         Properties Binding(1) TextureBinding(2)
//         {
//             Color     Color       ("Color")                    = (0.8, 0.4, 0.1, 1)
//             Float     Tiling      ("Tiling", Range(0.25, 64))  = 4
//             Texture2D u_AlbedoTex ("Albedo")                   = "white"
//         }
//
//         State
//         {
//             Cull Back        // None | Front | Back | FrontAndBack
//             ZTest LEqual     // Never|Less|Equal|LEqual|Greater|NotEqual|GEqual|Always | Off
//             ZWrite On
//             Blend Off        // Off | On | Alpha (alias of On)
//             Topology Triangles   // Triangles | Lines | Points | Patches <n>
//         }
//
//         Include  { /* GLSL inserted into every stage (after the generated header) */ }
//
//         Vertex   { /* GLSL */ }
//         Fragment { /* GLSL */ }
//         // Also: Compute, TessControl, TessEval
//
//         // Additional named passes (e.g. a depth-only shadow variant). Each pass has its own
//         // stages and may override individual State settings; everything else (Properties,
//         // Include, the auto-generated resources) is shared with the whole file. A pass is a
//         // separate shader program, addressable as "<ShaderName>/<PassName>".
//         Pass "Shadow"
//         {
//             State  { Cull Front }
//             Vertex { /* GLSL */ }
//         }
//     }
//
// The parser emits the same structures the legacy `#pragma` format produces
// (ShaderProgramMeta + per-stage GLSL), so everything downstream — shaderc, SPIR-V
// reflection, DataDrivenMaterial, the material UI — works unchanged. Emitted GLSL contains
// #line directives so shaderc errors point at the original .shader lines.

#include <Engine/Core/Formats/Shader.hpp>
#include <Engine/Core/Formats/ShaderProgramMeta.hpp>
#include <Common/Core/ResultStr.hpp>

#include <string>
#include <unordered_map>
#include <vector>

namespace Desert::Core::Preprocess
{
    struct DShaderPass
    {
        std::string                                                 Name;  // "" for the default pass
        Core::Formats::ShaderRenderState                            State; // file-level State + pass overrides
        std::unordered_map<Core::Formats::ShaderStage, std::string> Stages;
    };

    struct DShaderParseResult
    {
        std::string                                                 Name;
        Core::Formats::ShaderProgramMeta                            Meta;   // default pass meta (+ PassNames)
        std::unordered_map<Core::Formats::ShaderStage, std::string> Stages; // default pass GLSL
        std::vector<DShaderPass>                                    Passes; // all passes; [0] is the default

        // nullptr when the pass doesn't exist. Empty name = the default pass.
        const DShaderPass* FindPass( const std::string& name ) const
        {
            for ( const auto& p : Passes )
                if ( p.Name == name )
                    return &p;
            return nullptr;
        }
    };

    class DShaderParser
    {
    public:
        // True when the source is written in the DSL (first meaningful token is `Shader`);
        // false for the legacy `#pragma use_stage` format.
        static bool IsDShader( const std::string& source );

        // Errors carry the source line: `DShader parse error (line N): message`.
        static Common::ResultStr<DShaderParseResult> Parse( const std::string& source );
    };
} // namespace Desert::Core::Preprocess
