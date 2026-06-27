#include "RenderGraphBuilder.hpp"
#include "RenderPhaseRegistry.hpp"
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
                passData.RequiredPhases.insert( dep.RequiredPhase );
        }

        m_Passes.push_back( passData );
        m_PhasePasses[config.Phase].push_back( config );
    }

    void RenderGraphBuilder::AddPass( const std::string& name, RenderPhaseID phase,
                                      std::function<void()>                    executeFunc,
                                      const GraphicsPipelineSpecification&     pipelineSpec,
                                      std::shared_ptr<Framebuffer>             targetFramebuffer,
                                      const std::vector<RenderPassDependency>& dependencies,
                                      const std::optional<glm::vec4>&          clearColor )
    {
        PassConfig config;
        config.Name              = name;
        config.Phase             = phase;
        config.ExecuteFunc       = executeFunc;
        config.PipelineSpec      = pipelineSpec;
        config.TargetFramebuffer = targetFramebuffer;
        config.Dependencies      = dependencies;
        config.ClearColor        = clearColor;

        InternalPassData passData;
        passData.Config = config;

        for ( const auto& dep : config.Dependencies )
        {
            if ( dep.RequiredPhase != RenderPhase::None )
                passData.RequiredPhases.insert( dep.RequiredPhase );
        }

        m_Passes.push_back( passData );
        m_PhasePasses[phase].push_back( config );

        LOG_DEBUG( "Added pass '{}' to phase '{}' with {} dependencies", name,
                   RenderPhaseToString( phase ), dependencies.size() );
    }

    void RenderGraphBuilder::AddPhaseDependency( RenderPhaseID requiredPhase, RenderPhaseID dependentPhase )
    {
        m_PhaseDependencies[dependentPhase].insert( requiredPhase );
    }

    void RenderGraphBuilder::AddTextureDependency( const std::string& textureName,
                                                   RenderPhaseID      producerPhase,
                                                   RenderPhaseID      consumerPhase )
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
        // m_PhaseDependencies[B] = { A } means B depends on A (A must execute before B).

        std::unordered_map<RenderPhaseID, int>                     inDegree;
        std::unordered_map<RenderPhaseID, std::vector<RenderPhaseID>> dependents;

        for ( const auto& [phase, _] : m_PhasePasses )
            inDegree.emplace( phase, 0 );

        for ( const auto& [dependent, prereqs] : m_PhaseDependencies )
        {
            inDegree.emplace( dependent, 0 );
            for ( RenderPhaseID prereq : prereqs )
            {
                inDegree.emplace( prereq, 0 );
                dependents[prereq].push_back( dependent );
            }
        }

        for ( const auto& [dependent, prereqs] : m_PhaseDependencies )
            inDegree[dependent] += static_cast<int>( prereqs.size() );

        // Seed the ready-queue in registry declaration order so phases without
        // dependencies (or with equal priority) appear in a stable, predictable sequence.
        // User-registered phases appear after built-ins, in their registration order.
        const auto& declOrder = RenderPhaseRegistry::GetInstance().GetDeclarationOrder();
        std::queue<RenderPhaseID> ready;
        for ( RenderPhaseID p : declOrder )
        {
            auto it = inDegree.find( p );
            if ( it != inDegree.end() && it->second == 0 )
                ready.push( p );
        }

        m_PhaseOrder.clear();
        while ( !ready.empty() )
        {
            RenderPhaseID cur = ready.front();
            ready.pop();
            m_PhaseOrder.push_back( cur );

            for ( RenderPhaseID dep : dependents[cur] )
            {
                if ( --inDegree[dep] == 0 )
                    ready.push( dep );
            }
        }

        // Build the flat sorted pass list and cache RenderPass objects.
        m_SortedPasses.clear();
        for ( RenderPhaseID phase : m_PhaseOrder )
        {
            auto it = m_PhasePasses.find( phase );
            if ( it != m_PhasePasses.end() )
            {
                for ( auto& pass : it->second )
                {
                    if ( pass.TargetFramebuffer && !pass.CachedRenderPass )
                    {
                        RenderPassSpecification rpSpec;
                        rpSpec.TargetFramebuffer = pass.TargetFramebuffer;
                        rpSpec.DebugName         = pass.Name;
                        if ( pass.ClearColor )
                            rpSpec.ClearColor.Color = *pass.ClearColor;
                        pass.CachedRenderPass = RenderPass::Create( rpSpec );
                    }
                    m_SortedPasses.push_back( pass );
                }
            }
        }
    }

    bool RenderGraphBuilder::CheckForCycles() const
    {
        std::unordered_map<RenderPhaseID, std::vector<RenderPhaseID>> graph;
        std::unordered_map<RenderPhaseID, int>                        visited;

        for ( const auto& [phase, deps] : m_PhaseDependencies )
        {
            for ( RenderPhaseID dep : deps )
                graph[phase].push_back( dep );
        }

        std::function<bool( RenderPhaseID )> hasCycle = [&]( RenderPhaseID phase )
        {
            if ( visited[phase] == 1 )
                return true;
            if ( visited[phase] == 2 )
                return false;

            visited[phase] = 1;
            for ( RenderPhaseID neighbor : graph[phase] )
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
