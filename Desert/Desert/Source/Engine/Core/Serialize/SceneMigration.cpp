#include <Engine/Core/Serialize/SceneMigration.hpp>

#include <Common/Core/Logger.hpp>

#include <glm/trigonometric.hpp>

#include <cmath>
#include <optional>
#include <string>

namespace Desert::Core
{
    namespace
    {
        // How an old "Skybox" value has to be read, and what it becomes. Only four shapes exist because
        // SKY-05 kept the C++ member names byte-identical across the move: twelve of the fourteen mappings
        // below are the same name on both sides, which makes them copies that cannot be mistyped.
        enum class MappedKind
        {
            Bool,            // JSON true/false
            Scalar,          // JSON number -> float
            Color3,          // JSON array of exactly three finite numbers -> glm::vec3
            AngularDiameter, // JSON number in radians, and a RADIUS -> degrees, and a DIAMETER
        };

        struct SkyFieldMapping
        {
            const char* Old;
            const char* New;
            MappedKind  Kind;
        };

        // The mapping table of SKY-25, in the field order of ECS::SkyAtmosphereData so the payload this
        // function writes reads like the one a save would produce.
        //
        // What is deliberately NOT here: SkyboxHandle and Intensity stay under "Skybox" (they are the HDR
        // path, which did not move), and the six Cloud* fields belong to VolumetricCloudsComponent - this
        // function neither reads nor removes them.
        constexpr SkyFieldMapping kSkyFieldMappings[] = {
             { "Procedural", "Enabled", MappedKind::Bool },
             { "SkyBrightness", "SkyBrightness", MappedKind::Scalar },
             { "HorizonFalloff", "HorizonFalloff", MappedKind::Scalar },
             { "ZenithColor", "ZenithColor", MappedKind::Color3 },
             { "HorizonColor", "HorizonColor", MappedKind::Color3 },
             { "GroundColor", "GroundColor", MappedKind::Color3 },
             { "NightColor", "NightColor", MappedKind::Color3 },
             { "SunIntensity", "SunIntensity", MappedKind::Scalar },
             { "SunColor", "SunColor", MappedKind::Color3 },
             { "SunDiskRadius", "SunAngularDiameter", MappedKind::AngularDiameter },
             { "SunGlow", "SunGlow", MappedKind::Scalar },
             { "SunsetColor", "SunsetColor", MappedKind::Color3 },
             { "SunsetIntensity", "SunsetIntensity", MappedKind::Scalar },
             { "StarIntensity", "StarIntensity", MappedKind::Scalar },
        };

        constexpr int kMappedFieldCount = static_cast<int>( std::size( kSkyFieldMappings ) );

        static_assert( kMappedFieldCount <= kSkyAtmosphereFieldCount,
                       "every mapped field must land on a field the new component actually has" );

        // A number, whatever JSON spelling it arrived in. Integers matter: a hand-edited scene writes
        // "SunIntensity":22, which reflect-cpp parses as int64 and to_double() then refuses.
        // Non-finite is not a number we accept anywhere - a NaN that reaches the sky poisons every pixel
        // it touches and leaves no trace of where it came from.
        std::optional<double> AsFiniteNumber( const rfl::Generic& g )
        {
            if ( const auto d = g.to_double(); d.has_value() )
            {
                if ( std::isfinite( d.value() ) )
                    return d.value();
                return std::nullopt;
            }
            if ( const auto i = g.to_int64(); i.has_value() )
                return static_cast<double>( i.value() );
            return std::nullopt;
        }

        // The offending value, spelled out. A warning that says "wrong type" without saying WHAT was in
        // the file sends the next reader back to the file anyway.
        std::string Describe( const rfl::Generic& g )
        {
            if ( const auto b = g.to_bool(); b.has_value() )
                return b.value() ? "true" : "false";
            if ( const auto i = g.to_int64(); i.has_value() )
                return std::to_string( i.value() );
            if ( const auto d = g.to_double(); d.has_value() )
                return std::to_string( d.value() );
            if ( const auto s = g.to_string(); s.has_value() )
                return "\"" + s.value() + "\"";
            if ( const auto a = g.to_array(); a.has_value() )
                return "an array of " + std::to_string( a.value().size() ) + " element(s)";
            if ( g.to_object().has_value() )
                return "an object";
            return "null";
        }

        void WarnRejected( const std::string& tag, const SkyFieldMapping& mapping, const rfl::Generic& value,
                           const char* expected )
        {
            LOG_WARN( "[SceneMigration] entity '{0}': Skybox.{1} is {2}, expected {3} - SkyAtmosphere.{4} "
                      "keeps its default",
                      tag, mapping.Old, Describe( value ), expected, mapping.New );
        }

