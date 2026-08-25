// The cloud TYPE: its file format, and the SHIPPED LIBRARY that format carries.
//
// Two halves, and the second is the one that matters most. The first is ordinary format work — a
// round trip, and a refusal for every number the generator cannot honour. The second opens the nine
// `.decloudtype` files this task ships and asserts things about their CONTENTS: that the altitudes are
// meteorology rather than taste, that the four T0 inherited are unchanged to the digit, and that the
// built-in default an empty slot resolves to is the same row as the file that claims to be it.
//
// THAT LAST ONE IS THE POINT OF THE SUITE. "A preset table against the saved scenes" is the eighth row of
// the table in DEV_CONTRACT.md §2.3.1 — two places obliged to agree, each correct on its own, and the
// symptom of their disagreeing is a sky that is subtly not the one anybody authored. The built-in row
// lives in C++ because a scene with no type in its slot still has to render; the file lives on disk
// because an artist has to be able to open it. There is no way to make them one thing, so they are made
// one number at a time, here.
//
// The library is read from DISK rather than embedded, deliberately: an embedded copy would be a third
// statement of the same numbers and would pass while the shipped files were broken.

#include "CloudScheduleReference.hpp"

#include <Engine/Assets/CloudProceduralVolume.hpp>
#include <Engine/Graphic/Clouds/CloudPayload.hpp>

#include <Engine/Assets/CloudNoiseVolume.hpp>
#include <Engine/Assets/CloudTypeData.hpp>
#include <Engine/Graphic/Clouds/CloudTypeShape.hpp>

// The LAYER's Detail Strength, for the one relation that is between the library and the layer: the cut's
// depth is their product and it is clamped. PROPERTY expands to nothing, so this costs no reflection.
#include <Engine/ECS/VolumetricCloudComponent.hpp>

#include <Common/Core/Constants.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

using namespace Desert::Assets;
using Desert::Graphic::CloudTypeBaseKm;
using Desert::Graphic::CloudTypeShape;
using Desert::Graphic::CloudTypeTopKm;

namespace
{
    // A shape every field of which is legal, so a test that breaks ONE field is testing that field.
    CloudTypeShape LegalShape()
    {
        return CloudTypeShape{
             /* BaseAltitudeKm   */ 1.00f,
             /* TopAltitudeKm    */ 3.00f,
             /* EdgeTopFraction  */ 0.40f,
             /* BaseRampFraction */ 0.10f,
             /* TopTaper         */ 0.40f,
             /* AnvilAltitudeKm  */ 0.00f,
             /* AnvilThicknessKm */ 0.00f,
             /* AnvilStrength    */ 0.00f,
             /* DetailCharacter  */ 0.60f,
             /* DetailFactor     */ 1.00f,
             /* DensityFactor    */ 1.00f,
             /* ExtinctionFactor */ 1.00f,
             /* PlacementScale      */ 1.00f,
             /* PlacementAnisotropy */ 1.00f,
        };
    }

    CloudTypeData LegalData()
    {
        CloudTypeData data;
        data.FormatVersion = kCloudTypeFormatVersion;
        data.DisplayName   = "Test type";
        data.Notes         = "Written by the round-trip test.";
        data.Shape         = LegalShape();
        return data;
    }

    // WHERE THE SHIPPED LIBRARY IS, found by walking up from wherever the test binary was started. The
    // repository runs its suites from the workspace root (scripts/MacOS/RunTests.sh), but a developer
    // running one binary by hand from build/Bin/Tests/Debug is the normal case and a suite that fails for
    // them is a suite they will stop running.
    std::filesystem::path LibraryDirectory()
    {
        std::filesystem::path here = std::filesystem::current_path();
        for ( int up = 0; up < 6; ++up )
        {
            const std::filesystem::path candidate = here / "Editor" / "Resources" / "Assets" / "Clouds" / "Types";
            std::error_code             ec;
            if ( std::filesystem::is_directory( candidate, ec ) )
                return candidate;
            if ( !here.has_parent_path() )
                break;
            here = here.parent_path();
        }
        return {};
    }

    // Opens one shipped preset by name, or FAILS. Not skipped: a library that is not there is exactly the
    // failure this suite exists to catch — the migration writes these names into every scene it raises.
    CloudTypeData LoadShipped( const char* name )
    {
        const std::filesystem::path dir = LibraryDirectory();
        EXPECT_FALSE( dir.empty() ) << "Editor/Resources/Assets/Clouds/Types was not found from "
                                    << std::filesystem::current_path()
                                    << " or any of its six parents — the shipped cloud type library is "
                                       "missing, and every scene raised by the v4 -> v5 migration names it";

        const std::filesystem::path path = dir / ( std::string( name ) + kCloudTypeExtension );

        std::ifstream file( path );
        EXPECT_TRUE( file.good() ) << "shipped cloud type '" << path.string() << "' could not be opened";

        std::stringstream buffer;
        buffer << file.rdbuf();

        auto parsed = ParseCloudType( buffer.str() );
        EXPECT_TRUE( parsed ) << "shipped cloud type '" << path.string()
                              << "' does not parse: " << ( parsed ? std::string{} : parsed.GetError() );
        return parsed ? parsed.ExtractValue() : CloudTypeData{};
    }

    // ------------------------------------------------------------------------------------------------
    // WHAT A TYPE ACTUALLY PUTS IN THE SKY, measured off the PRODUCER
    // ------------------------------------------------------------------------------------------------
    //
    // These three used to evaluate Graphic::CloudProfileCurve — the parametric curve behind the profile
    // table. There is no curve and no table: the profile is the normalised distance field of a pile of
    // lumps the generator places, so a helper that still evaluated the curve would be asserting
    // meteorology about arithmetic nothing renders.
    //
    // They place the lumps this type would place, with the same function the bake calls, and read the
    // field up a vertical line through the tallest cluster. That is a STRONGER anchor than the curve was:
    // it measures where the material ends up rather than where a generator intended to put it.

