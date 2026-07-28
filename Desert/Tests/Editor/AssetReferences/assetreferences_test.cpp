#include <gtest/gtest.h>

#include <Editor/Core/AssetReferences.hpp>

using Desert::Editor::AssetReferenceIndex;

namespace
{
    AssetReferenceIndex::Entry Make( std::string path, std::string ext,
                                     std::vector<std::string> tokens, std::string text )
    {
        AssetReferenceIndex::Entry e;
        e.Path   = std::move( path );
        e.Ext    = std::move( ext );
        e.Tokens = std::move( tokens );
        e.Text   = std::move( text );
        return e;
    }
} // namespace

// A material that embeds a texture's handle is reported as a referencer of that texture.
TEST( AssetReferenceIndex, FindsHandleReference )
{
    AssetReferenceIndex idx;
    idx.Add( Make( "Textures/Albedo.png", ".png", { "555000111" }, "" ) ); // binary target
    idx.Add( Make( "Materials/M.demat", ".demat", { "999" }, "{\"Textures\":[555000111]}" ) );

    const auto refs = idx.ReferencersOf( "Textures/Albedo.png" );
    ASSERT_EQ( refs.size(), 1u );
    EXPECT_EQ( refs[0], "Materials/M.demat" );
    EXPECT_TRUE( idx.IsReferenced( "Textures/Albedo.png" ) );
}

// A handle token must not match as a substring of a longer number.
TEST( AssetReferenceIndex, HandleMatchIsDigitBounded )
{
    AssetReferenceIndex idx;
    idx.Add( Make( "Textures/T.png", ".png", { "123" }, "" ) );
    idx.Add( Make( "Materials/Bigger.demat", ".demat", {}, "{\"h\":91234}" ) ); // 123 inside 91234
    idx.Add( Make( "Materials/Exact.demat", ".demat", {}, "{\"h\":123}" ) );     // exact

    const auto refs = idx.ReferencersOf( "Textures/T.png" );
    ASSERT_EQ( refs.size(), 1u );
    EXPECT_EQ( refs[0], "Materials/Exact.demat" );
}

// References by path string are found too.
TEST( AssetReferenceIndex, FindsPathReference )
{
    AssetReferenceIndex idx;
    idx.Add( Make( "Textures/Grid.png", ".png", { "Assets/Textures/Grid.png" }, "" ) );
    idx.Add( Make( "Scenes/Main.desce", ".desce", {}, "tex = \"Assets/Textures/Grid.png\"" ) );

    EXPECT_TRUE( idx.IsReferenced( "Textures/Grid.png" ) );
}

// Binary entries (no text) are never counted as referencers.
TEST( AssetReferenceIndex, BinaryEntriesAreNotReferencers )
{
    AssetReferenceIndex idx;
    idx.Add( Make( "Textures/A.png", ".png", { "111" }, "" ) );
    idx.Add( Make( "Textures/B.png", ".png", { "222" }, "" ) ); // has the token but no text to scan

    EXPECT_FALSE( idx.IsReferenced( "Textures/A.png" ) );
    EXPECT_TRUE( idx.ReferencersOf( "Textures/A.png" ).empty() );
}

// Only unreferenced LEAF assets are orphans; roots (scenes) and referenced leaves are excluded.
TEST( AssetReferenceIndex, OrphansAreUnreferencedLeaves )
{
    AssetReferenceIndex idx;
    idx.Add( Make( "Textures/Used.png", ".png", { "700" }, "" ) );
    idx.Add( Make( "Textures/Unused.png", ".png", { "800" }, "" ) );
    idx.Add( Make( "Materials/M.demat", ".demat", { "900" }, "{\"Textures\":[700]}" ) );
    idx.Add( Make( "Scenes/Main.desce", ".desce", {}, "{\"mat\":900}" ) ); // references the material

    const auto orphans = idx.Orphans( { ".png", ".demat" } );

    // Used.png is referenced by M; M is referenced by the scene; the scene is a root (not a leaf ext).
    // Only Unused.png remains.
    ASSERT_EQ( orphans.size(), 1u );
    EXPECT_EQ( orphans[0], "Textures/Unused.png" );
}

// Unknown asset path yields no referencers rather than crashing.
TEST( AssetReferenceIndex, UnknownPathIsEmpty )
{
    AssetReferenceIndex idx;
    idx.Add( Make( "Textures/A.png", ".png", { "1" }, "" ) );
    EXPECT_TRUE( idx.ReferencersOf( "does/not/exist.png" ).empty() );
    EXPECT_FALSE( idx.IsReferenced( "does/not/exist.png" ) );
}

int main( int argc, char** argv )
{
    testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
