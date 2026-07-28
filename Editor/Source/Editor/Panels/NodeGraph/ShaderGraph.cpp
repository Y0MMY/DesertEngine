#include "ShaderGraph.hpp"

#include <ImGui/imgui.h>

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
    // ---------------------------------------------------------------- node catalogue ----------
    const std::vector<NodeSpec>& Specs()
    {
        static const std::vector<NodeSpec> s_Specs = {
            { "SurfaceOutput", "Surface Output", IM_COL32( 150, 90, 60, 255 ),
              { { "Albedo", ValueType::Color }, { "Emission", ValueType::Color }, { "Alpha", ValueType::Float } },
              {} },
            { "TextureSample", "Texture Sample", IM_COL32( 70, 110, 160, 255 ),
              { { "UV", ValueType::Vec2 } },
              { { "RGBA", ValueType::Color }, { "R", ValueType::Float } },
              /*param*/ true },
            { "ColorParam", "Color Param", IM_COL32( 160, 80, 90, 255 ), {},
              { { "Color", ValueType::Color } }, /*param*/ true, /*color*/ true },
            { "FloatParam", "Float Param", IM_COL32( 90, 140, 90, 255 ), {},
              { { "Value", ValueType::Float } }, /*param*/ true, false, /*float*/ true },
            { "ColorConst", "Color", IM_COL32( 120, 70, 80, 255 ), {},
              { { "Color", ValueType::Color } }, false, /*color*/ true },
            { "FloatConst", "Float", IM_COL32( 70, 110, 70, 255 ), {},
              { { "Value", ValueType::Float } }, false, false, /*float*/ true },
            { "UV", "UV", IM_COL32( 150, 130, 60, 255 ), {}, { { "UV", ValueType::Vec2 } } },
            { "TileUV", "Tile UV", IM_COL32( 150, 130, 60, 255 ),
              { { "UV", ValueType::Vec2 }, { "Scale", ValueType::Float } },
              { { "UV", ValueType::Vec2 } } },
            { "Multiply", "Multiply", IM_COL32( 90, 90, 120, 255 ),
              { { "A", ValueType::Color }, { "B", ValueType::Color } },
              { { "Out", ValueType::Color } } },
            { "Scale", "Scale (Color x Float)", IM_COL32( 90, 90, 120, 255 ),
              { { "Color", ValueType::Color }, { "Factor", ValueType::Float } },
              { { "Out", ValueType::Color } } },
            { "Add", "Add", IM_COL32( 90, 90, 120, 255 ),
              { { "A", ValueType::Color }, { "B", ValueType::Color } },
              { { "Out", ValueType::Color } } },
            { "Lerp", "Lerp", IM_COL32( 120, 90, 130, 255 ),
              { { "A", ValueType::Color }, { "B", ValueType::Color }, { "T", ValueType::Float } },
              { { "Out", ValueType::Color } } },
            { "OneMinus", "One Minus", IM_COL32( 110, 110, 110, 255 ),
              { { "In", ValueType::Color } }, { { "Out", ValueType::Color } } },
            { "MultiplyFloat", "Multiply (Float)", IM_COL32( 80, 120, 80, 255 ),
              { { "A", ValueType::Float }, { "B", ValueType::Float } },
              { { "Out", ValueType::Float } } },
            { "Saturate", "Saturate", IM_COL32( 110, 110, 110, 255 ),
              { { "In", ValueType::Color } }, { { "Out", ValueType::Color } } },
            { "Power", "Power", IM_COL32( 90, 90, 120, 255 ),
              { { "In", ValueType::Color }, { "Exp", ValueType::Float } },
              { { "Out", ValueType::Color } } },
            { "Sine", "Sine (Float)", IM_COL32( 80, 120, 80, 255 ),
              { { "In", ValueType::Float } }, { { "Out", ValueType::Float } } },
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
                else
                {
                    error = std::format( "unknown node kind '{}'", node.Kind );
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
    } // namespace

    Common::ResultStr<std::string> CompileToDShader( const Document& doc )
    {
        if ( !IsValidIdentifier( doc.Name ) )
            return Common::MakeError<std::string>(
                 std::format( "'{}' is not a valid shader name (letters/digits/underscore)", doc.Name ) );

        const Node* output = nullptr;
        for ( const auto& node : doc.Nodes )
        {
            if ( node.Kind != "SurfaceOutput" )
                continue;
            if ( output )
                return Common::MakeError<std::string>( "graph has more than one Surface Output" );
            output = &node;
        }
        if ( !output )
            return Common::MakeError<std::string>( "graph needs a Surface Output node" );

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
        const std::string albedo   = compiler.InputExpr( *output, 0, "vec4( 0.8, 0.8, 0.8, 1.0 )" );
        const std::string emission = compiler.InputExpr( *output, 1, "vec4( 0.0 )" );
        const std::string alpha    = compiler.InputExpr( *output, 2, "1.0" );
        if ( !compiler.error.empty() )
            return Common::MakeError<std::string>( compiler.error );

        std::ostringstream out;
        out << "// GENERATED by the Desert Shader Graph editor — edit the .dgraph, not this file.\n";
        out << "Shader \"" << doc.Name << "\"\n{\n    Domain Surface\n\n";

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

        out << "    State\n    {\n        Cull Back\n        ZTest LEqual\n        ZWrite On\n    }\n\n";

        // Vertex stage + depth pass mirror Unlit.shader — the generic-mesh contract (fixed vertex
        // layout, CameraUB at binding 0, Transform push constant).
        static const char* kVertex = R"(    Vertex
    {
        layout( location = 0 ) in vec3 a_Position;
        layout( location = 1 ) in vec3 a_Normal;
        layout( location = 2 ) in vec3 a_Tangent;
        layout( location = 3 ) in vec3 a_Bitangent;
        layout( location = 4 ) in vec2 a_TextureCoord;

        #include "../../Common/CameraUB.glslh"

        layout( push_constant ) uniform PushConstants
        {
            mat4 Transform;
        }
        m_PushConstants;

        layout( location = 0 ) out vec2 v_UV;

        void main()
        {
            v_UV        = a_TextureCoord;
            gl_Position = cameraUB.Projection * cameraUB.View * m_PushConstants.Transform * vec4( a_Position, 1.0 );
        }
    }
)";
        out << kVertex << "\n";

        out << "    Fragment\n    {\n";
        out << "        layout( location = 0 ) in vec2 v_UV;\n";
        out << "        layout( location = 0 ) out vec4 o_Color;\n\n";
        out << "        void main()\n        {\n";
        out << compiler.body.str();
        out << std::format( "            vec4 albedo = {};\n", albedo );
        out << std::format( "            o_Color = vec4( albedo.rgb + ( {} ).rgb, albedo.a * ( {} ) );\n",
                            emission, alpha );
        out << "        }\n    }\n\n";

        static const char* kDepthPass = R"(    Pass "Depth"
    {
        State
        {
            Cull Front
        }

        Vertex
        {
            layout( location = 0 ) in vec3 a_Position;
            layout( location = 1 ) in vec3 a_Normal;
            layout( location = 2 ) in vec3 a_Tangent;
            layout( location = 3 ) in vec3 a_Bitangent;
            layout( location = 4 ) in vec2 a_TextureCoord;

            #include "../../Common/CameraUB.glslh"

            layout( push_constant ) uniform PushConstants
            {
                mat4 Transform;
            }
            m_PushConstants;

            void main()
            {
                gl_Position = cameraUB.Projection * cameraUB.View * m_PushConstants.Transform * vec4( a_Position, 1.0 );
            }
        }
    }
)";
        out << kDepthPass;
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
