#include <Common/Utilities/ContentManifest.hpp>
#include <Common/Utilities/ContentUpdate.hpp>
#include <Common/Utilities/PakFile.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using namespace Common::Utils;

namespace
{
    fs::path MakeTempDir()
    {
        const fs::path dir = fs::temp_directory_path() / "desert_content_update_test";
        fs::remove_all( dir );
        fs::create_directories( dir );
        return dir;
    }

    void WriteFile( const fs::path& path, const std::string& content )
    {
        fs::create_directories( path.parent_path() );
        std::ofstream out( path, std::ios::binary | std::ios::trunc );
        out.write( content.data(), static_cast<std::streamsize>( content.size() ) );
    }

    std::string ReadFile( const fs::path& path )
    {
        std::ifstream in( path, std::ios::binary );
        return std::string( ( std::istreambuf_iterator<char>( in ) ), std::istreambuf_iterator<char>() );
    }

    ContentManifestEntry Entry( std::string key, const std::string& bytes )
    {
        return { std::move( key ), static_cast<uint64_t>( bytes.size() ),
                 PakContentHash( bytes.data(), bytes.size() ) };
    }

    // Unpacks an archive into a directory and records what it handed over — the installer this
    // mechanism is built for, reduced to the two lines it actually is.
    ContentManifest Install( const PakReader& pak, const fs::path& root )
    {
        for ( const auto& key : pak.KeysWithPrefix( "" ) )
            WriteFile( root / fs::path( key ), pak.Read( key ).value_or( std::string{} ) );
        return ContentManifest::FromPak( pak );
    }
} // namespace

// ============================================================================================
// THE ACCEPTANCE TEST. Both halves of one relation, against real .dpak archives, in one run —
// because they fail in OPPOSITE directions and either alone is satisfiable by a wrong mechanism:
// "never overwrite" passes half two and fails half one, "always overwrite" does the reverse.
// ============================================================================================
TEST( ContentUpdate, TheSourceRemovesItsOwnFileAndTheEditedOneSurvives )
{
    const fs::path dir     = MakeTempDir();
    const fs::path install = dir / "install";

    const std::string untouchedV1 = "untouched v1";
    const std::string untouchedV2 = "untouched v2"; // the source updates this one
    const std::string editedV1    = "shipped material";
    const std::string doomed      = "the next release drops this";
    const std::string quiet       = "neither side ever touches this";

    {
        PakWriter writer( dir / "v1.dpak" );
        ASSERT_TRUE( writer.IsOpen() );
        ASSERT_TRUE( writer.AddData( "meshes/untouched.txt", untouchedV1.data(), untouchedV1.size() ) );
        ASSERT_TRUE( writer.AddData( "meshes/edited.demat", editedV1.data(), editedV1.size() ) );
        ASSERT_TRUE( writer.AddData( "meshes/gone.txt", doomed.data(), doomed.size() ) );
        // Most of a real update is files that did not move at all; without one here the test would not
        // notice a mechanism that simply rewrites everything.
        ASSERT_TRUE( writer.AddData( "meshes/quiet.txt", quiet.data(), quiet.size() ) );
        ASSERT_TRUE( writer.Finalize() > 0 );
    }

    ContentManifest recorded;
    {
        PakReader v1( dir / "v1.dpak" );
        ASSERT_TRUE( v1.IsOpen() ) << v1.OpenError();
        recorded = Install( v1, install );
    }

    // A PERSON OPENS THE MATERIAL AND CHANGES IT. This is the whole reason the record exists: from the
    // outside, these bytes and a new release's bytes are both just "different from what we shipped".
    const std::string editedByHand = "material the artist retuned";
    WriteFile( install / "meshes" / "edited.demat", editedByHand );

    // The next release: one file updated, one file deleted, the edited one untouched by the source.
    {
        PakWriter writer( dir / "v2.dpak" );
        ASSERT_TRUE( writer.IsOpen() );
        ASSERT_TRUE( writer.AddData( "meshes/untouched.txt", untouchedV2.data(), untouchedV2.size() ) );
        ASSERT_TRUE( writer.AddData( "meshes/edited.demat", editedV1.data(), editedV1.size() ) );
        ASSERT_TRUE( writer.AddData( "meshes/quiet.txt", quiet.data(), quiet.size() ) );
        ASSERT_TRUE( writer.Finalize() > 0 );
    }

    PakReader v2( dir / "v2.dpak" );
    ASSERT_TRUE( v2.IsOpen() ) << v2.OpenError();
    const ContentManifest incoming = ContentManifest::FromPak( v2 );

    auto onDisk = ContentManifest::FromDirectory( install );
    ASSERT_TRUE( onDisk.IsSuccess() ) << onDisk.GetError();

    const auto plan =
         PlanContentUpdate( recorded, onDisk.GetValue(), incoming, ContentAuthorship::LocallyAuthored );
    EXPECT_EQ( plan.CountOf( ContentFileState::SourceUpdated ), 1u );
    EXPECT_EQ( plan.CountOf( ContentFileState::SourceDeleted ), 1u );
    EXPECT_EQ( plan.CountOf( ContentFileState::LocallyEdited ), 1u );
    EXPECT_EQ( plan.CountOf( ContentFileState::Unchanged ), 1u );
    ASSERT_TRUE( plan.CanApply() ) << "nothing here is a conflict: no file moved on both sides";

    const auto applied = ApplyContentUpdate( plan, recorded, incoming, install,
                                             [&]( const std::string& key ) { return v2.Read( key ); } );
    ASSERT_TRUE( applied.IsSuccess() ) << applied.GetError();

    // HALF ONE — a file the source deleted disappears from the installed content.
    EXPECT_FALSE( fs::exists( install / "meshes" / "gone.txt" ) );

    // HALF TWO — a file a person changed is not overwritten.
    EXPECT_EQ( ReadFile( install / "meshes" / "edited.demat" ), editedByHand );

    // And the ordinary case still works, or the two halves above could be had by doing nothing.
    EXPECT_EQ( ReadFile( install / "meshes" / "untouched.txt" ), untouchedV2 );

    const auto& report = applied.GetValue();
    EXPECT_EQ( report.Written, 1u );
    EXPECT_EQ( report.Removed, 1u );
    EXPECT_EQ( report.KeptLocalEdit, 1u );
    EXPECT_EQ( report.Unchanged, 1u );
    EXPECT_EQ( ReadFile( install / "meshes" / "quiet.txt" ), quiet );

    // THE RECORD FOLLOWS THE BYTES ON DISK. The edited file keeps the hash of the version that was
    // last actually installed — not the one we declined to install — or the NEXT update would find
    // disk == recorded and overwrite the person's work without a word.
    const auto* keptRecord = report.Recorded.Find( "meshes/edited.demat" );
    ASSERT_NE( keptRecord, nullptr );
    EXPECT_EQ( keptRecord->Hash, PakContentHash( editedV1.data(), editedV1.size() ) );
    EXPECT_EQ( report.Recorded.Find( "meshes/gone.txt" ), nullptr );
    const auto* advanced = report.Recorded.Find( "meshes/untouched.txt" );
    ASSERT_NE( advanced, nullptr );
    EXPECT_EQ( advanced->Hash, PakContentHash( untouchedV2.data(), untouchedV2.size() ) );
}

