#pragma once

#include "IPanel.hpp"

#include <Editor/Widgets/PreviewViewport.hpp>
#include <Editor/Widgets/UIHelper/ImGuiUI.hpp>

#include <Engine/Assets/Common.hpp>

#include <memory>
#include <string>

namespace Desert::Assets
{
    class AssetManager;
}

namespace Desert::Editor
{
    // A MATERIAL EDITOR window: the material an artist is working on, drawn live on a primitive under a
    // real key light and a procedural sky, orbitable, next to its parameters.
    //
    // WHY THIS IS NOT THE OLD 128 px THUMBNAIL. The Node Graph panel used to show a small PNG rendered by
    // AssetThumbnailRenderer, which drives its sphere through MaterialComponent::ShaderName — the
    // shader-OVERRIDE route. That route re-applies schema defaults every frame and is a different code path
    // in MeshRenderer from the one a scene mesh takes, so the thumbnail could show a correct material while
    // the scene showed something else. It did exactly that: see Docs/MaterialEditor/STAGE1_END_TO_END.md.
    // This panel previews a MATERIAL ASSET through PreviewViewport::SetMaterial, which fills
    // StaticMeshComponent::MaterialSlots — the same per-slot route a real mesh uses — so preview and
    // viewport are the same picture by construction rather than by coincidence. MaterialPreviewRoute in
    // Desert/Tests/Editor/MaterialPreviewRoute asserts that and fails in BOTH directions.
    //
    // COST WHEN CLOSED IS ZERO, not "small". The PreviewViewport (and with it a Scene, a SceneRenderer and
    // one of the six renderer slots) is created on the first frame the window is actually drawn and
    // DESTROYED as soon as it is not. A closed window holds no slot and records no passes; the proof is a
    // byte-identical frame against a build that has no panel at all.
    class MaterialPreviewPanel final : public IPanel
    {
    public:
        explicit MaterialPreviewPanel( const std::shared_ptr<Assets::AssetManager>& assetManager );
        ~MaterialPreviewPanel() override;

        ImVec2 GetDefaultSize() const override
        {
            return ImVec2( 760.0f, 560.0f );
        }

        void OnUIRender() override;
        void OnPreUpdate() override;

        // Cross-panel inbox, same shape as NodeGraphPanel::RequestOpen: show this material and open the
        // window. Used by the Node Graph's Compile so a freshly built shader is looked at immediately.
        static void RequestPreview( const Assets::AssetHandle& material );

        // Cross-panel inbox: the shader behind the previewed material was rebuilt, so this panel's OWN
        // pipeline cache must drop what it built from the old modules.
        //
        // AssetHotReload::PollShaders invalidates only the pipeline cache of the scene handed to Tick — the
        // main one. Every live SceneRenderer owns its own cache, so a preview that did not hear about the
        // rebuild would keep drawing the OLD shader while the viewport drew the new one. That is the
        // stage-1 defect in new clothes: a preview that disagrees with the game.
        static void RequestShaderRebuilt( const std::string& shaderName );

    private:
        void DrawToolbar();
        void DrawParameters();
        void EnsurePreview();  // create the viewport + scene + renderer (claims a slot)
        void ReleasePreview(); // destroy them (returns the slot)

        std::shared_ptr<Assets::AssetManager> m_AssetManager;

        // Null whenever the window is closed — this IS the zero-cost mechanism, not an optimisation on top
        // of one. unique_ptr rather than a value member for exactly that reason.
        std::unique_ptr<PreviewViewport> m_Preview;
        std::unique_ptr<UI::UIHelper>    m_UIHelper;

        Assets::AssetHandle    m_Material{ static_cast<uint64_t>( 0 ) };
        PreviewViewport::Shape m_Shape   = PreviewViewport::Shape::Sphere;
        bool                   m_Applied = false; // m_Material has been pushed into the viewport

        // Set in OnUIRender, consumed in OnPreUpdate: the render is only paid for while the window really
        // drew last frame, so a hidden dock tab costs nothing even before the window is closed outright.
        // (The Details panel uses the same flag; see ScenePropertiesPanel::OnPreUpdate.)
        bool m_DrewThisFrame = false;

        std::string m_Status;
    };
} // namespace Desert::Editor
