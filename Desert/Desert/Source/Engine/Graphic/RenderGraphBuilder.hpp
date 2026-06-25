#pragma once

#include "RenderPass.hpp"
#include "RenderPhase.hpp"
#include "Pipeline.hpp"

#include <set>
#include <optional>

namespace Desert::Graphic
{
    struct RenderPassDependency
    {
        RenderPhaseID RequiredPhase;
        std::string   RequiredTexture;

        RenderPassDependency( RenderPhaseID phase   = RenderPhase::None,
                              const std::string& texture = "" )
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
            RenderPhaseID                     Phase;
            std::function<void()>             ExecuteFunc;
            GraphicsPipelineSpecification     PipelineSpec;
            std::shared_ptr<Framebuffer>      TargetFramebuffer;
            std::vector<RenderPassDependency> Dependencies;

            // Optional per-pass color clear override. When unset the default (0.1 grey) is used. The
            // shadow pass needs 1.0 here so its R32F depth target clears to "far" (no occluder) — a 0.1
            // background would falsely occlude any receiver whose depth exceeds it.
            std::optional<glm::vec4>          ClearColor;

            // Cached once in Build(); reused every frame.
            std::shared_ptr<RenderPass>       CachedRenderPass;
        };

        RenderGraphBuilder();
        ~RenderGraphBuilder();

        void AddPass( const PassConfig& config );
        void AddPass( const std::string& name, RenderPhaseID phase,
                      std::function<void()>                    executeFunc,
                      const GraphicsPipelineSpecification&     pipelineSpec      = {},
                      std::shared_ptr<Framebuffer>             targetFramebuffer = nullptr,
                      const std::vector<RenderPassDependency>& dependencies      = {},
                      const std::optional<glm::vec4>&          clearColor        = std::nullopt );

        void AddPhaseDependency( RenderPhaseID requiredPhase, RenderPhaseID dependentPhase );
        void AddTextureDependency( const std::string& textureName,
                                   RenderPhaseID producerPhase,
                                   RenderPhaseID consumerPhase );

        bool Build();
        void Clear();

        const std::vector<PassConfig>&    GetSortedPasses() const;
        const std::vector<RenderPhaseID>& GetPhaseOrder() const
        {
            return m_PhaseOrder;
        }

        bool ValidateDependencies() const;

    private:
        struct InternalPassData
        {
            PassConfig               Config;
            size_t                   ExecutionOrder;
            std::set<RenderPhaseID>  RequiredPhases;
        };

        std::vector<InternalPassData>                                m_Passes;
        std::vector<RenderPhaseID>                                   m_PhaseOrder;
        std::unordered_map<RenderPhaseID, std::vector<PassConfig>>   m_PhasePasses;

        std::unordered_map<RenderPhaseID, std::set<RenderPhaseID>>                    m_PhaseDependencies;
        std::unordered_map<std::string, std::pair<RenderPhaseID, RenderPhaseID>>      m_TextureDependencies;

        std::vector<PassConfig> m_SortedPasses;

        void TopologicalSort();
        bool CheckForCycles() const;
    };
} // namespace Desert::Graphic