// The same three manifests, the other authorship. A game patch is not protecting anybody's authoring,
// so the identical inputs must produce the OPPOSITE outcome for the edited file — which is the proof
// that the policy is a decision made in one place and not two implementations that happen to differ.
TEST( ContentUpdate, TheSamePlanUnderSourceOwnedOverwritesTheEdit )
{
    const fs::path dir     = MakeTempDir();
    const fs::path install = dir / "install";

    const std::string shipped = "shipped bytes";
    const std::string edited  = "someone changed this";

    ContentManifest recorded;
    recorded.Insert( Entry( "a.txt", shipped ) );
    WriteFile( install / "a.txt", edited );

    ContentManifest incoming;
    incoming.Insert( Entry( "a.txt", shipped ) ); // the source did NOT change it

    auto onDisk = ContentManifest::FromDirectory( install );
    ASSERT_TRUE( onDisk.IsSuccess() ) << onDisk.GetError();

    const auto kept =
         PlanContentUpdate( recorded, onDisk.GetValue(), incoming, ContentAuthorship::LocallyAuthored );
    ASSERT_EQ( kept.Steps.size(), 1u );
    EXPECT_EQ( kept.Steps[0].State, ContentFileState::LocallyEdited );
    EXPECT_EQ( kept.Steps[0].Action, ContentAction::None );

    const auto restored =
         PlanContentUpdate( recorded, onDisk.GetValue(), incoming, ContentAuthorship::SourceOwned );
    ASSERT_EQ( restored.Steps.size(), 1u );
    EXPECT_EQ( restored.Steps[0].State, ContentFileState::LocallyEdited ); // the observation is the same
    EXPECT_EQ( restored.Steps[0].Action, ContentAction::Write );           // the decision is not

    const auto applied = ApplyContentUpdate( restored, recorded, incoming, install,
                                             [&]( const std::string& ) { return shipped; } );
    ASSERT_TRUE( applied.IsSuccess() ) << applied.GetError();
    EXPECT_EQ( ReadFile( install / "a.txt" ), shipped );
}

