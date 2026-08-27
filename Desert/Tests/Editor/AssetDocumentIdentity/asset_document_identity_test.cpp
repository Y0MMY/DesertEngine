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
        FakeDocument( const std::string& name, const AssetHandle& subject )
             : IAssetEditorPanel( name, subject, AssetTypeID::Material )
        {
        }

        void OnUIRender() override
        {
        }

        [[nodiscard]] bool HoldsRendererSlot() const override
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

int main( int argc, char** argv )
{
    testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
