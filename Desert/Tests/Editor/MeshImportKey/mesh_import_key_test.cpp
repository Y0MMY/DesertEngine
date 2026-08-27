// THE MESH IMPORTER'S KEY — a suite about a COLLISION BY CONSTRUCTION, not about a hash.
//
// What was broken. An imported material's stable id was hashed from `<fileStem>::<materialName>#<index>`,
// and the material's file was written to `Materials/<fileStem>/<materialName>.demat`. Neither carries the
// source's DIRECTORY. Two meshes with the same file name in different folders were therefore one asset as
// far as the importer was concerned: same id, same output folder — and since the writer skips a .demat
// that already exists, the second mesh did not overwrite the first, it silently ADOPTED it. The second
// model came in wearing the first one's surface, with nothing logged and nothing null.
//
// Why this suite and not the registry's type check. The two colliding records are both Materials. A
// lookup that verifies the stored type against the requested one — which AssetManager now does — cannot
// tell two Materials apart and never could. This one has to be fixed where it is made: in the key.
//
// The repository already stands one file away from it. Assets/Meshes/base.fbx, base_basic_pbr.fbx and
// base_basic_shaded.fbx each contain a material named "model" (hence three files all called model.demat,
// in three folders). All three keys are `<stem>::model#0`; the ONLY thing separating them is that the
// three stems differ. Drop a second base.fbx in from any other pack, in any folder, and two of them merge.
//
// The tests below assert a RELATION in both directions: two sources that are different assets must never
// produce one key, and a source that has not moved must never produce a different one — the second half
// is what keeps the three .demat files committed in this repository valid, and it is asserted against
// their literal ids rather than against a restatement of the formula.

#include <gtest/gtest.h>

#include <Common/Core/AssetHandle.hpp>
#include <Common/Core/Constants.hpp>

#include <Editor/Import/CookPaths.hpp>

#include <filesystem>
#include <set>
#include <string>

namespace CookPaths = Desert::Editor::CookPaths;

namespace
{
    // Restores the content roots CookPaths reads. They are process-wide mutable globals, so a test that
    // opens a project and walks away leaves every test after it measuring that project.
    class ProjectRootGuard
    {
    public:
        ProjectRootGuard()
             : m_Assets( Common::Constants::Path::ASSETS_PATH ), m_Mesh( Common::Constants::Path::MESH_PATH ),
               m_Material( Common::Constants::Path::MATERIAL_PATH ),
               m_MeshCooked( Common::Constants::Path::MESH_PATH_COOKED )
        {
        }

        ~ProjectRootGuard()
        {
            Common::Constants::Path::ASSETS_PATH      = m_Assets;
            Common::Constants::Path::MESH_PATH        = m_Mesh;
            Common::Constants::Path::MATERIAL_PATH    = m_Material;
            Common::Constants::Path::MESH_PATH_COOKED = m_MeshCooked;
        }

    private:
        std::filesystem::path m_Assets;
        std::filesystem::path m_Mesh;
        std::filesystem::path m_Material;
        std::filesystem::path m_MeshCooked;
    };

    // A project opened the way the editor opens one, so that the roots under test are remapped roots and
    // not the built-in sandbox defaults. The folder NAMES are arbitrary; what every test here measures is
    // where a source sits RELATIVE to them.
    void OpenProject()
    {
        Common::Constants::Path::SetProjectRoot( std::filesystem::path( "Project" ), "Content" );
    }

    std::filesystem::path MeshSource( const std::string& relativeToMeshes )
    {
        return Common::Constants::Path::MESH_PATH / relativeToMeshes;
    }
} // namespace

TEST( MeshImportKey, TwoSameNamedMeshesInDifferentFoldersDoNotShareAMaterialKey )
{
    ProjectRootGuard guard;
    OpenProject();

    // The sabotage, written as the scenario rather than as a mutation of the code: two source files whose
    // ONLY difference is the folder they sit in, and a material name they happen to share — which is the
    // normal case, not a contrived one ("model", "Material", "lambert1" are what exporters emit).
    const auto propsBase = MeshSource( "Props/base.fbx" );
    const auto charsBase = MeshSource( "Characters/base.fbx" );

    const std::string propsKey = CookPaths::MaterialKey( propsBase, "model", 0 );
    const std::string charsKey = CookPaths::MaterialKey( charsBase, "model", 0 );

    EXPECT_NE( propsKey, charsKey )
         << "both meshes derive the material key '" << propsKey
         << "', so both stamp the same MaterialId into their submeshes and both write into one folder — "
            "where the importer's write-only-if-missing rule makes the second mesh adopt the first's "
            "material instead of getting its own";

    // The id is what actually reaches the submesh and the registry, so assert THAT and not only the string
    // it is hashed from: a derivation that folded the two keys back together would satisfy the line above.
    EXPECT_NE( Common::AssetHandle::FromKey( propsKey ), Common::AssetHandle::FromKey( charsKey ) );

    // And the material FILES must not land on top of each other either. Same folder plus same name is the
    // half of the collision that survives even when the ids differ: the second mesh's .demat is never
    // written, so its new id resolves to nothing at all.
    EXPECT_NE( CookPaths::MaterialFolder( propsBase ), CookPaths::MaterialFolder( charsBase ) );
}