// The two cells a three-state model forgets. Both are about a file being ABSENT from one of the three
// points, which is why walking any single manifest cannot find them.
TEST( ContentUpdate, DeletingAFileIsAnEditAndAddingOneIsNotTheSourcesBusiness )
{
    const fs::path dir     = MakeTempDir();
    const fs::path install = dir / "install";
    fs::create_directories( install );

    const std::string shipped = "shipped";
    const std::string theirs  = "a file the person made";

    ContentManifest recorded;
    recorded.Insert( Entry( "deleted-by-hand.txt", shipped ) );

    ContentManifest incoming;
    incoming.Insert( Entry( "deleted-by-hand.txt", shipped + " v2" ) ); // the source updated it

    WriteFile( install / "mine.txt", theirs ); // present on disk, in neither the record nor incoming

    auto onDisk = ContentManifest::FromDirectory( install );
    ASSERT_TRUE( onDisk.IsSuccess() ) << onDisk.GetError();

    const auto plan =
         PlanContentUpdate( recorded, onDisk.GetValue(), incoming, ContentAuthorship::LocallyAuthored );
    ASSERT_TRUE( plan.CanApply() );

    const auto* removedByHand = plan.Find( "deleted-by-hand.txt" );
    ASSERT_NE( removedByHand, nullptr );
    EXPECT_EQ( removedByHand->State, ContentFileState::LocallyDeleted );
    EXPECT_EQ( removedByHand->Action, ContentAction::None ) << "removing a file is a change to it";

    const auto* mine = plan.Find( "mine.txt" );
    ASSERT_NE( mine, nullptr );
    EXPECT_EQ( mine->State, ContentFileState::LocallyAdded );
    EXPECT_EQ( mine->Action, ContentAction::None );

    const auto applied = ApplyContentUpdate( plan, recorded, incoming, install,
                                             [&]( const std::string& ) { return shipped + " v2"; } );
    ASSERT_TRUE( applied.IsSuccess() ) << applied.GetError();
    EXPECT_FALSE( fs::exists( install / "deleted-by-hand.txt" ) ) << "it was not resurrected";
    EXPECT_EQ( ReadFile( install / "mine.txt" ), theirs );
    EXPECT_EQ( applied.GetValue().NotRestored, 1u );
    EXPECT_EQ( applied.GetValue().KeptLocalAdd, 1u );

    // The record keeps the deleted key at its OLD hash: that is what makes the state stay
    // "they deleted it" on the next update instead of flipping to "the source added it".
    const auto* stillRecorded = applied.GetValue().Recorded.Find( "deleted-by-hand.txt" );
    ASSERT_NE( stillRecorded, nullptr );
    EXPECT_EQ( stillRecorded->Hash, PakContentHash( shipped.data(), shipped.size() ) );
}

TEST( ContentUpdate, BothSidesMovedIsARefusalAndNothingIsApplied )
{
    const fs::path dir     = MakeTempDir();
    const fs::path install = dir / "install";

    const std::string shipped = "v1";
    const std::string edited  = "edited by a person";

    ContentManifest recorded;
    recorded.Insert( Entry( "conflict.demat", shipped ) );
    recorded.Insert( Entry( "quiet.txt", shipped ) );

    WriteFile( install / "conflict.demat", edited );
    WriteFile( install / "quiet.txt", shipped );

    ContentManifest incoming;
    incoming.Insert( Entry( "conflict.demat", "v2 from the source" ) );
    incoming.Insert( Entry( "quiet.txt", "v2 from the source" ) );

    auto onDisk = ContentManifest::FromDirectory( install );
    ASSERT_TRUE( onDisk.IsSuccess() ) << onDisk.GetError();

    const auto plan =
         PlanContentUpdate( recorded, onDisk.GetValue(), incoming, ContentAuthorship::LocallyAuthored );
    EXPECT_FALSE( plan.CanApply() );
    ASSERT_EQ( plan.Conflicts.size(), 1u );
    EXPECT_EQ( plan.Conflicts[0], "conflict.demat" );

    // ALL OR NOTHING: the file that COULD have been updated is not, because a half-applied update
    // leaves an install matching no released version and no record of how it got there.
    const auto applied = ApplyContentUpdate( plan, recorded, incoming, install, [&]( const std::string& )
                                             { return std::string( "v2 from the source" ); } );
    EXPECT_FALSE( applied.IsSuccess() );
    EXPECT_NE( applied.GetError().find( "conflict.demat" ), std::string::npos ) << applied.GetError();
    EXPECT_EQ( ReadFile( install / "quiet.txt" ), shipped );
    EXPECT_EQ( ReadFile( install / "conflict.demat" ), edited );

    // The same conflict under SourceOwned is not a conflict at all — nobody authored anything there.
    const auto overwrite =
         PlanContentUpdate( recorded, onDisk.GetValue(), incoming, ContentAuthorship::SourceOwned );
    EXPECT_TRUE( overwrite.CanApply() );
}

