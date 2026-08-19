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

#include <Engine/Assets/CloudNoiseVolume.hpp>
#include <Engine/Assets/CloudTypeData.hpp>
#include <Engine/Graphic/Clouds/CloudProfileTable.hpp>

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
using Desert::Graphic::CloudProfileCurve;
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

    // The lowest altitude at which this type has any body at all, at the core of a placement patch, and
    // the highest. Measured off the generator rather than read off the fields, so a type whose ramps
    // swallow its own band is measured as it will be rendered.
    float ProfileBaseKm( const CloudTypeShape& shape )
    {
        const float top = CloudTypeTopKm( shape );
        for ( int i = 0; i <= 4000; ++i )
        {
            const float altitudeKm = top * static_cast<float>( i ) / 4000.0f;
            if ( CloudProfileCurve( shape, altitudeKm, 1.0f ) > 0.0f )
                return altitudeKm;
        }
        return top;
    }

    float ProfileTopKm( const CloudTypeShape& shape )
    {
        const float top = CloudTypeTopKm( shape );
        for ( int i = 4000; i >= 0; --i )
        {
            const float altitudeKm = top * static_cast<float>( i ) / 4000.0f;
            if ( CloudProfileCurve( shape, altitudeKm, 1.0f ) > 0.0f )
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
        const float step   = ( topKm - baseKm ) / 2000.0f;

        float total = 0.0f;
        for ( int i = 0; i < 2000; ++i )
            total += CloudProfileCurve( shape, baseKm + ( static_cast<float>( i ) + 0.5f ) * step, 1.0f ) * step;

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
    // Clouds_Demo's layer, which is the scene the verification protocol shoots and the one every number
    // below was measured on. The tile is the layer's, not a type's: a type states how much coarser or
    // finer than the layer it is, so the pair only means anything together.
    constexpr float kLayerWeatherTileKm = 12.0f;

    // The component's Max Steps default. It is a QUALITY TIER — an artist who lowers it is buying speed
    // with resolution on purpose — so the library is calibrated against the default rather than against
    // whatever a scene happens to carry.
    constexpr float kComponentMaxSteps = 256.0f;

    /**
     * THE FINEST CELL A TYPE PLACES, in kilometres, over all three axes of its own placement field.
     *
     * Mirrors Common/CloudField.glslh: the two horizontal basis vectors come from
     * Graphic::CloudSpeciesPlacementBasis and the VERTICAL frequency is derived from the across-wind axis
     * exactly as the producer derives it, `CLOUD_COVERAGE_FREQ_Y * length(across) / CLOUD_COVERAGE_FREQ_Z`.
     * The vertical is therefore always finer than the across-wind axis, and the along-wind axis is finer
     * than either only below an anisotropy of 1 / 2.182 — in the shipped library, the Lenticular alone.
     *
     * @p latticeCells is how many noise cells the volume packs into one period of that field. THE HIGH
     * frequency and not the low one: the coverage field is 0.65 of the coarse Alligator plus 0.35 of the
     * fine one, and it is the fine component that decides how short a chord can be. Measured against the
     * HF cell the median chord along a view ray is 0.52 to 1.22 of it across nine types and three
     * elevations; against the LF cell the same 27 measurements scatter over 0.26 to 0.61.
     */
    float FinestPlacementCellKm( const CloudTypeShape& shape, float latticeCells )
    {
        const Desert::Graphic::CloudPlacementBasis basis =
             Desert::Graphic::CloudSpeciesPlacementBasis( shape, 1.0f, 0.0f );

        const float alongFreq  = std::sqrt( basis.AlongX * basis.AlongX + basis.AlongZ * basis.AlongZ );
        const float acrossFreq = std::sqrt( basis.AcrossX * basis.AcrossX + basis.AcrossZ * basis.AcrossZ );
        const float verticalFreq =
             Desert::Graphic::kCloudCoverageFreqY * acrossFreq / Desert::Graphic::kCloudCoverageFreqZ;

        const float finestFreq = std::max( { alongFreq, acrossFreq, verticalFreq } );

        return kLayerWeatherTileKm / ( finestFreq * std::max( latticeCells, 1.0f ) );
    }

    /**
     * HOW MUCH OF A CELL A CHORD IS. Measured, not derived: T2 established that a body of cloud is a fixed
     * FRACTION of a placement cell — which is why doubling the lattice period could not cure the speckle —
     * and this is that fraction, floored.
     *
     * The instrument is the one T2b used, re-pointed at the species: Common/CloudField.glslh compiled as
     * C++, fed the shipped `.dcnv` through a trilinear REPEAT read, 2304 rays of a 45-degree frame per
     * elevation, three elevations, nine types, the median length of the runs on which the un-eroded
     * profile is positive. The 27 medians divided by this function's answer span 0.515 to 1.222; 0.5 is
     * the floor of that, so a type that satisfies the relation below satisfies it for every elevation
     * rather than on average.
     */
    constexpr float kMedianChordPerCell = 0.5f;

    /// What the volume a type reads packs into one period of its placement field, from the volume's own
    /// header rather than from a table here — the recipe is IN the file, which is what makes a `.dcnv` an
    /// asset rather than a blob.
    float VolumeLatticeCells( const std::optional<std::string>& reference )
    {
        // Memoized by path. The shipped decoder checks a CRC over eight mebibytes and nine types read two
        // volumes between them; decoding each of them once is the difference between a suite that runs in
        // a second and one that runs in eight, and it changes no answer.
        static std::map<std::string, float> cache;

        std::filesystem::path dir = LibraryDirectory();
        EXPECT_FALSE( dir.empty() );
        // Editor/Resources/Assets/Clouds/Types -> Editor/Resources/Assets
        const std::filesystem::path assets = dir.parent_path().parent_path();

        const std::string relative =
             reference.has_value() ? reference.value()
                                   : std::string( "Clouds/" ) + Desert::Assets::kCloudNoiseDefaultVolumeName;

        const std::filesystem::path path = assets / relative;

        const auto cached = cache.find( path.string() );
        if ( cached != cache.end() )
            return cached->second;

        std::ifstream file( path, std::ios::binary );
        EXPECT_TRUE( file.good() ) << "noise volume '" << path.string() << "' could not be opened";

        const std::vector<unsigned char> bytes( ( std::istreambuf_iterator<char>( file ) ),
                                                std::istreambuf_iterator<char>() );

        const auto decoded = Desert::Assets::DecodeCloudNoiseVolume( bytes );
        EXPECT_TRUE( decoded ) << "shipped noise volume '" << path.string()
                               << "' does not decode: " << ( decoded ? std::string{} : decoded.GetError() );

        const float cells = decoded ? decoded.GetValue().Params.BillowPeriodHighFrequency : 1.0f;
        cache.emplace( path.string(), cells );
        return cells;
    }
} // namespace

TEST( CloudTypeLibrary, NoShippedTypePlacesStructureThinnerThanTheMarchCanFind )
{
    // THE RELATION THIS TEST EXISTS FOR, and the reason it is a test rather than a comment: the two sides
    // moved three times without ever being compared. T2 measured the ratio on ONE type, brought it to
    // 1.03x Nyquist, and wrote the number down. T0 then made the shell the species' own envelope, which
    // moved the march's step; T3 then gave every species its own placement scale and anisotropy, which
    // moved the structure — by different factors, in different directions, per type. Re-measured on the
    // whole library afterwards, five of the nine were past Nyquist against the march's SEARCH step, the
    // worst of them (Cirrus) by 3.66x with three quarters of its chords thinner than one search step.
    //
    // The bound is the search step and not the integration step. Outside cloud the march strides by
    // CloudCoarseStepKm and only drops to the fine tier once a coarse sample has already found material,
    // so a chord that fits between two coarse samples is never seen at all — and whether it fits is
    // decided by the ray's jitter, which is the definition of speckle. CloudResolvableChordKm is that
    // statement; CloudFinestResolvableChordKm is its value at the finest the schedule ever marches.
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
        const CloudTypeData data = LoadShipped( name );

        const float cellKm  = FinestPlacementCellKm( data.Shape, VolumeLatticeCells( data.NoiseVolume ) );
        const float chordKm = cellKm * kMedianChordPerCell;

        std::printf( "[CloudTypeLibrary] %-18s finest cell %6.0f m -> median chord %6.0f m, %.2fx the "
                     "chord the march resolves\n",
                     name, cellKm * 1000.0f, chordKm * 1000.0f, chordKm / resolvableKm );

        EXPECT_GE( chordKm, resolvableKm )
             << name << " places its finest structure on a cell of " << cellKm * 1000.0f
             << " m, whose median chord along a view ray is " << chordKm * 1000.0f << " m — thinner than the "
             << resolvableKm * 1000.0f
             << " m the march can be relied on to find, so that fraction of this type is sampled or not "
                "sampled by the throw of a jitter and reads as dither. Either the type places coarser "
                "structure (its Placement Scale, its anisotropy, or the lattice of the volume it names) "
                "or the schedule marches finer (CLOUD_DISTANCE_TO_MAX_STEPS_KM in "
                "Common/CloudGeometry.glslh) — the two are one relation and this is where it is kept";
    }
}

int main( int argc, char** argv )
{
    ::testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
