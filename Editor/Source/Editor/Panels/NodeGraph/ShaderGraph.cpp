#include "ShaderGraph.hpp"

#include <rflcpp/rfl/json.hpp>
#include <rflcpp/rfl/DefaultIfMissing.hpp>

#include <algorithm>
#include <cctype>
#include <format>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace Desert::Editor::ShaderGraph
{
    // Header colour packed exactly like Dear ImGui's IM_COL32 (R at bit 0). Kept local so this file —
    // the graph SEMANTICS and compiler — stays free of any ImGui dependency and is unit-testable on
    // its own; the panel reads NodeSpec::HeaderColor back as an ImU32.
    static constexpr unsigned RGBA( unsigned r, unsigned g, unsigned b, unsigned a )
    {
        return ( a << 24 ) | ( b << 16 ) | ( g << 8 ) | r;
    }

    // ---------------------------------------------------------------- node catalogue ----------
    // Domain masks for the two families of nodes:
    //   CORE    — math / textures / params / Time: valid in every domain.
    //   SURFACE / POST — output nodes and domain-specific inputs, offered only in their own domain.
    static constexpr unsigned CORE    = AllDomains;
    static constexpr unsigned SURFACE = DomainBit( Domain::Surface );
    static constexpr unsigned POST    = DomainBit( Domain::PostProcess );

    const std::vector<NodeSpec>& Specs()
    {
        static const std::vector<NodeSpec> s_Specs = {
            // ---- domain-specific: outputs & special inputs ----
            { "SurfaceOutput", "Surface Output", RGBA( 150, 90, 60, 255 ),
              { { "Albedo", ValueType::Color }, { "Emission", ValueType::Color }, { "Alpha", ValueType::Float } },
              {}, false, false, false, SURFACE },
            { "PostProcessOutput", "Post Process Output", RGBA( 150, 90, 60, 255 ),
              { { "Color", ValueType::Color } }, {}, false, false, false, POST },
            { "SceneColor", "Scene Color", RGBA( 70, 110, 160, 255 ), {},
              { { "Color", ValueType::Color } }, false, false, false, POST },
            // ---- core: valid everywhere ----
            { "TextureSample", "Texture Sample", RGBA( 70, 110, 160, 255 ),
              { { "UV", ValueType::Vec2 } },
              { { "RGBA", ValueType::Color }, { "R", ValueType::Float } },
              /*param*/ true, false, false, CORE },
            { "ColorParam", "Color Param", RGBA( 160, 80, 90, 255 ), {},
              { { "Color", ValueType::Color } }, /*param*/ true, /*color*/ true, false, CORE },
            { "FloatParam", "Float Param", RGBA( 90, 140, 90, 255 ), {},
              { { "Value", ValueType::Float } }, /*param*/ true, false, /*float*/ true, CORE },
            { "ColorConst", "Color", RGBA( 120, 70, 80, 255 ), {},
              { { "Color", ValueType::Color } }, false, /*color*/ true, false, CORE },
            { "FloatConst", "Float", RGBA( 70, 110, 70, 255 ), {},
              { { "Value", ValueType::Float } }, false, false, /*float*/ true, CORE },
            { "UV", "UV", RGBA( 150, 130, 60, 255 ), {}, { { "UV", ValueType::Vec2 } },
              false, false, false, CORE },
            { "TileUV", "Tile UV", RGBA( 150, 130, 60, 255 ),
              { { "UV", ValueType::Vec2 }, { "Scale", ValueType::Float } },
              { { "UV", ValueType::Vec2 } }, false, false, false, /*mesh tiling*/ SURFACE },
            { "Multiply", "Multiply", RGBA( 90, 90, 120, 255 ),
              { { "A", ValueType::Color }, { "B", ValueType::Color } },
              { { "Out", ValueType::Color } }, false, false, false, CORE },
            { "Scale", "Scale (Color x Float)", RGBA( 90, 90, 120, 255 ),
              { { "Color", ValueType::Color }, { "Factor", ValueType::Float } },
              { { "Out", ValueType::Color } }, false, false, false, CORE },
            { "Add", "Add", RGBA( 90, 90, 120, 255 ),
              { { "A", ValueType::Color }, { "B", ValueType::Color } },
              { { "Out", ValueType::Color } }, false, false, false, CORE },
            { "Lerp", "Lerp", RGBA( 120, 90, 130, 255 ),
              { { "A", ValueType::Color }, { "B", ValueType::Color }, { "T", ValueType::Float } },
              { { "Out", ValueType::Color } }, false, false, false, CORE },
            { "OneMinus", "One Minus", RGBA( 110, 110, 110, 255 ),
              { { "In", ValueType::Color } }, { { "Out", ValueType::Color } }, false, false, false, CORE },
            { "MultiplyFloat", "Multiply (Float)", RGBA( 80, 120, 80, 255 ),
              { { "A", ValueType::Float }, { "B", ValueType::Float } },
              { { "Out", ValueType::Float } }, false, false, false, CORE },
            { "Saturate", "Saturate", RGBA( 110, 110, 110, 255 ),
              { { "In", ValueType::Color } }, { { "Out", ValueType::Color } }, false, false, false, CORE },
            { "Power", "Power", RGBA( 90, 90, 120, 255 ),
              { { "In", ValueType::Color }, { "Exp", ValueType::Float } },
              { { "Out", ValueType::Color } }, false, false, false, CORE },
            { "Sine", "Sine (Float)", RGBA( 80, 120, 80, 255 ),
              { { "In", ValueType::Float } }, { { "Out", ValueType::Float } }, false, false, false, CORE },
            { "Time", "Time", RGBA( 60, 140, 150, 255 ), {},
              { { "Seconds", ValueType::Float } }, false, false, false, CORE },
        };
        return s_Specs;
    }

    const NodeSpec* FindSpec( const std::string& kind )
    {
        for ( const auto& spec : Specs() )
            if ( kind == spec.Kind )
                return &spec;
        return nullptr;
    }

    const char* OutputKind( Domain domain )
    {
        return domain == Domain::PostProcess ? "PostProcessOutput" : "SurfaceOutput";
    }

    bool SpecInDomain( const NodeSpec& spec, Domain domain )
    {
        return ( spec.Domains & DomainBit( domain ) ) != 0;
    }

    Node MakeNode( Document& doc, const std::string& kind )
    {
        const NodeSpec* spec = FindSpec( kind );
        Node            node;
        node.Id   = doc.NextId++;
        node.Kind = kind;
        if ( spec )
        {
            for ( const auto& pin : spec->Inputs )
                node.Inputs.push_back( { doc.NextId++, pin.Name, static_cast<int>( pin.Type ) } );
            for ( const auto& pin : spec->Outputs )
                node.Outputs.push_back( { doc.NextId++, pin.Name, static_cast<int>( pin.Type ) } );
            if ( spec->HasParamName )
                node.ParamName = spec->HasColorValue ? "Tint" : ( spec->HasFloatValue ? "Amount" : "u_Texture" );
            if ( spec->HasFloatValue )
                node.Value = { 1, 0, 0, 0 };
        }
        return node;
    }

    // ---------------------------------------------------------------- compiler ----------------
    namespace
    {
        struct Compiler
        {
            const Document&                                doc;
            std::unordered_map<uint64_t, const Node*>      nodeByPin;   // any pin id -> node
            std::unordered_map<uint64_t, uint64_t>         linkIntoPin; // input pin id -> source OUTPUT pin id
            std::unordered_map<const Node*, std::string>   varOf;       // node -> emitted variable
            std::unordered_map<const Node*, int>           state;       // 0=unvisited 1=visiting 2=done
            std::ostringstream                             body;
            std::string                                    error;
            int                                            nextVar = 0;

            explicit Compiler( const Document& d ) : doc( d )
            {
                for ( const auto& node : doc.Nodes )
                {
                    for ( const auto& pin : node.Inputs )
                        nodeByPin[pin.Id] = &node;
                    for ( const auto& pin : node.Outputs )
                        nodeByPin[pin.Id] = &node;
                }
                for ( const auto& link : doc.Links )
                    linkIntoPin[link.To] = link.From;
            }

            static std::string Lit( float v )
            {
                return std::format( "{:.6g}", v ).find( '.' ) == std::string::npos &&
                               std::format( "{:.6g}", v ).find( 'e' ) == std::string::npos
                            ? std::format( "{:.1f}", v )
                            : std::format( "{:.6g}", v );
            }

            static std::string Vec4Lit( const std::array<float, 4>& v )
            {
                return std::format( "vec4( {}, {}, {}, {} )", Lit( v[0] ), Lit( v[1] ), Lit( v[2] ),
                                    Lit( v[3] ) );
            }

            // Expression feeding @p inputPin of @p node, or the type's default when unlinked.
            std::string InputExpr( const Node& node, size_t inputIndex, const char* fallback )
            {
                const Pin& pin = node.Inputs[inputIndex];
                auto       it  = linkIntoPin.find( pin.Id );
                if ( it == linkIntoPin.end() )
                    return fallback;

                const Node* src = nodeByPin.count( it->second ) ? nodeByPin.at( it->second ) : nullptr;
                if ( !src )
                    return fallback;
                const std::string var = EmitNode( *src );
                if ( !error.empty() )
                    return fallback;

                // Multi-output nodes: pick the component for the linked pin.
                if ( src->Kind == "TextureSample" && src->Outputs.size() == 2 &&
                     it->second == src->Outputs[1].Id )
                    return var + ".r";
                return var;
            }

            // Emits the node's statement once; returns its variable name.
            std::string EmitNode( const Node& node )
            {
                if ( auto it = varOf.find( &node ); it != varOf.end() )
                    return it->second;
                if ( state[&node] == 1 )
                {
                    error = std::format( "cycle detected at node '{}'", node.Kind );
                    return "vec4(0)";
                }
                state[&node] = 1;

                const std::string var  = std::format( "n{}", nextVar++ );
                std::string       decl;

                if ( node.Kind == "TextureSample" )
                {
                    const std::string uv = InputExpr( node, 0, "v_UV" );
                    decl = std::format( "vec4 {} = texture( {}, {} );", var, node.ParamName, uv );
                }
                else if ( node.Kind == "ColorParam" || node.Kind == "FloatParam" )
                {
                    const char* type = node.Kind == "ColorParam" ? "vec4" : "float";
                    decl = std::format( "{} {} = u_Material.{};", type, var, node.ParamName );
                }
                else if ( node.Kind == "ColorConst" )
                    decl = std::format( "vec4 {} = {};", var, Vec4Lit( node.Value ) );
                else if ( node.Kind == "FloatConst" )
                    decl = std::format( "float {} = {};", var, Lit( node.Value[0] ) );
                else if ( node.Kind == "SceneColor" )
                    decl = std::format( "vec4 {} = texture( u_SceneTexture, v_UV );", var );
                else if ( node.Kind == "UV" )
                    decl = std::format( "vec2 {} = v_UV;", var );
                else if ( node.Kind == "TileUV" )
                    decl = std::format( "vec2 {} = {} * {};", var, InputExpr( node, 0, "v_UV" ),
                                        InputExpr( node, 1, "1.0" ) );
                else if ( node.Kind == "Multiply" )
                    decl = std::format( "vec4 {} = {} * {};", var, InputExpr( node, 0, "vec4( 1.0 )" ),
                                        InputExpr( node, 1, "vec4( 1.0 )" ) );
                else if ( node.Kind == "Scale" )
                    decl = std::format( "vec4 {} = {} * {};", var, InputExpr( node, 0, "vec4( 1.0 )" ),
                                        InputExpr( node, 1, "1.0" ) );
                else if ( node.Kind == "Add" )
                    decl = std::format( "vec4 {} = {} + {};", var, InputExpr( node, 0, "vec4( 0.0 )" ),
                                        InputExpr( node, 1, "vec4( 0.0 )" ) );
                else if ( node.Kind == "Lerp" )
                    decl = std::format( "vec4 {} = mix( {}, {}, {} );", var,
                                        InputExpr( node, 0, "vec4( 0.0 )" ),
                                        InputExpr( node, 1, "vec4( 1.0 )" ), InputExpr( node, 2, "0.5" ) );
                else if ( node.Kind == "OneMinus" )
                    decl = std::format( "vec4 {} = vec4( 1.0 ) - {};", var,
                                        InputExpr( node, 0, "vec4( 0.0 )" ) );
                else if ( node.Kind == "MultiplyFloat" )
                    decl = std::format( "float {} = {} * {};", var, InputExpr( node, 0, "1.0" ),
                                        InputExpr( node, 1, "1.0" ) );
                else if ( node.Kind == "Saturate" )
                    decl = std::format( "vec4 {} = clamp( {}, vec4( 0.0 ), vec4( 1.0 ) );", var,
                                        InputExpr( node, 0, "vec4( 0.0 )" ) );
                else if ( node.Kind == "Power" )
                    decl = std::format( "vec4 {} = pow( max( {}, vec4( 0.0 ) ), vec4( {} ) );", var,
                                        InputExpr( node, 0, "vec4( 0.0 )" ), InputExpr( node, 1, "1.0" ) );
                else if ( node.Kind == "Sine" )
                    decl = std::format( "float {} = sin( {} );", var, InputExpr( node, 0, "0.0" ) );
                else if ( node.Kind == "Time" )
                    decl = std::format( "float {} = timeUB.TimeData.x;", var );
                else
                {
                    // ValidateGraph has already rejected kinds that are not in the catalogue, so
                    // reaching here means the OPPOSITE: a NodeSpec was added without an emitter
                    // branch. Saying so beats emitting a silent black.
                    error = std::format( "node kind '{}' is in the palette but has no compiler rule",
                                         node.Kind );
                    decl  = std::format( "vec4 {} = vec4( 0.0 );", var );
                }

                body << "            " << decl << "\n";
                varOf[&node] = var;
                state[&node] = 2;
                return var;
            }
        };

        bool IsValidIdentifier( const std::string& s )
        {
            if ( s.empty() || ( !std::isalpha( (unsigned char)s[0] ) && s[0] != '_' ) )
                return false;
            return std::all_of( s.begin(), s.end(),
                                []( unsigned char c ) { return std::isalnum( c ) || c == '_'; } );
        }

        // ------------------------------------------------------------ validation ---------------
        // The GLSL type a pin carries. This is the whole point of the checks below: the compiler
        // emits `float`/`vec2`/`vec4` declarations straight from the node kind, so two pins that
        // disagree here become a GLSL type error in generated code.
        const char* GlslTypeName( ValueType t )
        {
            switch ( t )
            {
                case ValueType::Float:
                    return "float";
                case ValueType::Vec2:
                    return "vec2";
                case ValueType::Color:
                    return "vec4";
            }
            return "<unknown>";
        }

        const char* DomainName( Domain d )
        {
            return d == Domain::PostProcess ? "Post Process" : "Surface";
        }

        // How the node reads on the canvas — its palette title, plus the parameter name when it has
        // one. Every diagnostic below is phrased in these terms so the artist is pointed at a node
        // they can see and click, not at a line of a file they never wrote.
        std::string NodeLabel( const Node& node )
        {
            const NodeSpec*   spec  = FindSpec( node.Kind );
            const std::string title = spec ? spec->Title : node.Kind;
            return node.ParamName.empty() ? std::format( "'{}'", title )
                                          : std::format( "'{}' ('{}')", title, node.ParamName );
        }

        // Where a pin id lives. Built once so a link can be resolved to (node, side, index) and
        // reported by name.
        struct PinRef
        {
            const Node* Owner = nullptr;
            bool        Input = false;
            size_t      Index = 0;
            ValueType   Type  = ValueType::Float;
        };

        // Full structural + type check of a graph document, run BEFORE a single line is emitted.
        //
        // The canvas already refuses a mismatched link while the artist drags it (NodeGraphPanel's
        // ed::QueryNewLink compares Pin::Type), but a .dgraph is plain JSON: one written by hand, by
        // a script, or by an older build reaches the compiler with none of that enforcement. Until
        // this function existed such a graph compiled happily and the mismatch surfaced from shaderc
        // as e.g. "MatBroken.shader:25: error: '=' : cannot convert from 'vec2' to 'vec4'" — a line
        // number in GENERATED code, naming neither the node nor the link that caused it.
        //
        // Node identity only exists at this level; by the time the text is emitted the nodes have
        // become n0, n1, n2. So this is the last place an error can name what the artist drew.
        //
        // Returns an empty string when the document is well-formed.
        std::string ValidateGraph( const Document& doc, Domain domain )
        {
            // ---- nodes: known kind, offered in this domain, pins agreeing with the catalogue ----
            for ( const auto& node : doc.Nodes )
            {
                const NodeSpec* spec = FindSpec( node.Kind );
                if ( !spec )
                    return std::format( "node id {} has unknown kind '{}'", node.Id, node.Kind );

                if ( !SpecInDomain( *spec, domain ) )
                    return std::format( "node {} is not available in the {} domain", NodeLabel( node ),
                                        DomainName( domain ) );

                // A node whose pin list disagrees with its kind is the crash case, not just a bad
                // message: the emitter indexes node.Inputs[i] positionally for every kind it knows,
                // so a hand-trimmed "Inputs": [] on a Multiply used to read off the end of the vector.
                if ( node.Inputs.size() != spec->Inputs.size() ||
                     node.Outputs.size() != spec->Outputs.size() )
                    return std::format(
                         "node {} has {} input(s) and {} output(s), but kind '{}' declares {} and {}",
                         NodeLabel( node ), node.Inputs.size(), node.Outputs.size(), node.Kind,
                         spec->Inputs.size(), spec->Outputs.size() );

                // Pin::Type in the file is a serialized MIRROR of the catalogue, and the canvas
                // type-checks against that mirror. If the two disagree the file can make the canvas
                // accept a link the compiler cannot emit, so the mirror is checked rather than trusted.
                for ( size_t i = 0; i < node.Inputs.size(); ++i )
                    if ( node.Inputs[i].Type != static_cast<int>( spec->Inputs[i].Type ) )
                        return std::format(
                             "node {}: input '{}' is stored as {} but kind '{}' declares it {}",
                             NodeLabel( node ), node.Inputs[i].Name,
                             GlslTypeName( static_cast<ValueType>( node.Inputs[i].Type ) ), node.Kind,
                             GlslTypeName( spec->Inputs[i].Type ) );

                for ( size_t i = 0; i < node.Outputs.size(); ++i )
                    if ( node.Outputs[i].Type != static_cast<int>( spec->Outputs[i].Type ) )
                        return std::format(
                             "node {}: output '{}' is stored as {} but kind '{}' declares it {}",
                             NodeLabel( node ), node.Outputs[i].Name,
                             GlslTypeName( static_cast<ValueType>( node.Outputs[i].Type ) ), node.Kind,
                             GlslTypeName( spec->Outputs[i].Type ) );
            }

            // ---- pin index, rejecting duplicate ids ----
            std::unordered_map<uint64_t, PinRef> pins;
            for ( const auto& node : doc.Nodes )
            {
                const NodeSpec* spec = FindSpec( node.Kind );
                const auto      add  = [&]( const Pin& pin, bool input, size_t index ) -> std::string
                {
                    if ( pin.Id == 0 )
                        return std::format( "node {}: pin '{}' has no id", NodeLabel( node ), pin.Name );
                    const ValueType type = input ? spec->Inputs[index].Type : spec->Outputs[index].Type;
                    auto [it, fresh] = pins.emplace( pin.Id, PinRef{ &node, input, index, type } );
                    if ( !fresh )
                        return std::format( "pin id {} is used by both node {} and node {}", pin.Id,
                                            NodeLabel( *it->second.Owner ), NodeLabel( node ) );
                    return {};
                };
                for ( size_t i = 0; i < node.Inputs.size(); ++i )
                    if ( std::string err = add( node.Inputs[i], true, i ); !err.empty() )
                        return err;
                for ( size_t i = 0; i < node.Outputs.size(); ++i )
                    if ( std::string err = add( node.Outputs[i], false, i ); !err.empty() )
                        return err;
            }

            // ---- links: both ends real, output -> input, one link per input, types equal ----
            std::unordered_set<uint64_t> takenInputs;
            for ( const auto& link : doc.Links )
            {
                auto from = pins.find( link.From );
                auto to   = pins.find( link.To );
                if ( from == pins.end() )
                    return std::format( "link {} starts at pin id {}, which no node owns", link.Id,
                                        link.From );
                if ( to == pins.end() )
                    return std::format( "link {} ends at pin id {}, which no node owns", link.Id, link.To );

                if ( from->second.Input )
                    return std::format( "link {} starts at input '{}' of node {} — a link must start at "
                                        "an output",
                                        link.Id, from->second.Owner->Inputs[from->second.Index].Name,
                                        NodeLabel( *from->second.Owner ) );
                if ( !to->second.Input )
                    return std::format( "link {} ends at output '{}' of node {} — a link must end at an "
                                        "input",
                                        link.Id, to->second.Owner->Outputs[to->second.Index].Name,
                                        NodeLabel( *to->second.Owner ) );

                // Two links into one input: the emitter keeps whichever the map saw last, so the
                // artist's picture and the generated code disagree with nothing to show for it.
                if ( !takenInputs.insert( link.To ).second )
                    return std::format( "input '{}' of node {} has more than one link into it",
                                        to->second.Owner->Inputs[to->second.Index].Name,
                                        NodeLabel( *to->second.Owner ) );

                // The rule is exactly the canvas's: equal types, no implicit conversion. Stating it
                // twice in two places is the risk here, so both sides compare the SAME catalogue
                // types — the canvas via Pin::Type, checked against the catalogue above.
                if ( from->second.Type != to->second.Type )
                    return std::format(
                         "cannot link output '{}' of node {} ({}) into input '{}' of node {} ({}): "
                         "types do not match",
                         from->second.Owner->Outputs[from->second.Index].Name,
                         NodeLabel( *from->second.Owner ), GlslTypeName( from->second.Type ),
                         to->second.Owner->Inputs[to->second.Index].Name,
                         NodeLabel( *to->second.Owner ), GlslTypeName( to->second.Type ) );
            }

            return {};
        }
    } // namespace

    Common::ResultStr<std::string> CompileToDShader( const Document& doc )
    {
        if ( !IsValidIdentifier( doc.Name ) )
            return Common::MakeError<std::string>(
                 std::format( "'{}' is not a valid shader name (letters/digits/underscore)", doc.Name ) );

        const Domain      domain   = doc.DomainEnum();
        const char* const outKind  = OutputKind( domain );
        const char* const outTitle = domain == Domain::PostProcess ? "Post Process Output" : "Surface Output";

        // Structure and types first: everything below indexes pins positionally and emits typed GLSL
        // declarations, both of which assume a well-formed document.
        if ( std::string err = ValidateGraph( doc, domain ); !err.empty() )
            return Common::MakeError<std::string>( std::move( err ) );

        const Node* output = nullptr;
        for ( const auto& node : doc.Nodes )
        {
            if ( node.Kind != outKind )
                continue;
            if ( output )
                return Common::MakeError<std::string>(
                     std::format( "graph has more than one {}", outTitle ) );
            output = &node;
        }
        if ( !output )
            return Common::MakeError<std::string>( std::format( "graph needs a {} node", outTitle ) );

        // Exposed properties: dedupe by name, validate identifiers.
        std::vector<const Node*> textures, colorParams, floatParams;
        {
            std::unordered_set<std::string> seen;
            for ( const auto& node : doc.Nodes )
            {
                const NodeSpec* spec = FindSpec( node.Kind );
                if ( !spec || !spec->HasParamName )
                    continue;
                if ( !IsValidIdentifier( node.ParamName ) )
                    return Common::MakeError<std::string>(
                         std::format( "'{}' is not a valid parameter name", node.ParamName ) );
                if ( !seen.insert( node.ParamName ).second )
                    continue; // same param used twice = same property, fine
                if ( node.Kind == "TextureSample" )
                    textures.push_back( &node );
                else if ( node.Kind == "ColorParam" )
                    colorParams.push_back( &node );
                else if ( node.Kind == "FloatParam" )
                    floatParams.push_back( &node );
            }
        }

        Compiler compiler( doc );
        std::string albedo, emission, alpha, sceneOut;
        if ( domain == Domain::PostProcess )
        {
            sceneOut = compiler.InputExpr( *output, 0, "vec4( 0.0 )" );
        }
        else
        {
            albedo   = compiler.InputExpr( *output, 0, "vec4( 0.8, 0.8, 0.8, 1.0 )" );
            emission = compiler.InputExpr( *output, 1, "vec4( 0.0 )" );
            alpha    = compiler.InputExpr( *output, 2, "1.0" );
        }
        if ( !compiler.error.empty() )
            return Common::MakeError<std::string>( compiler.error );

        const bool usesTime = std::any_of( doc.Nodes.begin(), doc.Nodes.end(),
                                           []( const Node& n ) { return n.Kind == "Time"; } );

        std::ostringstream out;
        out << "// GENERATED by the Desert Shader Graph editor — edit the .dgraph, not this file.\n";
        out << "Shader \"" << doc.Name << "\"\n{\n    Domain "
            << ( domain == Domain::PostProcess ? "PostProcess" : "Surface" ) << "\n\n";

        // Exposed properties block — shared across domains (post-process effects can expose params too).
        // Scene texture (post-process) sits at set 0 / binding 0, so params start at Binding(1).
        if ( !textures.empty() || !colorParams.empty() || !floatParams.empty() )
        {
            out << "    Properties";
            if ( !colorParams.empty() || !floatParams.empty() )
                out << " Binding(1)";
            if ( !textures.empty() )
                out << " TextureBinding(2)";
            out << "\n    {\n";
            for ( const auto* n : colorParams )
                out << std::format( "        Color     {} (\"{}\") = ({}, {}, {}, {})\n", n->ParamName,
                                    n->ParamName, Compiler::Lit( n->Value[0] ), Compiler::Lit( n->Value[1] ),
                                    Compiler::Lit( n->Value[2] ), Compiler::Lit( n->Value[3] ) );
            for ( const auto* n : floatParams )
                out << std::format( "        Float     {} (\"{}\") = {}\n", n->ParamName, n->ParamName,
                                    Compiler::Lit( n->Value[0] ) );
            for ( const auto* n : textures )
                out << std::format( "        Texture2D {} (\"{}\")\n", n->ParamName, n->ParamName );
            out << "    }\n\n";
        }

        // ---------------------------------------------------------- PostProcess domain ------------
        // Full-screen triangle over the rendered scene color; no mesh, no normals, no depth pass.
        if ( domain == Domain::PostProcess )
        {
            out << "    State\n    {\n        Cull None\n        ZTest Always\n        ZWrite Off\n    }\n\n";

            out << "    Vertex\n    {\n";
            out << "        #include <Common/QuadPositions.glslh>\n";
            out << "        #include <Common/QuadTextureCoords.glslh>\n\n";
            out << "        layout( location = 0 ) out vec2 v_UV;\n\n";
            out << "        void main()\n        {\n";
            out << "            v_UV        = QUAD_TEXTURE_COORDINATES[gl_VertexIndex];\n";
            out << "            gl_Position = vec4( QUAD_POSITIONS[gl_VertexIndex], 0.0, 1.0 );\n";
            out << "        }\n    }\n\n";

            out << "    Fragment\n    {\n";
            out << "        layout( location = 0 ) in vec2 v_UV;\n";
            out << "        layout( location = 0 ) out vec4 o_Color;\n";
            out << "        layout( set = 0, binding = 0 ) uniform sampler2D u_SceneTexture;\n";
            if ( usesTime )
                out << "\n        #include <Common/TimeUB.glslh>\n";
            out << "\n        void main()\n        {\n";
            out << compiler.body.str();
            out << std::format( "            o_Color = {};\n", sceneOut );
            out << "        }\n    }\n";
            out << "}\n";
            return Common::MakeSuccess( out.str() );
        }

        // ---------------------------------------------------------- Surface domain ----------------
        out << "    State\n    {\n        Cull Back\n        ZTest LEqual\n        ZWrite On\n    }\n\n";

        // NO GLSL boilerplate lives in this compiler: the vertex contract and the engine-filled UB
        // declarations are shared .glslh includes (Resources/Shaders/Common/), configured with
        // defines — hand-written shaders reuse the same files. The generated file only contains the
        // structure and the graph's own fragment expressions.
        out << "    Vertex\n    {\n";
        if ( doc.Lit )
            out << "        #define GRAPH_LIT 1\n";
        out << "        #include <Common/GraphVertex.glslh>\n";
        out << "    }\n\n";

        out << "    Fragment\n    {\n";
        out << "        layout( location = 0 ) in vec2 v_UV;\n";
        if ( doc.Lit )
            out << "        layout( location = 1 ) in vec3 v_Normal;\n";
        out << "        layout( location = 0 ) out vec4 o_Color;\n";
        if ( usesTime )
            out << "\n        #include <Common/TimeUB.glslh>\n";
        if ( doc.Lit )
            out << "\n        #include <Common/DirectionLightsUB.glslh>\n";
        out << "\n        void main()\n        {\n";
        out << compiler.body.str();
        out << std::format( "            vec4 albedo = {};\n", albedo );
        if ( doc.Lit )
        {
            out << "            vec3 N = normalize( v_Normal );\n";
            out << "            vec3 L = normalize( -directionLights.directionLights.Direction.xyz );\n";
            out << "            vec3 lightCol = directionLights.directionLights.ColorIntensity.rgb *\n"
                   "                            directionLights.directionLights.ColorIntensity.a;\n";
            out << "            vec3 shaded = albedo.rgb * ( vec3( 0.12 ) + max( dot( N, L ), 0.0 ) * "
                   "lightCol );\n";
            out << std::format(
                 "            o_Color = vec4( shaded + ( {} ).rgb, albedo.a * ( {} ) );\n", emission,
                 alpha );
        }
        else
        {
            out << std::format(
                 "            o_Color = vec4( albedo.rgb + ( {} ).rgb, albedo.a * ( {} ) );\n", emission,
                 alpha );
        }
        out << "        }\n    }\n\n";

        // Depth-only variant for shadow rendering, addressable as "<Name>/Depth".
        out << "    Pass \"Depth\"\n    {\n";
        out << "        State\n        {\n            Cull Front\n        }\n\n";
        out << "        Vertex\n        {\n";
        out << "            #define GRAPH_DEPTH_ONLY 1\n";
        out << "            #include <Common/GraphVertex.glslh>\n";
        out << "        }\n    }\n";
        out << "}\n";

        return Common::MakeSuccess( out.str() );
    }

    // ---------------------------------------------------------------- serialization -----------
    std::string Serialize( const Document& doc )
    {
        return rfl::json::write( doc );
    }

    Common::ResultStr<Document> Deserialize( const std::string& json )
    {
        auto parsed = rfl::json::read<Document, rfl::DefaultIfMissing>( json );
        if ( !parsed )
            return Common::MakeError<Document>( std::format( "bad .dgraph: {}", parsed.error().what() ) );
        return Common::MakeSuccess( parsed.value() );
    }
} // namespace Desert::Editor::ShaderGraph
