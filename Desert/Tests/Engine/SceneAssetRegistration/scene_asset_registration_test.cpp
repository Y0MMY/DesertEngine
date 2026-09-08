// Ф6 — ONE RELATION, ASSERTED TWICE, BECAUSE IT CAN FAIL IN TWO PLACES.
//
//     EVERY ASSET A LOADED SCENE REFERENCES IS REGISTERED IN ITS SERVICE, ON EVERY ROUTE THAT
//     RESOLVES THE REFERENCE.
//
// The defect this is about is the tree's most-repeated shape: both ends of a chain look right and the
// middle link silently drops half the work. `ComponentRegistry`'s material and mesh branches resolved a
// scene's reference as FIND-ELSE-CREATE and registered with the service only on the CREATE route. A
// lookup that returns an existing record is a correct lookup; registering a record you just made is a
// correct registration; the disagreement is that a reference to an asset SOMEBODY ELSE had already
// created resolved to a live handle no service could answer for.
//
// It never showed, because `AssetPreloader::PreloadCookedAssetsAndMaterials` registers every mesh and
// material under the two content roots it walks, and `EditorLayer::OnUpdate` refuses to load a scene
// until the startup stages have finished. THAT IS A SAFETY NET, NOT A GUARANTEE — it is stated nowhere
// the parse can read, it covers only what those two roots contain, and it leaves with the first refactor
// by somebody who does not know it is load-bearing.
//
// WHY TWO ASSERTIONS AND NOT ONE. The relation can be broken two ways and they need different tests:
//
//   1. THE RULE ITSELF could be written to treat the routes differently. `ResolveSceneReference` is the
//      one place the rule lives, it is a pure template, and fakes make both routes reachable without a
//      device. Asserted as an AGREEMENT between the routes (§4 of `desert-engine-verify`) and not as two
//      separate facts, because two separate facts is exactly how this project found one defect four
//      times.
//
//   2. THE CALL SITES could stop using it — most cheaply by doing what the task forbade in as many
//      words: adding a registration to the found branch and leaving the created branch's one in place.
//      Two registration sites is the same disagreement waiting to happen, so the census below asserts
//      that each service has EXACTLY ONE registration site in the resolver. On the tree before this
//      task it counted two per service, which is the red this suite was shown in.
//
// NO GPU, NO WINDOW, NO ASSET MANAGER. The rule is a template over three callables and the census is a
// text scan, so this suite runs anywhere the sweep does.

#include <gtest/gtest.h>

#include <Engine/Core/Serialize/AssetReferenceResolve.hpp>

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using Desert::Core::Serialize::ReferenceOrigin;
using Desert::Core::Serialize::ResolveSceneReference;

namespace
{
    // A record stands in for `Assets::Asset<MeshAsset>` / `Asset<MaterialAsset>`: a shared_ptr-like thing
    // whose falsiness means "the registry does not hold this file". Nothing about the relation depends on
    // what an asset IS, so nothing about an asset is compiled in here.
    struct Record
    {
        int Id = 0;

        explicit operator bool() const
        {
            return Id != 0;
        }
    };

    // What a service would have been told.
    struct Ledger
    {
        std::vector<std::pair<int, ReferenceOrigin>> Registered;

        size_t CountFor( ReferenceOrigin origin ) const
        {
            size_t n = 0;
            for ( const auto& entry : Registered )
                if ( entry.second == origin )
                    ++n;
            return n;
        }
    };

    std::string RepoRoot()
    {
        std::string prefix = "./";
        for ( int up = 0; up < 6; ++up )
        {
            std::ifstream probe( prefix + "Desert/Desert/Source/Engine/Core/Serialize/ComponentRegistry.cpp" );
            if ( probe )
                return prefix;
            prefix += "../";
        }
        return {};
    }

    std::string ReadAll( const std::filesystem::path& path )
    {
        std::ifstream      in( path, std::ios::binary );
        std::ostringstream buffer;
        buffer << in.rdbuf();
        return buffer.str();
    }

    size_t CountOccurrences( const std::string& haystack, const std::string& needle )
    {
        size_t n = 0;
        for ( size_t at = haystack.find( needle ); at != std::string::npos;
              at        = haystack.find( needle, at + needle.size() ) )
            ++n;
        return n;
    }
} // namespace

// ─────────────────────────────────────────────────────────────────────────────────────────────────
// 1. The rule: the two routes are not told apart.
// ─────────────────────────────────────────────────────────────────────────────────────────────────

TEST( SceneAssetRegistration, AFoundRecordIsRegistered )
{
    Ledger ledger;

    const Record resolved = ResolveSceneReference( [] { return Record{ 7 }; },
                                                   [] { return Record{ 0 }; }, // must not be reached
                                                   [&ledger]( const Record& r, ReferenceOrigin o )
                                                   { ledger.Registered.push_back( { r.Id, o } ); } );

    EXPECT_EQ( resolved.Id, 7 );
    ASSERT_EQ( ledger.Registered.size(), 1u );
    EXPECT_EQ( ledger.Registered[0].first, 7 );
    EXPECT_EQ( ledger.Registered[0].second, ReferenceOrigin::Found );
}