TEST( MeshImportKey, MaterialsWithinOneMeshStaySeparate )
{
    ProjectRootGuard guard;
    OpenProject();

    // The companion: a key that answered "different" to everything above could be a counter, and a key
    // that ignored the material entirely would collapse every material of one mesh into one record.
    const auto source = MeshSource( "Props/base.fbx" );

    std::set<std::string> keys;
    keys.insert( CookPaths::MaterialKey( source, "model", 0 ) );
    keys.insert( CookPaths::MaterialKey( source, "glass", 1 ) );
    // Two slots that share a NAME — exporters do emit this — separated by their index alone.
    keys.insert( CookPaths::MaterialKey( source, "model", 2 ) );

    EXPECT_EQ( keys.size(), 3u ) << "two materials of one mesh collapsed onto one key";
}

TEST( MeshImportKey, AMeshThatHasNotMovedKeepsItsKey )
{
    ProjectRootGuard guard;
    OpenProject();

    // The other direction of the relation, and the reason no content in this repository had to be
    // rewritten when the key gained the directory: for a mesh sitting directly in Assets/Meshes — where
    // all three of the repository's meshes sit — the directory-relative identity IS the stem, so the key
    // is character-for-character what it was.
    //
    // These are the ids literally present in Resources/Assets/Materials/<stem>/model.demat, read out of
    // the committed files. Asserting against the FILES rather than against a second copy of the formula is
    // what makes this a test: if the derivation drifts, every material in the repository silently stops
    // resolving, and this line is the only thing that says so.
    struct Committed
    {
        const char* stem;
        uint64_t    materialId;
    };
    const Committed committed[] = {
         { "base", 4958558474483748124ull },
         { "base_basic_pbr", 17955490653248971586ull },
         { "base_basic_shaded", 1820073035653820619ull },
    };

    for ( const auto& entry : committed )
    {
        const auto source = MeshSource( std::string( entry.stem ) + ".fbx" );

        EXPECT_EQ( CookPaths::MeshRelativeId( source ).generic_string(), entry.stem )
             << "a mesh directly under Assets/Meshes must still identify as its own name";

        const auto id = Common::AssetHandle::FromKey( CookPaths::MaterialKey( source, "model", 0 ) );
        EXPECT_EQ( static_cast<uint64_t>( id ), entry.materialId )
             << "the id derived for " << entry.stem
             << "'s 'model' material no longer matches the one committed in its .demat, so the mesh's "
                "submesh reference and the material asset have come apart";

        EXPECT_EQ( CookPaths::MaterialFolder( source ), Common::Constants::Path::MATERIAL_PATH / entry.stem )
             << "the committed .demat for " << entry.stem << " is no longer where the importer writes";
    }
}

TEST( MeshImportKey, ASourceOutsideTheMeshFolderKeepsItsPlaceInTheKey )
{
    ProjectRootGuard guard;
    OpenProject();

    // Mesh sources are not only found in Assets/Meshes — a character pack lands in
    // Assets/Collections/<pack>/ and cooks through the same ladder. Two packs shipping a "body.fbx" is the
    // same collision as above, arriving by the route the engine actually uses for third-party content.
    const auto packA = Common::Constants::Path::ASSETS_PATH / "Collections/PackA/body.fbx";
    const auto packB = Common::Constants::Path::ASSETS_PATH / "Collections/PackB/body.fbx";

    EXPECT_EQ( CookPaths::MeshRelativeId( packA ).generic_string(), "Collections/PackA/body" );
    EXPECT_NE( CookPaths::MaterialKey( packA, "Material", 0 ), CookPaths::MaterialKey( packB, "Material", 0 ) );
    EXPECT_NE( CookPaths::MaterialFolder( packA ), CookPaths::MaterialFolder( packB ) );
}

int main( int argc, char** argv )
{
    ::testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
