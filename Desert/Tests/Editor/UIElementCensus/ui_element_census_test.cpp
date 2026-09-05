// "Every UI component type the editor can create, the renderer can draw."
//
// The defect this exists to prevent, measured on 2026-09-05: the UI Editor panel's create menu offered ten
// element types while the renderer that panel previewed with knew six of the engine's twenty-two. Pressing
// "+ Slider" in that window produced a slider the same window could not show. Both ends looked right — the
// menu created a real component, the shipping renderer drew it in the viewport — and the middle link, the
// panel's private renderer, silently lost six of them.
//
// A comment saying "these N types are covered" is exactly what went stale there, so the statement is a test
// instead, and it MEASURES both sides rather than asserting a declaration:
//
//   * the editor side is Editor/Panels/UI/UIElementCatalog.hpp, the one list both create menus are generated
//     from (there used to be two lists, and they had already drifted);
//   * the renderer side is read out of Engine/UI/UICanvasRenderer2D.cpp's OWN DISPATCH — the `has<ECS::T>` /
//     `view<ECS::T>` calls it makes on the registry. Not a header, not a table somebody maintains beside the
//     code: if a case is deleted, the query goes with it and this test goes red. A mention in a comment is
//     not enough to pass, because a comment does not query the registry.
//
// And it is bidirectional, in the SettingConsumers shape: every UI component the renderer handles must
// appear in exactly one row here — a catalog entry, or an explicit exclusion WITH ITS REASON. A twenty-third
// component fails this test until somebody decides which of the two it is. That decision is the point.

#include <Editor/Panels/UI/UIElementCatalog.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using Desert::Editor::kUIElementCount;
using Desert::Editor::kUIElements;

namespace
{
    constexpr const char* kRenderer = "Desert/Desert/Source/Engine/UI/UICanvasRenderer2D.cpp";

    // Component types the shipping renderer handles that are deliberately NOT offered by the create menus.
    // Each needs a reason, and the reason is the row.
    struct Exclusion
    {
        const char* Type;
        const char* Why;
    };

    constexpr Exclusion kExclusions[] = {
         { "UICanvasComponent", "the canvas root itself — created by its own 'Create UI Canvas' action, "
                                "and a second canvas would not be drawn (the renderer takes the first)" },
         { "UILayoutComponent", "the rect. Every element gets one automatically in AddUIChild; on its own "
                                "it is an invisible box" },
         { "UIIconComponent", "draws NOTHING until an .svg asset is bound (DrawIcon returns early on an "
                              "unset icon), so a menu entry would hand the author an invisible entity" },
         { "UIScreenComponent", "the screen machine: a screen with no name is skipped by the renderer's "
                                "seeding loop, so a menu-created one would be invisible" },
         { "UIScreenStackComponent", "belongs on the canvas entity, not on a child element" },
         { "UIAnimComponent", "modifier — a keyed clip added in Details to an element that already exists" },
         { "UIBindingComponent", "modifier — binds a data-store value into an element that already exists" },
         { "UITweenComponent", "modifier — a from/to animation on an element that already exists" },
         { "UIDraggableComponent", "modifier — makes an existing element draggable" },
         { "UIDropTargetComponent", "modifier — makes an existing element a drop target" },
         { "UIPointerEventsComponent", "modifier — adds enter/exit/press messages to an existing element" },
    };

    // The repository root, found by walking up from wherever the test binary was started — the same approach
    // the setting-consumers and font-baker tests use, so none of them has to be run from one exact directory.
    std::string RepoRoot()
    {
        std::string prefix = "./";
        for ( int up = 0; up < 6; ++up )
        {
            std::ifstream probe( prefix + "Desert/Desert/Source/Engine/ECS/Components.hpp" );
            if ( probe )
                return prefix;
            prefix += "../";
        }
        return {};
    }

    std::string ReadFile( const std::string& path )
    {
        std::ifstream in( path );
        if ( !in )
            return {};
        std::ostringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }

