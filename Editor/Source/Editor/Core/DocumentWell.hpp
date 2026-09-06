#pragma once

// DELIBERATELY NOT AssetOpenRequest.hpp, for the reason AssetEditorRegistry.hpp gives at its own top: that
// header opens `namespace Desert::Editor::Core`, and this one is included by EditorLayer.hpp ahead of the
// render-system headers, which spell Desert::Core::Scene as an unqualified `Core::Scene` from inside
// Desert::Editor. Make Desert::Editor::Core visible before them and every one of those names silently
// rebinds to the wrong namespace.
#include <Editor/Panels/IPanel.hpp>

#include <Engine/Assets/Common.hpp>

#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Desert::Editor
{
    // THE OPEN DOCUMENTS, AND ONLY THE DOCUMENTS.
    //
    // The other half of the split PanelRegistry describes: tools live there, documents live here, and
    // nothing lives in both. A tool is a setting the user keeps; a document is a window over one asset that
    // exists only while that asset is being edited, and closing it is a DESTRUCTION — that is what returns
    // its Scene, its SceneRenderer and one of the six renderer slots.
    //
    // Because the two are separate owners, the View menu, the command palette and `--open-panel` cannot
    // list a document: they are loops over the registry, and there are no documents in it. That is the
    // point. The alternative on offer was a predicate on each of those loops, which would have left the same
    // trap in three places at once and made every later loop responsible for knowing about it.
    //
    // WHAT THIS CLASS DOES NOT DO. It does not destroy anything on its own. Release() hands the document
    // back to the caller, because destruction has to happen behind a device-idle wait and between frames —
    // the ordering ~PreviewViewport and CloseSceneView both established. The well knows WHICH document is
    // going and what to remember about it; the editor knows WHEN it is safe to let go.
    //
    // Nothing here touches ImGui, the renderer or a global, so the whole thing is drivable by a test with a
    // stub document — which is the only way a rule in this editor gets asserted at all (EditorLayer.cpp is
    // compiled by no suite, scripts/CI/UnreachedSources.sh).

    // The visible half of a document's name. A document's panel name is "<display>###assetdoc<handle>"
    // (AssetDocumentTitle), and every place that shows one to a person wants the part before the "###":
    // a user closes a window titled "M_Crate_Painted", not one titled
    // "M_Crate_Painted###assetdoc3333333333333333333". One function rather than the three separate
    // find-and-erase copies this rule used to have.
    [[nodiscard]] inline std::string DocumentDisplayName( const std::string& panelName )
    {
        std::string label = panelName;
        if ( const auto pos = label.find( "###" ); pos != std::string::npos )
            label.erase( pos );
        return label;
    }

    // A document that WAS open. Kept by value, because the panel it came from is gone by the time anyone
    // reads this: a pointer here would be the one dangling reference the whole split exists to avoid.
    struct ClosedDocument
    {
        std::string         DisplayName;
        Assets::AssetHandle Subject;
        Assets::AssetTypeID Type = Assets::AssetTypeID::Unknown;
    };

    class DocumentWell
    {
    public:
        // How many closed documents the empty state offers back. Long enough to undo a mistaken close and
        // short enough that the list is read rather than scanned.
        static constexpr std::size_t kRecentlyClosedLimit = 6;

        [[nodiscard]] std::size_t Count() const noexcept
        {
            return m_Documents.size();
        }

        [[nodiscard]] bool Empty() const noexcept
        {
            return m_Documents.empty();
        }

        [[nodiscard]] const std::vector<std::unique_ptr<IAssetEditorPanel>>& Documents() const noexcept
        {
            return m_Documents;
        }

        [[nodiscard]] auto begin() noexcept
        {
            return m_Documents.begin();
        }
        [[nodiscard]] auto end() noexcept
        {
            return m_Documents.end();
        }
        [[nodiscard]] auto begin() const noexcept
        {
            return m_Documents.begin();
        }
        [[nodiscard]] auto end() const noexcept
        {
            return m_Documents.end();
        }

        // The open document for @p subject, or nullptr. The null handle is "no asset" and never a document.
        [[nodiscard]] IAssetEditorPanel* Find( const Assets::AssetHandle& subject ) const
        {
            if ( static_cast<uint64_t>( subject ) == 0 )
                return nullptr;

            for ( const auto& document : m_Documents )
                if ( document->Subject() == subject )
                    return document.get();
            return nullptr;
        }

        // Takes ownership of a freshly-built document and makes it the most recently used one.
        IAssetEditorPanel& Add( std::unique_ptr<IAssetEditorPanel> document )
        {
            IAssetEditorPanel& ref = *document;
            m_Documents.emplace_back( std::move( document ) );
            Touch( ref.Subject() );
            return ref;
        }

        // Marks @p subject as the most recently used document. Focusing one is the only thing that reorders
        // the Ctrl+Tab ring; drawing one does not, or the ring would reorder itself every frame and Ctrl+Tab
        // would never leave the front two.
        void Touch( const Assets::AssetHandle& subject )
        {
            if ( !Find( subject ) )
                return;
            std::erase( m_MostRecent, subject );
            m_MostRecent.insert( m_MostRecent.begin(), subject );
        }

        // HANDS THE DOCUMENT BACK rather than destroying it: see the note above. Removes it from the well
        // and from the ring, and records it under RecentlyClosed. Returns nullptr for a subject that is not
        // open — a second close of one window in one frame, which is not an error.
        [[nodiscard]] std::unique_ptr<IAssetEditorPanel> Release( const Assets::AssetHandle& subject )
        {
            const auto it = std::find_if( m_Documents.begin(), m_Documents.end(),
                                          [&subject]( const std::unique_ptr<IAssetEditorPanel>& document )
                                          { return document->Subject() == subject; } );
            if ( it == m_Documents.end() )
                return nullptr;

            std::unique_ptr<IAssetEditorPanel> released = std::move( *it );
            m_Documents.erase( it );
            std::erase( m_MostRecent, subject );

            RememberClosed( ClosedDocument{ DocumentDisplayName( released->GetName() ), released->Subject(),
                                            released->SubjectType() } );
            return released;
        }

        // Every open document, in one call, for "Close All". Same contract as Release: the caller destroys
        // them, once, behind one device-idle wait rather than one per window.
        [[nodiscard]] std::vector<std::unique_ptr<IAssetEditorPanel>> ReleaseAll()
        {
            std::vector<std::unique_ptr<IAssetEditorPanel>> released;
            released.reserve( m_Documents.size() );
            // Front to back, so the recently-closed list ends up newest-first in the order they were opened
            // rather than in the order the vector happened to hold them.
            for ( auto& document : m_Documents )
            {
                RememberClosed( ClosedDocument{ DocumentDisplayName( document->GetName() ), document->Subject(),
                                                document->SubjectType() } );
                released.emplace_back( std::move( document ) );
            }
            m_Documents.clear();
            m_MostRecent.clear();
            return released;
        }

        // CTRL+TAB. The document after @p current in most-recently-used order, wrapping to the front. This
        // is what makes ten open documents bearable: the tab you want is usually the one you were just in,
        // and past about six the strip has it off-screen.
        //
        // nullopt when there is nothing to switch to (fewer than two open). A @p current that is not open —
        // the focus is on a tool, or on nothing — answers with the most recently used document, which is
        // where "back to what I was editing" should land.
        [[nodiscard]] std::optional<Assets::AssetHandle> NextMostRecent( const Assets::AssetHandle& current ) const
        {
            if ( m_MostRecent.empty() )
                return std::nullopt;

            const auto it = std::find( m_MostRecent.begin(), m_MostRecent.end(), current );
            if ( it == m_MostRecent.end() )
                return m_MostRecent.front();

            if ( m_MostRecent.size() < 2 )
                return std::nullopt;

            const auto next = std::next( it );
            return next == m_MostRecent.end() ? m_MostRecent.front() : *next;
        }

        // Most recently used first. The Documents menu and the Ctrl+Tab ring read the same order.
        [[nodiscard]] const std::vector<Assets::AssetHandle>& MostRecentOrder() const noexcept
        {
            return m_MostRecent;
        }

        // Newest first, capped at kRecentlyClosedLimit. What the empty area offers instead of being blank.
        [[nodiscard]] const std::vector<ClosedDocument>& RecentlyClosed() const noexcept
        {
            return m_RecentlyClosed;
        }

    private:
        void RememberClosed( ClosedDocument closed )
        {
            // Reopening and re-closing one asset must not fill the list with copies of it.
            std::erase_if( m_RecentlyClosed, [&closed]( const ClosedDocument& previous )
                           { return previous.Subject == closed.Subject; } );
            m_RecentlyClosed.insert( m_RecentlyClosed.begin(), std::move( closed ) );
            if ( m_RecentlyClosed.size() > kRecentlyClosedLimit )
                m_RecentlyClosed.resize( kRecentlyClosedLimit );
        }

        std::vector<std::unique_ptr<IAssetEditorPanel>> m_Documents;
        // Subjects, most recent first. Subjects and not pointers: a handle cannot dangle, and the whole
        // point of the split is that a document's lifetime is short.
        std::vector<Assets::AssetHandle> m_MostRecent;
        std::vector<ClosedDocument>      m_RecentlyClosed;
    };

    // How many of the six renderer slots the OPEN DOCUMENTS are holding right now.
    //
    // Beside PendingRendererSlotDemand (AssetEditorRegistry.hpp), which answers the other half: that one
    // counts claims that have not landed, this one counts the ones that have. Together they are what the
    // status bar shows and what a refusal has to be able to explain, and both are free functions over a
    // range for the same reason — EditorLayer.cpp is compiled by no suite, so a rule written there is a rule
    // nothing can assert.
    template <typename Range>
    [[nodiscard]] uint32_t RendererSlotsHeldByDocuments( const Range& documents )
    {
        uint32_t held = 0;
        for ( const auto& document : documents )
            if ( document && document->HoldsRendererSlot() )
                ++held;
        return held;
    }

    // THE PARTITION, COUNTED. The editor draws the tools and then the documents, and the two owners must
    // between them account for every panel exactly once: a panel in neither is one nobody can reach, and a
    // panel in both is the shared-container defect coming back wearing a second name.
    struct PanelCensus
    {
        std::size_t Tools     = 0;
        std::size_t Documents = 0;
        std::size_t Total     = 0;
        // Whether the tool side is free of documents. False is the defect this whole split removes: it is
        // exactly the state in which the View menu can list a document again.
        bool ToolsHoldNoDocument = true;
    };

    template <typename PanelRange, typename DocumentRange>
    [[nodiscard]] PanelCensus CensusOfPanels( const PanelRange& tools, const DocumentRange& documents )
    {
        PanelCensus census;
        for ( const auto& tool : tools )
        {
            ++census.Tools;
            ++census.Total;
            if ( dynamic_cast<const IAssetEditorPanel*>( &*tool ) )
                census.ToolsHoldNoDocument = false;
        }
        for ( const auto& document : documents )
        {
            (void)document;
            ++census.Documents;
            ++census.Total;
        }
        return census;
    }
} // namespace Desert::Editor
