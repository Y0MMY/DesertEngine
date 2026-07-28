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
            switch ( *state.DepthCompare )
            {
                case StateCompare::Never:          spec.DepthCompareOp = CompareOp::Never; break;
                case StateCompare::Less:           spec.DepthCompareOp = CompareOp::Less; break;
                case StateCompare::Equal:          spec.DepthCompareOp = CompareOp::Equal; break;
                case StateCompare::LessOrEqual:    spec.DepthCompareOp = CompareOp::LessOrEqual; break;
                case StateCompare::Greater:        spec.DepthCompareOp = CompareOp::Greater; break;
                case StateCompare::NotEqual:       spec.DepthCompareOp = CompareOp::NotEqual; break;
                case StateCompare::GreaterOrEqual: spec.DepthCompareOp = CompareOp::GreaterOrEqual; break;
                case StateCompare::Always:         spec.DepthCompareOp = CompareOp::Always; break;
            }
        }
        if ( state.Blend )
            spec.BlendEnable = *state.Blend;
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
            return k;
        }

        std::unordered_map<Key, std::shared_ptr<GraphicsPipeline>, KeyHash> m_Cache;
    };

} // namespace Desert::Graphic
