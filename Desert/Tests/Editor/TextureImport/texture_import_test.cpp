// The texture importer, compiled AS ITSELF.
//
// WHY THIS SUITE EXISTS. Editor/Source/Editor/Import/TextureImporter.cpp was compiled by no test project in
// this repository, which means the branch that decides whether a texture is re-cooked, and the derivation
// that decides what handle every material and scene resolves it by, could both be sabotaged with the whole
// sweep still green. The file reaches nothing but std::filesystem, stb_image and the shared cooked-path
// formula, so nothing stood in the way of compiling it into a test binary except nobody having done it.
//
// WHAT IS ASSERTED, and what each one costs when it breaks:
//
//   1. The handle is DERIVED from the source path (FNV-1a over the canonical path), not drawn at random.
//      A random handle frozen into a .tex is the defect the deleted "back-compat" branch used to preserve:
//      wipe Cooked/ and every material and scene reference to that texture dies. Asserted against the
//      derivation itself, so an importer that invented its own copy of the hash would not agree.
//   2. Re-importing after wiping Cooked/ yields the SAME handle. That is the property in 1 stated as the
//      thing an artist actually does.
//   3. The cooked .tex says what the image is, and the handle written INTO the file is the handle Import
//      returned. Two places obliged to agree, which is the defect class this programme keeps finding.
//   4. The up-to-date branch: a .tex at least as new as its source is left ALONE (bytes unchanged), and a
//      source newer than its .tex is re-cooked. Getting this backwards means either "edits never take" or
//      "every launch re-cooks the whole project".
//   5. CookedMetaPath is the shared formula, including for a texture OUTSIDE the Textures/ directory -
//      the case whose drift is what put the formula in CookPaths in the first place.
//
// A three-pixel BMP written by the test is the input: stb_image reads BMP, and a file the test authors byte
// by byte cannot go stale the way a checked-in fixture can.

#include <Editor/Import/TextureImporter.hpp>
#include <Editor/Import/CookPaths.hpp>

#include <Common/Core/AssetHandle.hpp>
#include <Common/Core/Constants.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

using Desert::Editor::TextureImporter;

namespace
{
    void Put16( std::vector<unsigned char>& out, uint16_t v )
    {
        out.push_back( (unsigned char)( v & 0xFF ) );
        out.push_back( (unsigned char)( v >> 8 ) );
    }

    void Put32( std::vector<unsigned char>& out, uint32_t v )
    {
        for ( int i = 0; i < 4; ++i )
            out.push_back( (unsigned char)( ( v >> ( 8 * i ) ) & 0xFF ) );
    }

    // A 24-bit uncompressed BMP of the given size, filled with one colour. Rows are padded to 4 bytes,
    // which is the part a hand-written BMP usually gets wrong.
    void WriteBmp( const fs::path& path, int width, int height, unsigned char red )
    {
        const int rowBytes = width * 3;
        const int padding  = ( 4 - ( rowBytes % 4 ) ) % 4;
        const int pixels   = ( rowBytes + padding ) * height;

        std::vector<unsigned char> file;
        file.push_back( 'B' );
        file.push_back( 'M' );
        Put32( file, (uint32_t)( 14 + 40 + pixels ) );
        Put16( file, 0 );
        Put16( file, 0 );
        Put32( file, 14 + 40 ); // pixel data offset

        Put32( file, 40 ); // DIB header size
        Put32( file, (uint32_t)width );
        Put32( file, (uint32_t)height );
        Put16( file, 1 );  // planes
        Put16( file, 24 ); // bits per pixel
        Put32( file, 0 );  // BI_RGB
        Put32( file, (uint32_t)pixels );
        Put32( file, 2835 );
        Put32( file, 2835 );
        Put32( file, 0 );
        Put32( file, 0 );

        for ( int y = 0; y < height; ++y )
        {
            for ( int x = 0; x < width; ++x )
            {
                file.push_back( 0x20 ); // blue
                file.push_back( 0x40 ); // green
                file.push_back( red );  // red
            }
            for ( int p = 0; p < padding; ++p )
                file.push_back( 0 );
        }

        fs::create_directories( path.parent_path() );
        std::ofstream out( path, std::ios::binary );
        out.write( (const char*)file.data(), (std::streamsize)file.size() );
    }

