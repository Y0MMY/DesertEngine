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

#include <Editor/Core/SubjectEditorRegistry.hpp>
#include <Editor/Core/DocumentWell.hpp>
#include <Editor/Core/PanelRegistry.hpp>
#include <Editor/Panels/IPanel.hpp>

#include <Engine/Core/RendererSlotPool.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

using Desert::Assets::AssetHandle;
using Desert::Assets::AssetTypeID;
using Desert::Editor::AssetSubject;
using Desert::Editor::CensusOfDocumentEditors;
using Desert::Editor::CensusOfPanels;
using Desert::Editor::ClosedDocument;
using Desert::Editor::ComponentSubject;
using Desert::Editor::DocumentDisplayName;
using Desert::Editor::DocumentTitle;
using Desert::Editor::DocumentWell;
using Desert::Editor::IPanel;
using Desert::Editor::ISubjectDocument;
using Desert::Editor::PanelRegistry;
using Desert::Editor::PendingRendererSlotDemand;
using Desert::Editor::RendererSlotsHeldByDocuments;
using Desert::Editor::SubjectDomain;
using Desert::Editor::SubjectEditorRegistry;
using Desert::Editor::SubjectId;
using Desert::Engine::RendererSlotPool;

namespace
{
    AssetHandle Handle( uint64_t value )
    {
        return AssetHandle( value );
    }

    // A FILE subject. The tests below say `Asset( 11 )` where they used to say `Handle( 11 )`: the seam is
    // keyed on a subject now and not on a handle, and the two differ in exactly the way that matters —
    // `Asset( 11 )` and `Anim( 11 )` are different documents over the same 64-bit number.
    SubjectId Asset( uint64_t value, AssetTypeID type = AssetTypeID::Material )
    {
        return AssetSubject( Handle( value ), static_cast<uint32_t>( type ) );
    }

    // A COMPONENT-ON-AN-ENTITY subject: the case the old asset-keyed seam could not express at all.
    SubjectId Component( uint64_t entity, std::string_view componentTypeName )
    {
        return ComponentSubject( ::Common::UUID( entity ), componentTypeName );
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
    class FakeDocument final : public ISubjectDocument
    {
    public:
        FakeDocument( const std::string& name, const SubjectId& subject, RendererSlotPool* pool = nullptr )
             : ISubjectDocument( name, subject ), m_Pool( pool )
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

        // The stand-in for "the entity is still there / the asset is still registered". Writable, because
        // what the suite has to be able to do is KILL a subject under a live document — which is the one
        // event the editor's close-with-the-subject rule turns on.
        [[nodiscard]] bool IsSubjectAlive() const override
        {
            return m_Alive;
        }

        [[nodiscard]] bool HoldsRendererSlot() const override
        {
            return m_Pool != nullptr && m_Slot != RendererSlotPool::kNoFreeSlot;
        }

        [[nodiscard]] bool ClaimsRendererSlot() const override
        {
            return m_ClaimsSlot;
        }

        // The same teardown a MaterialEditorPanel does when its window has been off screen: give the lease
        // back and keep the window. NOT the destructor — the document survives this.
        void ReleaseRendererSlot() override
        {
            if ( m_Pool && m_Slot != RendererSlotPool::kNoFreeSlot )
            {
                m_Pool->Release( m_Slot );
                m_Slot = RendererSlotPool::kNoFreeSlot;
            }
        }

        bool m_ClaimsSlot = true;
        bool m_Alive      = true;

    private:
        RendererSlotPool* m_Pool = nullptr;
        uint32_t          m_Slot = RendererSlotPool::kNoFreeSlot;
    };

    std::unique_ptr<ISubjectDocument> MakeDocument( const std::string& name, const SubjectId& subject,
                                                    RendererSlotPool* pool = nullptr )
    {
        return std::make_unique<FakeDocument>( name, subject, pool );
    }
} // namespace

// =================================================================================================
// 1. The tool container cannot hold a document
// =================================================================================================

// THE COMPILER IS THE ENFORCER, and this is the same predicate the static_assert inside
// PanelRegistry::Add uses. Written twice on purpose: once where it stops a build, once where a test run
// can print it. If someone relaxes the container to take an ISubjectDocument "just for now", this line is
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
        EXPECT_EQ( dynamic_cast<const ISubjectDocument*>( panel.get() ), nullptr )
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
    well.Add( MakeDocument( DocumentTitle( "M_Crate_Painted", Asset( 11 ) ), Asset( 11 ) ) );
    well.Add( MakeDocument( DocumentTitle( "CT_Cumulus_Fair", Asset( 12 ) ), Asset( 12 ) ) );

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
    mixed.emplace_back( MakeDocument( DocumentTitle( "M_Crate_Painted", Asset( 11 ) ), Asset( 11 ) ) );

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
    well.Add( MakeDocument( DocumentTitle( "M_Crate_Painted", Asset( 11 ) ), Asset( 11 ) ) );
    well.Add( MakeDocument( DocumentTitle( "M_Sand_Dune", Asset( 12 ) ), Asset( 12 ) ) );

