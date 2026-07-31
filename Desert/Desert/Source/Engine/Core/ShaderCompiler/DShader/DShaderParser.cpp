#include <Engine/Core/ShaderCompiler/DShader/DShaderParser.hpp>

#include <cctype>
#include <charconv>
#include <functional>
#include <regex>
#include <set>
#include <sstream>
#include <vector>

namespace Desert::Core::Preprocess
{
    using namespace Desert::Core::Formats;

    namespace
    {
        // ─── Cursor: char-by-char scanner with line tracking ────────────────────────

        struct Cursor
        {
            const std::string& Src;
            size_t             Pos  = 0;
            uint32_t           Line = 1;

            bool AtEnd() const
            {
                return Pos >= Src.size();
            }
            char Peek() const
            {
                return AtEnd() ? '\0' : Src[Pos];
            }
            char Advance()
            {
                const char c = Src[Pos++];
                if ( c == '\n' )
                    ++Line;
                return c;
            }
        };

        struct ParseError
        {
            uint32_t    Line;
            std::string Message;
        };

        // Skips whitespace plus // and /* */ comments.
        void SkipTrivia( Cursor& c )
        {
            while ( !c.AtEnd() )
            {
                const char ch = c.Peek();
                if ( std::isspace( static_cast<unsigned char>( ch ) ) )
                {
                    c.Advance();
                }
                else if ( ch == '/' && c.Pos + 1 < c.Src.size() && c.Src[c.Pos + 1] == '/' )
                {
                    while ( !c.AtEnd() && c.Peek() != '\n' )
                        c.Advance();
                }
                else if ( ch == '/' && c.Pos + 1 < c.Src.size() && c.Src[c.Pos + 1] == '*' )
                {
                    c.Advance();
                    c.Advance();
                    while ( !c.AtEnd() &&
                            !( c.Peek() == '*' && c.Pos + 1 < c.Src.size() && c.Src[c.Pos + 1] == '/' ) )
                        c.Advance();
                    if ( !c.AtEnd() )
                    {
                        c.Advance();
                        c.Advance();
                    }
                }
                else
                {
                    break;
                }
            }
        }

        bool IsIdentChar( char ch )
        {
            return std::isalnum( static_cast<unsigned char>( ch ) ) || ch == '_';
        }

        std::string ReadIdent( Cursor& c )
        {
            SkipTrivia( c );
            std::string out;
            while ( !c.AtEnd() && IsIdentChar( c.Peek() ) )
                out.push_back( c.Advance() );
            return out;
        }

        bool Expect( Cursor& c, char ch, ParseError& err, const char* context )
        {
            SkipTrivia( c );
            if ( c.Peek() != ch )
            {
                err = { c.Line, std::string( "expected '" ) + ch + "' " + context };
                return false;
            }
            c.Advance();
            return true;
        }

        bool ReadQuoted( Cursor& c, std::string& out, ParseError& err )
        {
            SkipTrivia( c );
            if ( c.Peek() != '"' )
            {
                err = { c.Line, "expected a quoted string" };
                return false;
            }
            c.Advance();
            out.clear();
            while ( !c.AtEnd() && c.Peek() != '"' && c.Peek() != '\n' )
                out.push_back( c.Advance() );
            if ( c.Peek() != '"' )
            {
                err = { c.Line, "unterminated string" };
                return false;
            }
            c.Advance();
            return true;
        }

        bool ReadNumber( Cursor& c, float& out, ParseError& err )
        {
            SkipTrivia( c );
            std::string tok;
            while ( !c.AtEnd() && ( std::isdigit( static_cast<unsigned char>( c.Peek() ) ) || c.Peek() == '.' ||
                                    c.Peek() == '-' || c.Peek() == '+' ) )
                tok.push_back( c.Advance() );
            try
            {
                out = std::stof( tok );
            }
            catch ( ... )
            {
                err = { c.Line, "expected a number, got '" + tok + "'" };
                return false;
            }
            return true;
        }

        // Reads a brace-balanced block verbatim (comments respected so a '}' inside them
        // doesn't end the block). Cursor must be at '{'. Returns the raw content and the
        // line the content starts on.
        bool ReadBlock( Cursor& c, std::string& outContent, uint32_t& outStartLine, ParseError& err )
        {
            SkipTrivia( c );
            if ( c.Peek() != '{' )
            {
                err = { c.Line, "expected '{'" };
                return false;
            }
            c.Advance();
            outStartLine = c.Line;

            const size_t begin = c.Pos;
            int          depth = 1;
            while ( !c.AtEnd() )
            {
                const char ch = c.Peek();
                if ( ch == '/' && c.Pos + 1 < c.Src.size() &&
                     ( c.Src[c.Pos + 1] == '/' || c.Src[c.Pos + 1] == '*' ) )
                {
                    SkipTrivia( c ); // consumes the whole comment; keeps line counting exact
                    continue;
                }
                if ( ch == '{' )
                    ++depth;
                else if ( ch == '}' )
                {
                    if ( --depth == 0 )
                    {
                        outContent = c.Src.substr( begin, c.Pos - begin );
                        c.Advance();
                        return true;
                    }
                }
                c.Advance();
            }
            err = { c.Line, "unterminated block (missing '}')" };
            return false;
        }

