#pragma once

// DELIBERATELY NOT AssetOpenRequest.hpp, even though this is what services one. That header opens
// `namespace Desert::Editor::Core`, and this one is included by EditorLayer.hpp — ahead of the render-system
// headers, which spell Desert::Core::Scene as an unqualified `Core::Scene` from inside Desert::Editor. Make
// Desert::Editor::Core visible before them and every one of those names silently rebinds to the wrong
// namespace. PreviewViewport.hpp carries a note about the same trap. So the registry takes the two fields of
// a request rather than the request, and nothing here drags that namespace along.
#include <Editor/Panels/IPanel.hpp>

#include <Engine/Assets/Common.hpp>

#include <functional>
#include <memory>
#include <unordered_map>

namespace Desert::Editor
{
    // WHICH EDITOR OPENS WHICH KIND OF ASSET. One entry per asset type; the editor for a `.demat` is
    // registered by EditorLayer at startup and the next kind is a second registration, not a second branch
    // in the asset browser.
    //
    // The registry deliberately does NOT own the documents it makes: it holds factories, and the documents
    // they build belong to Editor/Core/DocumentWell.hpp. There is EXACTLY ONE owner of open documents and
    // therefore exactly one answer to "which documents exist" — a second container here would be a second
    // answer, and the two would disagree the first frame a close was handled halfway; the editor already
    // paid for that shape once (Editor/Core/SceneViewIdentity.hpp). Open-or-focus is DocumentWell::Find.
    //
    // The well is a separate owner from the TOOL panels for a different reason again, and that one is about
    // lifetime rather than bookkeeping: a tool's visibility is a setting the user keeps and a document's
    // existence is not, so one flag cannot serve both. See Editor/Core/PanelRegistry.hpp.
    class AssetEditorRegistry
    {
    public:
        // Builds the document window for one asset. Given only the subject handle: everything else the
        // editor needs (the asset manager, the shape of the window) belongs to whatever registers the
        // factory, captured there once instead of threaded through this call.
        using Factory = std::function<std::unique_ptr<IAssetEditorPanel>( const Assets::AssetHandle& )>;

        // Registering a second factory for a type REPLACES the first and says so: two editors for one asset
        // kind is a programming error, and the silent winner would be whichever registration ran last.
        void Register( Assets::AssetTypeID type, Factory factory );

        [[nodiscard]] bool HasEditorFor( Assets::AssetTypeID type ) const noexcept;

        // The document for @p subject, or null when no editor is registered for @p type — logged with the
        // type's name, because "double-clicking it did nothing" is otherwise indistinguishable from a window
        // that failed to draw. Never returns a panel for a null subject handle.
        [[nodiscard]] std::unique_ptr<IAssetEditorPanel> Create( const Assets::AssetHandle& subject,
                                                                 Assets::AssetTypeID        type ) const;

    private:
        std::unordered_map<Assets::AssetTypeID, Factory> m_Factories;
    };

    // FindOpenAssetDocument USED TO LIVE HERE, and its removal is the point of the change that took it out.
    //
    // It searched a PANEL LIST for a document, by dynamic_cast, because documents were mixed in among the
    // tools and had to be sifted back out at every site that wanted one. That cast is gone from every such
    // site now: documents have their own owner, so "the already-open document for this subject" is
    // DocumentWell::Find and there is nothing to sift. Keeping this function beside it would have left two
    // functions answering one question — the very thing the note above says the editor has already paid for
    // once — and the survivor would have been the one that could still be pointed at the wrong container.

    // How many renderer slots the open documents have SPOKEN FOR but not yet taken.
    //
    // The cap is checked as `live + pending >= kMaxRendererSlots`, and `live` counts renderers that exist.
    // A document is created before it first draws, and a Material Editor builds its PreviewViewport on that
    // first frame — so between the two it holds no slot and has a claim coming. Counting only live
    // renderers would admit a document there is no slot for and discover it a frame later, with the
    // symptom being two surfaces quietly trading each other's per-frame camera.
    //
    // A DOCUMENT THAT WILL NEVER CLAIM ONE IS NOT PENDING DEMAND, and that half is not symmetry for its own
    // sake: the four cloud documents bake on the CPU and upload an Image2D, so five of them open beside the
    // main viewport would reach the cap on paper and the sixth would be refused — with a census telling the
    // user to close windows that were holding nothing and would never hold anything. See
    // IAssetEditorPanel::ClaimsRendererSlot.
    //
    // A free function over the range, rather than a loop inside EditorLayer, for the reason
    // FindOpenAssetDocument above is one: EditorLayer.cpp is compiled by no suite
    // (scripts/CI/UnreachedSources.sh), so a rule written there is a rule nothing can assert. Templated on
    // the range so a test can drive it with a plain vector and no editor anywhere near.
    template <typename Range>
    [[nodiscard]] uint32_t PendingRendererSlotDemand( const Range& panels )
    {
        uint32_t pending = 0;
        for ( const auto& panel : panels )
        {
            const auto* document = dynamic_cast<const IAssetEditorPanel*>( &*panel );
            if ( document && document->ClaimsRendererSlot() && !document->HoldsRendererSlot() )
                ++pending;
        }
        return pending;
    }
} // namespace Desert::Editor