    EXPECT_EQ( CensusOfPanels( registry, well ).Total, 3u );

    auto closed = well.Release( Asset( 12 ) );
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
    well.Add( MakeDocument( DocumentTitle( "M_Crate_Painted", Asset( 11 ) ), Asset( 11 ), &pool ) );
    well.Add( MakeDocument( DocumentTitle( "M_Sand_Dune", Asset( 12 ) ), Asset( 12 ), &pool ) );

    EXPECT_EQ( pool.InUseCount(), 3u );
    EXPECT_EQ( RendererSlotsHeldByDocuments( well ), 2u );

    // RELEASE IS NOT DESTRUCTION. The well hands the document back and the caller destroys it once the
    // device is idle, which is the ordering ~PreviewViewport established -- so the lease is still held
    // here, one statement after the well stopped listing it.
    auto closed = well.Release( Asset( 12 ) );
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
        well.Add( MakeDocument( DocumentTitle( "M_" + std::to_string( i ), Asset( i ) ), Asset( i ), &pool ) );

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
         DocumentTitle( "CT_Cumulus_Fair", Asset( 21, AssetTypeID::CloudType ) ),
         Asset( 21, AssetTypeID::CloudType ) ) ) );
    cpu.m_ClaimsSlot = false;

    well.Add( MakeDocument( DocumentTitle( "M_Crate_Painted", Asset( 22 ) ), Asset( 22 ) ) );

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
    well.Add( MakeDocument( DocumentTitle( "M_Crate_Painted", Asset( 11 ) ), Asset( 11 ) ) );

    ASSERT_NE( well.Find( Asset( 11 ) ), nullptr );

    auto closed = well.Release( Asset( 11 ) );
    ASSERT_NE( closed, nullptr );
    closed.reset();

    // GONE, not hidden. There is no flag left anywhere that says "this window exists but is not shown" --
    // which is what made the old View-menu tick unrecoverable, because ticking it back set a bool on an
    // object that had already been destroyed.
    EXPECT_EQ( well.Find( Asset( 11 ) ), nullptr );
    EXPECT_TRUE( well.Empty() );

    // What the well kept instead: enough to offer it back. This is the thing a visibility flag could not
    // do, and the reason the empty area is worth having at all.
    ASSERT_EQ( well.RecentlyClosed().size(), 1u );
    EXPECT_EQ( well.RecentlyClosed().front().DisplayName, "M_Crate_Painted" );
    EXPECT_EQ( well.RecentlyClosed().front().Subject, Asset( 11 ) );

    // And reopening really is an open, not an un-hide.
    well.Add( MakeDocument( DocumentTitle( "M_Crate_Painted", Asset( 11 ) ), Asset( 11 ) ) );
    EXPECT_NE( well.Find( Asset( 11 ) ), nullptr );
}

TEST( DocumentClose, ClosingAnUnknownSubjectIsNotAnError )
{
    DocumentWell well;
    EXPECT_EQ( well.Release( Asset( 99 ) ), nullptr );
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
        well.Add( MakeDocument( DocumentTitle( "M_Crate_Painted", Asset( 11 ) ), Asset( 11 ) ) );
        (void)well.Release( Asset( 11 ) );
    }
    EXPECT_EQ( well.RecentlyClosed().size(), 1u );

    for ( uint64_t i = 100; i < 100 + DocumentWell::kRecentlyClosedLimit + 3; ++i )
    {
        well.Add( MakeDocument( DocumentTitle( "M_" + std::to_string( i ), Asset( i ) ), Asset( i ) ) );
        (void)well.Release( Asset( i ) );
    }

    EXPECT_EQ( well.RecentlyClosed().size(), DocumentWell::kRecentlyClosedLimit );
    // Newest first.
    EXPECT_EQ( well.RecentlyClosed().front().Subject, Asset( 100 + DocumentWell::kRecentlyClosedLimit + 2 ) );
}