        // ─── Keyword tables ─────────────────────────────────────────────────────────

        std::string Lower( std::string s )
        {
            for ( auto& ch : s )
                ch = static_cast<char>( std::tolower( static_cast<unsigned char>( ch ) ) );
            return s;
        }

        ShaderStage StageFromKeyword( const std::string& kw )
        {
            const std::string s = Lower( kw );
            if ( s == "vertex" )
                return ShaderStage::Vertex;
            if ( s == "fragment" || s == "pixel" )
                return ShaderStage::Fragment;
            if ( s == "compute" )
                return ShaderStage::Compute;
            if ( s == "tesscontrol" )
                return ShaderStage::TessControl;
            if ( s == "tesseval" || s == "tessevaluation" )
                return ShaderStage::TessEvaluation;
            return ShaderStage::None;
        }

        bool ParamTypeFromKeyword( const std::string& kw, ShaderParam& param )
        {
            const std::string s = Lower( kw );
            using VT            = ShaderValueType;
            using W             = ShaderParamWidget;

            if ( s == "float" )
                param.Type = VT::Float;
            else if ( s == "vec2" )
                param.Type = VT::Float2;
            else if ( s == "vec3" )
                param.Type = VT::Float3;
            else if ( s == "vec4" )
                param.Type = VT::Float4;
            else if ( s == "int" )
                param.Type = VT::Int;
            else if ( s == "bool" )
                param.Type = VT::Bool;
            else if ( s == "color" || s == "color4" )
            {
                param.Type   = VT::Float4;
                param.Widget = W::Color;
            }
            else if ( s == "color3" )
            {
                param.Type   = VT::Float3;
                param.Widget = W::Color;
            }
            else if ( s == "texture2d" )
            {
                param.IsTexture = true;
                param.Type      = VT::Unknown;
            }
            else if ( s == "texturecube" )
            {
                param.IsTexture = true;
                param.Type      = VT::Unknown;
            }
            else
                return false;
            return true;
        }

        // GLSL declaration type for an auto-generated MaterialUB field.
        const char* GlslTypeOf( const ShaderParam& p )
        {
            switch ( p.Type )
            {
                case ShaderValueType::Float:
                    return "float";
                case ShaderValueType::Float2:
                    return "vec2";
                case ShaderValueType::Float3:
                    return "vec3";
                case ShaderValueType::Float4:
                    return "vec4";
                case ShaderValueType::Int:
                    return "int";
                case ShaderValueType::Bool:
                    return "int";
                default:
                    return "vec4";
            }
        }

        // Distinguishes 2D from cube for auto-generated samplers (Type is Unknown for both).
        struct PropertyExtra
        {
            bool IsCube = false;
        };

        // ─── Section parsers ────────────────────────────────────────────────────────

        struct PropertiesInfo
        {
            std::optional<uint32_t>    UBBinding;      // Binding(n)
            std::optional<uint32_t>    TextureBinding; // TextureBinding(n)
            std::vector<PropertyExtra> Extras;         // parallel to Meta.Params
        };

        bool ParsePropertyAttributes( Cursor& c, ShaderParam& param, ParseError& err )
        {
            // ( "Display Name" [, Range(a,b)] [, Category("...")] )
            if ( !Expect( c, '(', err, "after property name" ) )
                return false;

            SkipTrivia( c );
            if ( c.Peek() == '"' )
            {
                if ( !ReadQuoted( c, param.DisplayName, err ) )
                    return false;
            }

            SkipTrivia( c );
            while ( c.Peek() == ',' )
            {
                c.Advance();
                const std::string attr = Lower( ReadIdent( c ) );
                if ( attr == "range" )
                {
                    float a = 0, b = 0;
                    if ( !Expect( c, '(', err, "after Range" ) || !ReadNumber( c, a, err ) ||
                         !Expect( c, ',', err, "in Range" ) || !ReadNumber( c, b, err ) ||
                         !Expect( c, ')', err, "closing Range" ) )
                        return false;
                    param.Min = a;
                    param.Max = b;
                    if ( param.Widget == ShaderParamWidget::Auto )
                        param.Widget = ShaderParamWidget::Slider;
                }
                else if ( attr == "category" )
                {
                    if ( !Expect( c, '(', err, "after Category" ) || !ReadQuoted( c, param.Category, err ) ||
                         !Expect( c, ')', err, "closing Category" ) )
                        return false;
                }
                else
                {
                    err = { c.Line, "unknown property attribute '" + attr + "' (expected Range or Category)" };
                    return false;
                }
                SkipTrivia( c );
            }

            return Expect( c, ')', err, "closing the property attribute list" );
        }

