#pragma once

#include "../IPanel.hpp"

#include <Editor/Widgets/PreviewViewport.hpp>
#include <Editor/Widgets/UIHelper/ImGuiUI.hpp>

#include <Engine/Assets/Common.hpp>

#include <memory>
#include <string>

namespace Desert::Assets
{
    class AssetManager;
    class SurfaceMaterialAsset;
    struct MaterialData;
}

namespace Desert::Editor
{
    // THE MATERIAL EDITOR, one window per `.demat`, opened by double-clicking the asset — UE's flow. The
    // material is drawn live on a primitive under a real key light and a procedural sky, orbitable, with its
    // parameters beside it.
    //
    // WHAT REPLACED WHAT. This is MaterialPreviewPanel, which was a SINGLETON with a material combo — one
    // window, whichever material was picked in it. UE has no such thing and neither do we any more: the
    // subject is fixed at construction, two materials are two windows, and the combo is gone with the panel.
    //
    // WHY IT CANNOT FLATTER. PreviewViewport::SetMaterial fills StaticMeshComponent::MaterialSlots — the
    // per-SLOT route, the one a real scene mesh takes. The 128 px thumbnail this lineage replaced went
    // through MaterialComponent::ShaderName, the shader-OVERRIDE route, which MeshRenderer re-seeds with
    // schema defaults every frame; that is how the old preview showed a correct material while the scene
    // showed black (Docs/MaterialEditor/STAGE1_END_TO_END.md). Desert/Tests/Editor/MaterialPreviewRoute
    // guards the distinction and fails in BOTH directions.
    //
    // COST WHEN CLOSED IS ZERO, not "small". The PreviewViewport — and with it a Scene, a SceneRenderer and
    // one of the six renderer slots — is created on the first frame the window actually draws, and released
    // when the window is DISMISSED, which destroys this panel outright (EditorLayer::
    // CloseDismissedAssetDocuments). That is the difference between a document and a tool panel: a tool is
    // hidden and kept, so it has to be told to let go of its renderer; a document ceases to exist, so it
    // cannot forget to.
    class MaterialEditorPanel final : public IAssetEditorPanel
    {
    public:
        MaterialEditorPanel( const Assets::AssetHandle&                   material,
                             const std::shared_ptr<Assets::AssetManager>& assetManager );
        ~MaterialEditorPanel() override;

        ImVec2 GetDefaultSize() const override
        {
            return ImVec2( 760.0f, 560.0f );
        }

        void OnUIRender() override;
        void OnPreUpdate() override;

        [[nodiscard]] bool HoldsRendererSlot() const override
        {
            return m_Preview != nullptr;
        }

    private:
        // @p asset is null while the material is not loaded — the toolbar still draws its view controls,
        // but the actions that write the asset are not offered rather than offered and doing nothing.
        void DrawToolbar( Assets::SurfaceMaterialAsset* asset, bool isInstance );

        // Unity-style shader picker inside the material. Base assets only: an instance always renders with
        // its parent chain's shader, so a picker on one would be a control with nothing behind it.
        // Returns true when the shader changed (the runtime material is a different CLASS and must be
        // rebuilt, not merely re-valued).
        bool DrawShaderPicker( Assets::SurfaceMaterialAsset& asset );

        // The schema-driven parameter editor — every parameter the shader declares, TEXTURES INCLUDED.
        // Moved here from the Details panel's material fold (Docs/MaterialEditor/
        // PLAN_STAGE3_ASSET_DOCUMENTS.md, M2): authoring a material is this window's job, and until the
        // move a texture could only be bound in Details because the window's own table skipped texture
        // params. Returns true when anything changed.
        //
        // @p parentData / @p isInstance: material-INSTANCE mode — the schema comes from the parent's
        // shader, non-overridden rows display the PARENT's value, edits write overrides into the child,
        // and texture rows are read-only (per-instance texture descriptors are a v2).
        bool DrawParameters( Assets::SurfaceMaterialAsset& asset, const Assets::MaterialData* parentData,
                             bool isInstance );

        // The parent of an instance subject, or null for a base material (and for an instance whose parent
        // no longer resolves — the caller shows the schema defaults rather than inventing values).
        [[nodiscard]] std::shared_ptr<Assets::SurfaceMaterialAsset>
        ResolveParent( const Assets::SurfaceMaterialAsset& asset ) const;

        // Push an edit of the subject at everything already rendering it — this window's preview AND every
        // mesh in every open scene — without either side knowing about the other. See the implementation
        // for why both halves are needed.
        void PropagateEdit( Assets::SurfaceMaterialAsset& asset, bool isInstance );

        // Write the subject back to its `.demat` and drop the thumbnail rendered from the old values.
        void SaveSubject( Assets::SurfaceMaterialAsset& asset );

        void EnsurePreview();  // create the viewport + scene + renderer (claims a slot)
        void ReleasePreview(); // destroy them (returns the slot)

        // The name of the shader this document's material actually draws with, or empty if the material is
        // gone. Recomputed rather than cached: the shader a material names is editable (this window's own
        // picker, and the Node Graph rewriting its scratch material), so a cached copy would leave the
        // window watching rebuilds of a shader it no longer uses.
        [[nodiscard]] std::string EffectiveShaderName() const;

        std::shared_ptr<Assets::AssetManager> m_AssetManager;

        // Null whenever the window is not drawing — this IS the zero-cost mechanism, not an optimisation on
        // top of one. unique_ptr rather than a value member for exactly that reason.
        std::unique_ptr<PreviewViewport> m_Preview;
        std::unique_ptr<UI::UIHelper>    m_UIHelper;

        PreviewViewport::Shape m_Shape   = PreviewViewport::Shape::Sphere;
        bool                   m_Applied = false; // the subject has been pushed into the viewport

        // The rebuild count this window has already acted on; see MaterialShaderRebuild for why it is a
        // count each window compares against rather than a pending value one of them consumes.
        uint64_t m_SeenRebuildCount = 0;

        // Set in OnUIRender, consumed in OnPreUpdate: the render is only paid for while the window really
        // drew last frame, so a hidden dock tab costs nothing even before the window is closed outright.
        bool m_DrewThisFrame = false;
    };
} // namespace Desert::Editor