// =================================================================================================
// 5. The Ctrl+Tab ring
// =================================================================================================

TEST( DocumentRing, MostRecentlyUsedOrderFollowsFocusAndNotCreation )
{
    DocumentWell well;
    well.Add( MakeDocument( DocumentTitle( "A", Asset( 1 ) ), Asset( 1 ) ) );
    well.Add( MakeDocument( DocumentTitle( "B", Asset( 2 ) ), Asset( 2 ) ) );
    well.Add( MakeDocument( DocumentTitle( "C", Asset( 3 ) ), Asset( 3 ) ) );

    // Adding touches, so the newest is in front.
    ASSERT_EQ( well.MostRecentOrder().size(), 3u );
    EXPECT_EQ( well.MostRecentOrder()[0], Asset( 3 ) );

    well.Touch( Asset( 1 ) );
    EXPECT_EQ( well.MostRecentOrder()[0], Asset( 1 ) );
    EXPECT_EQ( well.MostRecentOrder()[1], Asset( 3 ) );
    EXPECT_EQ( well.MostRecentOrder()[2], Asset( 2 ) );
}

TEST( DocumentRing, CtrlTabWalksEveryDocumentAndWrapsRoundOnce )
{
    DocumentWell well;
    well.Add( MakeDocument( DocumentTitle( "A", Asset( 1 ) ), Asset( 1 ) ) );
    well.Add( MakeDocument( DocumentTitle( "B", Asset( 2 ) ), Asset( 2 ) ) );
    well.Add( MakeDocument( DocumentTitle( "C", Asset( 3 ) ), Asset( 3 ) ) );
    // Order is now C, B, A.

    // WITHOUT touching between presses -- which is exactly what the editor does while Ctrl is held down,
    // so that a second press moves on rather than coming back to where the first started.
    const SubjectId first = *well.NextMostRecent( Asset( 3 ) );
    EXPECT_EQ( first, Asset( 2 ) );
    const SubjectId second = *well.NextMostRecent( first );
    EXPECT_EQ( second, Asset( 1 ) );
    const SubjectId third = *well.NextMostRecent( second );
    EXPECT_EQ( third, Asset( 3 ) ) << "the ring must wrap to the front";
}

TEST( DocumentRing, TabbingFromOutsideTheDocumentsLandsOnTheMostRecent )
{
    DocumentWell well;
    well.Add( MakeDocument( DocumentTitle( "A", Asset( 1 ) ), Asset( 1 ) ) );
    well.Add( MakeDocument( DocumentTitle( "B", Asset( 2 ) ), Asset( 2 ) ) );

    // The keyboard is on a tool, or on nothing: "back to what I was editing" is the useful answer.
    EXPECT_EQ( *well.NextMostRecent( Asset( 0 ) ), Asset( 2 ) );
    EXPECT_EQ( *well.NextMostRecent( Asset( 77 ) ), Asset( 2 ) );
}

TEST( DocumentRing, NothingToSwitchToIsSaidRatherThanGuessed )
{
    DocumentWell well;
    EXPECT_FALSE( well.NextMostRecent( Asset( 0 ) ).has_value() );

    well.Add( MakeDocument( DocumentTitle( "A", Asset( 1 ) ), Asset( 1 ) ) );
    // One document, and it is the one you are in: there is nowhere to go, and answering "itself" would
    // make Ctrl+Tab look broken rather than inapplicable.
    EXPECT_FALSE( well.NextMostRecent( Asset( 1 ) ).has_value() );
}