        bool ParsePropertyDefault( Cursor& c, ShaderParam& param, ParseError& err )
        {
            SkipTrivia( c );
            if ( c.Peek() != '=' )
                return true; // defaults are optional
            c.Advance();
            SkipTrivia( c );

            if ( c.Peek() == '"' ) // texture default: = "white"
            {
                if ( !param.IsTexture )
                {
                    err = { c.Line, "string default is only valid for texture properties" };
                    return false;
                }
                return ReadQuoted( c, param.DefaultTexture, err );
            }

            if ( c.Peek() == '(' ) // vector default: = (r, g, b, a)
            {
                c.Advance();
                for ( glm::length_t i = 0; i < 4; ++i )
                {
                    float v = 0;
                    if ( !ReadNumber( c, v, err ) )
                        return false;
                    param.Default[i] = v;
                    SkipTrivia( c );
                    if ( c.Peek() == ',' )
                    {
                        c.Advance();
                        continue;
                    }
                    break;
                }
                return Expect( c, ')', err, "closing the default value" );
            }

            // scalar default: = 4
            float v = 0;
            if ( !ReadNumber( c, v, err ) )
                return false;
            param.Default[0] = v;
            return true;
        }

        bool ParsePropertiesBlock( Cursor& c, ShaderProgramMeta& meta, PropertiesInfo& info, ParseError& err )
        {
            // Optional Binding(n) / TextureBinding(n) before '{'
            SkipTrivia( c );
            while ( c.Peek() != '{' && !c.AtEnd() )
            {
                const std::string opt = Lower( ReadIdent( c ) );
                float             v   = 0;
                if ( opt == "binding" )
                {
                    if ( !Expect( c, '(', err, "after Binding" ) || !ReadNumber( c, v, err ) ||
                         !Expect( c, ')', err, "closing Binding" ) )
                        return false;
                    info.UBBinding = static_cast<uint32_t>( v );
                }
                else if ( opt == "texturebinding" )
                {
                    if ( !Expect( c, '(', err, "after TextureBinding" ) || !ReadNumber( c, v, err ) ||
                         !Expect( c, ')', err, "closing TextureBinding" ) )
                        return false;
                    info.TextureBinding = static_cast<uint32_t>( v );
                }
                else
                {
                    err = { c.Line,
                            "unknown Properties option '" + opt + "' (expected Binding or TextureBinding)" };
                    return false;
                }
                SkipTrivia( c );
            }

            if ( !Expect( c, '{', err, "opening the Properties block" ) )
                return false;

            while ( true )
            {
                SkipTrivia( c );
                if ( c.Peek() == '}' )
                {
                    c.Advance();
                    return true;
                }
                if ( c.AtEnd() )
                {
                    err = { c.Line, "unterminated Properties block" };
                    return false;
                }

                const uint32_t    entryLine = c.Line;
                const std::string typeKw    = ReadIdent( c );
                ShaderParam       param;
                if ( !ParamTypeFromKeyword( typeKw, param ) )
                {
                    err = { entryLine, "unknown property type '" + typeKw + "'" };
                    return false;
                }

                param.Name = ReadIdent( c );
                if ( param.Name.empty() )
                {
                    err = { c.Line, "property is missing a name" };
                    return false;
                }

                SkipTrivia( c );
                if ( c.Peek() == '(' )
                {
                    if ( !ParsePropertyAttributes( c, param, err ) )
                        return false;
                }
                if ( param.DisplayName.empty() )
                    param.DisplayName = param.Name;

                if ( !ParsePropertyDefault( c, param, err ) )
                    return false;

                info.Extras.push_back( { Lower( typeKw ) == "texturecube" } );
                meta.Params.push_back( std::move( param ) );
            }
        }

