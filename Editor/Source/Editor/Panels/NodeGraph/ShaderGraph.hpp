#pragma once

#include <Common/Core/ResultStr.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Desert::Editor
{
    // The shader-graph DOCUMENT: a plain serializable model (rfl::json <-> .dgraph) plus the
    // compiler that turns it into a Desert Shader Language (.shader) source. The panel owns the
    // interactive canvas; this file owns the semantics.
    namespace ShaderGraph
    {
        enum class ValueType : int
        {
            Float = 0,
            Vec2  = 1,
            Color = 2, // vec4
        };

        // Where a graph runs (mirrors UE's Material Domain / Godot's shader Mode). The domain is the
        // single axis that picks the output node, the vertex contract and the visible palette — the
        // whole graph is parameterized by it. Stored as int on the Document for reflection-friendly
        // serialization (same reason Pin::Type is an int).
        enum class Domain : int
        {
            Surface     = 0, // lit/unlit material on scene meshes (mesh vertex + normals)
            PostProcess = 1, // full-screen effect over the rendered scene color (fullscreen triangle)
        };

        // Bit for one domain; a NodeSpec lists the domains it belongs to as a mask.
        constexpr unsigned DomainBit( Domain d )
        {
            return 1u << static_cast<int>( d );
        }
        // "Core" nodes (math, Time, textures, params) live in every domain.
        constexpr unsigned AllDomains = ~0u;

        struct Pin
        {
            uint64_t    Id   = 0;
            std::string Name;
            int         Type = 0; // ValueType (int for reflection-friendly serialization)
        };

        // Node semantics are identified by Kind (see NodeSpecs in ShaderGraph.cpp):
        //   SurfaceOutput | PostProcessOutput | SceneColor | TextureSample | ColorParam | FloatParam |
        //   ColorConst | FloatConst | UV | TileUV | Multiply | Scale | Add | Lerp | OneMinus | MultiplyFloat
        struct Node
        {
            uint64_t             Id = 0;
            std::string          Kind;
            std::string          ParamName;          // TextureSample / *Param nodes: exposed property name
            std::array<float, 4> Value = { 1, 1, 1, 1 }; // *Const / *Param nodes: (default) value
            float                X = 0.0f, Y = 0.0f; // canvas position (captured on save)
            std::vector<Pin>     Inputs;
            std::vector<Pin>     Outputs;
        };

        struct Link
        {
            uint64_t Id   = 0;
            uint64_t From = 0; // output pin id
            uint64_t To   = 0; // input pin id
        };

        struct Document
        {
            std::string       Name   = "GraphShader";
            uint64_t          NextId = 1;
            int               Domain = static_cast<int>( ShaderGraph::Domain::Surface ); // ShaderGraph::Domain
            bool              Lit    = false; // Surface-only: Lambert from the scene's directional light
            std::vector<Node> Nodes;
            std::vector<Link> Links;

            ShaderGraph::Domain DomainEnum() const
            {
                return static_cast<ShaderGraph::Domain>( Domain );
            }
        };

        // Static description of a node kind — drives BOTH the palette/UI and the compiler.
        struct NodeSpec
        {
            const char* Kind;
            const char* Title;
            unsigned    HeaderColor; // IM_COL32 value
            struct PinSpec
            {
                const char* Name;
                ValueType   Type;
            };
            std::vector<PinSpec> Inputs;
            std::vector<PinSpec> Outputs;
            bool                 HasParamName = false; // shows a name field, emits a Property
            bool                 HasColorValue = false; // shows a vec4 editor
            bool                 HasFloatValue = false; // shows a float editor
            unsigned             Domains = AllDomains;  // which domains this node is offered in
        };

        const std::vector<NodeSpec>& Specs();
        const NodeSpec*              FindSpec( const std::string& kind );

        // Node kind that terminates a graph in the given domain (SurfaceOutput / PostProcessOutput).
        const char* OutputKind( Domain domain );

        // Is this node kind available in the given domain?
        bool SpecInDomain( const NodeSpec& spec, Domain domain );

        // Instantiate a node of the given kind (allocates pin ids from doc.NextId).
        Node MakeNode( Document& doc, const std::string& kind );

        // Compile the graph to DShader source. Errors (no Surface Output, cycles, bad param
        // names) come back as the error string.
        Common::ResultStr<std::string> CompileToDShader( const Document& doc );

        // .dgraph (JSON) round-trip.
        std::string                  Serialize( const Document& doc );
        Common::ResultStr<Document>  Deserialize( const std::string& json );
    } // namespace ShaderGraph
} // namespace Desert::Editor
