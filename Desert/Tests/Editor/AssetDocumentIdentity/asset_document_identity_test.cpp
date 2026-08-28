// TWO ASSET DOCUMENTS MUST BE TWO WINDOWS, AND THE SAME ASSET MUST BE ONE.
//
// Every panel in this editor is drawn with ImGui::Begin( PanelDisplayTitle( panel->GetName() ) ), and ImGui
// takes a window's identity from the text after the LAST "###" in that string. Two windows that agree there
// are ONE window: the second one's content is drawn into the first, merged, with no error anywhere. Every
// panel before asset documents was a singleton and so never met the problem; ViewportPanel is the one type
// that did, and it escapes by baking "###sceneview<id>" into its title.
//
// An asset document escapes the same way, keyed on the SUBJECT HANDLE rather than on a counter — which is
// also what makes open-or-focus fall out for free, because the same material can then only ever produce the
// same window. That is one relation with two halves, and BOTH halves are asserted here: different subjects
// must not collide, and the same subject must not diverge.
//
// Why these live in headers at all: EditorLayer.cpp is one of the editor translation units no suite compiles
// (scripts/CI/UnreachedSources.sh), and neither the naming nor the lookup can be exercised through a window.
// So they sit in Editor/Panels/IPanel.hpp and Editor/Core/AssetEditorRegistry.hpp as pure functions, for
// exactly the reason Editor/Core/SceneViewIdentity.hpp does — see Tests/Engine/RendererSlots.

#include <Editor/Core/AssetEditorRegistry.hpp>
#include <Editor/Panels/IPanel.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

using Desert::Assets::AssetHandle;
using Desert::Assets::AssetTypeID;
using Desert::Editor::AssetDocumentTitle;
using Desert::Editor::FindOpenAssetDocument;
using Desert::Editor::IAssetEditorPanel;
using Desert::Editor::IPanel;
using Desert::Editor::PendingRendererSlotDemand;

namespace
{
    // The id half of a title: everything from the "###" the document introduced. This is the substring ImGui
    // hashes, so it is the thing two windows must not share.
    std::string WindowId( const std::string& title )
    {
        const auto pos = title.rfind( "###" );
        return pos == std::string::npos ? std::string{} : title.substr( pos );
    }

    // A document with nothing in it: the naming and the lookup are what is under test, and a real
    // MaterialEditorPanel would drag a Scene and a SceneRenderer in, neither constructible without a device.
    class FakeDocument final : public IAssetEditorPanel
    {
    public:
        FakeDocument( const std::string& name, const AssetHandle& subject,
                      AssetTypeID type = AssetTypeID::Material )
             : IAssetEditorPanel( name, subject, type )
        {
        }

        void OnUIRender() override
        {
        }

        [[nodiscard]] bool HoldsRendererSlot() const override
        {
            return m_HoldsSlot;
        }

        [[nodiscard]] bool ClaimsRendererSlot() const override
        {
            return m_ClaimsSlot;
        }

        // Set by the slot-census tests. Public because this is a stub, and a setter for each would be two
        // lines of ceremony around a bool the test is about.
        bool m_HoldsSlot  = false;
        bool m_ClaimsSlot = true;
    };

    // A CPU-only document — the four cloud editors. It never holds a renderer slot and never will, which is
    // the distinction PendingRendererSlotDemand exists to make.
    class FakeCpuDocument final : public IAssetEditorPanel
    {
    public:
        FakeCpuDocument( const std::string& name, const AssetHandle& subject, AssetTypeID type )
             : IAssetEditorPanel( name, subject, type )
        {
        }

        void OnUIRender() override
        {
        }

        [[nodiscard]] bool HoldsRendererSlot() const override
        {
            return false;
        }

        [[nodiscard]] bool ClaimsRendererSlot() const override
        {
            return false;
        }
    };

    // A tool panel — the thing the lookup must never mistake for a document, however it is named.
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
} // namespace

// --- The id relation ---------------------------------------------------------------------------------

TEST( AssetDocumentIdentity, TwoMaterialsGiveTwoDifferentWindowIds )
{
    const std::string a = AssetDocumentTitle( "MP_GreenTint", AssetHandle( 111u ) );
    const std::string b = AssetDocumentTitle( "CB_Orange", AssetHandle( 222u ) );

    EXPECT_NE( WindowId( a ), WindowId( b ) )
         << "Two materials produced the same ImGui window id, so ImGui would draw them as ONE merged window "
            "-- the second document's content inside the first document's frame, with no error anywhere.";
}

