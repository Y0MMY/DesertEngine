// A TOOL AND A DOCUMENT MUST NOT SHARE AN OWNER, AND THE EDITOR MUST NOT BE ABLE TO CONFUSE THEM.
//
// Tool panels and asset documents have opposite lifetimes. A tool is built once at startup and lives until
// the editor exits; its visibility is a SETTING the user keeps, and hiding it is the whole of closing it. A
// document is built for one asset and DESTROYED when its window is dismissed -- that destruction is what
// returns the Scene, the SceneRenderer and one of the six renderer slots.
//
// They used to live in one vector behind one visibility bool, and the defect that follows from it is not a
// bug in any function: unticking a document in the View menu set GetVisibility() false, the close pass read
// that as "the user dismissed this window" and destroyed the object on the next frame, and re-ticking could
// not bring it back because there was nothing left to tick. Any single-flag design has that property. So the
// fix is two owners, and these are the relations that say the split is real:
//
//   1. The tool container CANNOT HOLD A DOCUMENT -- checked by the compiler, stated here as an expression so
//      a suite can show it going red rather than merely failing to build.
//   2. The two owners PARTITION the editor's panels: every panel is in exactly one, and no document is among
//      the tools. That second half is the state in which the View menu could list a document again.
//   3. A CLOSED DOCUMENT GIVES ITS SLOT BACK. This is the whole reason a document cannot be "hidden": a
//      hidden panel still owns its renderer, so the lease is only returned by destruction.
//   4. Closing is not hiding: a closed document is GONE from the well and REOPENABLE from what the well
//      remembered, which is what the old visibility flag could not offer.
//
// Why these live in headers at all: EditorLayer.cpp is one of the editor translation units no suite compiles
// (scripts/CI/UnreachedSources.sh), so a rule written there is a rule nothing can assert.

#include <Editor/Core/AssetEditorRegistry.hpp>
#include <Editor/Core/DocumentWell.hpp>
#include <Editor/Core/PanelRegistry.hpp>
#include <Editor/Panels/IPanel.hpp>

#include <Engine/Core/RendererSlotPool.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

using Desert::Assets::AssetHandle;
using Desert::Assets::AssetTypeID;
using Desert::Editor::AssetDocumentTitle;
using Desert::Editor::CensusOfPanels;
using Desert::Editor::ClosedDocument;
using Desert::Editor::DocumentDisplayName;
using Desert::Editor::DocumentWell;
using Desert::Editor::IAssetEditorPanel;
using Desert::Editor::IPanel;
using Desert::Editor::PanelRegistry;
using Desert::Editor::PendingRendererSlotDemand;
using Desert::Editor::RendererSlotsHeldByDocuments;
using Desert::Engine::RendererSlotPool;

namespace
{
    AssetHandle Handle( uint64_t value )
    {
        return AssetHandle( value );
    }

    // A tool: no subject, no renderer, nothing but a name. The real ones drag a Scene and a device in.
    class FakeTool final : public IPanel
    {
    public:
        explicit FakeTool( std::string name ) : IPanel( std::move( name ) )
        {
        }

        void OnUIRender() override
        {
        }
    };

    // A document that LEASES A RENDERER SLOT FROM A REAL POOL and returns it in its destructor -- exactly
    // the shape a Material Editor's PreviewViewport has. The pool is the shipped one
    // (Engine/Core/RendererSlotPool.hpp), not a stand-in, so "the slot came back" is the real relation and
    // not a counter this file maintains for itself.
    class FakeDocument final : public IAssetEditorPanel
    {
    public:
        FakeDocument( const std::string& name, const AssetHandle& subject, RendererSlotPool* pool = nullptr,
                      AssetTypeID type = AssetTypeID::Material )
             : IAssetEditorPanel( name, subject, type ), m_Pool( pool )
        {
            if ( m_Pool )
                m_Slot = m_Pool->Claim();
        }

        ~FakeDocument() override
        {
            if ( m_Pool )
                m_Pool->Release( m_Slot );
        }

        void OnUIRender() override
        {
        }

        [[nodiscard]] bool HoldsRendererSlot() const override
        {
            return m_Pool != nullptr && m_Slot != RendererSlotPool::kNoFreeSlot;
        }

        [[nodiscard]] bool ClaimsRendererSlot() const override
        {
            return m_ClaimsSlot;
        }