TEST( DocumentRing, AClosedDocumentLeavesTheRing )
{
    DocumentWell well;
    well.Add( MakeDocument( DocumentTitle( "A", Asset( 1 ) ), Asset( 1 ) ) );
    well.Add( MakeDocument( DocumentTitle( "B", Asset( 2 ) ), Asset( 2 ) ) );
    well.Add( MakeDocument( DocumentTitle( "C", Asset( 3 ) ), Asset( 3 ) ) );

    auto closed = well.Release( Asset( 2 ) );
    ASSERT_NE( closed, nullptr );
    closed.reset();

    ASSERT_EQ( well.MostRecentOrder().size(), 2u );
    EXPECT_EQ( *well.NextMostRecent( Asset( 3 ) ), Asset( 1 ) );
    EXPECT_EQ( *well.NextMostRecent( Asset( 1 ) ), Asset( 3 ) );
}

// =================================================================================================
// 6. The name a person reads
// =================================================================================================

TEST( DocumentNaming, TheVisibleHalfOfTheTitleIsWhatTheUserSees )
{
    // The census, the Documents menu, the well's list and the recently-closed rows all show this, and they
    // used to each carry their own find-and-erase of "###". One function, so the four cannot disagree.
    EXPECT_EQ( DocumentDisplayName( DocumentTitle( "M_Crate_Painted", Asset( 11 ) ) ), "M_Crate_Painted" );
    EXPECT_EQ( DocumentDisplayName( "Details" ), "Details" );
    EXPECT_EQ( DocumentDisplayName( "" ), "" );
}

// =================================================================================================
// 7. A SUBJECT IS NOT A HANDLE  (task U7)
// =================================================================================================
//
// The seam used to be keyed on an Assets::AssetHandle, so a document could only ever be a file. An anim
// graph, a particle emitter and a UI canvas are authored data held by a COMPONENT ON AN ENTITY; the button
// the owner asked for in Details — "edit the thing this component holds" — had nowhere to send its request,
// because the request carried a handle and a component is not one.
//
// These are the relations that say the new key is real rather than an asset handle with a longer name.

TEST( SubjectIdentity, TwoDocumentsOverOneNumberAreTwoDocuments )
{
    // THE FAILURE THIS PREVENTS IS SILENT AND TOTAL. An entity's UUID and an asset's handle are drawn from
    // one 64-bit space (AssetHandle derives from UUID), and one entity's anim graph and its particle
    // emitter share an owner outright. Key the well on the number and all three are ONE document; key the
    // WINDOW on the number — DocumentTitle appends it after "###" — and ImGui merges all three into one
    // window, drawing the second and third into the first.
    const SubjectId asset     = Asset( 42 );
    const SubjectId anim      = Component( 42, "AnimationComponent" );
    const SubjectId particles = Component( 42, "ParticleEmitterComponent" );

    EXPECT_FALSE( asset == anim );
    EXPECT_FALSE( anim == particles );
    EXPECT_FALSE( asset == particles );

    // ...and the three window ids differ, which is the half ImGui reads.
    EXPECT_NE( DocumentTitle( "Hero", asset ), DocumentTitle( "Hero", anim ) );
    EXPECT_NE( DocumentTitle( "Hero", anim ), DocumentTitle( "Hero", particles ) );

    DocumentWell well;
    well.Add( MakeDocument( DocumentTitle( "Hero", asset ), asset ) );
    well.Add( MakeDocument( DocumentTitle( "Hero", anim ), anim ) );
    well.Add( MakeDocument( DocumentTitle( "Hero", particles ), particles ) );

    EXPECT_EQ( well.Count(), 3u );
    ASSERT_NE( well.Find( anim ), nullptr );
    EXPECT_EQ( well.Find( anim )->Subject(), anim );
    EXPECT_EQ( well.Find( particles )->Subject(), particles );
}

TEST( SubjectIdentity, TheSameSubjectIsTheSameDocument )
{
    // The other half, and the one open-or-focus rests on: asking twice for one thing must find the window
    // that is already on it. Two SubjectIds built independently from the same three parts must compare
    // equal — a facet derived from a type NAME (EditorSubject.hpp::ComponentFacet) has to be deterministic
    // across call sites or the Details button would open a second window every press.
    EXPECT_EQ( Component( 7, "AnimationComponent" ), Component( 7, "AnimationComponent" ) );
    EXPECT_EQ( DocumentTitle( "Hero", Component( 7, "AnimationComponent" ) ),
               DocumentTitle( "Hero", Component( 7, "AnimationComponent" ) ) );

    DocumentWell well;
    const SubjectId anim = Component( 7, "AnimationComponent" );
    well.Add( MakeDocument( DocumentTitle( "Hero", anim ), anim ) );
    EXPECT_NE( well.Find( Component( 7, "AnimationComponent" ) ), nullptr )
         << "open-or-focus is keyed on the subject; a subject rebuilt from its parts must find its window";
}

