#pragma once

#include "../IPanel.hpp"

#include <memory>

namespace Desert::Core
{
    class Scene;
}

namespace Desert::Editor
{
    // Dedicated authoring surface for the selected entity's ParticleEmitterComponent — a friendlier home than
    // the flat auto-Details: one-click presets (Fire / Smoke / Sparks / Magic / Explosion), a colour-over-life
    // gradient bar, a size-over-life curve plot (driven by the ease-power), grouped emission/motion controls
    // and a live stats readout. Hidden by default; View -> Particle Editor. Cross-panel RequestOpen so a
    // Details button can reveal it.
    class ParticleEditorPanel final : public IPanel
    {
    public:
        explicit ParticleEditorPanel( const std::shared_ptr<::Desert::Core::Scene>& scene );

        ImVec2 GetDefaultSize() const override
        {
            return ImVec2( 460.0f, 620.0f );
        }
        void OnUIRender() override;

        // Contextual: an emitter must be selected to author it.
        // NOT contextual: selecting an object is not a request to open the particle editor. It opens only when
        // asked for — a button in Details, the View menu, or the command palette (Core::PanelRequests).
        bool IsContextual() const override
        {
            return false;
        }
        bool IsRelevant() const override;
        void OnPreUpdate() override;
        void SetScene( const std::shared_ptr<Desert::Core::Scene>& scene ) override
        {
            m_Scene = scene;
        }

        static void RequestOpen();

    private:
        std::shared_ptr<::Desert::Core::Scene> m_Scene;
    };
} // namespace Desert::Editor