        // Reads one old value and, if it is usable, writes the new one. Returns false when the value was
        // present but unusable, which is the ONLY case the caller counts as rejected: a value that is
        // simply absent is not an error, it is a scene that predates the field.
        bool CarryField( const std::string& tag, const SkyFieldMapping& mapping, const rfl::Generic& value,
                         rfl::Generic::Object& out )
        {
            switch ( mapping.Kind )
            {
                case MappedKind::Bool:
                {
                    const auto b = value.to_bool();
                    if ( !b.has_value() )
                    {
                        WarnRejected( tag, mapping, value, "a boolean" );
                        return false;
                    }
                    out[mapping.New] = b.value();
                    return true;
                }
                case MappedKind::Scalar:
                {
                    const auto n = AsFiniteNumber( value );
                    if ( !n.has_value() )
                    {
                        WarnRejected( tag, mapping, value, "a finite number" );
                        return false;
                    }
                    out[mapping.New] = n.value();
                    return true;
                }
                case MappedKind::Color3:
                {
                    const auto arr = value.to_array();
                    if ( !arr.has_value() || arr.value().size() != 3 )
                    {
                        WarnRejected( tag, mapping, value, "an array of 3 numbers" );
                        return false;
                    }

                    rfl::Generic::Array colour;
                    for ( const auto& component : arr.value() )
                    {
                        const auto n = AsFiniteNumber( component );
                        if ( !n.has_value() )
                        {
                            WarnRejected( tag, mapping, value, "an array of 3 finite numbers" );
                            return false;
                        }
                        colour.push_back( rfl::Generic( n.value() ) );
                    }
                    out[mapping.New] = std::move( colour );
                    return true;
                }
                case MappedKind::AngularDiameter:
                {
                    // The one conversion in the whole table: the old field was the angular RADIUS in
                    // RADIANS, the new one is the angular DIAMETER in DEGREES. A radius of zero or less is
                    // not a sun that ever rendered, so it is rejected rather than converted to a zero disk.
                    const auto n = AsFiniteNumber( value );
                    if ( !n.has_value() || n.value() <= 0.0 )
                    {
                        WarnRejected( tag, mapping, value, "a finite number greater than 0" );
                        return false;
                    }
                    out[mapping.New] = glm::degrees( n.value() ) * 2.0;
                    return true;
                }
            }
            return false;
        }
    } // namespace

    SkyMigrationReport MigrateSkyV0ToV1( std::vector<Assets::EntityData>& entities )
    {
        SkyMigrationReport report;

        for ( auto& entity : entities )
        {
            const auto skybox = entity.Components.get( "Skybox" );
            if ( !skybox.has_value() )
                continue; // no sky on this entity - nothing to move, and nothing to report

            // Idempotence, and the reason it matters: a scene may be re-read (undo, prefab instancing,
            // a second load) after it was already raised. Re-running the mapping would overwrite values
            // the user has since edited with the stale ones still sitting under "Skybox".
            if ( entity.Components.get( "SkyAtmosphere" ).has_value() )
                continue;

            const std::string tag = entity.Tag.value_or( "Entity" );

            const auto oldFields = skybox.value().to_object();
            if ( !oldFields.has_value() )
            {
                LOG_WARN( "[SceneMigration] entity '{0}': the Skybox payload is {1}, not an object - no "
                          "SkyAtmosphere component was created for it",
                          tag, Describe( skybox.value() ) );
                continue;
            }

            rfl::Generic::Object sky;
            int                  carried  = 0;
            int                  rejected = 0;

            for ( const auto& mapping : kSkyFieldMappings )
            {
                const auto value = oldFields.value().get( mapping.Old );
                if ( !value.has_value() )
                    continue; // absent: the new field keeps its C++ default, which is not a failure

                if ( CarryField( tag, mapping, value.value(), sky ) )
                    ++carried;
                else
                    ++rejected;
            }

            // Everything the new component has and the old payload could not fill - the time-of-day
            // block, the environment-bake knobs, ActivePreset and PlanetRadius - is left OUT of the
            // payload on purpose: an absent key is exactly how the reflection serializer spells "keep the
            // C++ default", so the defaults live in one place (the component) instead of two.
            entity.Components["SkyAtmosphere"] = rfl::Generic( std::move( sky ) );

            report.Entities += 1;
            report.FieldsCarried += carried;
            report.FieldsRejected += rejected;
            report.FieldsDefaulted += kSkyAtmosphereFieldCount - carried - rejected;
        }

        return report;
    }

} // namespace Desert::Core
