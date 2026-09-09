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
            // APPENDED AND NEVER REORDERED, like the pin lists below: a .dgraph stores Pin::Type as this
            // integer, so inserting a value would silently retype every saved pin above it. Vec3 exists
            // because the Volume domain's contract is written in three-component quantities that are NOT
            // colours-with-alpha — a position in kilometres, an albedo, an emission per kilometre — and
            // spelling them vec4 would make "what does .w mean here" a question with no answer.
            Vec3 = 3,
        };

        // Where a graph runs (mirrors UE's Material Domain / Godot's shader Mode). The domain is the
        // single axis that picks the output node, the vertex contract and the visible palette — the
        // whole graph is parameterized by it. Stored as int on the Document for reflection-friendly
        // serialization (same reason Pin::Type is an int).
        enum class Domain : int
        {
            Surface     = 0, // lit/unlit material on scene meshes (mesh vertex + normals)
            PostProcess = 1, // full-screen effect over the rendered scene color (fullscreen triangle)
            // THE CLOUD MEDIUM — what a cloud IS at a point in space, and the one domain that compiles to
            // a program FRAGMENT rather than to a program. Its output is a `Medium { ... }` block that
            // four shipped programs are compiled against (Docs/Clouds/O1_DESIGN.md §10.3); it has no
            // vertex contract, no framebuffer and no draw of its own, because it never draws — it is
            // substituted into things that do.
            Volume = 2,
        };

        // Where the graph's OWN textures start in the descriptor set. Above every engine binding a
        // generated shader can declare (the highest are the last two cascade shadow maps at 22 and 23),
        // because the parser numbers a Properties block's textures upward from this base one at a time
        // and a collision between two GLSL declarations at one binding is silent — see the note at the
        // Properties emitter in ShaderGraph.cpp.
        constexpr unsigned kGraphTextureBinding = 24;

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

        // ---- The Volume domain's material-parameter register -------------------------------------
        //
        // WHICH OF THE CLOUD MATERIAL'S OWN PROPERTIES A GRAPH NODE MAY READ, and it is a REGISTER with a
        // reason per row rather than a list, because the interesting half is what is NOT here.
        //
        // ABOUT HALF OF THAT MATERIAL'S VALUES ARE INPUTS TO A CPU BAKE — a 256x32x256 volume over
        // several thousand cloud bodies, 3.3 to 14.1 seconds — and the graph runs on the GPU, per sample,
        // inside a march that reads the RESULT of that bake. A `Timing(Rebake)` property is therefore not
        // merely inconvenient to reach from here: it is not in the shader's scope at all, and a node
        // pretending to read one would either fail to compile or, worse, read a same-named field that
        // means something else. The split is shown to the author in the Material Editor (every property
        // states its Timing) and is made UNEXPRESSIBLE here.
        //
        // AND IT IS A TEST, NOT A CONVENTION. Desert/Tests/Editor/ShaderGraphVolumeDomain parses the
        // shipped CloudRaymarch.shader and asserts, in both directions:
        //   * every row below names a property that exists and is Timing(Immediate) — so exposing a bake
        //     input goes RED;
        //   * every Immediate property of the schema is either a row below or a row of the out-of-scope
        //     register beside it, with its reason — so a NEW property cannot be added without somebody
        //     deciding whether the graph may read it.
        // There is no hand-written name list anywhere in that suite.
        struct VolumeParam
        {
            const char* SchemaName; // the property's name in CloudRaymarch.shader's Properties block
            const char* Expression; // the GLSL it becomes inside a Medium block
            const char* Units;      // what the number IS at that point, which is not always what the panel shows
        };
        const std::vector<VolumeParam>& VolumeParams();

        /// An Immediate property the graph deliberately does NOT expose, and why. See VolumeParams().
        struct VolumeParamOutOfScope
        {
            const char* SchemaName;
            const char* Reason;
        };
        const std::vector<VolumeParamOutOfScope>& VolumeParamsOutOfScope();

        /// A Volume Output pin in which the Cloud Sample node's `ShadowRay` output means something.
        ///
        /// The medium is five functions and only TWO of them are ever called by a march that integrates
        /// optical depth — the sun quadrature, the cloud shadow map and the sky-occlusion volume all ask
        /// for a density and an extinction and nothing else. In the other three `ShadowRay` is the
        /// constant zero, so a graph branching on it there would be authoring a path that can never be
        /// taken: a dead knob, which this project's contract refuses in the same breath as a stub.
        ///
        /// Desert/Tests/Editor/ShaderGraphCompiler DERIVES this set from the shader tree — it reads which
        /// entry points the three shadow-ray marches actually call — and compares it with the rows below,
        /// so the day a shadow march starts asking for an albedo this register goes red instead of the
        /// author's branch quietly disappearing.
        struct ShadowRayScope
        {
            const char* OutputPin;  // the Volume Output input the medium function is compiled from
            const char* EntryPoint; // the GLSL function a shadow-ray march calls
        };
        const std::vector<ShadowRayScope>& ShadowRayScopes();

        // Node kind that terminates a graph in the given domain (SurfaceOutput / PostProcessOutput).
        const char* OutputKind( Domain domain );

        // Is this node kind available in the given domain?
        bool SpecInDomain( const NodeSpec& spec, Domain domain );

        // Instantiate a node of the given kind (allocates pin ids from doc.NextId).
        Node MakeNode( Document& doc, const std::string& kind );

        // Compile the graph to DShader source. Errors (no Surface Output, cycles, bad param
        // names) come back as the error string.
        Common::ResultStr<std::string> CompileToDShader( const Document& doc );

        // Bring every node in @p doc up to the CURRENT catalogue by appending the pins its kind has
        // grown since the document was written, and return how many pins were appended.
        //
        // Pins are stored positionally in a .dgraph and links reference them by id, so a pin that is
        // APPENDED changes nothing that already exists: index 0..n-1 keep their meaning and every
        // saved link still lands where it landed. That is also the limit of what can be repaired
        // here — a node whose stored pins are not a PREFIX of the catalogue's (a renamed pin, a
        // changed type, a reorder) is left exactly as it is, so ValidateGraph rejects it by name
        // instead of this function quietly rewriting the artist's graph into something else.
        //
        // Pure: takes a document, returns a document, touches no file and no global state.
        int MigrateToCatalogue( Document& doc );

        // A document as it came off disk, plus what had to change to make it current. Deserialize
        // hands back both TOGETHER and not a bare Document, because a silent migration is the thing
        // the contract forbids: the caller cannot be given the new document without also being told
        // how much of it is new.
        struct Loaded
        {
            Document Doc;
            int      MigratedPins = 0;
        };

        // .dgraph (JSON) round-trip.
        std::string               Serialize( const Document& doc );
        Common::ResultStr<Loaded> Deserialize( const std::string& json );
    } // namespace ShaderGraph
} // namespace Desert::Editor