        bool m_ClaimsSlot = true;

    private:
        RendererSlotPool* m_Pool = nullptr;
        uint32_t          m_Slot = RendererSlotPool::kNoFreeSlot;
    };

    std::unique_ptr<IAssetEditorPanel> MakeDocument( const std::string& name, uint64_t subject,
                                                     RendererSlotPool* pool = nullptr,
                                                     AssetTypeID       type = AssetTypeID::Material )
    {
        return std::make_unique<FakeDocument>( name, Handle( subject ), pool, type );
    }
} // namespace

// =================================================================================================
// 1. The tool container cannot hold a document
// =================================================================================================

// THE COMPILER IS THE ENFORCER, and this is the same predicate the static_assert inside
// PanelRegistry::Add uses. Written twice on purpose: once where it stops a build, once where a test run
// can print it. If someone relaxes the container to take an IAssetEditorPanel "just for now", this line is
// what says so out loud instead of the change passing a green sweep.
static_assert( PanelRegistry::Accepts<FakeTool>, "a tool must be admissible to the tool registry" );
static_assert( !PanelRegistry::Accepts<FakeDocument>, "a document must NOT be admissible to the tool registry" );
// And the hole a runtime check would have left open: a unique_ptr<IPanel> hides which of the two it is, so
// the base class itself is refused rather than deduced past the check.
static_assert( !PanelRegistry::Accepts<IPanel>, "the base class must not be an accepted concrete panel type" );

TEST( PanelRegistryAcceptance, ADocumentIsNotATool )
{
    EXPECT_TRUE( PanelRegistry::Accepts<FakeTool> );
    EXPECT_FALSE( PanelRegistry::Accepts<FakeDocument> );
    EXPECT_FALSE( PanelRegistry::Accepts<IPanel> );
}

TEST( PanelRegistryAcceptance, TheRegistryHoldsWhatItAccepted )
{
    PanelRegistry registry;
    registry.Add<FakeTool>( std::string( "Details" ) );
    registry.Add<FakeTool>( std::string( "Scene Outliner" ) );
    registry.Adopt( std::make_unique<FakeTool>( "Logs" ) );

    ASSERT_EQ( registry.Size(), 3u );

    // The View menu, the command palette and --open-panel are all loops of exactly this shape. What makes
    // them list tools only is that this walk cannot produce a document -- not a predicate each of them
    // remembers to write.
    for ( const auto& panel : registry )
        EXPECT_EQ( dynamic_cast<const IAssetEditorPanel*>( panel.get() ), nullptr )
             << "a document reached the tool registry: " << panel->GetName();
}

// =================================================================================================
// 2. The two owners partition the panels
// =================================================================================================

TEST( PanelCensusRelation, ToolsPlusDocumentsAccountForEveryPanel )
{
    PanelRegistry registry;
    registry.Add<FakeTool>( std::string( "Details" ) );
    registry.Add<FakeTool>( std::string( "Scene Outliner" ) );
    registry.Add<FakeTool>( std::string( "Assets" ) );

    DocumentWell well;
    well.Add( MakeDocument( AssetDocumentTitle( "M_Crate_Painted", Handle( 11 ) ), 11 ) );
    well.Add( MakeDocument( AssetDocumentTitle( "CT_Cumulus_Fair", Handle( 12 ) ), 12 ) );

    const auto census = CensusOfPanels( registry, well );

    EXPECT_EQ( census.Tools, 3u );
    EXPECT_EQ( census.Documents, 2u );
    // The relation, not the two numbers: nothing the editor draws is unaccounted for, and nothing is
    // counted twice.
    EXPECT_EQ( census.Tools + census.Documents, census.Total );
    EXPECT_EQ( census.Total, 5u );
    EXPECT_TRUE( census.ToolsHoldNoDocument );
}

TEST( PanelCensusRelation, TheCensusNoticesADocumentAmongTheTools )
{
    // The state the split removes, built by hand so the assertion above is known to be capable of failing.
    // A plain vector<unique_ptr<IPanel>> is what m_Panels WAS, and it holds both kinds happily -- which is
    // the whole point: the type is what stops this, and with the type gone nothing does.
    std::vector<std::unique_ptr<IPanel>> mixed;
    mixed.emplace_back( std::make_unique<FakeTool>( "Details" ) );
    mixed.emplace_back( MakeDocument( AssetDocumentTitle( "M_Crate_Painted", Handle( 11 ) ), 11 ) );

    DocumentWell empty;
    const auto   census = CensusOfPanels( mixed, empty );

    EXPECT_EQ( census.Total, 2u );
    EXPECT_FALSE( census.ToolsHoldNoDocument );
}