        bool ParseStateBlock( Cursor& c, ShaderRenderState& state, ParseError& err )
        {
            if ( !Expect( c, '{', err, "opening the State block" ) )
                return false;

            while ( true )
            {
                SkipTrivia( c );
                if ( c.Peek() == '}' )
                {
                    c.Advance();
                    return true;
                }
                if ( c.AtEnd() )
                {
                    err = { c.Line, "unterminated State block" };
                    return false;
                }

                const uint32_t    line   = c.Line;
                const std::string rawCmd = ReadIdent( c );
                const std::string cmd    = Lower( rawCmd );

                if ( cmd == "cull" )
                {
                    const std::string v = Lower( ReadIdent( c ) );
                    if ( v == "none" )
                        state.Cull = StateCull::None;
                    else if ( v == "front" )
                        state.Cull = StateCull::Front;
                    else if ( v == "back" )
                        state.Cull = StateCull::Back;
                    else if ( v == "frontandback" )
                        state.Cull = StateCull::FrontAndBack;
                    else
                    {
                        err = { line, "unknown Cull mode '" + v + "'" };
                        return false;
                    }
                }
                else if ( cmd == "ztest" )
                {
                    const std::string v = Lower( ReadIdent( c ) );
                    using C             = StateCompare;
                    if ( v == "off" )
                        state.DepthTest = false;
                    else
                    {
                        std::optional<C> cmp;
                        if ( v == "never" )
                            cmp = C::Never;
                        else if ( v == "less" )
                            cmp = C::Less;
                        else if ( v == "equal" )
                            cmp = C::Equal;
                        else if ( v == "lequal" || v == "lessorequal" )
                            cmp = C::LessOrEqual;
                        else if ( v == "greater" )
                            cmp = C::Greater;
                        else if ( v == "notequal" )
                            cmp = C::NotEqual;
                        else if ( v == "gequal" || v == "greaterorequal" )
                            cmp = C::GreaterOrEqual;
                        else if ( v == "always" )
                            cmp = C::Always;
                        if ( !cmp )
                        {
                            err = { line, "unknown ZTest mode '" + v + "'" };
                            return false;
                        }
                        state.DepthCompare = cmp;
                        state.DepthTest    = true;
                    }
                }
                else if ( cmd == "zwrite" )
                {
                    const std::string v = Lower( ReadIdent( c ) );
                    state.DepthWrite    = ( v == "on" || v == "true" );
                }
                else if ( cmd == "blend" )
                {
                    // `Blend Off | On | Alpha` OR custom factors `Blend <src> <dst>` (e.g.
                    // `Blend SrcAlpha OneMinusSrcAlpha`, `Blend One One` for additive).
                    const auto factor = []( const std::string& v ) -> std::optional<StateBlendFactor>
                    {
                        if ( v == "zero" )
                            return StateBlendFactor::Zero;
                        if ( v == "one" )
                            return StateBlendFactor::One;
                        if ( v == "srccolor" )
                            return StateBlendFactor::SrcColor;
                        if ( v == "oneminussrccolor" )
                            return StateBlendFactor::OneMinusSrcColor;
                        if ( v == "dstcolor" )
                            return StateBlendFactor::DstColor;
                        if ( v == "oneminusdstcolor" )
                            return StateBlendFactor::OneMinusDstColor;
                        if ( v == "srcalpha" )
                            return StateBlendFactor::SrcAlpha;
                        if ( v == "oneminussrcalpha" )
                            return StateBlendFactor::OneMinusSrcAlpha;
                        if ( v == "dstalpha" )
                            return StateBlendFactor::DstAlpha;
                        if ( v == "oneminusdstalpha" )
                            return StateBlendFactor::OneMinusDstAlpha;
                        return std::nullopt;
                    };
                    const std::string v = Lower( ReadIdent( c ) );
                    if ( v == "off" || v == "false" )
                        state.Blend = false;
                    else if ( v == "on" || v == "alpha" || v == "true" )
                        state.Blend = true;
                    else if ( auto src = factor( v ) )
                    {
                        const std::string d   = Lower( ReadIdent( c ) );
                        auto              dst = factor( d );
                        if ( !dst )
                        {
                            err = { line, "unknown Blend dst factor '" + d + "'" };
                            return false;
                        }
                        state.Blend    = true;
                        state.BlendSrc = *src;
                        state.BlendDst = *dst;
                    }
                    else
                    {
                        err = { line, "unknown Blend value '" + v + "'" };
                        return false;
                    }
                }
                else if ( cmd == "stencil" )
                {
                    // `Stencil <compare> <ref> [<fail> <pass> <depthFail>]`. Ops default Keep/Replace/Keep
                    // (write the ref where the test passes — the outline-mask idiom).
                    const auto compare = []( const std::string& v ) -> std::optional<StateCompare>
                    {
                        if ( v == "never" )
                            return StateCompare::Never;
                        if ( v == "less" )
                            return StateCompare::Less;
                        if ( v == "equal" )
                            return StateCompare::Equal;
                        if ( v == "lequal" || v == "lessorequal" )
                            return StateCompare::LessOrEqual;
                        if ( v == "greater" )
                            return StateCompare::Greater;
                        if ( v == "notequal" )
                            return StateCompare::NotEqual;
                        if ( v == "gequal" || v == "greaterorequal" )
                            return StateCompare::GreaterOrEqual;
                        if ( v == "always" )
                            return StateCompare::Always;
                        return std::nullopt;
                    };
                    const auto stencilOp = []( const std::string& v ) -> std::optional<StateStencilOp>
                    {
                        if ( v == "keep" )
                            return StateStencilOp::Keep;
                        if ( v == "zero" )
                            return StateStencilOp::Zero;
                        if ( v == "replace" )
                            return StateStencilOp::Replace;
                        if ( v == "incrclamp" || v == "incrementclamp" )
                            return StateStencilOp::IncrementClamp;
                        if ( v == "decrclamp" || v == "decrementclamp" )
                            return StateStencilOp::DecrementClamp;
                        if ( v == "invert" )
                            return StateStencilOp::Invert;
                        if ( v == "incrwrap" || v == "incrementwrap" )
                            return StateStencilOp::IncrementWrap;
                        if ( v == "decrwrap" || v == "decrementwrap" )
                            return StateStencilOp::DecrementWrap;
                        return std::nullopt;
                    };

                    const std::string cmpS = Lower( ReadIdent( c ) );
                    auto              cmp  = compare( cmpS );
                    if ( !cmp )
                    {
                        err = { line, "unknown Stencil compare '" + cmpS + "'" };
                        return false;
                    }
                    float ref = 0;
                    if ( !ReadNumber( c, ref, err ) )
                        return false;

                    state.StencilTest    = true;
                    state.StencilCompare = cmp;
                    state.StencilRef     = static_cast<uint32_t>( ref );

                    // Optional 3 ops: peek — if the next ident is a stencil op, consume fail/pass/depthFail
                    // (Cursor has a reference member so it isn't assignable; rewind via Pos/Line).
                    const size_t   savePos  = c.Pos;
                    const uint32_t saveLine = c.Line;
                    if ( stencilOp( Lower( ReadIdent( c ) ) ) )
                    {
                        c.Pos                  = savePos;
                        c.Line                 = saveLine;
                        state.StencilFail      = stencilOp( Lower( ReadIdent( c ) ) );
                        state.StencilPass      = stencilOp( Lower( ReadIdent( c ) ) );
                        state.StencilDepthFail = stencilOp( Lower( ReadIdent( c ) ) );
                        if ( !state.StencilFail || !state.StencilPass || !state.StencilDepthFail )
                        {
                            err = { line, "Stencil expects 3 ops (fail pass depthFail) when any is given" };
                            return false;
                        }
                    }
                    else
                    {
                        c.Pos  = savePos; // not ops -> leave for the next State command
                        c.Line = saveLine;
                    }
                }
                else if ( cmd == "topology" )
                {
                    const std::string v = Lower( ReadIdent( c ) );
                    if ( v == "triangles" )
                        state.Topology = StateTopology::Triangles;
                    else if ( v == "lines" )
                        state.Topology = StateTopology::Lines;
                    else if ( v == "points" )
                        state.Topology = StateTopology::Points;
                    else if ( v == "patches" )
                    {
                        state.Topology = StateTopology::Patches;
                        float n        = 0;
                        if ( !ReadNumber( c, n, err ) )
                            return false;
                        state.PatchControlPoints = static_cast<uint32_t>( n );
                    }
                    else
                    {
                        err = { line, "unknown Topology '" + v + "'" };
                        return false;
                    }
                }
                else
                {
                    err = { line, "unknown State command '" + rawCmd + "'" };
                    return false;
                }
            }
        }

