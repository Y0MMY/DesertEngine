#pragma once

#include <Engine/Graphic/Pipeline.hpp>
#include <Engine/Core/Formats/ShaderProgramMeta.hpp>

#include <memory>
#include <unordered_map>

namespace Desert::Graphic
{
    // Overlays a shader's `#pragma state` declarations onto a pipeline spec. Only fields the shader
    // actually declared (std::optional set) are applied — everything else keeps the renderer's default.
    // This lets a .shader own its pipeline state (e.g. terrain declaring patch-list topology) instead of
    // every renderer hardcoding it.
    inline void ApplyShaderRenderState( GraphicsPipelineSpecification&          spec,
                                        const Core::Formats::ShaderRenderState& state )
    {
        using namespace Core::Formats;

        if ( state.Cull )
        {
            switch ( *state.Cull )
            {
                case StateCull::None:         spec.CullMode = CullMode::None; break;
                case StateCull::Front:        spec.CullMode = CullMode::Front; break;
                case StateCull::Back:         spec.CullMode = CullMode::Back; break;
                case StateCull::FrontAndBack: spec.CullMode = CullMode::FrontAndBack; break;
            }
        }
        if ( state.DepthTest )
            spec.DepthTestEnabled = *state.DepthTest;
        if ( state.DepthWrite )
            spec.DepthWriteEnabled = *state.DepthWrite;
        if ( state.DepthCompare )
        {
            // `ZTest Less` IS MIRRORED HERE, and this is the whole reason the mapping is not a
            // one-to-one table.
            //
            // A .shader authors its depth test the way every shading language does — `ZTest Less` means
            // "the fragment in front wins", `ZTest LEqual` means "in front, or exactly coplanar". That
            // spelling is what a shader author knows and what every other engine's documentation means
            // by it. The engine, however, renders REVERSED-Z (Core/Projection.hpp), where in front is
            // the LARGER stored value, so the arithmetic test that implements "Less" is Greater.
            //
            // Doing it here rather than by rewriting the five .shader files that spell a compare is not
            // tidiness. The next shader will be written by copying a neighbour, and if the neighbours
            // said `ZTest Greater` the copy would too, and it would be invisible — a whole material
            // rendered behind the world with nothing to point at. The DSL keeps ONE meaning; the
            // convention is applied once, at the boundary, in the only place that knows about pipelines.
            //
            // STENCIL COMPARES ARE NOT MIRRORED. The stencil buffer has no depth convention; its
            // StateCompare goes through `toCompare` below, unchanged, deliberately.
            // clang-format off
            // Kept one-case-per-line to read as the table it is, and to sit beside the identically
            // shaped `toCompare` for stencil below — which is the comparison a reader needs to make.
            const auto mirrored = []( StateCompare c )
            {
                switch ( c )
                {
                    case StateCompare::Never:          return CompareOp::Never;
                    case StateCompare::Less:           return CompareOp::Greater;
                    case StateCompare::Equal:          return CompareOp::Equal;
                    case StateCompare::LessOrEqual:    return CompareOp::GreaterOrEqual;
                    case StateCompare::Greater:        return CompareOp::Less;
                    case StateCompare::NotEqual:       return CompareOp::NotEqual;
                    case StateCompare::GreaterOrEqual: return CompareOp::LessOrEqual;
                    case StateCompare::Always:         return CompareOp::Always;
                }
                return CompareOp::Always;
            };
            // clang-format on
            spec.DepthCompareOp = mirrored( *state.DepthCompare );
        }
        if ( state.Blend )
            spec.BlendEnable = *state.Blend;

        const auto toBlendFactor = []( StateBlendFactor f )
        {
            switch ( f )
            {
                case StateBlendFactor::Zero:             return BlendFactor::Zero;
                case StateBlendFactor::One:              return BlendFactor::One;
                case StateBlendFactor::SrcColor:         return BlendFactor::SrcColor;
                case StateBlendFactor::OneMinusSrcColor: return BlendFactor::OneMinusSrcColor;
                case StateBlendFactor::DstColor:         return BlendFactor::DstColor;
                case StateBlendFactor::OneMinusDstColor: return BlendFactor::OneMinusDstColor;
                case StateBlendFactor::SrcAlpha:         return BlendFactor::SrcAlpha;
                case StateBlendFactor::OneMinusSrcAlpha: return BlendFactor::OneMinusSrcAlpha;
                case StateBlendFactor::DstAlpha:         return BlendFactor::DstAlpha;
                case StateBlendFactor::OneMinusDstAlpha: return BlendFactor::OneMinusDstAlpha;
            }
            return BlendFactor::One;
        };
        if ( state.BlendSrc )
            spec.SrcColorBlendFactor = toBlendFactor( *state.BlendSrc );
        if ( state.BlendDst )
            spec.DstColorBlendFactor = toBlendFactor( *state.BlendDst );

        if ( state.StencilTest )
        {
            spec.StencilTestEnabled = *state.StencilTest;
            const auto toStencilOp  = []( StateStencilOp o )
            {
                switch ( o )
                {
                    case StateStencilOp::Keep:           return StencilOp::Keep;
                    case StateStencilOp::Zero:           return StencilOp::Zero;
                    case StateStencilOp::Replace:        return StencilOp::Replace;
                    case StateStencilOp::IncrementClamp: return StencilOp::IncrementAndClamp;
                    case StateStencilOp::DecrementClamp: return StencilOp::DecrementAndClamp;
                    case StateStencilOp::Invert:         return StencilOp::Invert;
                    case StateStencilOp::IncrementWrap:  return StencilOp::IncrementAndWrap;
                    case StateStencilOp::DecrementWrap:  return StencilOp::DecrementAndWrap;
                }
                return StencilOp::Keep;
            };
            const auto toCompare = []( StateCompare c )
            {
                switch ( c )
                {
                    case StateCompare::Never:          return CompareOp::Never;
                    case StateCompare::Less:           return CompareOp::Less;
                    case StateCompare::Equal:          return CompareOp::Equal;
                    case StateCompare::LessOrEqual:    return CompareOp::LessOrEqual;
                    case StateCompare::Greater:        return CompareOp::Greater;
                    case StateCompare::NotEqual:       return CompareOp::NotEqual;
                    case StateCompare::GreaterOrEqual: return CompareOp::GreaterOrEqual;
                    case StateCompare::Always:         return CompareOp::Always;
                }
                return CompareOp::Always;
            };
            StencilOpState os;
            os.CompareOp   = state.StencilCompare ? toCompare( *state.StencilCompare ) : CompareOp::Always;
            os.FailOp      = state.StencilFail ? toStencilOp( *state.StencilFail ) : StencilOp::Keep;
            os.PassOp      = state.StencilPass ? toStencilOp( *state.StencilPass ) : StencilOp::Replace;
            os.DepthFailOp = state.StencilDepthFail ? toStencilOp( *state.StencilDepthFail ) : StencilOp::Keep;
            os.Reference   = state.StencilRef.value_or( 1u );
            spec.StencilFront = os;
            spec.StencilBack  = os;
        }

        if ( state.Topology )
        {
            switch ( *state.Topology )
            {
                case StateTopology::Triangles: spec.Topology = PrimitiveTopology::Triangles; break;
                case StateTopology::Lines:     spec.Topology = PrimitiveTopology::Lines; break;
                case StateTopology::Points:    spec.Topology = PrimitiveTopology::Points; break;
                case StateTopology::Patches:   spec.Topology = PrimitiveTopology::Patches; break;
            }
        }
        if ( state.PatchControlPoints )
            spec.PatchControlPoints = *state.PatchControlPoints;
    }

