// HOW MANY GPU PROGRAMS JUDGE THE CLOUD MEDIUM, and the census that makes the number impossible to grow
// by accident.
//
// WHY THIS SUITE EXISTS, AND IT IS THE COST INPUT OF O1-C. The O1 design (Docs/Clouds/O1_DESIGN.md §4)
// names the seam an authored medium has to enter through as TWO sites: the view march's call to
// CloudSampleDensity and the light march inside it. The tree has FOUR shipped GPU programs that sample the
// cloud field, and three of them are not the view march. An authored medium that reached only the march
// would leave the other three judging the same sky by a different field — the shape each of those files
// warns about IN ITS OWN HEADER:
//
//   * Programs/Clouds/CloudShadowMap.shader — "a cirrus eroded by the fine volume for the eye and by the
//     default one for the shadow map would be two different clouds in one frame";
//   * Programs/Clouds/CloudSkyOcclusionVolume.shader — "a column integrated from a different field than
//     the one the eye marches would darken clouds that are not there";
//   * Programs/Compute/BakeProceduralSky.shader — "not by an analytic dome standing beside them, which
//     would be a SECOND model of the clouds and therefore a mirror that drifts. This project has paid for
//     that shape once already, in the grey-clouds defect".
//
// AND IT IS MEASURED, not argued. Zeroing the medium at ONE consumer's own call site and shooting
// Clouds_Protocol at 90 frames, 1280-wide, against a noise floor measured at 0 of 560 560 differing
// pixels on every point (2026-09-08, Debug/MoltenVK):
//
//   consumer                    zenith            mid              horizon
//   cloud shadow map            0.000 %           0.000 %          29.978 %, max 146/255
//   sky-light occlusion volume  89.411 %, max 32  88.273 %, max 32 99.117 %, max 29
//   sky panorama bake (IBL)     0.000 %           0.000 %          29.799 %, max  44
//
// So every one of the three is LIVE, the occlusion volume touches nine tenths of every frame, and two of
// them are invisible from the zenith and the mid angle — which is exactly how a divergence introduced here
// would pass a review that shot only upward.
//
// WHAT IS ASSERTED. The set of programs that sample the field, BY NAME, and that each of them takes its
// density from the shared CloudSampleDensity rather than deriving one. A fifth consumer is red, and it is
// red on purpose: whoever adds it has to decide what it does about an authored medium, in the same change.
// The count is quoted so a census that silently shrank is visible too.
//
// Pure: reads the shader tree as text. It IS a text census, and that is what a census of FILES has to be —
// the subject is which files exist, which no C++ symbol can answer.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>

namespace
{
    std::filesystem::path RepoRoot()
    {
        std::filesystem::path prefix = ".";
        for ( int up = 0; up < 6; ++up )
        {
            if ( std::filesystem::exists( prefix / "Desert/Desert/Source/Engine/Core/SceneSettings.hpp" ) )
                return prefix;
            prefix /= "..";
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

    // Every file under the shader tree with the given extension, as a path relative to the shader root, so
    // a message names the file the way a person would type it.
    std::set<std::string> ShaderFiles( const std::filesystem::path& root, const std::string& extension )
    {
        std::set<std::string> found;
        if ( !std::filesystem::exists( root ) )
            return found;
        for ( const auto& entry : std::filesystem::recursive_directory_iterator( root ) )
        {
            if ( entry.is_regular_file() && entry.path().extension() == extension )
                found.insert( std::filesystem::relative( entry.path(), root ).generic_string() );
        }
        return found;
    }
} // namespace

// ── THE CENSUS ─────────────────────────────────────────────────────────────────────────────────────────
TEST( CloudMediumConsumers, EveryProgramThatSamplesTheCloudFieldIsOneOfTheFourKnownOnes )
{
    const std::filesystem::path shaders  = RepoRoot() / "Editor/Resources/Shaders";
    const std::set<std::string> programs = ShaderFiles( shaders / "Programs", ".shader" );
    ASSERT_FALSE( programs.empty() ) << "the shader tree was not found, so this census counted nothing";

    std::set<std::string> consumers;
    std::set<std::string> deriveTheirOwn;

    for ( const std::string& relative : programs )
    {
        const std::string source = ReadAll( shaders / "Programs" / relative );
        if ( source.find( "SampleCloudField(" ) == std::string::npos )
            continue;

        consumers.insert( relative );

        // TAKING THE DENSITY FROM THE SHARED FUNCTION IS THE WHOLE POINT OF THE SEAM. A program that has
        // the field sample and computes a density of its own is a second model of the medium, which is the
        // defect BakeProceduralSky's own header describes having been paid for once.
        if ( source.find( "CloudSampleDensity(" ) == std::string::npos )
            deriveTheirOwn.insert( relative );
    }

    // THE FOUR, NAMED, with what each of them is for — a count alone would pass on the wrong four the day
    // one is renamed, and the names are what a reader of a red message needs.
    const std::set<std::string> expected = {
         "Clouds/CloudRaymarch.shader",           // the view march: what the camera sees
         "Clouds/CloudShadowMap.shader",          // the clouds' shadow on the ground
         "Clouds/CloudSkyOcclusionVolume.shader", // how much sky each column can see
         "Compute/BakeProceduralSky.shader",      // the panorama the scene is lit from (IBL)
    };

    EXPECT_EQ( consumers, expected )
         << "the set of GPU programs that sample the cloud field is not the four this tree knows about. A "
            "program ADDED here has to answer the question O1-C exists to answer — where does its medium "
            "come from once a material authors one — and a program REMOVED here means one of the four "
            "stopped judging the sky by the shared field, which is a change of what the frame shows.";

    EXPECT_TRUE( deriveTheirOwn.empty() )
         << deriveTheirOwn.size()
         << " program(s) sample the cloud field and do NOT call CloudSampleDensity, so they carry a second "
            "model of the medium. The first of them is '"
         << ( deriveTheirOwn.empty() ? std::string( "-" ) : *deriveTheirOwn.begin() ) << "'.";

    std::printf( "[CloudMediumConsumers] %zu of %zu shipped programs sample the cloud medium\n", consumers.size(),
                 programs.size() );
}

// ── THE SEAM IS ONE TEXT ───────────────────────────────────────────────────────────────────────────────
//
// The four consumers above reach the medium through ONE definition, and the second march inside that same
// header (the sun-transmittance quadrature) reaches it there too. Asserted because it is what makes an
// authored medium a ONE-POINT injection rather than a four-point one: the day a copy of the density chain
// appears in a second header, O1-C's cost quadruples and nothing else would say so.
TEST( CloudMediumConsumers, TheDensityChainIsDefinedInExactlyOnePlace )
{
    const std::filesystem::path shaders = RepoRoot() / "Editor/Resources/Shaders";
    const std::set<std::string> headers = ShaderFiles( shaders / "Common", ".glslh" );
    ASSERT_FALSE( headers.empty() ) << "the shared shader headers were not found";

    std::set<std::string> definers;
    for ( const std::string& relative : headers )
    {
        const std::string source = ReadAll( shaders / "Common" / relative );
        if ( source.find( "float CloudSampleDensity(" ) != std::string::npos ||
             source.find( "float CloudSampleDensity( " ) != std::string::npos )
            definers.insert( relative );
    }

    EXPECT_EQ( definers, ( std::set<std::string>{ "CloudField.glslh" } ) )
         << "the cloud density chain is defined in " << definers.size()
         << " header(s). One is the seam; two are a mirror, and the four programs above would then be "
            "judging the sky by whichever of them they happened to include.";
}

int main( int argc, char** argv )
{
    ::testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
