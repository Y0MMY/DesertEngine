#include "RenderGraphBuilder.hpp"
#include <algorithm>
#include <stack>
#include <queue>

namespace Desert::Graphic
{
    RenderGraphBuilder::RenderGraphBuilder()
    {
    }
    RenderGraphBuilder::~RenderGraphBuilder()
    {
    }

    void RenderGraphBuilder::AddPass( const PassConfig& config )
    {
        InternalPassData passData;
        passData.Config = config;

        for ( const auto& dep : config.Dependencies )
        {
            if ( dep.RequiredPhase != RenderPhase::None )
            {
                passData.RequiredPhases.insert( dep.RequiredPhase );
            }
        }

        m_Passes.push_back( passData );
        m_PhasePasses[config.Phase].push_back( config );
    }

    void RenderGraphBuilder::AddPass( const std::string& name, RenderPhase phase,
                                      std::function<void()> executeFunc, const PipelineSpecification& pipelineSpec,
                                      std::shared_ptr<Framebuffer>             targetFramebuffer,
                                      const std::vector<RenderPassDependency>& dependencies )
    {
        PassConfig config;
        config.Name              = name;
        config.Phase             = phase;
        config.ExecuteFunc       = executeFunc;
        config.PipelineSpec      = pipelineSpec;
        config.TargetFramebuffer = targetFramebuffer;
        config.Dependencies      = dependencies;

        InternalPassData passData;
        passData.Config = config;

        for ( const auto& dep : config.Dependencies )
        {
            if ( dep.RequiredPhase != RenderPhase::None )
            {
                passData.RequiredPhases.insert( dep.RequiredPhase );
            }
        }

        m_Passes.push_back( passData );

        m_PhasePasses[phase].push_back( config );

        LOG_DEBUG( "Added pass '{}' to phase '{}' with {} dependencies", name, "TODO", dependencies.size() );
    }

    void RenderGraphBuilder::AddPhaseDependency( RenderPhase requiredPhase, RenderPhase dependentPhase )
    {
        m_PhaseDependencies[dependentPhase].insert( requiredPhase );
    }

    void RenderGraphBuilder::AddTextureDependency( const std::string& textureName, RenderPhase producerPhase,
                                                   RenderPhase consumerPhase )
    {
        m_TextureDependencies[textureName] = { producerPhase, consumerPhase };
        AddPhaseDependency( producerPhase, consumerPhase );
    }

    bool RenderGraphBuilder::Build()
    {
        if ( !ValidateDependencies() )
        {
            LOG_ERROR( "RenderGraph validation failed!" );
            return false;
        }

        m_PhaseOrder = { RenderPhase::DepthPrePass, RenderPhase::Sky,         RenderPhase::Geometry,
                         RenderPhase::Outline,      RenderPhase::Decals,      RenderPhase::Lighting,
                         RenderPhase::Transparency, RenderPhase::PostProcess, RenderPhase::Overlay,
                         RenderPhase::UI,           RenderPhase::Debug };

        TopologicalSort();

        return true;
    }

    bool RenderGraphBuilder::ValidateDependencies() const
    {
        if ( CheckForCycles() )
        {
            LOG_ERROR( "Cyclic dependencies detected in render graph!" );
            return false;
        }

        for ( const auto& [textureName, phases] : m_TextureDependencies )
        {
            if ( phases.first == RenderPhase::None || phases.second == RenderPhase::None )
            {
                LOG_ERROR( "Texture '{}' has invalid phase dependencies", textureName );
                return false;
            }
        }

        return true;
    }

    void RenderGraphBuilder::TopologicalSort()
    {
        for ( auto& [phase, passes] : m_PhasePasses )
        {
            std::vector<PassConfig> sortedPasses;

            std::sort( passes.begin(), passes.end(), []( const PassConfig& a, const PassConfig& b )
                       { return a.Dependencies.size() < b.Dependencies.size(); } );
        }
    }

    bool RenderGraphBuilder::CheckForCycles() const
    {
        std::unordered_map<RenderPhase, std::vector<RenderPhase>> graph;
        std::unordered_map<RenderPhase, int>                      visited;

        for ( const auto& [phase, deps] : m_PhaseDependencies )
        {
            for ( RenderPhase dep : deps )
            {
                graph[phase].push_back( dep );
            }
        }

        std::function<bool( RenderPhase )> hasCycle = [&]( RenderPhase phase )
        {
            if ( visited[phase] == 1 )
                return true;
            if ( visited[phase] == 2 )
                return false;

            visited[phase] = 1;
            for ( RenderPhase neighbor : graph[phase] )
            {
                if ( hasCycle( neighbor ) )
                    return true;
            }
            visited[phase] = 2;
            return false;
        };

        for ( const auto& [phase, _] : graph )
        {
            if ( hasCycle( phase ) )
                return true;
        }

        return false;
    }

    const std::vector<RenderGraphBuilder::PassConfig>& RenderGraphBuilder::GetSortedPasses() const
    {
        static std::vector<PassConfig> result;
        result.clear();

        for ( RenderPhase phase : m_PhaseOrder )
        {
            if ( auto it = m_PhasePasses.find( phase ); it != m_PhasePasses.end() )
            {
                for ( const auto& pass : it->second )
                {
                    result.push_back( pass );
                }
            }
        }

        return result;
    }

    void RenderGraphBuilder::Clear()
    {
        m_Passes.clear();
        m_PhasePasses.clear();
        m_PhaseOrder.clear();
        m_PhaseDependencies.clear();
        m_TextureDependencies.clear();

        LOG_DEBUG( "Render graph builder cleared" );
    }

} // namespace Desert::Graphic