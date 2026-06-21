#include "RenderGraphBuilder.hpp"
#include "RenderPhase.hpp"
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
                                      std::function<void()> executeFunc, const GraphicsPipelineSpecification& pipelineSpec,
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

        LOG_DEBUG( "Added pass '{}' to phase '{}' with {} dependencies", name, RenderPhaseToString( phase ),
                   dependencies.size() );
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
        // Kahn's algorithm on the phase dependency graph.
        // m_PhaseDependencies[B] = { A, C } means B depends on A and C (A,C must come before B).

        // Collect all phases that appear either as a key or as a dependency.
        std::unordered_map<RenderPhase, int> inDegree;
        std::unordered_map<RenderPhase, std::vector<RenderPhase>> dependents; // A -> [phases that need A done first]

        for ( const auto& [phase, _] : m_PhasePasses )
            inDegree.emplace( phase, 0 );

        for ( const auto& [dependent, prereqs] : m_PhaseDependencies )
        {
            inDegree.emplace( dependent, 0 );
            for ( RenderPhase prereq : prereqs )
            {
                inDegree.emplace( prereq, 0 );
                dependents[prereq].push_back( dependent );
            }
        }

        for ( const auto& [dependent, prereqs] : m_PhaseDependencies )
            inDegree[dependent] += static_cast<int>( prereqs.size() );

        // Stable seed order so phases without dependencies appear in declaration order.
        constexpr RenderPhase kDeclOrder[] = {
            RenderPhase::DepthPrePass, RenderPhase::Sky,         RenderPhase::Geometry,
            RenderPhase::Outline,      RenderPhase::Decals,      RenderPhase::Lighting,
            RenderPhase::Transparency, RenderPhase::PostProcess, RenderPhase::Overlay,
            RenderPhase::UI,           RenderPhase::Debug
        };

        std::queue<RenderPhase> ready;
        for ( RenderPhase p : kDeclOrder )
        {
            auto it = inDegree.find( p );
            if ( it != inDegree.end() && it->second == 0 )
                ready.push( p );
        }

        m_PhaseOrder.clear();
        while ( !ready.empty() )
        {
            RenderPhase cur = ready.front();
            ready.pop();
            m_PhaseOrder.push_back( cur );

            for ( RenderPhase dep : dependents[cur] )
            {
                if ( --inDegree[dep] == 0 )
                    ready.push( dep );
            }
        }

        // Build the flat sorted pass list and cache RenderPass objects.
        m_SortedPasses.clear();
        for ( RenderPhase phase : m_PhaseOrder )
        {
            auto it = m_PhasePasses.find( phase );
            if ( it != m_PhasePasses.end() )
            {
                for ( auto& pass : it->second )
                {
                    if ( pass.TargetFramebuffer && !pass.CachedRenderPass )
                    {
                        pass.CachedRenderPass = RenderPass::Create( {
                             .TargetFramebuffer = pass.TargetFramebuffer,
                             .DebugName         = pass.Name,
                        } );
                    }
                    m_SortedPasses.push_back( pass );
                }
            }
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
        return m_SortedPasses;
    }

    void RenderGraphBuilder::Clear()
    {
        m_Passes.clear();
        m_PhasePasses.clear();
        m_PhaseOrder.clear();
        m_SortedPasses.clear();
        m_PhaseDependencies.clear();
        m_TextureDependencies.clear();

        LOG_DEBUG( "Render graph builder cleared" );
    }

} // namespace Desert::Graphic