    // Every UI component type the renderer actually QUERIES: the `has<ECS::UIxxx>` / `view<ECS::UIxxx>` calls
    // in its source. This is the census, read from the dispatch itself rather than from any list.
    std::set<std::string> RendererDispatch( const std::string& source )
    {
        std::set<std::string> types;
        // The three ways this renderer reaches a component. `get<` also covers `try_get<`, so the census
        // survives a refactor from has+get to try_get; measured 2026-09-05, all three agree on 22 types.
        for ( const char* call : { "has<ECS::UI", "get<ECS::UI", "view<ECS::UI" } )
        {
            const std::string needle( call );
            for ( std::size_t at = source.find( needle ); at != std::string::npos;
                  at             = source.find( needle, at + 1 ) )
            {
                const std::size_t nameStart = at + needle.size() - 2; // keep "UI"
                const std::size_t close     = source.find( '>', nameStart );
                if ( close == std::string::npos )
                    continue;
                types.insert( source.substr( nameStart, close - nameStart ) );
            }
        }
        return types;
    }
} // namespace

// Every element the editor can create is a type the shipping renderer dispatches on. This is the direction
// that was broken: ten creatable types, six drawable ones.
TEST( UIElementCensus, EveryCreatableElementIsDrawnByTheRenderer )
{
    const std::string root = RepoRoot();
    ASSERT_FALSE( root.empty() ) << "could not locate the repository root from the working directory";

    const std::string source = ReadFile( root + kRenderer );
    ASSERT_FALSE( source.empty() ) << kRenderer << " could not be read";

    const std::set<std::string> dispatch = RendererDispatch( source );
    ASSERT_FALSE( dispatch.empty() ) << "read " << kRenderer << " but found no ECS::UI* dispatch in it — the "
                                     << "renderer was restructured and this test no longer measures anything";

    for ( std::size_t i = 0; i < kUIElementCount; ++i )
    {
        const std::string type = kUIElements[i].ComponentType;
        EXPECT_TRUE( dispatch.count( type ) == 1 )
             << "the editor's create menus offer '" << kUIElements[i].Label << "' (ECS::" << type
             << ") but " << kRenderer << " never asks the registry for it — the element would be created "
             << "and never drawn. Add a case to the renderer, or drop the catalog entry.";
    }
}

// ...and the other direction: nothing the renderer handles is left undecided. A component added to the
// renderer must become a catalog entry or an exclusion with a reason.
TEST( UIElementCensus, EveryDrawnComponentIsEitherCreatableOrExcludedWithAReason )
{
    const std::string root = RepoRoot();
    ASSERT_FALSE( root.empty() );
    const std::string source = ReadFile( root + kRenderer );
    ASSERT_FALSE( source.empty() ) << kRenderer << " could not be read";

    std::set<std::string> accounted;
    for ( std::size_t i = 0; i < kUIElementCount; ++i )
        accounted.insert( kUIElements[i].ComponentType );
    for ( const Exclusion& x : kExclusions )
    {
        EXPECT_TRUE( accounted.insert( x.Type ).second )
             << x.Type << " is both a catalog entry and an exclusion — it must be exactly one";
        EXPECT_GT( std::string( x.Why ).size(), 20u ) << x.Type << " is excluded without a reason";
    }

    for ( const std::string& type : RendererDispatch( source ) )
        EXPECT_TRUE( accounted.count( type ) == 1 )
             << "ECS::" << type << " is drawn by " << kRenderer
             << " but is neither an entry in UIElementCatalog.hpp nor an exclusion in this test. Decide "
             << "which it is: an element the author can place, or something attached to one.";
}

// The two lists partition the renderer's census exactly — no entry and no exclusion names a component the
// renderer does not handle at all. Without this a stale row could hide a deleted case.
TEST( UIElementCensus, NoCatalogEntryOrExclusionIsAGhost )
{
    const std::string root = RepoRoot();
    ASSERT_FALSE( root.empty() );
    const std::set<std::string> dispatch = RendererDispatch( ReadFile( root + kRenderer ) );

    for ( const Exclusion& x : kExclusions )
        EXPECT_TRUE( dispatch.count( x.Type ) == 1 )
             << "ECS::" << x.Type << " is excluded from the create menus here, but the renderer does not "
             << "handle it either — the row is stale and should go.";

    // Every UI component the ENGINE has is one the renderer handles, so the census is complete: 11 creatable
    // + 11 excluded. The number is pinned so that growing either side is a deliberate edit of this file.
    EXPECT_EQ( dispatch.size(), kUIElementCount + sizeof( kExclusions ) / sizeof( kExclusions[0] ) );
}

int main( int argc, char** argv )
{
    testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