        // ─── Stage GLSL assembly ────────────────────────────────────────────────────

        struct RawBlock
        {
            std::string Content;
            uint32_t    StartLine = 1;
        };

        // Auto-generated resource declarations from the Properties block (opt-in via Binding /
        // TextureBinding). Injected into the fragment stage (and compute, for kernel shaders) —
        // material parameters belong to shading, and a single-stage declaration keeps the
        // reflection unambiguous.
        std::string BuildAutoDeclarations( const ShaderProgramMeta& meta, const PropertiesInfo& info )
        {
            std::ostringstream out;

            if ( info.UBBinding )
            {
                bool any = false;
                for ( const auto& p : meta.Params )
                    if ( !p.IsTexture )
                        any = true;

                if ( any )
                {
                    out << "layout( binding = " << *info.UBBinding << " ) uniform MaterialUB\n{\n";
                    for ( const auto& p : meta.Params )
                    {
                        if ( p.IsTexture )
                            continue;
                        out << "    " << GlslTypeOf( p ) << " " << p.Name << ";\n";
                    }
                    out << "}\nu_Material;\n";
                }
            }

            if ( info.TextureBinding )
            {
                uint32_t binding = *info.TextureBinding;
                for ( size_t i = 0; i < meta.Params.size(); ++i )
                {
                    const auto& p = meta.Params[i];
                    if ( !p.IsTexture )
                        continue;
                    const bool cube = i < info.Extras.size() && info.Extras[i].IsCube;
                    out << "layout( binding = " << binding++ << " ) uniform "
                        << ( cube ? "samplerCube" : "sampler2D" ) << " " << p.Name << ";\n";
                }
            }

            return out.str();
        }

        // Pass-level State overrides file-level State field-by-field (only the settings the
        // pass actually specifies).
        ShaderRenderState MergeState( const ShaderRenderState& base, const ShaderRenderState& over )
        {
            ShaderRenderState out = base;
            if ( over.Cull )
                out.Cull = over.Cull;
            if ( over.DepthTest )
                out.DepthTest = over.DepthTest;
            if ( over.DepthWrite )
                out.DepthWrite = over.DepthWrite;
            if ( over.DepthCompare )
                out.DepthCompare = over.DepthCompare;
            if ( over.Blend )
                out.Blend = over.Blend;
            if ( over.BlendSrc )
                out.BlendSrc = over.BlendSrc;
            if ( over.BlendDst )
                out.BlendDst = over.BlendDst;
            if ( over.Topology )
                out.Topology = over.Topology;
            if ( over.PatchControlPoints )
                out.PatchControlPoints = over.PatchControlPoints;
            if ( over.StencilTest )
            {
                out.StencilTest      = over.StencilTest;
                out.StencilCompare   = over.StencilCompare;
                out.StencilRef       = over.StencilRef;
                out.StencilFail      = over.StencilFail;
                out.StencilPass      = over.StencilPass;
                out.StencilDepthFail = over.StencilDepthFail;
            }
            return out;
        }

