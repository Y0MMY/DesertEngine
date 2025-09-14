#pragma once

#include "RenderPass.hpp"
#include "RenderPhase.hpp"
#include "Pipeline.hpp"

#include <set>

namespace Desert::Graphic
{
    struct RenderPassDependency
    {
        RenderPhase RequiredPhase;
        std::string RequiredTexture;

        RenderPassDependency( RenderPhase phase = RenderPhase::None, const std::string& texture = "" )
             : RequiredPhase( phase ), RequiredTexture( texture )
        {
        }
    };

    class RenderGraphBuilder
    {
    public:
        struct PassConfig
        {
            std::string                       Name;
            RenderPhase                       Phase;
            std::function<void()>             ExecuteFunc;
            PipelineSpecification             PipelineSpec;
            std::shared_ptr<Framebuffer>      TargetFramebuffer;
            std::vector<RenderPassDependency> Dependencies;
        };

        RenderGraphBuilder();
        ~RenderGraphBuilder();

        void AddPass( const PassConfig& config );
        void AddPass( const std::string& name, RenderPhase phase, std::function<void()> executeFunc,
                      const PipelineSpecification&             pipelineSpec      = {},
                      std::shared_ptr<Framebuffer>             targetFramebuffer = nullptr,
                      const std::vector<RenderPassDependency>& dependencies      = {} );

        void AddPhaseDependency( RenderPhase requiredPhase, RenderPhase dependentPhase );
        void AddTextureDependency( const std::string& textureName, RenderPhase producerPhase,
                                   RenderPhase consumerPhase );

        bool Build();
        void Clear();

        const std::vector<PassConfig>&  GetSortedPasses() const;
        const std::vector<RenderPhase>& GetPhaseOrder() const
        {
            return m_PhaseOrder;
        }

        bool ValidateDependencies() const;

    private:
        struct InternalPassData
        {
            PassConfig            Config;
            size_t                ExecutionOrder;
            std::set<RenderPhase> RequiredPhases;
        };

        std::vector<InternalPassData>                            m_Passes;
        std::vector<RenderPhase>                                 m_PhaseOrder;
        std::unordered_map<RenderPhase, std::vector<PassConfig>> m_PhasePasses;

        std::unordered_map<RenderPhase, std::set<RenderPhase>>               m_PhaseDependencies;
        std::unordered_map<std::string, std::pair<RenderPhase, RenderPhase>> m_TextureDependencies;

        void TopologicalSort();
        bool CheckForCycles() const;
    };
} // namespace Desert::Graphic