    // Caches GraphicsPipelines keyed by (shader + target + render-state) so identical requests share one
    // pipeline instead of every renderer creating its own. Owned by SceneRenderer; cleared on Init (full
    // rebuild) after a WaitDeviceIdle. Created pipelines are Invalidate()'d here.
    class PipelineCache
    {
    public:
        std::shared_ptr<GraphicsPipeline> GetOrCreate( const GraphicsPipelineSpecification& spec )
        {
            const Key key = MakeKey( spec );

            if ( auto it = m_Cache.find( key ); it != m_Cache.end() )
                return it->second;

            auto pipeline = GraphicsPipeline::Create( spec );
            if ( pipeline )
                pipeline->Invalidate();

            m_Cache.emplace( key, pipeline );
            return pipeline;
        }

        void Clear()
        {
            m_Cache.clear();
        }

        // Drops every pipeline built against @p shader — used by shader hot-reload so the next
        // GetOrCreate() rebuilds with the freshly compiled modules. Caller must WaitDeviceIdle first.
        void InvalidateByShader( const void* shader )
        {
            for ( auto it = m_Cache.begin(); it != m_Cache.end(); )
            {
                if ( it->first.Shader == shader )
                    it = m_Cache.erase( it );
                else
                    ++it;
            }
        }

    private:
        struct Key
        {
            const void* Shader        = nullptr;
            const void* Framebuffer   = nullptr;
            const void* Renderpass    = nullptr;
            bool        DepthTest     = false;
            bool        DepthWrite    = false;
            bool        StencilTest   = false;
            bool        Blend         = false;
            int         DepthCompare  = 0;
            int         Cull          = 0;
            int         Topology      = 0;
            int         Polygon       = 0;
            uint32_t    PatchPoints   = 0;
            int         BlendSrc      = 0;
            int         BlendDst      = 0;
            int         StencilState  = 0; // packed compare + fail/pass/depthfail ops
            uint32_t    StencilRef    = 0;