    /// The bake parameters for one type on its own, at the layer's shipped lattice.
    Desert::Assets::CloudProceduralFieldParams TypeParams( const CloudTypeShape& shape )
    {
        Desert::Assets::CloudProceduralFieldParams params;

        const float latticeKm = 3.0f * std::max( shape.PlacementScale, 1e-3f );

        params.RegionSizeKm      = std::max( latticeKm * 6.0f, 16.1f );
        params.LayerBottomKm     = CloudTypeBaseKm( shape );
        params.LayerThicknessKm  = std::max( CloudTypeTopKm( shape ) - params.LayerBottomKm, 0.001f );
        params.BlendRadiusKm     = 0.02f * latticeKm;
        params.ProfileDepthKm    = 0.12f * latticeKm;
        params.Coverage          = 1.0f;
        params.CoverageContrast  = 1.0f;
        params.Seed              = 1u;
        params.WindAxis          = glm::vec2( 1.0f, 0.0f );
        params.ResolvableChordKm = Desert::Graphic::CloudFinestResolvableChordKm( 256.0f );

        Desert::Assets::CloudProceduralSpecies species;
        species.Shape      = shape;
        species.CellKm     = latticeKm;
        species.Anisotropy = std::max( shape.PlacementAnisotropy, 1e-3f );
        params.Species.push_back( species );

        return params;
    }

    /// The tallest cluster this type places in one region, and the lumps of that region.
    struct TypeColumn
    {
        Desert::Assets::CloudProceduralFieldParams      Params;
        std::vector<Desert::Assets::CloudModellingBlob> Blobs;
        glm::vec3                                       TallestKm{ 0.0f };
    };

    const TypeColumn& ColumnOf( const CloudTypeShape& shape )
    {
        // Memoized on the shape's bytes: nine types, three helpers each, and a bake of the lump list is
        // the expensive part. It changes no answer — the generator is a pure function.
        static std::map<std::string, TypeColumn> cache;

        const std::string key( reinterpret_cast<const char*>( &shape ), sizeof( CloudTypeShape ) );

        const auto it = cache.find( key );
        if ( it != cache.end() )
            return it->second;

        TypeColumn column;
        column.Params = TypeParams( shape );

        const glm::vec2 origin = Desert::Assets::CloudProceduralRegionOriginKm( column.Params, 0.0f, 0.0f );

        column.Blobs = Desert::Assets::GenerateCloudProceduralBlobs( column.Params, 0u, origin );

        if ( !column.Blobs.empty() )
        {
            column.TallestKm = column.Blobs.front().CentreKm;
            for ( const Desert::Assets::CloudModellingBlob& blob : column.Blobs )
            {
                if ( blob.CentreKm.y > column.TallestKm.y )
                    column.TallestKm = blob.CentreKm;
            }
        }

        return cache.emplace( key, std::move( column ) ).first->second;
    }

    /// The profile up the vertical line through that cluster, sampled at @p altitudeKm.
    float ProfileAt( const CloudTypeShape& shape, float altitudeKm )
    {
        const TypeColumn& column = ColumnOf( shape );
        if ( column.Blobs.empty() )
            return 0.0f;

        return Desert::Assets::EvaluateCloudProceduralProfile(
             column.Params, column.Blobs, glm::vec3( column.TallestKm.x, altitudeKm, column.TallestKm.z ) );
    }

    float ProfileBaseKm( const CloudTypeShape& shape )
    {
        const float top = CloudTypeTopKm( shape );
        for ( int i = 0; i <= 2000; ++i )
        {
            const float altitudeKm = top * static_cast<float>( i ) / 2000.0f;
            if ( ProfileAt( shape, altitudeKm ) > 0.0f )
                return altitudeKm;
        }
        return top;
    }

    float ProfileTopKm( const CloudTypeShape& shape )
    {
        const float top = CloudTypeTopKm( shape );
        for ( int i = 2000; i >= 0; --i )
        {
            const float altitudeKm = top * static_cast<float>( i ) / 2000.0f;
            if ( ProfileAt( shape, altitudeKm ) > 0.0f )
                return altitudeKm;
        }
        return 0.0f;
    }

    // How much matter a column of this type holds at the core of a patch: the profile integrated over
    // altitude, weighted by how opaque the type's own matter is. Two types with the same integral are two
    // types that will not be told apart in a frame, whatever their names.
    float ColumnOpacity( const CloudTypeShape& shape )
    {
        const float baseKm = CloudTypeBaseKm( shape );
        const float topKm  = CloudTypeTopKm( shape );
        const float step   = ( topKm - baseKm ) / 1000.0f;

        float total = 0.0f;
        for ( int i = 0; i < 1000; ++i )
            total += ProfileAt( shape, baseKm + ( static_cast<float>( i ) + 0.5f ) * step ) * step;

        return total * shape.DensityFactor * shape.ExtinctionFactor;
    }
} // namespace

// ---------------------------------------------------------------------------------------------------
// The format.
// ---------------------------------------------------------------------------------------------------

TEST( CloudTypeFormat, ATypeSurvivesBeingWrittenAndReadBack )
{
    const CloudTypeData original = LegalData();

    auto parsed = ParseCloudType( WriteCloudType( original ) );
    ASSERT_TRUE( parsed ) << parsed.GetError();

    const CloudTypeData round = parsed.ExtractValue();

    EXPECT_EQ( round.DisplayName, original.DisplayName );
    EXPECT_EQ( round.Notes, original.Notes );
    EXPECT_EQ( round.NoiseVolume, original.NoiseVolume );
    EXPECT_EQ( round.FormatVersion.value_or( 0 ), kCloudTypeFormatVersion );

    // Every one of the twelve, named individually rather than compared as bytes: a field that stopped
    // being written would otherwise be reported as "the structs differ" and leave the reader to find which.
    EXPECT_FLOAT_EQ( round.Shape.BaseAltitudeKm, original.Shape.BaseAltitudeKm );
    EXPECT_FLOAT_EQ( round.Shape.TopAltitudeKm, original.Shape.TopAltitudeKm );
    EXPECT_FLOAT_EQ( round.Shape.EdgeTopFraction, original.Shape.EdgeTopFraction );
    EXPECT_FLOAT_EQ( round.Shape.BaseRampFraction, original.Shape.BaseRampFraction );
    EXPECT_FLOAT_EQ( round.Shape.TopTaper, original.Shape.TopTaper );
    EXPECT_FLOAT_EQ( round.Shape.AnvilAltitudeKm, original.Shape.AnvilAltitudeKm );
    EXPECT_FLOAT_EQ( round.Shape.AnvilThicknessKm, original.Shape.AnvilThicknessKm );
    EXPECT_FLOAT_EQ( round.Shape.AnvilStrength, original.Shape.AnvilStrength );
    EXPECT_FLOAT_EQ( round.Shape.DetailCharacter, original.Shape.DetailCharacter );
    EXPECT_FLOAT_EQ( round.Shape.DetailFactor, original.Shape.DetailFactor );
    EXPECT_FLOAT_EQ( round.Shape.DensityFactor, original.Shape.DensityFactor );
    EXPECT_FLOAT_EQ( round.Shape.ExtinctionFactor, original.Shape.ExtinctionFactor );
}