TEST( SubjectIdentity, NothingIsNotASubject )
{
    // All three parts have to be set. A domain with no owner is a kind with no instance; an owner with no
    // domain does not say what the number means. DocumentWell::Find and SubjectEditorRegistry::Create both
    // refuse a null subject by name rather than opening a window over nothing.
    EXPECT_TRUE( SubjectId{}.IsNull() );
    EXPECT_TRUE( Asset( 0 ).IsNull() ) << "the null handle is 'no asset'";
    EXPECT_TRUE( Component( 0, "AnimationComponent" ).IsNull() ) << "the null UUID is 'no entity'";
    EXPECT_FALSE( Asset( 11 ).IsNull() );

    DocumentWell well;
    well.Add( MakeDocument( DocumentTitle( "A", Asset( 11 ) ), Asset( 11 ) ) );
    EXPECT_EQ( well.Find( SubjectId{} ), nullptr );
}

TEST( SubjectIdentity, TheTextFormCarriesAllThreeParts )
{
    // This string is the ImGui window id AND the `subject` field on the control channel. A digest of the
    // three parts would make a collision silent in both places, so it is written out.
    const SubjectId anim = Component( 88, "AnimationComponent" );
    EXPECT_EQ( anim.ToString(), std::string( "component:" ) + std::to_string( anim.Facet ) + ":88" );
    EXPECT_EQ( Asset( 333, AssetTypeID::Material ).ToString(),
               std::string( "asset:" ) + std::to_string( static_cast<uint32_t>( AssetTypeID::Material ) ) +
                    ":333" );
    EXPECT_NE( anim.ToString(), Component( 88, "ParticleEmitterComponent" ).ToString() );
}

// =================================================================================================
// 8. THE SET OF DOCUMENTS IS THE SET OF REGISTERED EDITORS  (the census the owner asked for)
// =================================================================================================
//
// "Множество документов = множество редакторов, зарегистрированных по типу предмета" — a RELATION, not a
// list. Every document comes from SubjectEditorRegistry::Create, so this ought to hold by construction, and
// "ought to by construction" is exactly the claim that stops being true the day somebody adds a second way
// in. It has happened here before: documents were once built in three hand-wired places
// (NodeGraphPanel::RequestOpen, MaterialPreviewPanel::RequestPreview, SceneOpenRequest), none of which the
// registry knew about.

namespace
{
    // A registration whose factory builds one of the fakes above. The NAME and the ICON are required by the
    // registry, which is the point of them being there: they used to be two hand-written tables in
    // EditorLayer.cpp that a new kind of document had to be entered into separately.
    SubjectEditorRegistry::Registration FakeEditor( std::string name )
    {
        return SubjectEditorRegistry::Registration{
             std::move( name ), "*", []( const SubjectId& subject ) -> std::unique_ptr<ISubjectDocument>
             { return std::make_unique<FakeDocument>( DocumentTitle( "doc", subject ), subject ); } };
    }
} // namespace

TEST( DocumentEditorCensus, EveryOpenDocumentHasARegisteredEditor )
{
    SubjectEditorRegistry registry;
    registry.Register( Asset( 1 ).Type(), FakeEditor( "Material" ) );
    registry.Register( Component( 1, "AnimationComponent" ).Type(), FakeEditor( "AnimationComponent" ) );

    DocumentWell well;
    well.Add( registry.Create( Asset( 11 ) ) );
    well.Add( registry.Create( Component( 12, "AnimationComponent" ) ) );
    well.Add( registry.Create( Asset( 13 ) ) );

    const auto census = CensusOfDocumentEditors( registry, well );

    EXPECT_EQ( census.Documents, 3u );
    EXPECT_EQ( census.RegisteredTypes, 2u ) << "three documents over two kinds is normal";
    EXPECT_EQ( census.UnregisteredDocuments, 0u );
    EXPECT_TRUE( census.EveryDocumentCameFromTheRegistry() );
}