            bool operator==( const Key& ) const = default;
        };

        struct KeyHash
        {
            size_t operator()( const Key& k ) const
            {
                size_t h = 1469598103934665603ull; // FNV offset
                auto   mix = [&h]( size_t v )
                {
                    h ^= v;
                    h *= 1099511628211ull; // FNV prime
                };
                mix( reinterpret_cast<size_t>( k.Shader ) );
                mix( reinterpret_cast<size_t>( k.Framebuffer ) );
                mix( reinterpret_cast<size_t>( k.Renderpass ) );
                mix( ( k.DepthTest ? 1u : 0u ) | ( k.DepthWrite ? 2u : 0u ) | ( k.StencilTest ? 4u : 0u ) |
                     ( k.Blend ? 8u : 0u ) );
                mix( static_cast<size_t>( k.DepthCompare ) );
                mix( static_cast<size_t>( k.Cull ) );
                mix( static_cast<size_t>( k.Topology ) );
                mix( static_cast<size_t>( k.Polygon ) );
                mix( static_cast<size_t>( k.PatchPoints ) );
                mix( static_cast<size_t>( k.BlendSrc ) );
                mix( static_cast<size_t>( k.BlendDst ) );
                mix( static_cast<size_t>( k.StencilState ) );
                mix( static_cast<size_t>( k.StencilRef ) );
                return h;
            }
        };

        static Key MakeKey( const GraphicsPipelineSpecification& s )
        {
            Key k;
            k.Shader       = s.Shader.get();
            k.Framebuffer  = s.Framebuffer.get();
            k.Renderpass   = s.Renderpass.get();
            k.DepthTest    = s.DepthTestEnabled;
            k.DepthWrite   = s.DepthWriteEnabled;
            k.StencilTest  = s.StencilTestEnabled;
            k.Blend        = s.BlendEnable;
            k.DepthCompare = static_cast<int>( s.DepthCompareOp );
            k.Cull         = static_cast<int>( s.CullMode );
            k.Topology     = static_cast<int>( s.Topology );
            k.Polygon      = static_cast<int>( s.PolygonMode );
            k.PatchPoints  = s.PatchControlPoints;
            k.BlendSrc     = s.BlendEnable ? static_cast<int>( s.SrcColorBlendFactor ) : 0;
            k.BlendDst     = s.BlendEnable ? static_cast<int>( s.DstColorBlendFactor ) : 0;
            if ( s.StencilTestEnabled )
            {
                k.StencilState = static_cast<int>( s.StencilFront.CompareOp ) |
                                 ( static_cast<int>( s.StencilFront.FailOp ) << 4 ) |
                                 ( static_cast<int>( s.StencilFront.PassOp ) << 8 ) |
                                 ( static_cast<int>( s.StencilFront.DepthFailOp ) << 12 );
                k.StencilRef = s.StencilFront.Reference;
            }
            return k;
        }

        std::unordered_map<Key, std::shared_ptr<GraphicsPipeline>, KeyHash> m_Cache;
    };

} // namespace Desert::Graphic
