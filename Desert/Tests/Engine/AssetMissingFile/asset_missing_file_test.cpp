// The MISSING-FILE branch of the loaders, which was DEAD CODE until 2026-09-05. Each of these
// loaders was written with a deliberate answer to "the file is not there":
//
//   - SurfaceMaterialAsset::Load: "New / empty material — canonical defaults; editable and
//     re-savable" (a success, by design — the editor creates materials by naming a file that does
//     not exist yet);
//   - CloudTypeAsset::Load: "a file that is missing ... is an ERROR carrying the reason" (its own
//     header says so);
//   - PrefabAsset::Load: "Prefab file is empty or missing" (an error the scene loader logs and
//     survives — covered by the live editor run, not here, because PrefabAsset.cpp includes
//     Scene.hpp and no GPU-free suite can compile it).
//
// None of those branches could execute for a genuinely absent file, because
// FileSystem::ReadFileContent aborted the process before returning. These tests pin the branches
// now that the primitive is soft: each Load RETURNS (the suite being alive is half the assertion)
// and answers with exactly the policy its author wrote. Reverting the primitive's miss path to
// DESERT_VERIFY kills this suite outright.

#include <Engine/Assets/CloudTypeAsset.hpp>
#include <Engine/Assets/Mesh/SurfaceMaterialAsset.hpp>

#include <gtest/gtest.h>

#include <filesystem>

namespace fs = std::filesystem;

namespace
{
    fs::path MissingPath( const char* name )
    {
        const fs::path dir = fs::temp_directory_path() / "desert_missing_asset_test";
        fs::remove_all( dir );
        fs::create_directories( dir );
        return dir / name; // never created
    }
} // namespace

TEST( AssetMissingFile, SurfaceMaterialLoadsCanonicalDefaults )
{
    const fs::path path = MissingPath( "brand_new.demat" );
    ASSERT_FALSE( fs::exists( path ) );

    Desert::Assets::SurfaceMaterialAsset material( Desert::Assets::AssetPriority::Medium, path );
    const auto result = material.Load();

    // The branch's own comment: a missing .demat is a NEW material — usable, editable, re-savable.
    EXPECT_TRUE( result.IsSuccess() );
    EXPECT_TRUE( material.IsReadyForUse() );
    // Canonical defaults: no authored parameters, and the shader falls back to the standard surface.
    EXPECT_TRUE( material.Data().Params.empty() );
    EXPECT_FALSE( material.Data().ShaderName.has_value() );
}

TEST( AssetMissingFile, CloudTypeLoadRefusesWithTheReason )
{
    const fs::path path = MissingPath( "gone.decloudtype" );
    ASSERT_FALSE( fs::exists( path ) );

    Desert::Assets::CloudTypeAsset type( Desert::Assets::AssetPriority::Medium, path );
    const auto result = type.Load();

    // The header's contract: missing is an ERROR carrying the reason — never a quiet default type.
    ASSERT_FALSE( result.IsSuccess() );
    EXPECT_NE( result.GetError().find( "empty or could not be opened" ), std::string::npos )
         << result.GetError();
    EXPECT_FALSE( type.IsReadyForUse() );
}

int main( int argc, char** argv )
{
    testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