TEST( PanelCensusRelation, ClosingADocumentMovesItOutOfTheTotal )
{
    PanelRegistry registry;
    registry.Add<FakeTool>( std::string( "Details" ) );

    DocumentWell well;
    well.Add( MakeDocument( AssetDocumentTitle( "M_Crate_Painted", Handle( 11 ) ), 11 ) );
    well.Add( MakeDocument( AssetDocumentTitle( "M_Sand_Dune", Handle( 12 ) ), 12 ) );

    EXPECT_EQ( CensusOfPanels( registry, well ).Total, 3u );

    auto closed = well.Release( Handle( 12 ) );
    ASSERT_NE( closed, nullptr );
    closed.reset();

    const auto after = CensusOfPanels( registry, well );
    EXPECT_EQ( after.Tools, 1u );
    EXPECT_EQ( after.Documents, 1u );
    EXPECT_EQ( after.Total, 2u );
}

// =================================================================================================
// 3. A closed document gives its renderer slot back
// =================================================================================================

TEST( DocumentSlotLease, ClosingADocumentReturnsItsSlot )
{
    RendererSlotPool pool;
    const uint32_t   viewport = pool.Claim(); // the main viewport holds one for the whole session
    ASSERT_NE( viewport, RendererSlotPool::kNoFreeSlot );

    DocumentWell well;
    well.Add( MakeDocument( AssetDocumentTitle( "M_Crate_Painted", Handle( 11 ) ), 11, &pool ) );
    well.Add( MakeDocument( AssetDocumentTitle( "M_Sand_Dune", Handle( 12 ) ), 12, &pool ) );

    EXPECT_EQ( pool.InUseCount(), 3u );
    EXPECT_EQ( RendererSlotsHeldByDocuments( well ), 2u );

    // RELEASE IS NOT DESTRUCTION. The well hands the document back and the caller destroys it once the
    // device is idle, which is the ordering ~PreviewViewport established -- so the lease is still held
    // here, one statement after the well stopped listing it.
    auto closed = well.Release( Handle( 12 ) );
    ASSERT_NE( closed, nullptr );
    EXPECT_EQ( well.Count(), 1u );
    EXPECT_EQ( pool.InUseCount(), 3u ) << "the slot must still be held until the document is destroyed";

    closed.reset();

    EXPECT_EQ( pool.InUseCount(), 2u );
    EXPECT_EQ( RendererSlotsHeldByDocuments( well ), 1u );
}

TEST( DocumentSlotLease, ClosingEveryDocumentLeavesOnlyTheViewport )
{
    RendererSlotPool pool;
    const uint32_t   viewport = pool.Claim();
    ASSERT_NE( viewport, RendererSlotPool::kNoFreeSlot );

    DocumentWell well;
    for ( uint64_t i = 1; i <= 4; ++i )
        well.Add( MakeDocument( AssetDocumentTitle( "M_" + std::to_string( i ), Handle( i ) ), i, &pool ) );

    EXPECT_EQ( pool.InUseCount(), 5u );

    auto all = well.ReleaseAll();
    EXPECT_EQ( all.size(), 4u );
    EXPECT_TRUE( well.Empty() );
    all.clear();

    EXPECT_EQ( pool.InUseCount(), 1u );
}

TEST( DocumentSlotLease, ACpuDrawnDocumentIsNotPendingDemand )
{
    // The four cloud documents bake on the CPU: they hold no slot and never will, so counting them as
    // claims would refuse a window that costs nothing. The rule lives in AssetEditorRegistry.hpp and is
    // asserted here against the OWNER the editor now asks, rather than against the panel list it used to.
    DocumentWell well;
    auto&        cpu = static_cast<FakeDocument&>( well.Add( MakeDocument(
         AssetDocumentTitle( "CT_Cumulus_Fair", Handle( 21 ) ), 21, nullptr, AssetTypeID::CloudType ) ) );
    cpu.m_ClaimsSlot = false;

    well.Add( MakeDocument( AssetDocumentTitle( "M_Crate_Painted", Handle( 22 ) ), 22 ) );

    // One claim outstanding, not two: the material has a slot coming, the cloud never will.
    EXPECT_EQ( PendingRendererSlotDemand( well.Documents() ), 1u );
    EXPECT_EQ( RendererSlotsHeldByDocuments( well ), 0u );
}

