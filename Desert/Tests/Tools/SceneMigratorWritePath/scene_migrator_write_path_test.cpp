// THE TOOL'S FILE LOOP, driven end to end through RunSceneMigrator — the function main() calls with
// argv. The nine step suites and SceneMigratorEndToEnd prove the migrations; none of them compiles
// the loop that READS the file, WRITES it back and turns failures into the exit code, and that loop
// carried the one defect a pure-function suite can never see: the write opened the scene itself with
// trunc and checked only the OPEN, so any failure after it — full disk, dropped permissions, a killed
// process — left a zero-byte file behind a green "raised ... 0 failed" report. The write is atomic
// now (temp beside the file, verify after close, rename over), and this suite pins the claim that
// matters to an operator pointing the tool at a repository:
//
//   a write that fails costs the RUN its exit code, and costs the FILE nothing.
//
// The failing write is built by blocking the primitive's temp path with a directory — a situation in
// which writing the scene in place would still SUCCEED, so the first test is red against the old
// code (mutation-checked), not merely untested against it.

#include <MigratorMain.hpp>
#include <SceneMigration.hpp>

#include <rflcpp/rfl/json.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

using Desert::Migration::RunSceneMigrator;
using Desert::Migration::SceneSerialized;

namespace
{
    fs::path MakeTempDir( const char* name )
    {
        const fs::path dir = fs::temp_directory_path() / name;
        fs::remove_all( dir );
        fs::create_directories( dir );
        return dir;
    }

    std::string ReadRaw( const fs::path& p )
    {
        std::ifstream      in( p, std::ios::binary );
        std::ostringstream buffer;
        buffer << in.rdbuf();
        return buffer.str();
    }

    // A scene the tool has real work on: v1, so every later step runs. Serialised through the same
    // rfl::json the tool parses with, so the fixture cannot drift from the format.
    void WriteSceneAtV1( const fs::path& p )
    {
        SceneSerialized scene;
        scene.SceneName    = "WritePathFixture";
        scene.SceneVersion = 1;

        std::ofstream out( p, std::ios::binary );
        out << rfl::json::write( scene );
    }

    // One call, all three streams. Returns the exit code; the report and errors come back by
    // reference so the assertions can read them like an operator would.
    int RunTool( const std::vector<std::string>& args, std::string& report, std::string& errors )
    {
        std::ostringstream out;
        std::ostringstream err;
        const int          code = RunSceneMigrator( args, out, err );
        report                  = out.str();
        errors                  = err.str();
        return code;
    }

    // The spelling RunSceneMigrator prints for one schema step, composed from the same constants it
    // composes from — asserting a literal "v3->v4" here would be a second statement of the versions.
    std::string StepLabel( int from, int to )
    {
        return "scene v" + std::to_string( from ) + "->v" + std::to_string( to );
    }
} // namespace

// THE ERROR PATH. The temp path is blocked, so the atomic write must refuse; the run exits non-zero,
// the failure is NAMED on the error stream, and the scene on disk is byte-identical. Against the old
// in-place write this exact setup succeeds — the scene itself is writable — so the old code exits 0
// with the file rewritten, and every one of the three assertions goes red.
TEST( SceneMigratorWritePath, AFailingWriteExitsNonZeroAndLeavesTheSceneByteIdentical )
{
    const fs::path dir   = MakeTempDir( "desert_migrator_write_fail" );
    const fs::path scene = dir / "old.desce";
    WriteSceneAtV1( scene );
    const std::string before = ReadRaw( scene );

    fs::path temp = scene;
    temp += ".tmp";
    fs::create_directories( temp ); // blocks the primitive's working file

    std::string report;
    std::string errors;
    const int   code = RunTool( { scene.string() }, report, errors );

    EXPECT_EQ( code, 1 ) << report << errors;
    EXPECT_NE( errors.find( "FAIL" ), std::string::npos ) << errors;
    EXPECT_NE( report.find( "1 failed" ), std::string::npos ) << report;
    EXPECT_EQ( ReadRaw( scene ), before ) << "the failed run cost the scene its contents";

    fs::remove_all( dir );
}

// THE SUCCESS PATH, closed by the tool's own --check: the raise is written, the run exits 0, and a
// second run in check mode finds nothing to do — which is the acceptance the tool is run under
// against the whole repository ("0 would change" after a migration proves the write kept what the
// migration produced).
TEST( SceneMigratorWritePath, ARaisedSceneIsWrittenAndASecondCheckFindsNothingToChange )
{
    const fs::path dir   = MakeTempDir( "desert_migrator_write_ok" );
    const fs::path scene = dir / "old.desce";
    WriteSceneAtV1( scene );

    std::string report;
    std::string errors;
    EXPECT_EQ( RunTool( { scene.string() }, report, errors ), 0 ) << report << errors;
    EXPECT_NE( report.find( "raised " ), std::string::npos ) << report;
    EXPECT_NE( report.find( "0 failed" ), std::string::npos ) << report;

    EXPECT_EQ( RunTool( { "--check", scene.string() }, report, errors ), 0 ) << report << errors;
    EXPECT_NE( report.find( "0 would change" ), std::string::npos ) << report;

    fs::remove_all( dir );
}

// §4.7 — SILENT MIGRATION IS FORBIDDEN, and the three cloud steps were silent until 2026-09-06:
// their Raised flags fed Changed(), the file was rewritten, and the report line jumped from v3
// straight to v6 with nothing in between. A layer whose scalar type became a species, then a path,
// then the first slot of a set travelled the whole way without the operator being told. This test
// runs a v3 scene with exactly that layer through the tool and requires every step of the chain to
// be named with its versions; deleting any of the three print blocks turns it red.
TEST( SceneMigratorWritePath, TheThreeCloudStepsAreNamedInTheReportWithTheirVersions )
{
    const fs::path dir   = MakeTempDir( "desert_migrator_cloud_report" );
    const fs::path scene = dir / "clouds.desce";

    SceneSerialized fixture;
    fixture.SceneName    = "CloudReportFixture";
    fixture.SceneVersion = Desert::Migration::kSceneVersionCloudNoise; // v3: the cloud chain is all ahead
    fixture.UnitVersion  = Desert::Migration::kUnitVersion;            // keep the units axis out of this

    Desert::Assets::EntityData clouds;
    clouds.Tag = "Sky";
    rfl::Generic::Object payload;
    payload["CloudType"]                 = 0.6; // the scalar the v3->v4->v5->v6 chain carries through
    clouds.Components["VolumetricCloud"] = rfl::Generic( std::move( payload ) );
    fixture.Entities.push_back( std::move( clouds ) );

    {
        std::ofstream out( scene, std::ios::binary );
        out << rfl::json::write( fixture );
    }

    std::string report;
    std::string errors;
    EXPECT_EQ( RunTool( { "--check", scene.string() }, report, errors ), 1 ) << report << errors;

    using namespace Desert::Migration;
    for ( const auto& [from, to] : { std::pair{ kSceneVersionCloudNoise, kSceneVersionCloudSpecies },
                                     std::pair{ kSceneVersionCloudSpecies, kSceneVersionCloudType },
                                     std::pair{ kSceneVersionCloudType, kSceneVersionCloudSet } } )
        EXPECT_NE( report.find( StepLabel( from, to ) ), std::string::npos )
             << "step " << StepLabel( from, to ) << " ran silently; report was:\n"
             << report;

    fs::remove_all( dir );
}

int main( int argc, char** argv )
{
    ::testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