        // Desert shader-language sugar over GLSL's `layout(...)` qualifiers — a thin, ShaderLab-flavoured
        // vocabulary that translates 1:1 to plain GLSL. Every rule is SINGLE-LINE so the line count is
        // preserved and `#line`-based error mapping stays exact. Raw `layout(...)` still works verbatim
        // (the sugar keywords are Capitalized; GLSL keywords are lowercase, so they never collide):
        //   In(n)  T x;         -> layout(location = n) in  T x;
        //   Out(n) T x;         -> layout(location = n) out T x;
        //   Uniform(n) ...      -> layout(binding = n) uniform ...            (UBO block or a sampler)
        //   Uniform(s, n) ...   -> layout(set = s, binding = n) uniform ...   (explicit descriptor set)
        //   Buffer(n) ...       -> layout(std430, binding = n) buffer ...
        //   ReadBuffer(n) ...   -> layout(std430, binding = n) readonly  buffer ...
        //   WriteBuffer(n) ...  -> layout(std430, binding = n) writeonly buffer ...
        //   LocalSize(x, y, z)  -> layout(local_size_x = x, local_size_y = y, local_size_z = z) in
        //   PushConstant ...    -> layout(push_constant) uniform ...          (block name + instance kept)
        //
        // AUTO NUMBERS (drop the parentheses): `In T x;` / `Out T x;` / `Uniform Name {}` / `Buffer Name {}`
        // / `ReadBuffer`/`WriteBuffer` with NO (n) auto-allocate the lowest free slot, in declaration order,
        // per STAGE. Three independent spaces: `in` locations, `out` locations, and descriptor bindings; auto
        // slots skip any EXPLICIT numbers already used in the stage so the two can be mixed. Caveat (per-stage,
        // by design): a resource SHARED across stages (e.g. the camera UB from an include) or one a C++ site
        // binds by a fixed number must keep its EXPLICIT (n) so every stage / the host agree — auto can't
        // coordinate across stages.
        //
        // Storage-image format qualifiers (`layout(binding=n, rgba32f) uniform imageCube`) and tessellation
        // layout (`layout(vertices=n) out`, `layout(quads,...) in`) are inherently GLSL-structural and stay
        // as raw `layout(...)` — the only sanctioned escape (DShaderTool allows exactly these forms).
        std::string TranslateLayoutSugar( const std::string& src )
        {
            // (1) EXPLICIT forms first — a numbered declaration always wins and is left untouched by the
            // auto pass below (the capitalized keyword is consumed here).
            static const std::pair<std::regex, std::string> kRules[] = {
                 { std::regex( R"(\bIn\s*\(\s*(\d+)\s*\))" ), "layout(location = $1) in" },
                 { std::regex( R"(\bOut\s*\(\s*(\d+)\s*\))" ), "layout(location = $1) out" },
                 { std::regex( R"(\bUniform\s*\(\s*(\d+)\s*,\s*(\d+)\s*\))" ),
                   "layout(set = $1, binding = $2) uniform" },
                 { std::regex( R"(\bUniform\s*\(\s*(\d+)\s*\))" ), "layout(binding = $1) uniform" },
                 { std::regex( R"(\bReadBuffer\s*\(\s*(\d+)\s*\))" ),
                   "layout(std430, binding = $1) readonly buffer" },
                 { std::regex( R"(\bWriteBuffer\s*\(\s*(\d+)\s*\))" ),
                   "layout(std430, binding = $1) writeonly buffer" },
                 { std::regex( R"(\bBuffer\s*\(\s*(\d+)\s*\))" ), "layout(std430, binding = $1) buffer" },
                 { std::regex( R"(\bLocalSize\s*\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\))" ),
                   "layout(local_size_x = $1, local_size_y = $2, local_size_z = $3) in" },
                 { std::regex( R"(\bPushConstant\b)" ), "layout(push_constant) uniform" },
            };
            std::string out = src;
            for ( const auto& [re, rep] : kRules )
                out = std::regex_replace( out, re, rep );

            // (2) Seed each auto-allocator with the EXPLICIT numbers already present so auto never collides.
            const auto collect = [&]( const std::regex& re, std::set<int>& used )
            {
                for ( std::sregex_iterator it( out.begin(), out.end(), re ), end; it != end; ++it )
                    used.insert( std::stoi( ( *it )[1].str() ) );
            };
            std::set<int> usedIn, usedOut, usedBind;
            collect( std::regex( R"(location\s*=\s*(\d+)\s*\)\s*in\b)" ), usedIn );
            collect( std::regex( R"(location\s*=\s*(\d+)\s*\)\s*out\b)" ), usedOut );
            collect( std::regex( R"(binding\s*=\s*(\d+))" ), usedBind );

            const auto alloc = []( std::set<int>& used )
            {
                int n = 0;
                while ( used.count( n ) )
                    ++n;
                used.insert( n );
                return n;
            };

            // (3) Replace paren-less forms left-to-right (declaration order). Replacing the FIRST occurrence
            // each pass consumes the capitalized keyword, so the next search advances to the next one; the
            // lowercase `in`/`out`/`uniform`/`buffer` in the replacement is never re-matched.
            const auto replaceFirst =
                 [&]( const std::regex& re, const std::function<std::string( const std::smatch& )>& make )
            {
                std::smatch m;
                while ( std::regex_search( out, m, re ) )
                    out = m.prefix().str() + make( m ) + m.suffix().str();
            };

            replaceFirst( std::regex( R"(\bIn\b(?!\s*\())" ), [&]( const std::smatch& )
                          { return "layout(location = " + std::to_string( alloc( usedIn ) ) + ") in"; } );
            replaceFirst( std::regex( R"(\bOut\b(?!\s*\())" ), [&]( const std::smatch& )
                          { return "layout(location = " + std::to_string( alloc( usedOut ) ) + ") out"; } );
            // One combined pass over the four binding keywords so they share ONE binding space in text order.
            replaceFirst( std::regex( R"(\b(Uniform|ReadBuffer|WriteBuffer|Buffer)\b(?!\s*\())" ),
                          [&]( const std::smatch& m )
                          {
                              const int         n  = alloc( usedBind );
                              const std::string kw = m[1].str();
                              if ( kw == "Uniform" )
                                  return "layout(binding = " + std::to_string( n ) + ") uniform";
                              if ( kw == "ReadBuffer" )
                                  return "layout(std430, binding = " + std::to_string( n ) + ") readonly buffer";
                              if ( kw == "WriteBuffer" )
                                  return "layout(std430, binding = " + std::to_string( n ) + ") writeonly buffer";
                              return "layout(std430, binding = " + std::to_string( n ) + ") buffer";
                          } );
            return out;
        }