// =================================================================================================
// 4. Closing is not hiding
// =================================================================================================

TEST( DocumentClose, AClosedDocumentIsGoneAndReopenable )
{
    DocumentWell well;
    well.Add( MakeDocument( AssetDocumentTitle( "M_Crate_Painted", Handle( 11 ) ), 11 ) );

    ASSERT_NE( well.Find( Handle( 11 ) ), nullptr );

    auto closed = well.Release( Handle( 11 ) );
    ASSERT_NE( closed, nullptr );
    closed.reset();

    // GONE, not hidden. There is no flag left anywhere that says "this window exists but is not shown" --
    // which is what made the old View-menu tick unrecoverable, because ticking it back set a bool on an
    // object that had already been destroyed.
    EXPECT_EQ( well.Find( Handle( 11 ) ), nullptr );
    EXPECT_TRUE( well.Empty() );

    // What the well kept instead: enough to offer it back. This is the thing a visibility flag could not
    // do, and the reason the empty area is worth having at all.
    ASSERT_EQ( well.RecentlyClosed().size(), 1u );
    EXPECT_EQ( well.RecentlyClosed().front().DisplayName, "M_Crate_Painted" );
    EXPECT_EQ( well.RecentlyClosed().front().Subject, Handle( 11 ) );
    EXPECT_EQ( well.RecentlyClosed().front().Type, AssetTypeID::Material );

    // And reopening really is an open, not an un-hide.
    well.Add( MakeDocument( AssetDocumentTitle( "M_Crate_Painted", Handle( 11 ) ), 11 ) );
    EXPECT_NE( well.Find( Handle( 11 ) ), nullptr );
}

TEST( DocumentClose, ClosingAnUnknownSubjectIsNotAnError )
{
    DocumentWell well;
    EXPECT_EQ( well.Release( Handle( 99 ) ), nullptr );
    EXPECT_TRUE( well.RecentlyClosed().empty() );
}

TEST( DocumentClose, TheRecentlyClosedListIsCappedAndCarriesNoDuplicates )
{
    DocumentWell well;

    // One asset, opened and closed three times, is one entry -- not three copies of the same row.
    //
    // The released document is dropped ON PURPOSE in both loops: what is under test is the
    // RecentlyClosed bookkeeping, and letting the returned unique_ptr die at the semicolon is exactly
    // what a caller that only wanted the window shut does. Written as `(void)` rather than left
    // implicit, because a discarded owner and a forgotten one look identical without it.
    for ( int i = 0; i < 3; ++i )
    {
        well.Add( MakeDocument( AssetDocumentTitle( "M_Crate_Painted", Handle( 11 ) ), 11 ) );
        (void)well.Release( Handle( 11 ) );
    }
    EXPECT_EQ( well.RecentlyClosed().size(), 1u );

    for ( uint64_t i = 100; i < 100 + DocumentWell::kRecentlyClosedLimit + 3; ++i )
    {
        well.Add( MakeDocument( AssetDocumentTitle( "M_" + std::to_string( i ), Handle( i ) ), i ) );
        (void)well.Release( Handle( i ) );
    }

    EXPECT_EQ( well.RecentlyClosed().size(), DocumentWell::kRecentlyClosedLimit );
    // Newest first.
    EXPECT_EQ( well.RecentlyClosed().front().Subject, Handle( 100 + DocumentWell::kRecentlyClosedLimit + 2 ) );
}

// =================================================================================================
// 5. The Ctrl+Tab ring
// =================================================================================================

TEST( DocumentRing, MostRecentlyUsedOrderFollowsFocusAndNotCreation )
{
    DocumentWell well;
    well.Add( MakeDocument( AssetDocumentTitle( "A", Handle( 1 ) ), 1 ) );
    well.Add( MakeDocument( AssetDocumentTitle( "B", Handle( 2 ) ), 2 ) );
    well.Add( MakeDocument( AssetDocumentTitle( "C", Handle( 3 ) ), 3 ) );

    // Adding touches, so the newest is in front.
    ASSERT_EQ( well.MostRecentOrder().size(), 3u );
    EXPECT_EQ( well.MostRecentOrder()[0], Handle( 3 ) );

    well.Touch( Handle( 1 ) );
    EXPECT_EQ( well.MostRecentOrder()[0], Handle( 1 ) );
    EXPECT_EQ( well.MostRecentOrder()[1], Handle( 3 ) );
    EXPECT_EQ( well.MostRecentOrder()[2], Handle( 2 ) );
}