TEST( CloudTypeFormat, TheOptionalFieldsAreOptionalAndTheShapeIsNot )
{
    // A file an artist wrote by hand, with nothing in it but the numbers that have no answer.
    const std::string minimal = R"({"Shape":{
        "BaseAltitudeKm":1.0,"TopAltitudeKm":3.0,"EdgeTopFraction":0.4,"BaseRampFraction":0.1,
        "TopTaper":0.4,"AnvilAltitudeKm":0.0,"AnvilThicknessKm":0.0,"AnvilStrength":0.0,
        "DetailCharacter":0.6,"DetailFactor":1.0,"DensityFactor":1.0,"ExtinctionFactor":1.0,
        "PlacementScale":1.0,"PlacementAnisotropy":1.0}})";

    auto parsed = ParseCloudType( minimal );
    ASSERT_TRUE( parsed ) << parsed.GetError();
    EXPECT_FALSE( parsed.GetValue().DisplayName.has_value() );
    EXPECT_FALSE( parsed.GetValue().NoiseVolume.has_value() );

    // And the other way round: a shape with a field MISSING is refused rather than defaulted, because a
    // number nobody wrote is not a number anybody chose.
    const std::string incomplete = R"({"Shape":{
        "BaseAltitudeKm":1.0,"TopAltitudeKm":3.0,"EdgeTopFraction":0.4,"BaseRampFraction":0.1,
        "TopTaper":0.4,"AnvilAltitudeKm":0.0,"AnvilThicknessKm":0.0,"AnvilStrength":0.0,
        "DetailCharacter":0.6,"DetailFactor":1.0,"DensityFactor":1.0,
        "PlacementScale":1.0,"PlacementAnisotropy":1.0}})";
    EXPECT_FALSE( ParseCloudType( incomplete ) ) << "a shape missing ExtinctionFactor was accepted";

    // A VERSION-1 FILE IS REFUSED, NOT FILLED IN. It carries the twelve numbers T1 shipped and neither of
    // the two T3 added, and a fibrous cirrus and a round-patched one are different KINDS of cloud — so
    // guessing the missing pair would render a sky the file does not describe while claiming it does. The
    // nine shipped files were rewritten in the same commit, which is what makes the loud refusal
    // affordable (§4.5).
    const std::string versionOne = R"({"FormatVersion":1,"Shape":{
        "BaseAltitudeKm":1.0,"TopAltitudeKm":3.0,"EdgeTopFraction":0.4,"BaseRampFraction":0.1,
        "TopTaper":0.4,"AnvilAltitudeKm":0.0,"AnvilThicknessKm":0.0,"AnvilStrength":0.0,
        "DetailCharacter":0.6,"DetailFactor":1.0,"DensityFactor":1.0,"ExtinctionFactor":1.0}})";

    const auto refused = ParseCloudType( versionOne );
    ASSERT_FALSE( refused ) << "a version-1 file was read as if it were a version-2 one";
    EXPECT_NE( refused.GetError().find( "version" ), std::string::npos )
         << "the refusal does not say the format version is the problem: " << refused.GetError();
}

TEST( CloudTypeFormat, EveryRefusalNamesTheNumberThatIsWrong )
{
    EXPECT_FALSE( ParseCloudType( "" ) );
    EXPECT_FALSE( ParseCloudType( "not json at all" ) );
    EXPECT_FALSE( ParseCloudType( R"({"FormatVersion":9,"Shape":{}})" ) );

    // Each of these is one field away from legal, and the message has to say which field. Checked by
    // SUBSTRING rather than by "it failed", because a refusal that does not name the number is the silent
    // fallback §1.4 forbids wearing an error's clothes.
    struct Case
    {
        const char* Field;
        CloudTypeShape ( *Break )( CloudTypeShape );
    };

    const Case cases[] = {
         { "TopAltitudeKm",
           []( CloudTypeShape s )
           {
               s.TopAltitudeKm = s.BaseAltitudeKm;
               return s;
           } },
         { "EdgeTopFraction",
           []( CloudTypeShape s )
           {
               s.EdgeTopFraction = 1.5f;
               return s;
           } },
         { "BaseRampFraction",
           []( CloudTypeShape s )
           {
               s.BaseRampFraction = 0.0f;
               return s;
           } },
         { "TopTaper",
           []( CloudTypeShape s )
           {
               s.TopTaper = -0.1f;
               return s;
           } },
         { "AnvilThicknessKm",
           []( CloudTypeShape s )
           {
               s.AnvilStrength    = 0.8f;
               s.AnvilThicknessKm = 0.0f;
               return s;
           } },
         { "DetailCharacter",
           []( CloudTypeShape s )
           {
               s.DetailCharacter = 2.0f;
               return s;
           } },
         { "DensityFactor",
           []( CloudTypeShape s )
           {
               s.DensityFactor = -1.0f;
               return s;
           } },
         { "ExtinctionFactor",
           []( CloudTypeShape s )
           {
               s.ExtinctionFactor = 99.0f;
               return s;
           } },
         { "BaseAltitudeKm",
           []( CloudTypeShape s )
           {
               s.BaseAltitudeKm = std::nanf( "" );
               return s;
           } },
         // T3'S TWO. The scale is a DIVISOR in the placement basis, so a zero there is an infinity in a
         // texture coordinate and a sky that is banded black with nothing in any log - which is exactly
         // the class of failure this table exists to refuse at the door.
         { "PlacementScale",
           []( CloudTypeShape s )
           {
               s.PlacementScale = 0.0f;
               return s;
           } },
         { "PlacementAnisotropy",
           []( CloudTypeShape s )
           {
               s.PlacementAnisotropy = 40.0f;
               return s;
           } },
    };

    for ( const Case& c : cases )
    {
        const auto broken = ValidateCloudTypeShape( c.Break( LegalShape() ) );
        ASSERT_FALSE( broken ) << c.Field << " was accepted";
        EXPECT_NE( broken.GetError().find( c.Field ), std::string::npos )
             << "the refusal does not name the field that is wrong: " << broken.GetError();
    }

    // And the legal one is legal, so the eight above are testing the fields and not the function.
    EXPECT_TRUE( ValidateCloudTypeShape( LegalShape() ) );
}