TEST( AssetDocumentIdentity, TwoMATERIALSWITHTHESAMENAMEStillGiveDifferentWindowIds )
{
    // Two `.demat` files with the same stem in different folders. The NAME is not the identity; the handle
    // is. Keying on the display name is the obvious shortcut and it merges these two into one window.
    const std::string a = AssetDocumentTitle( "Material", AssetHandle( 111u ) );
    const std::string b = AssetDocumentTitle( "Material", AssetHandle( 222u ) );

    EXPECT_NE( WindowId( a ), WindowId( b ) )
         << "Two different materials that happen to share a file name collided. The window id must come from "
            "the subject handle, never from the label.";
}

TEST( AssetDocumentIdentity, TheSameMaterialAlwaysGivesTheSameWindowId )
{
    // The other half of the relation, and the one open-or-focus rests on: asking for a material that is
    // already open must land on the window that is already there.
    EXPECT_EQ( WindowId( AssetDocumentTitle( "MP_GreenTint", AssetHandle( 111u ) ) ),
               WindowId( AssetDocumentTitle( "MP_GreenTint", AssetHandle( 111u ) ) ) );
}

TEST( AssetDocumentIdentity, TheDisplayNameCannotDecideTheWindowId )
{
    // ImGui takes the id from the LAST "###", and EditorLayer appends "###" + the whole name again when it
    // composes the visible title. So a display name carrying a "###" of its own would end up deciding the
    // id -- and two assets whose names did that would merge into one window. An asset file may be named
    // anything, so this is reachable from content, not only from code.
    const std::string a = AssetDocumentTitle( "Evil###shared", AssetHandle( 111u ) );
    const std::string b = AssetDocumentTitle( "Evil###shared", AssetHandle( 222u ) );

    EXPECT_NE( WindowId( a ), WindowId( b ) );
    EXPECT_EQ( WindowId( a ), "###assetdoc111" );
}

TEST( AssetDocumentIdentity, TheDocumentsIdIsTheLastMarkerInItsName )
{
    // What EditorLayer hands to Begin() is "<icon>  <label>###<name>", so the document's own marker is only
    // the id if it is the last "###" in the name. Asserted as the property rather than by re-composing
    // PanelDisplayTitle here: a copy of that rule could drift from the rule.
    const std::string title = AssetDocumentTitle( "MP_GreenTint", AssetHandle( 111u ) );
    EXPECT_EQ( title.rfind( "###" ), title.find( "###assetdoc" ) );
}

// --- Open-or-focus, keyed by the subject -------------------------------------------------------------

TEST( AssetDocumentIdentity, AnOpenDocumentIsFoundByItsSubject )
{
    std::vector<std::unique_ptr<IPanel>> panels;
    panels.push_back( std::make_unique<FakeTool>( "Details" ) );
    panels.push_back( std::make_unique<FakeDocument>( "MP_GreenTint", AssetHandle( 111u ) ) );
    panels.push_back( std::make_unique<FakeDocument>( "CB_Orange", AssetHandle( 222u ) ) );

    auto* found = FindOpenAssetDocument( panels, AssetHandle( 222u ) );
    ASSERT_NE( found, nullptr ) << "A material that is already open was not found, so the editor would open a "
                                   "SECOND window on it -- two parameter tables editing one asset.";
    EXPECT_EQ( found->Subject(), AssetHandle( 222u ) );
}

TEST( AssetDocumentIdentity, AMaterialThatIsNotOpenIsNotFound )
{
    std::vector<std::unique_ptr<IPanel>> panels;
    panels.push_back( std::make_unique<FakeDocument>( "MP_GreenTint", AssetHandle( 111u ) ) );

    EXPECT_EQ( FindOpenAssetDocument( panels, AssetHandle( 999u ) ), nullptr );
}