    std::string ReadAll( const fs::path& path )
    {
        std::ifstream      in( path, std::ios::binary );
        std::ostringstream buffer;
        buffer << in.rdbuf();
        return buffer.str();
    }

    // One temporary project per test, with the content and cooked trees pointed at it. SetProjectRoot is
    // the same call the editor makes while parsing --project, so the importer sees exactly the layout it
    // sees in the editor.
    class TextureImport : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            // One directory per test, numbered rather than randomised: the tests run in one process, one
            // after another, and TearDown removes it - a random name would only make a failed run's
            // leftovers harder to find.
            m_Root = fs::temp_directory_path() / ( "desert_texture_import_" + std::to_string( ++s_Counter ) );
            fs::remove_all( m_Root );
            fs::create_directories( m_Root / "Resources" / "Assets" / "Textures" );
            Common::Constants::Path::SetProjectRoot( m_Root, "Resources/Assets" );
        }

        void TearDown() override
        {
            std::error_code ec;
            fs::remove_all( m_Root, ec );
        }

        fs::path TexturesDir() const
        {
            return m_Root / "Resources" / "Assets" / "Textures";
        }

        fs::path        m_Root;
        static unsigned s_Counter;
    };

    unsigned TextureImport::s_Counter = 0;
} // namespace

// 1 + 3. The handle is the FNV-1a derivation over the canonical source path, and the .tex written for it
// carries that same handle and the image's real size.
TEST_F( TextureImport, HandleIsDerivedFromTheSourcePathAndIsWrittenIntoTheCookedFile )
{
    const fs::path source = TexturesDir() / "T_Test.bmp";
    WriteBmp( source, 4, 3, 0xF0 );

    TextureImporter    importer;
    const Common::UUID handle = importer.Import( source );

    const std::string canonical = fs::weakly_canonical( source ).string();
    EXPECT_EQ( (uint64_t)handle, (uint64_t)Common::AssetHandle::FromKey( canonical ) );
    EXPECT_NE( (uint64_t)handle, 0u );

    const fs::path meta = TextureImporter::CookedMetaPath( source );
    ASSERT_TRUE( fs::exists( meta ) );

    const std::string text = ReadAll( meta );
    EXPECT_NE( text.find( "\"Handle\":" + std::to_string( (uint64_t)handle ) ), std::string::npos ) << text;
    EXPECT_NE( text.find( "\"Width\":4" ), std::string::npos ) << text;
    EXPECT_NE( text.find( "\"Height\":3" ), std::string::npos ) << text;
    EXPECT_NE( text.find( "\"Channels\":4" ), std::string::npos ) << text;
    EXPECT_NE( text.find( ".dds" ), std::string::npos ) << text;
}

// 2. Wipe Cooked/ and cook again: the same handle comes back. This is the property that keeps every
// material and every scene resolving after a clean re-cook.
TEST_F( TextureImport, HandleSurvivesWipingTheCookedTree )
{
    const fs::path source = TexturesDir() / "T_Test.bmp";
    WriteBmp( source, 2, 2, 0x11 );

    TextureImporter    first;
    const Common::UUID before = first.Import( source );

    fs::remove_all( Common::Constants::Path::COOKED_PATH );
    ASSERT_FALSE( fs::exists( TextureImporter::CookedMetaPath( source ) ) );

    TextureImporter    second; // a fresh importer, so the in-memory cache cannot be what answers
    const Common::UUID after = second.Import( source );

    EXPECT_EQ( (uint64_t)before, (uint64_t)after );
}