TEST( SceneAssetRegistration, ACreatedRecordIsRegistered )
{
    Ledger ledger;

    const Record resolved = ResolveSceneReference( [] { return Record{ 0 }; }, [] { return Record{ 9 }; },
                                                   [&ledger]( const Record& r, ReferenceOrigin o )
                                                   { ledger.Registered.push_back( { r.Id, o } ); } );

    EXPECT_EQ( resolved.Id, 9 );
    ASSERT_EQ( ledger.Registered.size(), 1u );
    EXPECT_EQ( ledger.Registered[0].first, 9 );
    EXPECT_EQ( ledger.Registered[0].second, ReferenceOrigin::Created );
}

// THE RELATION, stated as the agreement between the two routes rather than as two facts about them. A
// change that registers on one route and not the other passes both tests above one at a time and fails
// here — which is the whole reason this project stopped asserting sides.
TEST( SceneAssetRegistration, BothRoutesRegisterTheSameNumberOfTimes )
{
    Ledger ledger;

    ResolveSceneReference( [] { return Record{ 1 }; }, [] { return Record{ 0 }; },
                           [&ledger]( const Record& r, ReferenceOrigin o )
                           { ledger.Registered.push_back( { r.Id, o } ); } );
    ResolveSceneReference( [] { return Record{ 0 }; }, [] { return Record{ 2 }; },
                           [&ledger]( const Record& r, ReferenceOrigin o )
                           { ledger.Registered.push_back( { r.Id, o } ); } );

    EXPECT_EQ( ledger.CountFor( ReferenceOrigin::Found ), ledger.CountFor( ReferenceOrigin::Created ) );
    EXPECT_EQ( ledger.Registered.size(), 2u );
}

// A reference that names nothing registers nothing — and, just as importantly, does not report anything
// from inside the rule. The caller owns that message because only the caller knows the asset type and
// the spelling that failed (DC 1.4).
TEST( SceneAssetRegistration, AReferenceThatResolvesToNothingRegistersNothing )
{
    Ledger ledger;

    const Record resolved = ResolveSceneReference( [] { return Record{ 0 }; }, [] { return Record{ 0 }; },
                                                   [&ledger]( const Record& r, ReferenceOrigin o )
                                                   { ledger.Registered.push_back( { r.Id, o } ); } );

    EXPECT_FALSE( static_cast<bool>( resolved ) );
    EXPECT_TRUE( ledger.Registered.empty() );
}

// The Found route must not run `create`, and the Created route must not run `find` twice. Both are what
// makes the rule affordable to apply to EVERY reference in a scene: the common case is one map lookup.
TEST( SceneAssetRegistration, TheRouteNotTakenIsNotRun )
{
    int finds = 0, creates = 0;

    ResolveSceneReference( [&finds] { ++finds; return Record{ 4 }; },
                           [&creates] { ++creates; return Record{ 5 }; },
                           []( const Record&, ReferenceOrigin ) {} );

    EXPECT_EQ( finds, 1 );
    EXPECT_EQ( creates, 0 );
}

// ─────────────────────────────────────────────────────────────────────────────────────────────────
// 2. The census: the resolver has ONE registration site per service, so the routes CANNOT disagree.
// ─────────────────────────────────────────────────────────────────────────────────────────────────

TEST( SceneAssetRegistration, TheResolverHasExactlyOneRegistrationSitePerService )
{
    const std::string root = RepoRoot();
    ASSERT_FALSE( root.empty() ) << "ComponentRegistry.cpp was not found from the test's working directory";

    const std::string source =
         ReadAll( root + "Desert/Desert/Source/Engine/Core/Serialize/ComponentRegistry.cpp" );
    ASSERT_FALSE( source.empty() );

    // HOW MANY TIMES THIS FILE REACHES EACH SERVICE AT ALL — not how many times it calls a particular
    // method on one. Counting `->Register(` would pass a file that registered eagerly in one branch and
    // lazily in another, and it would also pass a branch whose only contact with the service is a
    // `->Get()` used as a guard, which is the shape this task removed (it BUILT the material it was
    // asking about). One accessor call is the strongest form of "one site" that a text scan can state,
    // and it is what the file now looks like: `auto* service = ...GetMeshService();` inside
    // EnsureMeshRegistered, and nowhere else.
    //
    // MEASURED ON THE TREE THIS TASK STARTED FROM: 2 for the mesh service (FromPath's create branch,
    // FromGuid's guard) and 3 for the material service (the same two plus FromGuid's `Get`-as-a-guard).
    // That is the red this suite was shown in.
    const size_t meshSites     = CountOccurrences( source, "GetMeshService()" );
    const size_t materialSites = CountOccurrences( source, "GetMaterialService()" );

    EXPECT_EQ( meshSites, 1u ) << "the mesh reference must reach the service in ONE place, so the found "
                                  "and created routes cannot drift apart";
    EXPECT_EQ( materialSites, 1u ) << "the material reference must reach the service in ONE place, so the "
                                      "found and created routes cannot drift apart";

    // And that one place is reached through the rule above, not by a hand-written find-else-create beside
    // it. Two call sites: the mesh reference and the material reference.
    EXPECT_GE( CountOccurrences( source, "ResolveSceneReference(" ), 2u );
}

int main( int argc, char** argv )
{
    testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
