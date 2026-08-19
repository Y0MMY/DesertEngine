#include <Engine/Assets/CloudTypeData.hpp>

#include <cmath>

#include <rflcpp/rfl/json.hpp>

namespace Desert::Assets
{
    namespace
    {
        // Every number that reaches the profile generator has to be a number. A NaN multiplied into the
        // table produces a shell whose bounds are NaN, a march whose step count is NaN and a sky that is
        // black with nothing in the log — and it takes a day to find out which of twelve floats it came
        // from. Naming the field here is the whole difference.
        Common::BoolResultStr Finite( const char* field, float value )
        {
            if ( !std::isfinite( value ) )
                return Common::MakeFormattedError<bool>( "{} is {}, which is not a finite number", field, value );
            return BOOLSUCCESS;
        }

        Common::BoolResultStr InRange( const char* field, float value, float low, float high )
        {
            if ( auto finite = Finite( field, value ); !finite )
                return finite;
            if ( value < low || value > high )
                return Common::MakeFormattedError<bool>( "{} is {}, outside the legal range [{}, {}]", field,
                                                         value, low, high );
            return BOOLSUCCESS;
        }
    } // namespace

    const Graphic::CloudTypeShape& CloudTypeDefaultShape()
    {
        // T0'S CUMULUS CONGESTUS, DIGIT FOR DIGIT. Base and top from meteorology, an edge fraction of 0.15
        // because a congestus is a flat pad at the rim of a patch and a tower in its middle, and a density
        // slightly above a cumulus because it is made of more water. The three factors that T0 did not have
        // are 1 here by construction: this row IS the reference the artist's Density Scale, Extinction
        // Scale and Detail Strength are relative to, so anything else would move the sky of every existing
        // scene while claiming to be its default.
        static const Graphic::CloudTypeShape kDefault{
             /* BaseAltitudeKm   */ 2.20f,
             /* TopAltitudeKm    */ 5.80f,
             /* EdgeTopFraction  */ 0.15f,
             /* BaseRampFraction */ 0.04f,
             /* TopTaper         */ 0.50f,
             /* AnvilAltitudeKm  */ 0.0f,
             /* AnvilThicknessKm */ 0.0f,
             /* AnvilStrength    */ 0.0f,
             /* DetailCharacter  */ 1.00f,
             /* DetailFactor     */ 1.00f,
             /* DensityFactor    */ 1.15f,
             /* ExtinctionFactor */ 1.00f,
             // T3'S TWO, AND BOTH ARE THE IDENTITY HERE, for exactly the reason the three factors above
             // are 1: this row is the reference every other type is stated against, and a placement scale
             // of anything but 1 would move the sky of every scene that has ever used the default while
             // claiming to be the default. A congestus field at the layer's own tile with round patches is
             // what T1 shipped, and this is that, digit for digit.
             /* PlacementScale      */ 1.00f,
             /* PlacementAnisotropy */ 1.00f,
        };
        return kDefault;
    }

    CloudTypeData CloudTypeDefault()
    {
        CloudTypeData data;
        data.FormatVersion = kCloudTypeFormatVersion;
        data.DisplayName   = "Cumulus congestus (built-in)";
        data.Notes         = "The type an empty slot resolves to. It is not a file: a scene that names no "
                             "type still has to have a sky.";
        data.Shape         = CloudTypeDefaultShape();
        return data;
    }

