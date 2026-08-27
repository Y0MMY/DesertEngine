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
    // The registry deliberately does NOT own the documents it makes. EditorLayer owns every panel, and an
    // asset document is a panel — that is what makes it dockable, focusable and torn down by the existing
    // shutdown path with no new code. A second container of open documents here would be a second answer to
    // "which documents exist", and the two would disagree the first frame a close was handled halfway; the
    // editor already paid for that shape once (Editor/Core/SceneViewIdentity.hpp). So open-or-focus is
    // answered by looking at the PANEL LIST — see FindOpenAssetDocument below, which is the same range +
    // projection form IndexOfSceneView uses and for the same reason.
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

    // The already-open document for @p subject in @p panels, or nullptr.
    //
    // A range over the panels the editor already owns, rather than a map kept beside them: the subject lives
    // in the document and nowhere else, so there is no second copy to fall out of step when a window is
    // closed. Templated on the range so a test can drive it with a plain vector and no editor anywhere near.
    template <typename Range>
    [[nodiscard]] IAssetEditorPanel* FindOpenAssetDocument( const Range&               panels,
                                                            const Assets::AssetHandle& subject )
    {
        if ( static_cast<uint64_t>( subject ) == 0 )
            return nullptr; // the null handle is "no asset", never a document to focus

        for ( const auto& panel : panels )
        {
            auto* document = dynamic_cast<IAssetEditorPanel*>( &*panel );
            if ( document && document->Subject() == subject )
                return document;
        }
        return nullptr;
    }
} // namespace Desert::Editor