TEST( DocumentEditorCensus, ADocumentFromSomewhereElseIsCounted )
{
    // The state the relation exists to detect, built by hand so the assertion above is known to be capable
    // of failing: a document put into the well WITHOUT going through the registry. That is what a fourth
    // file-static inbox looks like from here.
    SubjectEditorRegistry registry;
    registry.Register( Asset( 1 ).Type(), FakeEditor( "Material" ) );

    DocumentWell well;
    well.Add( registry.Create( Asset( 11 ) ) );
    well.Add( MakeDocument( DocumentTitle( "smuggled", Component( 12, "UICanvasComponent" ) ),
                            Component( 12, "UICanvasComponent" ) ) );

    const auto census = CensusOfDocumentEditors( registry, well );
    EXPECT_EQ( census.Documents, 2u );
    EXPECT_EQ( census.UnregisteredDocuments, 1u );
    EXPECT_FALSE( census.EveryDocumentCameFromTheRegistry() );
}

TEST( DocumentEditorCensus, AnAssetTypeAndAComponentTypeCannotCollide )
{
    // The registry is keyed on (domain, facet) and not on the facet alone. AssetTypeID::Material is 2; a
    // component whose 32-bit digest happened to be 2 would share its editor if the two halves were
    // flattened, and the symptom would be a Details button opening the Material Editor.
    SubjectEditorRegistry registry;
    registry.Register( Desert::Editor::SubjectTypeKey{ SubjectDomain::Asset, 2u }, FakeEditor( "Material" ) );
    registry.Register( Desert::Editor::SubjectTypeKey{ SubjectDomain::EntityComponent, 2u },
                       FakeEditor( "SomeComponent" ) );

    EXPECT_EQ( registry.Size(), 2u ) << "one key each: the domain is part of the key";
    EXPECT_EQ( registry.TypeName( Desert::Editor::SubjectTypeKey{ SubjectDomain::Asset, 2u } ), "Material" );
    EXPECT_EQ( registry.TypeName( Desert::Editor::SubjectTypeKey{ SubjectDomain::EntityComponent, 2u } ),
               "SomeComponent" );
}

TEST( DocumentEditorCensus, AnUnregisteredKindOpensNothingRatherThanSomethingElse )
{
    SubjectEditorRegistry registry;
    registry.Register( Asset( 1 ).Type(), FakeEditor( "Material" ) );

    EXPECT_TRUE( registry.HasEditorFor( Asset( 11 ).Type() ) );
    EXPECT_FALSE( registry.HasEditorFor( Component( 11, "AnimationComponent" ).Type() ) );
    EXPECT_EQ( registry.Create( Component( 11, "AnimationComponent" ) ), nullptr );
    EXPECT_EQ( registry.Create( SubjectId{} ), nullptr ) << "a null subject is refused, not built";

    // And the name of a kind nothing claims is still PRINTABLE, because every caller of TypeName is
    // building a message a person has to be able to act on.
    EXPECT_NE( registry.TypeName( Component( 11, "AnimationComponent" ) ).find( "unregistered" ),
               std::string::npos );
}

// =================================================================================================
// 9. A DOCUMENT CLOSES WITH ITS SUBJECT
// =================================================================================================
//
// The owner's decision, and the counterpart to the one he refused. A document is NOT closed when it loses
// the focus — a layout that rearranges itself reads as an editor that lost your panel. It IS closed when the
// thing it edits stops existing, because the alternative is a window whose every control writes into a
// resolution that returns null.
//
// EditorLayer::CloseDocumentsWhoseSubjectIsGone is the sweep; what it is a sweep OVER is this.