TEST( ContentUpdate, AnEditThatMatchesTheNewReleaseIsNotAConflict )
{
    const std::string shipped = "v1";
    const std::string next    = "v2";

    ContentManifest recorded;
    recorded.Insert( Entry( "a.txt", shipped ) );
    ContentManifest onDisk;
    onDisk.Insert( Entry( "a.txt", next ) ); // they happened to make the same change
    ContentManifest incoming;
    incoming.Insert( Entry( "a.txt", next ) );

    const auto plan = PlanContentUpdate( recorded, onDisk, incoming, ContentAuthorship::LocallyAuthored );
    ASSERT_EQ( plan.Steps.size(), 1u );
    EXPECT_EQ( plan.Steps[0].State, ContentFileState::Unchanged );
    EXPECT_EQ( plan.Steps[0].Action, ContentAction::None );
    EXPECT_TRUE( plan.CanApply() );
}

// THE MIGRATION, and the reason it is not optional: every install that exists today has no record, and
// without adoption every one of its files reads as "the person added this" and is frozen for ever —
// which is exactly the defect the record was introduced to end, preserved for precisely the installs
// that have it.
TEST( ContentUpdate, AnInstallWithNoRecordAdoptsWhatItCanProveIsTheSources )
{
    const std::string shipped = "what the source is offering";
    const std::string edited  = "and something that is not";

    ContentManifest onDisk;
    onDisk.Insert( Entry( "same.txt", shipped ) );
    onDisk.Insert( Entry( "different.txt", edited ) );
    onDisk.Insert( Entry( "theirs.txt", edited ) ); // the source has never heard of this one

    ContentManifest incoming;
    incoming.Insert( Entry( "same.txt", shipped ) );
    incoming.Insert( Entry( "different.txt", shipped ) );

    const ContentManifest adopted = AdoptUnrecordedInstall( onDisk, incoming );

    // Identical bytes cannot be somebody's edit, so that one is provably ours and is adopted.
    ASSERT_EQ( adopted.Count(), 1u );
    ASSERT_NE( adopted.Find( "same.txt" ), nullptr );
    // The other two are not: one differs (an edit, or an older release — nothing on disk can say
    // which), the other was never the source's. Guessing on either would overwrite somebody's work.
    EXPECT_EQ( adopted.Find( "different.txt" ), nullptr );
    EXPECT_EQ( adopted.Find( "theirs.txt" ), nullptr );

    // What the adoption BUYS, which is the point: the adopted file is now updatable, and the other two
    // are left exactly as conservatively as before.
    ContentManifest nextRelease;
    nextRelease.Insert( Entry( "same.txt", std::string( "a corrected material" ) ) );
    nextRelease.Insert( Entry( "different.txt", shipped ) );

    const auto plan = PlanContentUpdate( adopted, onDisk, nextRelease, ContentAuthorship::LocallyAuthored );
    ASSERT_TRUE( plan.CanApply() );
    const auto* updatable = plan.Find( "same.txt" );
    ASSERT_NE( updatable, nullptr );
    EXPECT_EQ( updatable->State, ContentFileState::SourceUpdated );
    EXPECT_EQ( updatable->Action, ContentAction::Write );

    const auto* frozen = plan.Find( "different.txt" );
    ASSERT_NE( frozen, nullptr );
    EXPECT_EQ( frozen->State, ContentFileState::LocallyAdded );
    EXPECT_EQ( frozen->Action, ContentAction::None );
}

TEST( ContentUpdate, AKeyThatLeavesTheInstallIsRefusedBeforeAnyByteMoves )
{
    const fs::path dir     = MakeTempDir();
    const fs::path install = dir / "install";
    fs::create_directories( install );

    ContentManifest recorded;
    ContentManifest onDisk;
    ContentManifest incoming;
    incoming.Insert( Entry( "../escaped.txt", "anywhere the process can reach" ) );
    incoming.Insert( Entry( "fine.txt", "ordinary" ) );

    const auto plan    = PlanContentUpdate( recorded, onDisk, incoming, ContentAuthorship::SourceOwned );
    const auto applied = ApplyContentUpdate( plan, recorded, incoming, install,
                                             [&]( const std::string& ) { return std::string( "x" ); } );
    EXPECT_FALSE( applied.IsSuccess() );
    EXPECT_FALSE( fs::exists( dir / "escaped.txt" ) );
    EXPECT_FALSE( fs::exists( install / "fine.txt" ) ) << "validated whole, then executed whole";
}

int main( int argc, char** argv )
{
    testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
