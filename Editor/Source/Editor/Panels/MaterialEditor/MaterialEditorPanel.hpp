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

        // NOT ALWAYS — and that is the point. A material whose shader draws no mesh geometry (a
        // Terrain-domain one; see PreviewUnavailableReason) never builds a PreviewViewport at all, so it
        // is not demand for a renderer slot that has yet to land. Answering the base class's `true` here
        // would make such a window count against the six for ever, and the census would tell the user to
        // close a window that holds nothing and never will — the exact failure the four cloud documents
        // caused before IAssetEditorPanel::ClaimsRendererSlot existed.
        //
        // Read from the same string the pane prints, so the census and what the artist is looking at
        // cannot disagree. Empty before the first draw, which is the conservative answer: a window that
        // has not decided yet is counted as a claimant.
        [[nodiscard]] bool ClaimsRendererSlot() const override
        {
            return m_PreviewUnavailable.empty();
        }

    private:
        // @p asset is null while the material is not loaded — the toolbar still draws its view controls,
        // but the actions that write the asset are not offered rather than offered and doing nothing.
        void DrawToolbar( Assets::SurfaceMaterialAsset* asset, bool isInstance );

        // Unity-style shader picker inside the material. Base assets only: an instance always renders with
        // its parent chain's shader, so a picker on one would be a control with nothing behind it.
        // Returns true when the shader changed (the runtime material is a different CLASS and must be
        // rebuilt, not merely re-valued).
        //
        // THE LIST IS THIS MATERIAL'S OWN DOMAIN, not a fixed one. It used to filter on a hardcoded
        // `Domain != Surface`, so a Terrain material displayed `Terrain` over a list that could not
        // contain it: the one value the combo was showing was the one value it could not reproduce, and
        // any click at all moved the material into another domain — where its parameters name uniform
        // fields the new shader does not have. Nothing downstream rejects that mismatch (it builds a
        // valid pipeline and draws wrong), so the picker is the only place it can be prevented.
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

        // Why this material cannot be shown on a preview primitive, or empty while it can.
        //
        // The pane draws a sphere, a cube or a plane through the ordinary mesh path, so a material only
        // appears in it if its shader draws MESH GEOMETRY. Both of the engine's Terrain-domain shaders
        // synthesize their geometry from `gl_VertexIndex` instead — Terrain.shader as a control-point
        // patch grid for the tessellator, Grass.shader as indirect blade instances — and neither has
        // anything that could be fed by a primitive's vertex buffer. Nothing errors and nothing crashes:
        // the pane simply renders an empty scene, and a grey rectangle that explains nothing is the
        // silent fallback the delivery contract forbids (§1.4). An artist cannot tell "this domain has no
        // preview shape" from "the preview is broken", and the difference decides whether they go looking
        // for a bug.
        //
        // Also answers for the two states that are not about the domain at all — a shader that is not
        // loaded, and one that is registered but has no compiled stages (ShaderService keeps the NAME
        // either way, deliberately, and MeshRenderer skips the draw) — because an empty pane means the
        // same thing to the eye in all three cases and something different in each.
        [[nodiscard]] std::string PreviewUnavailableReason( const std::string& shaderName ) const;

        // The pane when there is no image: the same rectangle as before, with the reason written inside
        // it. Inside, not underneath — the message has to be where the picture would have been, or it is
        // one more line in a column of labels.
        void DrawPreviewPlaceholder( float side, const std::string& reason ) const;

        // The name of the shader this document's material actually draws with, or empty if the material is
        // gone. Recomputed rather than cached: the shader a material names is editable (this window's own
        // picker, and the Node Graph rewriting its scratch material), so a cached copy would leave the
        // window watching rebuilds of a shader it no longer uses.
        //
        // An INSTANCE resolves through its parent, because an instance has no shader of its own — that is
        // why it is shown no picker. Reading the child's own name gave "StaticMeshPBR", the default a
        // material with no name reports, so an instance window watched rebuilds of a shader it does not
        // draw with and kept its pipelines from before the parent shader's recompile.
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

        // PreviewUnavailableReason for the material as it stood on the last drawn frame; empty means the
        // pane shows a real render. Computed in OnUIRender and read by OnPreUpdate on the next frame —
        // the same one-frame handshake m_DrewThisFrame uses, and for the same reason: OnPreUpdate is the
        // only place allowed to build or destroy the renderer, and it must not resolve the shader a
        // second time and risk answering differently from the message already on screen.
        std::string m_PreviewUnavailable;
    };
} // namespace Desert::Editor
