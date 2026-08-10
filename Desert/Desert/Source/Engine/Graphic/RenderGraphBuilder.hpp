#pragma once

#include "RenderPass.hpp"
#include "RenderPhase.hpp"
#include "RenderGraphSort.hpp"
#include "Pipeline.hpp"

#include <map>
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

            // Where this pass sits INSIDE its phase; lower draws first. Leave it at Default unless the
            // pass has a real reason to overrule registration order — see the AddPass comment below and
            // RenderPassOrder in RenderGraphSort.hpp.
            int32_t OrderInPhase = RenderPassOrder::Default;

            // Assigned by AddPass(); anything a caller writes here is overwritten. It is the tie-break
            // that makes the order inside a phase reproducible instead of "whatever came out".
            uint64_t RegistrationIndex = 0;

            // Cached once in Build(); reused every frame.
            std::shared_ptr<RenderPass>       CachedRenderPass;
        };

        RenderGraphBuilder();
        ~RenderGraphBuilder();

        // Passes execute in a fully defined order, and you are reading the place where you must decide
        // yours.
        //
        //   between phases — the topological order of the phase graph (AddPhaseDependency /
        //                    AddTextureDependency), ties broken by the phase registry's declaration
        //                    order;
        //   inside a phase — OrderInPhase first (lower draws first), then the order AddPass was called
        //                    in.
        //
        // Registration order is the default because it is the one thing the author of a pass can see:
        // the pass a system adds first, and the system SceneRenderer::Init registers first, draw first.
        // It is *not* a licence to encode a visual dependency in a call-site ordering hundreds of lines
        // away — if your pass must be over or under a specific neighbour, say so with `orderInPhase`,
        // and the coupling survives someone reshuffling Init.
        void AddPass( const PassConfig& config );
        void AddPass( const std::string& name, RenderPhaseID phase, std::function<void()> executeFunc,
                      const GraphicsPipelineSpecification&     pipelineSpec      = {},
                      std::shared_ptr<Framebuffer>             targetFramebuffer = nullptr,
                      const std::vector<RenderPassDependency>& dependencies      = {},
                      const std::optional<glm::vec4>&          clearColor        = std::nullopt,
                      int32_t                                  orderInPhase      = RenderPassOrder::Default );

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
        // Ordered containers throughout, on purpose: every one of these is walked while deciding the
        // frame's draw order, and an unordered_map hands out its contents in hash-bucket order — stable
        // for a given set of keys, and quietly different the moment a system is added. The sort must not
        // be able to notice which container it read from.
        std::vector<RenderPhaseID>                       m_PhaseOrder;
        std::map<RenderPhaseID, std::vector<PassConfig>> m_PhasePasses;

        RenderPhaseDependencies                                        m_PhaseDependencies;
        std::map<std::string, std::pair<RenderPhaseID, RenderPhaseID>> m_TextureDependencies;

        std::vector<PassConfig> m_SortedPasses;

        // Monotonic across the builder's lifetime, reset by Clear(). Never reused, so it is a total
        // order over the passes of one build.
        uint64_t m_NextRegistrationIndex = 0;

        void TopologicalSort();
        bool CheckForCycles() const;
    };
} // namespace Desert::Graphic