TEST( DocumentRing, CtrlTabWalksEveryDocumentAndWrapsRoundOnce )
{
    DocumentWell well;
    well.Add( MakeDocument( AssetDocumentTitle( "A", Handle( 1 ) ), 1 ) );
    well.Add( MakeDocument( AssetDocumentTitle( "B", Handle( 2 ) ), 2 ) );
    well.Add( MakeDocument( AssetDocumentTitle( "C", Handle( 3 ) ), 3 ) );
    // Order is now C, B, A.

    // WITHOUT touching between presses -- which is exactly what the editor does while Ctrl is held down,
    // so that a second press moves on rather than coming back to where the first started.
    const AssetHandle first = *well.NextMostRecent( Handle( 3 ) );
    EXPECT_EQ( first, Handle( 2 ) );
    const AssetHandle second = *well.NextMostRecent( first );
    EXPECT_EQ( second, Handle( 1 ) );
    const AssetHandle third = *well.NextMostRecent( second );
    EXPECT_EQ( third, Handle( 3 ) ) << "the ring must wrap to the front";
}

TEST( DocumentRing, TabbingFromOutsideTheDocumentsLandsOnTheMostRecent )
{
    DocumentWell well;
    well.Add( MakeDocument( AssetDocumentTitle( "A", Handle( 1 ) ), 1 ) );
    well.Add( MakeDocument( AssetDocumentTitle( "B", Handle( 2 ) ), 2 ) );

    // The keyboard is on a tool, or on nothing: "back to what I was editing" is the useful answer.
    EXPECT_EQ( *well.NextMostRecent( Handle( 0 ) ), Handle( 2 ) );
    EXPECT_EQ( *well.NextMostRecent( Handle( 77 ) ), Handle( 2 ) );
}

TEST( DocumentRing, NothingToSwitchToIsSaidRatherThanGuessed )
{
    DocumentWell well;
    EXPECT_FALSE( well.NextMostRecent( Handle( 0 ) ).has_value() );

    well.Add( MakeDocument( AssetDocumentTitle( "A", Handle( 1 ) ), 1 ) );
    // One document, and it is the one you are in: there is nowhere to go, and answering "itself" would
    // make Ctrl+Tab look broken rather than inapplicable.
    EXPECT_FALSE( well.NextMostRecent( Handle( 1 ) ).has_value() );
}

TEST( DocumentRing, AClosedDocumentLeavesTheRing )
{
    DocumentWell well;
    well.Add( MakeDocument( AssetDocumentTitle( "A", Handle( 1 ) ), 1 ) );
    well.Add( MakeDocument( AssetDocumentTitle( "B", Handle( 2 ) ), 2 ) );
    well.Add( MakeDocument( AssetDocumentTitle( "C", Handle( 3 ) ), 3 ) );

    auto closed = well.Release( Handle( 2 ) );
    ASSERT_NE( closed, nullptr );
    closed.reset();

    ASSERT_EQ( well.MostRecentOrder().size(), 2u );
    EXPECT_EQ( *well.NextMostRecent( Handle( 3 ) ), Handle( 1 ) );
    EXPECT_EQ( *well.NextMostRecent( Handle( 1 ) ), Handle( 3 ) );
}

// =================================================================================================
// 6. The name a person reads
// =================================================================================================

TEST( DocumentNaming, TheVisibleHalfOfTheTitleIsWhatTheUserSees )
{
    // The census, the Documents menu, the well's list and the recently-closed rows all show this, and they
    // used to each carry their own find-and-erase of "###". One function, so the four cannot disagree.
    EXPECT_EQ( DocumentDisplayName( AssetDocumentTitle( "M_Crate_Painted", Handle( 11 ) ) ), "M_Crate_Painted" );
    EXPECT_EQ( DocumentDisplayName( "Details" ), "Details" );
    EXPECT_EQ( DocumentDisplayName( "" ), "" );
}

int main( int argc, char** argv )
{
    testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