        std::string AssembleStage( ShaderStage stage, const RawBlock& code, const RawBlock& include,
                                   const std::string& autoDecls )
        {
            std::ostringstream out;
            out << "#version 450\n";

            if ( !autoDecls.empty() && ( stage == ShaderStage::Fragment || stage == ShaderStage::Compute ) )
                out << autoDecls;

            if ( !include.Content.empty() )
            {
                // GLSL: after `#line N`, the NEXT line is numbered N+1.
                out << "#line " << ( include.StartLine > 0 ? include.StartLine - 1 : 0 ) << "\n";
                out << TranslateLayoutSugar( include.Content ) << "\n";
            }

            out << "#line " << ( code.StartLine > 0 ? code.StartLine - 1 : 0 ) << "\n";
            out << TranslateLayoutSugar( code.Content );

            return out.str();
        }

    } // namespace

    // ─── Public API ─────────────────────────────────────────────────────────────────

    bool DShaderParser::IsDShader( const std::string& source )
    {
        Cursor c{ source };
        SkipTrivia( c );
        size_t      probe = c.Pos;
        std::string ident;
        while ( probe < source.size() && IsIdentChar( source[probe] ) )
            ident.push_back( source[probe++] );
        return ident == "Shader";
    }

    std::string DShaderParser::TranslateSugar( const std::string& source )
    {
        return TranslateLayoutSugar( source );
    }