TEST( AssetDocumentIdentity, ToolPanelsAreNeverMistakenForDocuments )
{
    // The panel list holds both kinds. A lookup that matched on anything but "is an asset document with this
    // subject" would focus the Logs panel and never open the material.
    std::vector<std::unique_ptr<IPanel>> panels;
    panels.push_back( std::make_unique<FakeTool>( "Logs" ) );
    panels.push_back( std::make_unique<FakeTool>( "Assets" ) );

    EXPECT_EQ( FindOpenAssetDocument( panels, AssetHandle( 111u ) ), nullptr );
}

TEST( AssetDocumentIdentity, TheNullHandleMatchesNothing )
{
    // "No asset" is not a document to focus. Without this a failed path-to-handle resolution -- which yields
    // the null handle -- would focus whichever document happened to have been constructed from one, instead
    // of reporting that nothing could be opened.
    std::vector<std::unique_ptr<IPanel>> panels;
    panels.push_back( std::make_unique<FakeDocument>( "Broken", AssetHandle( static_cast<uint64_t>( 0 ) ) ) );

    EXPECT_EQ( FindOpenAssetDocument( panels, AssetHandle( static_cast<uint64_t>( 0 ) ) ), nullptr );
}

TEST( AssetDocumentIdentity, ADocumentsSubjectIsFixedForItsLife )
{
    // The subject is the window's identity: the title, and therefore the ImGui id, is built from it. A
    // subject that could change under a live window would either merge it into another document's window or
    // orphan its saved dock entry -- which is why IAssetEditorPanel exposes it read-only and holds it const.
    static_assert( !std::is_assignable_v<decltype( std::declval<FakeDocument&>().Subject() ), AssetHandle>,
                   "IAssetEditorPanel::Subject() must not be assignable -- the subject is the window's id." );
    SUCCEED();
}

// --- Cloud assets are documents too (Р3) --------------------------------------------------------------

TEST( AssetDocumentIdentity, TwoCloudTypesGiveTwoDifferentWindowIds )
{
    // The owner's request in one assertion: an artist opens two `.decloudtype` and gets two windows to
    // compare them in. The naming rule is shared with materials, so what this really guards is that nothing
    // about the cloud documents opted out of it — a cloud panel that kept its old constant panel name
    // ("Cloud Type") would put both subjects in one merged window and lose the second silently.
    const std::string a = AssetDocumentTitle( "Cumulus.decloudtype", AssetHandle( 501u ) );
    const std::string b = AssetDocumentTitle( "Stratus.decloudtype", AssetHandle( 502u ) );

    EXPECT_NE( WindowId( a ), WindowId( b ) )
         << "Two cloud types produced the same ImGui window id, so ImGui would draw them as ONE merged "
            "window -- the second document's controls inside the first document's frame.";
}

TEST( AssetDocumentIdentity, EveryCloudFormatIsFoundByItsOwnSubject )
{
    // Four formats, four open documents, one panel list. Open-or-focus is keyed on the SUBJECT and never on
    // the type, so a `.dcnv` and a `.decloudtype` open at once must not find each other -- which is what a
    // lookup that had fallen back to matching on SubjectType would do.
    std::vector<std::unique_ptr<IPanel>> panels;
    panels.push_back( std::make_unique<FakeTool>( "Assets" ) );
    panels.push_back(
         std::make_unique<FakeCpuDocument>( "N.dcnv", AssetHandle( 601u ), AssetTypeID::CloudNoiseVolume ) );
    panels.push_back(
         std::make_unique<FakeCpuDocument>( "T.decloudtype", AssetHandle( 602u ), AssetTypeID::CloudType ) );
    panels.push_back(
         std::make_unique<FakeCpuDocument>( "B.dcmv", AssetHandle( 603u ), AssetTypeID::CloudModellingVolume ) );
    panels.push_back(
         std::make_unique<FakeCpuDocument>( "L.dclayout", AssetHandle( 604u ), AssetTypeID::CloudLayout ) );

    ASSERT_NE( FindOpenAssetDocument( panels, AssetHandle( 601u ) ), nullptr );
    EXPECT_EQ( FindOpenAssetDocument( panels, AssetHandle( 602u ) )->SubjectType(), AssetTypeID::CloudType );
    EXPECT_EQ( FindOpenAssetDocument( panels, AssetHandle( 603u ) )->SubjectType(),
               AssetTypeID::CloudModellingVolume );
    EXPECT_EQ( FindOpenAssetDocument( panels, AssetHandle( 604u ) )->SubjectType(), AssetTypeID::CloudLayout );
    EXPECT_EQ( FindOpenAssetDocument( panels, AssetHandle( 605u ) ), nullptr );
}