TEST( CloudTypeFormat, TheRelativeDirectoryAgreesWithTheProjectPath )
{
    // TWO STATEMENTS OF ONE DIRECTORY. The migration composes its paths from the relative constant,
    // because it is pure and cannot read a project root; the preloader scans the absolute one. If they
    // ever part company, every scene raised by the migration names a file the scan does not load, and the
    // symptom is a sky that quietly reverts to the built-in default.
    const std::string absolute = Common::Constants::Path::CLOUD_TYPE_PATH.generic_string();
    const std::string relative = kCloudTypeAssetsRelativeDir;

    ASSERT_GE( absolute.size(), relative.size() );
    EXPECT_EQ( absolute.compare( absolute.size() - relative.size(), relative.size(), relative ), 0 )
         << "'" << absolute << "' does not end with '" << relative << "'";

    EXPECT_EQ( CloudTypeAssetRelativePath( kCloudTypeCumulusCongestus ),
               "Clouds/Types/Cumulus_Congestus.decloudtype" );
}

// ---------------------------------------------------------------------------------------------------
// The shipped library — content, read off the disk.
// ---------------------------------------------------------------------------------------------------

TEST( CloudTypeLibrary, TheBuiltInDefaultIsTheShippedCumulusCongestus )
{
    // THE RELATION THIS SUITE EXISTS FOR. An empty slot renders CloudTypeDefaultShape; a scene raised by
    // the v4 -> v5 migration renders Cumulus_Congestus.decloudtype; both are described everywhere as "the
    // default cumulus congestus". Nothing but this test makes that true.
    const CloudTypeShape& builtIn = CloudTypeDefaultShape();
    const CloudTypeShape  shipped = LoadShipped( kCloudTypeCumulusCongestus ).Shape;

    EXPECT_FLOAT_EQ( shipped.BaseAltitudeKm, builtIn.BaseAltitudeKm );
    EXPECT_FLOAT_EQ( shipped.TopAltitudeKm, builtIn.TopAltitudeKm );
    EXPECT_FLOAT_EQ( shipped.EdgeTopFraction, builtIn.EdgeTopFraction );
    EXPECT_FLOAT_EQ( shipped.BaseRampFraction, builtIn.BaseRampFraction );
    EXPECT_FLOAT_EQ( shipped.TopTaper, builtIn.TopTaper );
    EXPECT_FLOAT_EQ( shipped.AnvilAltitudeKm, builtIn.AnvilAltitudeKm );
    EXPECT_FLOAT_EQ( shipped.AnvilThicknessKm, builtIn.AnvilThicknessKm );
    EXPECT_FLOAT_EQ( shipped.AnvilStrength, builtIn.AnvilStrength );
    EXPECT_FLOAT_EQ( shipped.DetailCharacter, builtIn.DetailCharacter );
    EXPECT_FLOAT_EQ( shipped.DetailFactor, builtIn.DetailFactor );
    EXPECT_FLOAT_EQ( shipped.DensityFactor, builtIn.DensityFactor );
    EXPECT_FLOAT_EQ( shipped.ExtinctionFactor, builtIn.ExtinctionFactor );
    EXPECT_FLOAT_EQ( shipped.PlacementScale, builtIn.PlacementScale );
    EXPECT_FLOAT_EQ( shipped.PlacementAnisotropy, builtIn.PlacementAnisotropy );
}

TEST( CloudTypeLibrary, TheFourTypesT0ShippedAreUnchanged )
{
    // The v4 -> v5 migration is a RENAME and not a reinterpretation, and this is what makes that claim
    // true: a scene that said "Species 3" before this task renders the same cumulonimbus after it. The
    // numbers are T0's own table (commit 68fcc34e), copied here as literals on purpose — comparing the
    // library against itself would assert nothing.
    struct Expected
    {
        const char* Name;
        float       BaseKm;
        float       TopKm;
        float       Edge;
        float       Ramp;
        float       Taper;
        float       AnvilKm;
        float       AnvilThickness;
        float       AnvilStrength;
        float       Detail;
        float       Density;
    };

    const Expected rows[] = {
         { kCloudTypeStratus, 0.15f, 0.55f, 0.88f, 0.12f, 0.35f, 0.0f, 0.0f, 0.0f, 0.05f, 0.70f },
         { kCloudTypeCumulusMediocris, 0.90f, 1.90f, 0.45f, 0.06f, 0.45f, 0.0f, 0.0f, 0.0f, 0.70f, 1.00f },
         { kCloudTypeCumulusCongestus, 2.20f, 5.80f, 0.15f, 0.04f, 0.50f, 0.0f, 0.0f, 0.0f, 1.00f, 1.15f },
         { kCloudTypeCumulonimbus, 0.90f, 9.00f, 0.12f, 0.04f, 0.40f, 9.5f, 1.8f, 0.85f, 0.85f, 1.35f },
    };

    for ( const Expected& row : rows )
    {
        const CloudTypeShape shape = LoadShipped( row.Name ).Shape;

        EXPECT_FLOAT_EQ( shape.BaseAltitudeKm, row.BaseKm ) << row.Name;
        EXPECT_FLOAT_EQ( shape.TopAltitudeKm, row.TopKm ) << row.Name;
        EXPECT_FLOAT_EQ( shape.EdgeTopFraction, row.Edge ) << row.Name;
        EXPECT_FLOAT_EQ( shape.BaseRampFraction, row.Ramp ) << row.Name;
        EXPECT_FLOAT_EQ( shape.TopTaper, row.Taper ) << row.Name;
        EXPECT_FLOAT_EQ( shape.AnvilAltitudeKm, row.AnvilKm ) << row.Name;
        EXPECT_FLOAT_EQ( shape.AnvilThicknessKm, row.AnvilThickness ) << row.Name;
        EXPECT_FLOAT_EQ( shape.AnvilStrength, row.AnvilStrength ) << row.Name;
        EXPECT_FLOAT_EQ( shape.DetailCharacter, row.Detail ) << row.Name;
        EXPECT_FLOAT_EQ( shape.DensityFactor, row.Density ) << row.Name;
    }
}