    Common::ResultStr<DShaderParseResult> DShaderParser::Parse( const std::string& source )
    {
        DShaderParseResult result;
        ParseError         err{ 0, "" };

        const auto fail = [&err]()
        {
            return Common::MakeFormattedError<DShaderParseResult>( "DShader parse error (line {}): {}", err.Line,
                                                                   err.Message );
        };

        Cursor c{ source };

        if ( ReadIdent( c ) != "Shader" )
        {
            err = { c.Line, "file must start with: Shader \"Name\"" };
            return fail();
        }
        if ( !ReadQuoted( c, result.Name, err ) || result.Name.empty() )
        {
            if ( err.Message.empty() )
                err = { c.Line, "shader name must not be empty" };
            return fail();
        }
        if ( !Expect( c, '{', err, "opening the Shader body" ) )
            return fail();

        struct PendingPass
        {
            std::string                               Name;
            ShaderRenderState                         State;
            std::unordered_map<ShaderStage, RawBlock> Blocks;
        };

        PropertiesInfo           propInfo;
        RawBlock                 includeBlock;
        PendingPass              defaultPass; // top-level stage blocks
        std::vector<PendingPass> namedPasses;

        // Reads one stage block into the given pass. Returns false + err on problems.
        const auto readStageBlock = [&]( Cursor& cur, PendingPass& pass, ShaderStage stage,
                                         const std::string& section, uint32_t line ) -> bool
        {
            if ( pass.Blocks.count( stage ) )
            {
                err = { line, "duplicate stage block '" + section + "'" };
                return false;
            }
            RawBlock block;
            if ( !ReadBlock( cur, block.Content, block.StartLine, err ) )
                return false;
            if ( block.Content.find( "#version" ) != std::string::npos )
            {
                err = { block.StartLine,
                        "stage blocks must not declare #version — the compiler emits the header" };
                return false;
            }
            pass.Blocks.emplace( stage, std::move( block ) );
            return true;
        };

        while ( true )
        {
            SkipTrivia( c );
            if ( c.Peek() == '}' )
            {
                c.Advance();
                break;
            }
            if ( c.AtEnd() )
            {
                err = { c.Line, "unterminated Shader body (missing '}')" };
                return fail();
            }

            const uint32_t    line    = c.Line;
            const std::string section = ReadIdent( c );
            const std::string lower   = Lower( section );

            if ( lower == "domain" )
            {
                const std::string v = Lower( ReadIdent( c ) );
                if ( v == "surface" )
                    result.Meta.Domain = ShaderDomain::Surface;
                else if ( v == "terrain" )
                    result.Meta.Domain = ShaderDomain::Terrain;
                else if ( v == "skybox" )
                    result.Meta.Domain = ShaderDomain::Skybox;
                else if ( v == "postprocess" )
                    result.Meta.Domain = ShaderDomain::PostProcess;
                else
                {
                    err = { line, "unknown Domain '" + v + "'" };
                    return fail();
                }
            }
            else if ( lower == "properties" )
            {
                if ( !ParsePropertiesBlock( c, result.Meta, propInfo, err ) )
                    return fail();
            }
            else if ( lower == "state" )
            {
                if ( !ParseStateBlock( c, defaultPass.State, err ) )
                    return fail();
            }
            else if ( lower == "include" )
            {
                if ( !ReadBlock( c, includeBlock.Content, includeBlock.StartLine, err ) )
                    return fail();
            }
            else if ( lower == "pass" )
            {
                PendingPass pass;
                if ( !ReadQuoted( c, pass.Name, err ) || pass.Name.empty() )
                {
                    if ( err.Message.empty() )
                        err = { line, "Pass name must not be empty" };
                    return fail();
                }
                for ( const auto& existing : namedPasses )
                {
                    if ( existing.Name == pass.Name )
                    {
                        err = { line, "duplicate Pass \"" + pass.Name + "\"" };
                        return fail();
                    }
                }
                if ( !Expect( c, '{', err, "opening the Pass body" ) )
                    return fail();

                while ( true )
                {
                    SkipTrivia( c );
                    if ( c.Peek() == '}' )
                    {
                        c.Advance();
                        break;
                    }
                    if ( c.AtEnd() )
                    {
                        err = { c.Line, "unterminated Pass body (missing '}')" };
                        return fail();
                    }

                    const uint32_t    passLine    = c.Line;
                    const std::string passSection = ReadIdent( c );
                    const std::string passLower   = Lower( passSection );

                    if ( passLower == "state" )
                    {
                        if ( !ParseStateBlock( c, pass.State, err ) )
                            return fail();
                    }
                    else if ( const ShaderStage stage = StageFromKeyword( passSection );
                              stage != ShaderStage::None )
                    {
                        if ( !readStageBlock( c, pass, stage, passSection, passLine ) )
                            return fail();
                    }
                    else
                    {
                        err = { passLine,
                                "unknown Pass section '" + passSection + "' (expected State or a stage block)" };
                        return fail();
                    }
                }

                if ( pass.Blocks.empty() )
                {
                    err = { line, "Pass \"" + pass.Name + "\" defines no stage blocks" };
                    return fail();
                }
                namedPasses.push_back( std::move( pass ) );
            }
            else if ( const ShaderStage stage = StageFromKeyword( section ); stage != ShaderStage::None )
            {
                if ( !readStageBlock( c, defaultPass, stage, section, line ) )
                    return fail();
            }
            else
            {
                err = { line, "unknown section '" + section + "'" };
                return fail();
            }
        }

        if ( defaultPass.Blocks.empty() && namedPasses.empty() )
        {
            err = { c.Line, "shader defines no stage blocks (Vertex/Fragment/Compute...)" };
            return fail();
        }

        const std::string autoDecls = BuildAutoDeclarations( result.Meta, propInfo );

        const auto assemblePass = [&]( const PendingPass& pending, const ShaderRenderState& state )
        {
            DShaderPass out;
            out.Name  = pending.Name;
            out.State = state;
            for ( const auto& [stage, block] : pending.Blocks )
                out.Stages.emplace( stage, AssembleStage( stage, block, includeBlock, autoDecls ) );
            return out;
        };

        // The default program is the top-level stage set; when a shader consists solely of
        // named passes, the first pass doubles as the default program.
        if ( !defaultPass.Blocks.empty() )
            result.Passes.push_back( assemblePass( defaultPass, defaultPass.State ) );

        for ( const auto& pass : namedPasses )
        {
            result.Passes.push_back( assemblePass( pass, MergeState( defaultPass.State, pass.State ) ) );
            result.Meta.PassNames.push_back( pass.Name );
        }

        result.Meta.State = result.Passes.front().State;
        result.Stages     = result.Passes.front().Stages;

        return Common::MakeSuccess( std::move( result ) );
    }

} // namespace Desert::Core::Preprocess
