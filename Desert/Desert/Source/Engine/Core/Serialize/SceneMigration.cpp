#include <Engine/Core/Serialize/SceneMigration.hpp>

#include <Common/Core/Logger.hpp>
#include <Common/Core/Units.hpp>

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
        // path, which did not move) - this
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

    namespace
    {
        // One world unit used to be a metre and is now a centimetre, so every length in an unstamped file
        // is short by this factor. Named once here so the migration and Units.hpp cannot drift.
        constexpr double kMetresToUnits = static_cast<double>( Common::Units::UnitsPerMetre );

        enum class Arity
        {
            Scalar, // a single number
            Vec3,   // an array of exactly three numbers
        };

        // Every LENGTH a component payload can carry, by the key the ComponentRegistry writes it under.
        // This is the complete list - a field that is not here is not a distance (an angle, a colour, a
        // count, a frequency), and a field that is here and is NOT a distance would silently inflate a
        // scene by a hundred. It is stated as data rather than code so the census can be read in one look.
        struct ScaledField
        {
            const char* Component;
            const char* Field;
            Arity       Kind;
        };

        constexpr ScaledField kScaledFields[] = {
             { "Camera", "Near", Arity::Scalar },
             { "Camera", "Far", Arity::Scalar },
             { "PointLight", "Radius", Arity::Scalar },
             { "PointLight", "MinRadius", Arity::Scalar },
             { "SpotLight", "Range", Arity::Scalar },
             { "Collider", "HalfExtents", Arity::Vec3 },
             { "Collider", "Radius", Arity::Scalar },
             { "Collider", "HalfHeight", Arity::Scalar },
             { "CharacterController", "Radius", Arity::Scalar },
             { "CharacterController", "Height", Arity::Scalar },
             { "CharacterController", "Gravity", Arity::Scalar },
             { "Terrain", "Size", Arity::Scalar },
             { "Terrain", "HeightScale", Arity::Scalar },
             { "Terrain", "GrassHeight", Arity::Scalar },
             { "Text", "Size", Arity::Scalar },
        };

        // Multiplies one value in place. Returns false when the key was there but unusable, which is the
        // only case worth reporting - an absent key is a scene that predates the field, not a failure.
        bool ScaleValue( const std::string& where, const char* field, Arity kind, rfl::Generic::Object& obj )
        {
            const auto value = obj.get( field );
            if ( !value.has_value() )
                return true; // absent: nothing to scale, and nothing went wrong

            if ( kind == Arity::Scalar )
            {
                const auto n = AsFiniteNumber( value.value() );
                if ( !n.has_value() )
                {
                    LOG_WARN( "[SceneMigration] {0}.{1} is {2}, expected a finite number - left in metres", where,
                              field, Describe( value.value() ) );
                    return false;
                }
                obj[field] = n.value() * kMetresToUnits;
                return true;
            }

            const auto arr = value.value().to_array();
            if ( !arr.has_value() || arr.value().size() != 3 )
            {
                LOG_WARN( "[SceneMigration] {0}.{1} is {2}, expected an array of 3 numbers - left in metres",
                          where, field, Describe( value.value() ) );
                return false;
            }

            rfl::Generic::Array scaled;
            for ( const auto& component : arr.value() )
            {
                const auto n = AsFiniteNumber( component );
                if ( !n.has_value() )
                {
                    LOG_WARN( "[SceneMigration] {0}.{1} is {2}, expected 3 finite numbers - left in metres", where,
                              field, Describe( value.value() ) );
                    return false;
                }
                scaled.push_back( rfl::Generic( n.value() * kMetresToUnits ) );
            }
            obj[field] = std::move( scaled );
            return true;
        }

        // A top-level transform vector (Translation / Scale). Absent means the entity never authored one,
        // and the component default it will be created with is not a metres-era length - see the header.
        bool ScaleTransform( const std::string& tag, const char* what, std::optional<glm::vec3>& v )
        {
            if ( !v.has_value() )
                return false;

            const glm::vec3 before = *v;
            *v *= static_cast<float>( kMetresToUnits );
            if ( !std::isfinite( v->x ) || !std::isfinite( v->y ) || !std::isfinite( v->z ) )
            {
                LOG_WARN( "[SceneMigration] entity '{0}': {1} is not finite after scaling - restored", tag, what );
                *v = before;
                return false;
            }
            return true;
        }

        // A procedural primitive is REGENERATED at its authored size by the factory, so its Scale is a
        // multiplier on geometry the engine builds, not a length the file owns - scaling it would cube the
        // object. A file-backed mesh has no such regeneration and its Scale is a real length.
        bool HasProceduralMesh( const Assets::EntityData& entity )
        {
            const auto mesh = entity.Components.get( "StaticMesh" );
            if ( !mesh.has_value() )
                return false;
            const auto fields = mesh.value().to_object();
            return fields.has_value() && fields.value().get( "Primitive" ).has_value();
        }
    } // namespace

    UnitMigrationReport MigrateMetresToUnits( std::vector<Assets::EntityData>& entities,
                                              std::optional<rfl::Generic>&     settings )
    {
        UnitMigrationReport report;

        for ( auto& entity : entities )
        {
            const std::string tag     = entity.Tag.value_or( "Entity" );
            int               values  = 0;
            int               refused = 0;

            if ( entity.Translation.has_value() )
            {
                if ( ScaleTransform( tag, "Translation", entity.Translation ) )
                    ++values;
                else
                    ++refused;
            }

            if ( entity.Scale.has_value() && !HasProceduralMesh( entity ) )
            {
                if ( ScaleTransform( tag, "Scale", entity.Scale ) )
                    ++values;
                else
                    ++refused;
            }

            for ( const auto& scaled : kScaledFields )
            {
                const auto payload = entity.Components.get( scaled.Component );
                if ( !payload.has_value() )
                    continue;

                auto fields = payload.value().to_object();
                if ( !fields.has_value() )
                {
                    LOG_WARN( "[SceneMigration] entity '{0}': the {1} payload is {2}, not an object - its "
                              "lengths stay in metres",
                              tag, scaled.Component, Describe( payload.value() ) );
                    ++refused;
                    continue;
                }
                if ( !fields.value().get( scaled.Field ).has_value() )
                    continue; // the field is absent; the component default applies and is already in units

                const std::string where = "entity '" + tag + "': " + scaled.Component;
                if ( ScaleValue( where, scaled.Field, scaled.Kind, fields.value() ) )
                    ++values;
                else
                    ++refused;

                entity.Components[scaled.Component] = rfl::Generic( std::move( fields.value() ) );
            }

            if ( values > 0 )
                report.Entities += 1;
            report.Values += values;
            report.Rejected += refused;
        }

        // Gravity is the one scene-wide length-per-second-squared. Absent keeps the C++ default, which is
        // already stated in world units.
        if ( settings.has_value() )
        {
            auto fields = settings->to_object();
            if ( fields.has_value() && fields.value().get( "Gravity" ).has_value() )
            {
                if ( ScaleValue( "Settings", "Gravity", Arity::Scalar, fields.value() ) )
                    report.Values += 1;
                else
                    report.Rejected += 1;
                settings = rfl::Generic( std::move( fields.value() ) );
            }
        }

        return report;
    }

    SceneMigrationReport MigrateScene( SceneSerialized& scene )
    {
        SceneMigrationReport report;

        if ( scene.SceneVersion.value_or( 0 ) < kSceneVersion )
        {
            report.SkyRaised = true;
            report.Sky       = MigrateSkyV0ToV1( scene.Entities );
        }

        if ( scene.UnitVersion.value_or( 0 ) < kUnitVersion )
        {
            report.UnitsRaised = true;
            report.Units       = MigrateMetresToUnits( scene.Entities, scene.Settings );
        }

        // Stamped whether or not anything moved: an empty scene at version 0 is still a scene at version 0,
        // and leaving it unstamped is how every load ends up re-running a migration that already happened.
        scene.SceneVersion = kSceneVersion;
        scene.UnitVersion  = kUnitVersion;

        return report;
    }

} // namespace Desert::Core