// 4a. A .tex at least as new as its source is up to date: the file is left byte for byte alone. Re-cooking
// it anyway would mean every project launch rewrites every texture it has.
TEST_F( TextureImport, CookedMetadataNewerThanTheSourceIsLeftUntouched )
{
    const fs::path source = TexturesDir() / "T_Test.bmp";
    WriteBmp( source, 4, 3, 0xF0 );

    TextureImporter    first;
    const Common::UUID handle = first.Import( source );

    const fs::path    meta   = TextureImporter::CookedMetaPath( source );
    const std::string cooked = ReadAll( meta );

    // Mark the metadata as newer than its source, which is the state a just-cooked file is in.
    fs::last_write_time( meta, fs::last_write_time( source ) + std::chrono::seconds( 10 ) );

    TextureImporter    second;
    const Common::UUID again = second.Import( source );

    EXPECT_EQ( (uint64_t)again, (uint64_t)handle );
    EXPECT_EQ( ReadAll( meta ), cooked );
}

// 4b. ...and a source EDITED after its .tex was written is re-cooked, with the new size in the file. The
// opposite mistake to 4a, and the one that reads as "my texture change did nothing".
TEST_F( TextureImport, SourceNewerThanTheCookedMetadataIsRecooked )
{
    const fs::path source = TexturesDir() / "T_Test.bmp";
    WriteBmp( source, 4, 3, 0xF0 );

    TextureImporter first;
    first.Import( source );

    const fs::path meta = TextureImporter::CookedMetaPath( source );
    ASSERT_NE( ReadAll( meta ).find( "\"Width\":4" ), std::string::npos );

    // The artist edits the texture: same path, different image, later timestamp.
    WriteBmp( source, 7, 5, 0x0A );
    fs::last_write_time( source, fs::last_write_time( meta ) + std::chrono::seconds( 10 ) );

    TextureImporter second;
    second.Import( source );

    const std::string text = ReadAll( meta );
    EXPECT_NE( text.find( "\"Width\":7" ), std::string::npos ) << text;
    EXPECT_NE( text.find( "\"Height\":5" ), std::string::npos ) << text;
}

// 5. The cooked path is the SHARED formula, for a texture in Textures/ and for one outside it. The second
// case is the one that used to escape Cooked/Textures with "../" and silently never get discovered.
TEST_F( TextureImport, CookedMetaPathIsTheSharedFormulaInsideAndOutsideTheTextureDirectory )
{
    const fs::path inside  = TexturesDir() / "Sub" / "T_Test.bmp";
    const fs::path outside = m_Root / "Resources" / "Assets" / "Collections" / "Pack" / "T_Other.bmp";

    EXPECT_EQ( TextureImporter::CookedMetaPath( inside ),
               Desert::Editor::CookPaths::CookedTexture( inside, ".tex" ) );
    EXPECT_EQ( TextureImporter::CookedMetaPath( outside ),
               Desert::Editor::CookPaths::CookedTexture( outside, ".tex" ) );

    // And it really is under the cooked texture tree in both cases - the relation the formula exists for.
    const std::string cookedRoot = Common::Constants::Path::TEXTURE_PATH_COOKED.string();
    EXPECT_EQ( TextureImporter::CookedMetaPath( inside ).string().rfind( cookedRoot, 0 ), 0u );
    EXPECT_EQ( TextureImporter::CookedMetaPath( outside ).string().rfind( cookedRoot, 0 ), 0u );
    EXPECT_EQ( TextureImporter::CookedMetaPath( inside ).extension().string(), ".tex" );
}

// The importer's own cache answers the second call for the same path, and answers it with the same id.
TEST_F( TextureImport, SecondImportOfTheSamePathReturnsTheSameHandle )
{
    const fs::path source = TexturesDir() / "T_Test.bmp";
    WriteBmp( source, 2, 2, 0x33 );

    TextureImporter    importer;
    const Common::UUID first  = importer.Import( source );
    const Common::UUID second = importer.Import( source );

    EXPECT_EQ( (uint64_t)first, (uint64_t)second );
}

int main( int argc, char** argv )
{
    ::testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