    Common::BoolResultStr ValidateCloudTypeShape( const Graphic::CloudTypeShape& shape )
    {
        // THE ALTITUDES FIRST, because everything else is a fraction of the span they define. A span that
        // is zero or negative is a division the generator guards against and a shell the packer floors at a
        // metre — both of which produce a cloud nobody can see rather than a crash, which is precisely the
        // kind of failure that has to be refused at the door instead of survived.
        if ( auto r = InRange( "BaseAltitudeKm", shape.BaseAltitudeKm, 0.0f, 20.0f ); !r )
            return r;
        if ( auto r = InRange( "TopAltitudeKm", shape.TopAltitudeKm, 0.0f, 20.0f ); !r )
            return r;
        if ( !( shape.TopAltitudeKm > shape.BaseAltitudeKm ) )
            return Common::MakeFormattedError<bool>(
                 "TopAltitudeKm is {} and BaseAltitudeKm is {}: a type has to have a top above its base",
                 shape.TopAltitudeKm, shape.BaseAltitudeKm );

        if ( auto r = InRange( "EdgeTopFraction", shape.EdgeTopFraction, 0.0f, 1.0f ); !r )
            return r;

        // Strictly above zero rather than merely finite: the generator divides by each of them, and clamps
        // the divisor at a thousandth. A zero authored here would be silently answered by that clamp, which
        // is a value nobody chose standing in for one somebody typed.
        if ( auto r = InRange( "BaseRampFraction", shape.BaseRampFraction, 0.001f, 1.0f ); !r )
            return r;
        if ( auto r = InRange( "TopTaper", shape.TopTaper, 0.001f, 1.0f ); !r )
            return r;

        if ( auto r = InRange( "AnvilStrength", shape.AnvilStrength, 0.0f, 1.0f ); !r )
            return r;
        if ( auto r = InRange( "AnvilAltitudeKm", shape.AnvilAltitudeKm, 0.0f, 20.0f ); !r )
            return r;
        if ( auto r = InRange( "AnvilThicknessKm", shape.AnvilThicknessKm, 0.0f, 10.0f ); !r )
            return r;
        // A LOBE WITH NO THICKNESS IS NOT A LOBE. The generator answers this pair by drawing nothing, so a
        // file in this state carries an anvil the artist can see in the numbers and never in the sky.
        if ( shape.AnvilStrength > 0.0f && !( shape.AnvilThicknessKm > 0.0f ) )
            return Common::MakeFormattedError<bool>(
                 "AnvilStrength is {} but AnvilThicknessKm is {}: an anvil with no thickness never appears",
                 shape.AnvilStrength, shape.AnvilThicknessKm );

        if ( auto r = InRange( "DetailCharacter", shape.DetailCharacter, 0.0f, 1.0f ); !r )
            return r;
        // The three factors multiply the artist's own scales, so their ceiling is generous — a cirrus at a
        // twentieth of a cumulus' extinction and a cumulonimbus at four times it are both real weather.
        if ( auto r = InRange( "DetailFactor", shape.DetailFactor, 0.0f, 8.0f ); !r )
            return r;
        if ( auto r = InRange( "DensityFactor", shape.DensityFactor, 0.0f, 8.0f ); !r )
            return r;
        if ( auto r = InRange( "ExtinctionFactor", shape.ExtinctionFactor, 0.0f, 8.0f ); !r )
            return r;

        // THE PLACEMENT PAIR. Strictly above zero on the scale, because the packer divides by it to build
        // the field's frequency and a zero there is an infinity in a texture coordinate — a sky that is
        // black or white in bands, with nothing in the log. The ceiling of 8 is a placement period eight
        // times the layer's tile, which at the shipped 12 km is a 96 km cell: one cloud from horizon to
        // horizon, which is a legitimate overcast and the largest thing worth authoring.
        if ( auto r = InRange( "PlacementScale", shape.PlacementScale, 0.05f, 8.0f ); !r )
            return r;
        // BELOW ONE IS LEGAL AND MEANS SOMETHING. Above 1 the patches are drawn out ALONG the wind, which
        // is fibrous cirrus and cloud streets; below 1 they are drawn out ACROSS it, which is what a wave
        // cloud is — a lenticular stands in a mountain's lee wave with its crest perpendicular to the flow,
        // and so do the bars of an altocumulus undulatus. One number covers both because they are the same
        // stretch about the same axis, and forbidding one half of it would have made the lenticular
        // unreachable.
        if ( auto r = InRange( "PlacementAnisotropy", shape.PlacementAnisotropy, 0.1f, 16.0f ); !r )
            return r;

        return BOOLSUCCESS;
    }

    Common::ResultStr<CloudTypeData> ParseCloudType( const std::string& text )
    {
        if ( text.empty() )
            return Common::MakeFormattedError<CloudTypeData>( "the file is empty" );

        // THE VERSION IS READ FIRST, ON ITS OWN, and that ordering is the whole difference between a
        // diagnosable refusal and a puzzling one. A version-1 file is missing the two placement numbers
        // T3 added, so a full parse fails with "field PlacementScale not found" — true, and useless: the
        // reader is looking at a file that was correct when it was written, and what they need to be told
        // is that the FORMAT moved, not that a field is absent. Reading the header alone lets the version
        // check answer first.
        //
        // AN UNKNOWN FORMAT IS REFUSED, NOT READ ANYWAY, in either direction. A file claiming a version
        // this build does not know was written by a build this one is not, and reading its numbers as if
        // they meant what they mean here is how a field that moved becomes a sky nobody can explain.
        // Read as an untyped tree rather than into a header struct: a struct would impose the rest of the
        // schema on a document whose whole problem may be that it does not match the schema, which is the
        // circularity this read exists to break.
        if ( const auto tree = rfl::json::read<rfl::Generic>( text ); tree )
        {
            if ( const auto fields = tree.value().to_object(); fields )
            {
                if ( const auto stated = fields.value().get( "FormatVersion" ); stated.has_value() )
                {
                    const auto number = stated.value().to_int();
                    if ( number.has_value() && number.value() != kCloudTypeFormatVersion )
                        return Common::MakeFormattedError<CloudTypeData>(
                             "format version {} was written by a different build; this one reads version {}",
                             number.value(), kCloudTypeFormatVersion );
                }
            }
        }

        const auto parsed = rfl::json::read<CloudTypeData>( text );
        if ( !parsed )
            return Common::MakeFormattedError<CloudTypeData>( "{}", parsed.error().what() );

        CloudTypeData data = parsed.value();

        const int32_t version = data.FormatVersion.value_or( kCloudTypeFormatVersion );
        if ( version != kCloudTypeFormatVersion )
            return Common::MakeFormattedError<CloudTypeData>(
                 "format version {} was written by a different build; this one reads version {}", version,
                 kCloudTypeFormatVersion );

        if ( auto valid = ValidateCloudTypeShape( data.Shape ); !valid )
            return Common::MakeFormattedError<CloudTypeData>( "{}", valid.GetError() );

        data.FormatVersion = kCloudTypeFormatVersion;
        return Common::MakeSuccess( std::move( data ) );
    }

    std::string WriteCloudType( const CloudTypeData& data )
    {
        return rfl::json::write( data, YYJSON_WRITE_PRETTY );
    }
} // namespace Desert::Assets