TEST( CloudTypeLibrary, EveryTypeSitsWhereMeteorologyPutsIt )
{
    // THE ABSOLUTE ANCHOR, and it is on the CONTENT now rather than on a table in a header, because the
    // content is where the numbers live. A set of ratios is satisfied at any scale; these are metres above
    // the ground, and a library that drifts out of them is a library that has stopped describing weather.
    //
    // The bands are the standard ones (WMO cloud classification, and Nubis Cubed's own p.11 catalogue).

    // Stratus: a sheet on the ground. Never above 600 m.
    EXPECT_LE( ProfileTopKm( LoadShipped( kCloudTypeStratus ).Shape ), 0.6f );
    EXPECT_GE( ProfileBaseKm( LoadShipped( kCloudTypeStratus ).Shape ), 0.0f );

    // Cumulus mediocris: base on the condensation level, top a kilometre up. 0.8 to 2.0 km.
    EXPECT_GE( ProfileBaseKm( LoadShipped( kCloudTypeCumulusMediocris ).Shape ), 0.8f );
    EXPECT_LE( ProfileTopKm( LoadShipped( kCloudTypeCumulusMediocris ).Shape ), 2.0f );

    // Cumulus humilis: the same base, and it STOPS. Flatter than mediocris is the whole species.
    EXPECT_GE( ProfileBaseKm( LoadShipped( kCloudTypeCumulusHumilis ).Shape ), 0.8f );
    EXPECT_LE( ProfileTopKm( LoadShipped( kCloudTypeCumulusHumilis ).Shape ), 1.5f );

    // Cumulonimbus: base in the 0.5-1.5 km band, top past eight kilometres.
    EXPECT_GE( ProfileBaseKm( LoadShipped( kCloudTypeCumulonimbus ).Shape ), 0.5f );
    EXPECT_LE( ProfileBaseKm( LoadShipped( kCloudTypeCumulonimbus ).Shape ), 1.5f );
    EXPECT_GT( ProfileTopKm( LoadShipped( kCloudTypeCumulonimbus ).Shape ), 8.0f );

    // Stratocumulus: a LOW deck. Base below a kilometre, top below two and a half.
    EXPECT_LE( ProfileBaseKm( LoadShipped( kCloudTypeStratocumulus ).Shape ), 1.0f );
    EXPECT_LE( ProfileTopKm( LoadShipped( kCloudTypeStratocumulus ).Shape ), 2.5f );

    // Altocumulus: the MID level, 2 to 7 km, and thin with it.
    const CloudTypeShape alto = LoadShipped( kCloudTypeAltocumulus ).Shape;
    EXPECT_GE( ProfileBaseKm( alto ), 2.0f );
    EXPECT_LE( ProfileTopKm( alto ), 7.0f );
    EXPECT_LE( ProfileTopKm( alto ) - ProfileBaseKm( alto ), 1.5f ) << "an altocumulus a kilometre and a "
                                                                       "half thick is a stratocumulus that "
                                                                       "has moved house";

    // Cirrus: ICE, and ice does not form below six kilometres.
    EXPECT_GE( ProfileBaseKm( LoadShipped( kCloudTypeCirrus ).Shape ), 6.0f );

    // Lenticular: it stands over a mountain, in the low-to-mid band.
    EXPECT_GE( ProfileBaseKm( LoadShipped( kCloudTypeLenticular ).Shape ), 1.5f );
    EXPECT_LE( ProfileTopKm( LoadShipped( kCloudTypeLenticular ).Shape ), 6.0f );
}

TEST( CloudTypeLibrary, ThePlacementFieldSaysWhatKindOfPatchEachTypeMakes )
{
    // THE THREE DEFECTS T3 WAS HANDED BY NAME, pinned to the numbers that answer them. Each of the three
    // was named as unreachable by any profile — a profile decides a silhouette in ELEVATION, and all three
    // are about the shape of a patch in PLAN.
    //
    // 1. CIRRUS READS AS A MACKEREL SKY RATHER THAN AS FIBROUS BANDS. A band is a patch far longer along
    //    the wind than across it, and until a type carried its own anisotropy the placement field was one
    //    field, isotropic, shared by everything in the sky.
    const CloudTypeShape cirrus = LoadShipped( kCloudTypeCirrus ).Shape;
    EXPECT_GE( cirrus.PlacementAnisotropy, 4.0f )
         << "cirrus is not stretched along the wind, so it is a field of thin blobs rather than fibres";

    // 2. LENTICULAR AND ALTOCUMULUS DIFFER QUANTITATIVELY RATHER THAN IN KIND. They now differ in the SIGN
    //    of the stretch: a wave cloud's crest lies ACROSS the flow (anisotropy below 1), a mackerel sky's
    //    rows lie ALONG it (above 1). That is a difference of kind and not of degree, and no setting of
    //    the two profiles could have produced it.
    const CloudTypeShape lenticular = LoadShipped( kCloudTypeLenticular ).Shape;
    const CloudTypeShape alto       = LoadShipped( kCloudTypeAltocumulus ).Shape;

    EXPECT_LT( lenticular.PlacementAnisotropy, 1.0f )
         << "the lenticular is not stretched across the wind, so it is not a wave cloud";
    EXPECT_GT( alto.PlacementAnisotropy, 1.0f ) << "the altocumulus is not rowed along the wind";

    // 3. STRATOCUMULUS IN THE ZENITH: ONE CELL FILLING THE FRAME. The layer's shipped Weather Tile Size is
    //    12 km and its coarse cell is a quarter of that — 3 km — under a deck 1 km thick. A cell three
    //    times the layer's own depth is one lump from horizon to horizon looking up. The type's own scale
    //    is what fixes it, and the number below is the one that puts the cell near the deck's thickness.
    const CloudTypeShape strato = LoadShipped( kCloudTypeStratocumulus ).Shape;

    constexpr float kLayerTileKm = 12.0f;
    constexpr float kCellOfTile  = 0.25f;

    const float cellKm  = kLayerTileKm * kCellOfTile * strato.PlacementScale;
    const float depthKm = ProfileTopKm( strato ) - ProfileBaseKm( strato );

    EXPECT_LT( cellKm, 2.0f * depthKm )
         << "a stratocumulus cell of " << cellKm << " km under a deck " << depthKm
         << " km thick is one lump filling the zenith, which is the defect this number answers";

    // AND THE LIBRARY DOES NOT COLLAPSE ONTO ONE PLACEMENT. Nine types that all placed themselves at the
    // layer's tile would be the T3 version of "nine labels on one cloud": the profiles would differ and
    // every patch would be the same size and shape.
    const char* names[] = { kCloudTypeStratus,          kCloudTypeCumulusHumilis, kCloudTypeCumulusMediocris,
                            kCloudTypeCumulusCongestus, kCloudTypeCumulonimbus,   kCloudTypeStratocumulus,
                            kCloudTypeAltocumulus,      kCloudTypeCirrus,         kCloudTypeLenticular };

    float smallest = 1e9f;
    float largest  = 0.0f;
    for ( const char* name : names )
    {
        const CloudTypeShape shape = LoadShipped( name ).Shape;
        smallest                   = std::min( smallest, shape.PlacementScale );
        largest                    = std::max( largest, shape.PlacementScale );
    }

    std::printf( "[CloudTypeLibrary] shipped placement scales span %.2f to %.2f of the layer's tile\n", smallest,
                 largest );

    EXPECT_GT( largest / smallest, 4.0f )
         << "every shipped type places itself at nearly the same scale, so the per-type placement field is "
            "authored but says nothing";
}