TEST( DocumentLiveness, TheSweepFindsExactlyTheDeadOnes )
{
    DocumentWell well;
    auto& alive = static_cast<FakeDocument&>(
         well.Add( MakeDocument( DocumentTitle( "A", Asset( 11 ) ), Asset( 11 ) ) ) );
    auto& doomed = static_cast<FakeDocument&>( well.Add( MakeDocument(
         DocumentTitle( "Hero", Component( 12, "AnimationComponent" ) ), Component( 12, "AnimationComponent" ) ) ) );

    const auto deadSubjects = [&well]
    {
        std::vector<SubjectId> dead;
        for ( const auto& document : well )
            if ( !document->IsSubjectAlive() )
                dead.push_back( document->Subject() );
        return dead;
    };

    EXPECT_TRUE( deadSubjects().empty() );

    // The entity was deleted, or its AnimationComponent removed, or its scene closed. All three are one
    // event from here, which is why the document answers a single question rather than the editor
    // subscribing to three.
    doomed.m_Alive = false;

    ASSERT_EQ( deadSubjects().size(), 1u );
    EXPECT_EQ( deadSubjects().front(), Component( 12, "AnimationComponent" ) );
    EXPECT_TRUE( alive.IsSubjectAlive() ) << "a sibling document must not be closed with it";

    // And closing it is an ordinary close: the well forgets it and remembers it, so the empty state can
    // still offer it back even though reopening will now be refused for the same reason.
    auto closed = well.Release( Component( 12, "AnimationComponent" ) );
    ASSERT_NE( closed, nullptr );
    closed.reset();
    EXPECT_EQ( well.Count(), 1u );
    EXPECT_EQ( well.RecentlyClosed().front().Subject, Component( 12, "AnimationComponent" ) );
}

// =================================================================================================
// 10. A HIDDEN DOCUMENT GIVES ITS SLOT BACK AND KEEPS ITS WINDOW
// =================================================================================================

TEST( DocumentSlotLease, ReleasingTheSlotOfAHiddenDocumentDoesNotCloseIt )
{
    // Four documents docked as tabs in one node show one tab. The other three were rendering previews
    // nobody could see and holding three of the six slots while they did it, so the fifth document the user
    // opened was refused over resources being spent on hidden windows.
    RendererSlotPool pool;
    const uint32_t   viewport = pool.Claim();
    ASSERT_NE( viewport, RendererSlotPool::kNoFreeSlot );

    DocumentWell well;
    auto&        hidden = static_cast<FakeDocument&>(
         well.Add( MakeDocument( DocumentTitle( "A", Asset( 11 ) ), Asset( 11 ), &pool ) ) );
    well.Add( MakeDocument( DocumentTitle( "B", Asset( 12 ) ), Asset( 12 ), &pool ) );

    EXPECT_EQ( pool.InUseCount(), 3u );
    EXPECT_EQ( RendererSlotsHeldByDocuments( well ), 2u );

    hidden.ReleaseRendererSlot();

    // THE CONTRACT: HoldsRendererSlot answers false afterwards. EditorLayer checks this and logs an error
    // if it does not, because a document that inherited the empty default while genuinely holding a slot
    // would keep it for ever and the refusal census would go on blaming a window the user cannot fix.
    EXPECT_FALSE( hidden.HoldsRendererSlot() );
    EXPECT_EQ( pool.InUseCount(), 2u ) << "the lease really went back to the pool";
    EXPECT_EQ( RendererSlotsHeldByDocuments( well ), 1u );

    // AND THE WINDOW IS STILL OPEN. This is the whole difference from a close, and it is the shape the
    // owner asked for: the resource goes, the layout does not move.
    EXPECT_EQ( well.Count(), 2u );
    EXPECT_NE( well.Find( Asset( 11 ) ), nullptr );

    // Which also makes it pending demand again — it will claim a slot the moment its window is drawn.
    EXPECT_EQ( PendingRendererSlotDemand( well.Documents() ), 1u );
}

TEST( DocumentSlotLease, ReleasingTwiceIsNotAnError )
{
    // The sweep runs every frame while a document stays hidden. A second release must not hand a slot back
    // to the pool twice — that would free somebody else's.
    RendererSlotPool pool;
    DocumentWell     well;
    auto&            hidden = static_cast<FakeDocument&>(
         well.Add( MakeDocument( DocumentTitle( "A", Asset( 11 ) ), Asset( 11 ), &pool ) ) );

    EXPECT_EQ( pool.InUseCount(), 1u );
    hidden.ReleaseRendererSlot();
    hidden.ReleaseRendererSlot();
    EXPECT_EQ( pool.InUseCount(), 0u );
    EXPECT_FALSE( hidden.HoldsRendererSlot() );
}

int main( int argc, char** argv )
{
    testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