// --- What is spoken for, and what is not ---------------------------------------------------------------

TEST( PendingRendererSlotDemand, AnOpenDocumentThatHasNotDrawnYetIsCounted )
{
    // The original reason the count exists: a Material Editor is created before it first draws, and builds
    // its PreviewViewport on that frame. Between the two it holds nothing and has a claim coming.
    std::vector<std::unique_ptr<IPanel>> panels;
    panels.push_back( std::make_unique<FakeDocument>( "MP_GreenTint", AssetHandle( 111u ) ) );

    EXPECT_EQ( PendingRendererSlotDemand( panels ), 1u );
}

TEST( PendingRendererSlotDemand, ADocumentThatAlreadyHoldsItsSlotIsNotCountedTwice )
{
    // It is already in the LIVE renderer count, so counting it here as well would refuse the cap one
    // document early for every window that had drawn.
    auto drawn         = std::make_unique<FakeDocument>( "MP_GreenTint", AssetHandle( 111u ) );
    drawn->m_HoldsSlot = true;

    std::vector<std::unique_ptr<IPanel>> panels;
    panels.push_back( std::move( drawn ) );

    EXPECT_EQ( PendingRendererSlotDemand( panels ), 0u );
}

TEST( PendingRendererSlotDemand, CpuOnlyDocumentsAreNotPendingDemand )
{
    // THE DEFECT THIS RULE EXISTS FOR. The four cloud editors bake on the CPU and upload an Image2D; they
    // hold no renderer slot and never will. Counted as pending demand -- which is what "open, holding
    // nothing" meant before ClaimsRendererSlot existed -- five of them beside the main viewport would make
    // `live + pending` reach the six-slot cap, and the sixth cloud asset an artist double-clicked would be
    // refused with a census listing windows that hold nothing and would never hold anything.
    std::vector<std::unique_ptr<IPanel>> panels;
    for ( uint64_t i = 0; i < 5; ++i )
    {
        panels.push_back(
             std::make_unique<FakeCpuDocument>( "cloud", AssetHandle( 700u + i ), AssetTypeID::CloudType ) );
    }

    EXPECT_EQ( PendingRendererSlotDemand( panels ), 0u )
         << "A CPU-only document was counted as a renderer-slot claim, so opening a sixth cloud asset would "
            "be refused for a shortage that does not exist.";
}

TEST( PendingRendererSlotDemand, CountsOnlyTheDocumentsThatWillActuallyClaim )
{
    // The mixed list, which is the one the editor really has: tool panels, cloud documents, a drawn
    // material and an undrawn one. Only the last is demand.
    auto drawn         = std::make_unique<FakeDocument>( "Drawn", AssetHandle( 801u ) );
    drawn->m_HoldsSlot = true;

    std::vector<std::unique_ptr<IPanel>> panels;
    panels.push_back( std::make_unique<FakeTool>( "Logs" ) );
    panels.push_back( std::move( drawn ) );
    panels.push_back( std::make_unique<FakeDocument>( "Undrawn", AssetHandle( 802u ) ) );
    panels.push_back(
         std::make_unique<FakeCpuDocument>( "L.dclayout", AssetHandle( 803u ), AssetTypeID::CloudLayout ) );

    EXPECT_EQ( PendingRendererSlotDemand( panels ), 1u );
}

TEST( PendingRendererSlotDemand, ADocumentThatDoesNotSayIsTreatedAsAClaimant )
{
    // ClaimsRendererSlot defaults to TRUE, and that default is the conservative one: a new document type
    // that forgets to answer is refused early rather than admitted past the cap and discovered later as two
    // surfaces trading each other's per-frame camera. Asserted on the base class's own default so that
    // flipping it to false-by-default cannot pass unnoticed.
    std::vector<std::unique_ptr<IPanel>> panels;
    panels.push_back( std::make_unique<FakeDocument>( "Silent", AssetHandle( 901u ) ) );

    EXPECT_TRUE( static_cast<const IAssetEditorPanel*>( panels.back().get() )->ClaimsRendererSlot() );
    EXPECT_EQ( PendingRendererSlotDemand( panels ), 1u );
}

int main( int argc, char** argv )
{
    testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