TEST( CloudTypeLibrary, NoTwoTypesAreTheSameCloudUnderTwoNames )
{
    // THE PLANK IS DISTINGUISHABILITY, NOT COUNT. A type that renders as its neighbour is a signature, not
    // a kind of cloud, and shipping nine of those would be the "nine labels on one cloud" failure
    // PLAN_CLOUD_TYPES.md §1 warns about — in the exact place it warned it would appear.
    //
    // Two types are held to be distinguishable when they differ by a fifth in the ALTITUDE BAND they
    // occupy, or by a third in how much opaque matter a column of them holds. Those are the two things a
    // camera on the ground can see: where the cloud is, and how solidly it blocks the sky.
    const char* names[] = {
         kCloudTypeStratus,          kCloudTypeCumulusHumilis, kCloudTypeCumulusMediocris,
         kCloudTypeCumulusCongestus, kCloudTypeCumulonimbus,   kCloudTypeStratocumulus,
         kCloudTypeAltocumulus,      kCloudTypeCirrus,         kCloudTypeLenticular,
    };

    std::vector<CloudTypeShape> shapes;
    for ( const char* name : names )
        shapes.push_back( LoadShipped( name ).Shape );

    for ( size_t a = 0; a < shapes.size(); ++a )
    {
        for ( size_t b = a + 1; b < shapes.size(); ++b )
        {
            const float baseGap = std::fabs( CloudTypeBaseKm( shapes[a] ) - CloudTypeBaseKm( shapes[b] ) );
            const float topGap  = std::fabs( CloudTypeTopKm( shapes[a] ) - CloudTypeTopKm( shapes[b] ) );
            const float span    = std::max( CloudTypeTopKm( shapes[a] ) - CloudTypeBaseKm( shapes[a] ),
                                            CloudTypeTopKm( shapes[b] ) - CloudTypeBaseKm( shapes[b] ) );

            const float opacityA = ColumnOpacity( shapes[a] );
            const float opacityB = ColumnOpacity( shapes[b] );
            const float opacityRatio =
                 std::max( opacityA, opacityB ) / std::max( std::min( opacityA, opacityB ), 1e-6f );

            const bool differentPlace  = ( baseGap + topGap ) > 0.2f * span;
            const bool differentMatter = opacityRatio > 1.33f;

            EXPECT_TRUE( differentPlace || differentMatter )
                 << names[a] << " and " << names[b] << " occupy the same band (bases "
                 << CloudTypeBaseKm( shapes[a] ) << " / " << CloudTypeBaseKm( shapes[b] ) << " km, tops "
                 << CloudTypeTopKm( shapes[a] ) << " / " << CloudTypeTopKm( shapes[b] )
                 << " km) and hold the same matter (" << opacityA << " / " << opacityB
                 << ") — one of them is a signature rather than a kind of cloud";
        }
    }
}

TEST( CloudTypeLibrary, OnlyTheTypesThatNeedTheirOwnNoiseNameOne )
{
    // A type's noise reference is what makes "the edge belongs to the kind of cloud" true rather than
    // stated. Cirrus is the one that needs it — ice at eight kilometres is wisps, not lobes — and it names
    // the FINER of the two shipped volumes. Every other type leaves the field absent, which is the
    // documented "use the built-in default", and a library where all nine named a volume would be a
    // library that had turned a meaningful choice into boilerplate.
    const CloudTypeData cirrus = LoadShipped( kCloudTypeCirrus );
    ASSERT_TRUE( cirrus.NoiseVolume.has_value() ) << "the one type whose edge is its identity names no volume";
    EXPECT_EQ( cirrus.NoiseVolume.value(), "Clouds/CloudNoise_FineWisp.dcnv" );

    for ( const char* name :
          { kCloudTypeStratus, kCloudTypeCumulusMediocris, kCloudTypeCumulusCongestus, kCloudTypeCumulonimbus,
            kCloudTypeStratocumulus, kCloudTypeAltocumulus, kCloudTypeCumulusHumilis, kCloudTypeLenticular } )
    {
        EXPECT_FALSE( LoadShipped( name ).NoiseVolume.has_value() )
             << name << " names a noise volume; only the type whose edge is its identity should";
    }
}

TEST( CloudTypeLibrary, EveryShippedTypeIsOneTheLoaderWouldAccept )
{
    // The shipped library and the loader's own rules, held against each other. A preset that Validate
    // rejects would be a file the engine refuses to open — shipped, in the repository, and only ever
    // discovered by whoever selected it in the dropdown.
    for ( const char* name : { kCloudTypeStratus, kCloudTypeCumulusHumilis, kCloudTypeCumulusMediocris,
                               kCloudTypeCumulusCongestus, kCloudTypeCumulonimbus, kCloudTypeStratocumulus,
                               kCloudTypeAltocumulus, kCloudTypeCirrus, kCloudTypeLenticular } )
    {
        const CloudTypeData data = LoadShipped( name );
        EXPECT_TRUE( ValidateCloudTypeShape( data.Shape ) ) << name;
        EXPECT_TRUE( data.DisplayName.has_value() ) << name << " has no display name, so a slot would show "
                                                    << "its file stem instead of what it is";
        EXPECT_TRUE( data.Notes.has_value() ) << name << " carries no note saying what weather it is for";
    }
}

// ---------------------------------------------------------------------------------------------------
// THE LIBRARY AGAINST THE MARCH: no shipped type may place structure the march cannot find
// ---------------------------------------------------------------------------------------------------

namespace
{
    // The component's Max Steps default. It is a QUALITY TIER — an artist who lowers it is buying speed
    // with resolution on purpose — so the library is calibrated against the default rather than against
    // whatever a scene happens to carry.
    constexpr float kComponentMaxSteps = 256.0f;
} // namespace

// ---------------------------------------------------------------------------------------------------
// THE RELATION THIS PROGRAMME HAS BEEN BITTEN BY TWICE: what the library PLACES against what the march
// can FIND
// ---------------------------------------------------------------------------------------------------
//
// HOW THIS TEST CHANGED IN PHASE Э5, and it is worth stating because the shape of the answer changed and
// not only the arithmetic.
//
// It used to derive the finest CELL of a type's placement noise — three frequencies from
// CloudSpeciesPlacementBasis against the lattice of the `.dcnv` it names — multiply by a measured
// chord-per-cell of 0.5, and assert the result against the march's search step. It was a good test and it
// found five of nine types past Nyquist. It measured a chain that no longer exists: there is no placement
// noise, and a type's structure is the LUMPS the generator places.
//
// So it measures the lumps, and the relation is the same relation, held closer to the thing it is about:
// the smallest semi-axis of any lump the type places, doubled, against the chord the march resolves.
// Closer, because the old version needed a measured 0.5 to get from a cell to a chord and this needs
// nothing — a lump's diameter IS a chord through it.
//
// AND THE ANSWER IS NOW A CLAMP RATHER THAN A HOPE. The generator floors every semi-axis at half the
// chord it is handed, so this cannot fail for a shipped type unless the clamp is removed. That is the
// point: it is the line that fails the day somebody decides the clamp is unnecessary.
TEST( CloudTypeLibrary, NoShippedTypePlacesStructureThinnerThanTheMarchCanFind )
{
    using namespace Desert::Tests::CloudScheduleRef;

    const float resolvableKm = CloudFinestResolvableChordKm( kComponentMaxSteps );
    ASSERT_GT( resolvableKm, 0.0f );

    std::printf( "[CloudTypeLibrary] the march resolves chords down to %.0f m (fine step %.1f m, search "
                 "step %.1f m at Max Steps %.0f)\n",
                 resolvableKm * 1000.0f, CLOUD_DISTANCE_TO_MAX_STEPS_KM / kComponentMaxSteps * 1000.0f,
                 CLOUD_COARSE_STEP_MULTIPLIER * CLOUD_DISTANCE_TO_MAX_STEPS_KM / kComponentMaxSteps * 1000.0f,
                 kComponentMaxSteps );

    for ( const char* name : { kCloudTypeStratus, kCloudTypeCumulusHumilis, kCloudTypeCumulusMediocris,
                               kCloudTypeCumulusCongestus, kCloudTypeCumulonimbus, kCloudTypeStratocumulus,
                               kCloudTypeAltocumulus, kCloudTypeCirrus, kCloudTypeLenticular } )
    {
        const CloudTypeData data   = LoadShipped( name );
        const TypeColumn&   column = ColumnOf( data.Shape );

        ASSERT_FALSE( column.Blobs.empty() ) << name << " places nothing at full coverage";

        float smallestKm = column.Blobs.front().RadiiKm.x;
        for ( const Desert::Assets::CloudModellingBlob& blob : column.Blobs )
            smallestKm = std::min( { smallestKm, blob.RadiiKm.x, blob.RadiiKm.y, blob.RadiiKm.z } );

        const float chordKm = 2.0f * smallestKm;

        std::printf( "[CloudTypeLibrary] %-18s %5zu lumps, thinnest chord %6.0f m, %.2fx the %.0f m the "
                     "march resolves\n",
                     name, column.Blobs.size(), chordKm * 1000.0f, chordKm / resolvableKm,
                     resolvableKm * 1000.0f );

        EXPECT_GE( chordKm, resolvableKm )
             << name << " places a lump only " << chordKm * 1000.0f << " m across, thinner than the "
             << resolvableKm * 1000.0f
             << " m the march can be relied on to find, so that part of this type is sampled or not "
                "sampled by the throw of a jitter and reads as dither. The generator's own clamp is what "
                "makes this impossible — if this line fails, the clamp is gone";
    }
}

// ---------------------------------------------------------------------------------------------------
// THE SAME RELATION, READ BACKWARDS: how far Max Steps may fall before the library stops working
// ---------------------------------------------------------------------------------------------------
//
// THE ANSWER CHANGED IN PHASE Э5 AND THE TEST WITH IT. Before, a type's structure was fixed by its
// placement noise and the march's step was the only moving part, so a lower Max Steps put types past
// Nyquist — measured at five of nine when halved — and that measurement is why Graphic::CloudQualityScale
// has no Max Steps field at all.
//
// The generator is handed the march's own resolvable chord and floors every lump against it, so lowering
// Max Steps now buys COARSER CLOUDS rather than speckle. The bound is gone, and this asserts what
// replaced it: at every count a tier could plausibly march with, every shipped type still clears THAT
// count's chord.
//
// WHAT THIS MEANS FOR THE REFUSAL RECORDED IN CloudQualityScale: its carrying input has changed, so the
// refusal should be re-measured rather than trusted. That is a finding of this phase and it is stated
// here rather than acted on — a tier that lowers Max Steps is a cost-versus-quality decision with its own
// frames to shoot, and this phase did not shoot them.
TEST( CloudTypeLibrary, LoweringMaxStepsBuysCoarserCloudsRatherThanSpeckle )
{
    using namespace Desert::Tests::CloudScheduleRef;

    for ( const float maxSteps : { 256.0f, 192.0f, 128.0f, 64.0f } )
    {
        const float chordKm = CloudFinestResolvableChordKm( maxSteps );

        for ( const char* name : { kCloudTypeStratus, kCloudTypeCumulusHumilis, kCloudTypeCumulusMediocris,
                                   kCloudTypeCumulusCongestus, kCloudTypeCumulonimbus, kCloudTypeStratocumulus,
                                   kCloudTypeAltocumulus, kCloudTypeCirrus, kCloudTypeLenticular } )
        {
            const CloudTypeData data = LoadShipped( name );

            Desert::Assets::CloudProceduralFieldParams params = TypeParams( data.Shape );
            params.ResolvableChordKm                          = chordKm;

            const glm::vec2 origin = Desert::Assets::CloudProceduralRegionOriginKm( params, 0.0f, 0.0f );

            const std::vector<Desert::Assets::CloudModellingBlob> blobs =
                 Desert::Assets::GenerateCloudProceduralBlobs( params, 0u, origin );

            ASSERT_FALSE( blobs.empty() ) << name << " at Max Steps " << maxSteps << " placed nothing";

            for ( const Desert::Assets::CloudModellingBlob& blob : blobs )
            {
                const float thinnestKm = 2.0f * std::min( { blob.RadiiKm.x, blob.RadiiKm.y, blob.RadiiKm.z } );

                EXPECT_GE( thinnestKm, chordKm )
                     << name << " at Max Steps " << maxSteps << " places a lump " << thinnestKm * 1000.0f
                     << " m across against a search chord of " << chordKm * 1000.0f
                     << " m — the generator is not reading the count it is handed";
            }
        }

        std::printf( "[CloudTypeLibrary] at Max Steps %3.0f the march resolves %5.0f m and every shipped "
                     "type clears it\n",
                     maxSteps, chordKm * 1000.0f );
    }
}

// ---------------------------------------------------------------------------------------------------
// THE LIBRARY AGAINST THE LAYER'S EROSION SLIDER — task DS, 2026-08-24
// ---------------------------------------------------------------------------------------------------

TEST( CloudTypeLibrary, TheLayersDetailStrengthStillMovesEveryShippedType )
{
    // THE RELATION. The depth of the erosion's cut is `clamp(DetailStrength * DetailFactor, 0, 1)` — the
    // layer's slider times the TYPE's own multiplier, formed per sample in Common/CloudField.glslh because
    // one product formed on the CPU could only describe one kind of cloud. That clamp is not decoration:
    // the erosion is subtracted from a profile that lives in [0, 1] and a depth above 1 would collapse the
    // remap's window.
    //
    // WHAT IT COSTS WHEN IT CLOSES. A type whose product has reached 1 stops responding to the layer's
    // slider ALTOGETHER: the artist drags Detail Strength and that type does not change. This programme
    // calls a control that moves nothing a TODO wearing a feature's clothes (contract §1.3), and here it
    // would be a control that moves eight types out of nine.
    //
    // WHY IT IS ASSERTED HERE AND NOT ON THE COMPONENT. The bound is a property of the LIBRARY — of the
    // largest DetailFactor anyone shipped — and the library is nine files on disk. This suite is the one
    // that opens them. The component's own default is read rather than transcribed, so the two sides of
    // the relation are the two real ones.
    //
    // AND THERE IS A SECOND, TIGHTER BOUND THAN THE CLAMP, MEASURED ON THE FRAME. A type does not have to
    // reach the clamp to be ruined by the erosion; a THIN type dissolves well before it. Task DS raised
    // the layer's Detail Strength from 0.10 to 0.40 — for the march's sake, see the component — and shot
    // every type against a CLOUDLESS frame at the same camera to measure what survived:
    //
    //     type          factor   effective cut   its contribution to the frame, as a share of un-eroded
    //     cirrus         2.50    0.25 -> 1.00    33.3 %  ->  4.3 %
    //     altocumulus    1.60    0.16 -> 0.64    51.5 %  ->  7.5 %
    //
    // Both were authored against a layer of 0.10 and both are half the density of a cumulus, so the same
    // arithmetic that gives a congestus an edge deletes them. Their factors were re-based — 0.625 and
    // 0.40 — which restores their effective cut DEPTH to what their files were authored at, exactly, and
    // restores nothing else: Detail Tile Size is the LAYER's and it moved for all nine types, so both now
    // meet an erosion four times finer than their files ever saw. The amount of each type in the sky is
    // preserved to about one per cent; where its material sits is not (CALIBRATION.md §DS).
    //
    // So the bound asserted below is the reference type's own cut: no type may be cut DEEPER than the
    // congestus whose factor is 1 by definition, because past that depth the measurement says a thin body
    // stops being a cloud. A type that genuinely wants a deeper cut needs the density to carry it, and
    // that is content work with its own frames.
    const Desert::ECS::VolumetricCloudData layer;

    float       largestFactor = 0.0f;
    const char* largestName   = "";

    for ( const char* name : { kCloudTypeStratus, kCloudTypeCumulusHumilis, kCloudTypeCumulusMediocris,
                               kCloudTypeCumulusCongestus, kCloudTypeCumulonimbus, kCloudTypeStratocumulus,
                               kCloudTypeAltocumulus, kCloudTypeCirrus, kCloudTypeLenticular } )
    {
        const CloudTypeData data = LoadShipped( name );

        const float depth = layer.DetailStrength * data.Shape.DetailFactor;

        std::printf( "[CloudTypeLibrary] %-18s detail factor %.2f -> cut depth %.3f at the layer's %.2f\n", name,
                     data.Shape.DetailFactor, depth, layer.DetailStrength );

        if ( data.Shape.DetailFactor > largestFactor )
        {
            largestFactor = data.Shape.DetailFactor;
            largestName   = name;
        }

        EXPECT_LE( depth, 1.0f )
             << name << " has a detail factor of " << data.Shape.DetailFactor
             << ", which at the layer's shipped Detail Strength of " << layer.DetailStrength
             << " gives a cut depth of " << depth
             << " — past the clamp in Common/CloudField.glslh, so the layer's slider moves this type by "
                "nothing at all from here upward";

        // AND THE OTHER END: a factor of zero is a type that ignores the slider in the other direction.
        // The lenticular's 0.15 is the smallest shipped and is deliberate — a lens has to stay a lens —
        // but zero would be a type nobody can erode, which is what Detail Factor 0 is FOR and is
        // therefore allowed. What is asserted is only that the shipped library does not do it by accident.
        EXPECT_GT( data.Shape.DetailFactor, 0.0f )
             << name << " cannot be eroded at all, which is a smooth silhouette however the layer is set";

        // THE MEASURED BOUND. The reference type's factor is 1 by definition, so this says "no shipped
        // type is cut deeper than the reference". At 1.6 and 2.5 the frames measured 7.5 % and 4.3 % of
        // the type left in the picture — a type that is gone is not a wispy type.
        EXPECT_LE( data.Shape.DetailFactor, 1.0f )
             << name << " is cut " << data.Shape.DetailFactor
             << " times as deep as the reference congestus. Measured on the frame, a factor above 1 at the "
                "shipped Detail Strength dissolves a thin type rather than shredding it: the cirrus at 2.5 "
                "kept 4.3 % of its contribution to the picture and the altocumulus at 1.6 kept 7.5 %";
    }

    std::printf( "[CloudTypeLibrary] the largest factor shipped is %s at %.2f, which fixes the layer's "
                 "Detail Strength ceiling at %.3f\n",
                 largestName, largestFactor, 1.0f / largestFactor );

    // Stated the other way round, so the failure names the ceiling rather than the type: the shipped
    // Detail Strength may not exceed the reciprocal of the largest factor anyone authored.
    EXPECT_LE( layer.DetailStrength, 1.0f / largestFactor )
         << "the layer's Detail Strength of " << layer.DetailStrength << " is above the " << 1.0f / largestFactor
         << " that the library's largest Detail Factor (" << largestName << " at " << largestFactor
         << ") leaves room for";
}

int main( int argc, char** argv )
{
    ::testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
