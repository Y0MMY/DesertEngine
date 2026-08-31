#include <Engine/Core/Serialize/SceneMigration.hpp>

#include <Engine/Core/SceneSettings.hpp>
// For the shipped presets' names and the directory they live in, and nothing else. The v4 -> v5 migration
// turns the species integer a v4 file carries into the PATH of the preset that holds the same twelve
// numbers, and spelling that path here as a literal would be a second statement of it — the exact
// duplication that agrees with itself until somebody moves the library. The header carries no renderer and
// no asset manager, so it does not bring another layer into this one.
#include <Engine/Assets/CloudTypeData.hpp>

#include <Common/Core/Logger.hpp>
#include <Common/Core/Units.hpp>

#include <glm/trigonometric.hpp>

#include <algorithm>
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

    namespace
    {
        // The reflected field name of SceneSettings::Tonemapper - the key the generic serializer reads
        // and writes. Stated once so the migration and the round trip cannot disagree about spelling.
        constexpr const char* kTonemapperKey = "Tonemapper";
    } // namespace

    TonemapMigrationReport MigrateTonemapperV1ToV2( std::optional<rfl::Generic>& settings )
    {
        TonemapMigrationReport report;

        rfl::Generic::Object fields;
        if ( settings.has_value() )
        {
            auto parsed = settings->to_object();
            if ( !parsed.has_value() )
            {
                // Not an object: this scene's whole settings block is unreadable. Replacing it with a
                // fresh one would discard every other scene-wide value to save this single field, so it
                // is left exactly as found and said out loud instead - the scene will load on the C++
                // default (ACES) and its author needs to know that before wondering why it re-graded.
                LOG_WARN( "[SceneMigration] the Settings payload is {0}, not an object - the tonemapper "
                          "could not be pinned and this scene will load on the default operator",
                          Describe( *settings ) );
                return report;
            }
            fields = std::move( parsed.value() );
        }
        else
        {
            report.SettingsCreated = true;
        }

        if ( fields.get( kTonemapperKey ).has_value() )
            return report; // the file already states its operator - see the idempotence note in the header

        fields[kTonemapperKey] = static_cast<int64_t>( TonemapOperator::Reinhard );
        report.OperatorPinned  = true;

        settings = rfl::Generic( std::move( fields ) );
        return report;
    }

    CloudNoiseMigrationReport MigrateCloudNoiseV2ToV3( std::vector<Assets::EntityData>& entities )
    {
        // The four keys the GPU bake was parameterised by. Named as data rather than tested for one at a
        // time so the list can be read in one look and so the count in the report cannot drift from it.
        static constexpr const char* kRemovedBakeKeys[] = { "WeatherSeed", "WeatherOctaves", "DetailSeed",
                                                            "DetailOctaves" };

        CloudNoiseMigrationReport report;

        for ( auto& entity : entities )
        {
            const auto clouds = entity.Components.get( "VolumetricCloud" );
            if ( !clouds.has_value() )
                continue;

            const auto fields = clouds.value().to_object();
            if ( !fields.has_value() )
            {
                LOG_WARN( "[SceneMigration] entity '{0}': the VolumetricCloud payload is {1}, not an object - "
                          "its bake settings could not be removed",
                          entity.Tag.value_or( "Entity" ), Describe( clouds.value() ) );
                continue;
            }

            // Rebuilt rather than erased in place: rfl::Object is an ordered vector of pairs with no erase,
            // and copying every key except the four states the intent more plainly than an index dance
            // would. Order is preserved, so a re-saved file differs from the old one only by the four lines.
            rfl::Generic::Object kept;
            int                  dropped = 0;

            for ( const auto& [key, value] : fields.value() )
            {
                const bool isBakeKey = std::any_of( std::begin( kRemovedBakeKeys ), std::end( kRemovedBakeKeys ),
                                                    [&key]( const char* removed ) { return key == removed; } );
                if ( isBakeKey )
                {
                    ++dropped;
                    continue;
                }
                kept[key] = value;
            }

            if ( dropped == 0 )
                continue; // already raised, or authored after the move - leave the tree byte-identical

            entity.Components["VolumetricCloud"] = rfl::Generic( std::move( kept ) );
            report.Entities += 1;
            report.FieldsDropped += dropped;
        }

        return report;
    }

    CloudSpeciesMigrationReport MigrateCloudSpeciesV3ToV4( std::vector<Assets::EntityData>& entities )
    {
        // The three keys with nowhere to go. Named as data rather than tested one at a time so the list
        // reads in one look and the count in the report cannot drift from it.
        static constexpr const char* kRemovedKeys[] = { "LayerBottomAltitude", "LayerThickness",
                                                        "CloudTypeVariance" };
        static constexpr const char* kTypeKey       = "CloudType";
        static constexpr const char* kSpeciesKey    = "Species";

        CloudSpeciesMigrationReport report;

        for ( auto& entity : entities )
        {
            const auto clouds = entity.Components.get( "VolumetricCloud" );
            if ( !clouds.has_value() )
                continue;

            const auto fields = clouds.value().to_object();
            if ( !fields.has_value() )
            {
                LOG_WARN( "[SceneMigration] entity '{0}': the VolumetricCloud payload is {1}, not an object - "
                          "its cloud type could not be turned into a species",
                          entity.Tag.value_or( "Entity" ), Describe( clouds.value() ) );
                continue;
            }

            // Rebuilt rather than erased in place, like the migration above it: rfl::Object is an ordered
            // vector of pairs with no erase, and copying every key except the dropped ones states the
            // intent more plainly than an index dance. Order is preserved.
            rfl::Generic::Object kept;
            int                  dropped = 0;
            bool                 typed   = false;

            for ( const auto& [key, value] : fields.value() )
            {
                const bool isRemoved = std::any_of( std::begin( kRemovedKeys ), std::end( kRemovedKeys ),
                                                    [&key]( const char* removed ) { return key == removed; } );
                if ( isRemoved )
                {
                    ++dropped;
                    continue;
                }

                if ( key == kTypeKey )
                {
                    ++dropped;

                    const auto scalar = value.to_double();
                    if ( !scalar.has_value() )
                    {
                        // A CloudType that is not a number tells us nothing about what the author wanted,
                        // and the C++ default is a species in its own right. Said out loud, because a
                        // value we drop silently is a value nobody will ever find again.
                        LOG_WARN( "[SceneMigration] entity '{0}': CloudType is {1}, not a number - the layer "
                                  "keeps the default species",
                                  entity.Tag.value_or( "Entity" ), Describe( value ) );
                        continue;
                    }

                    // The library is ordered from the flattest species to the tallest, which is the axis
                    // the old scalar ran along, so the quarters below are a translation rather than a
                    // guess. 0.6 - the component's own former default - lands on cumulus congestus, which
                    // the default of the version after this one also names.
                    //
                    // The integers are indices into kSpeciesOrder below, which is the ONE statement of that
                    // order left in this file; the enumerator they used to name was deleted when the
                    // species became an asset, and this migration deliberately still writes the OLD key,
                    // because the v4 -> v5 step is what turns it into a handle. Chaining like that is the
                    // whole reason each step is gated on its own version integer.
                    const double type = scalar.value();
                    int          species;
                    if ( type < 0.25 )
                        species = 0; // Stratus
                    else if ( type < 0.55 )
                        species = 1; // Cumulus mediocris
                    else if ( type < 0.85 )
                        species = 2; // Cumulus congestus
                    else
                        species = 3; // Cumulonimbus

                    kept[kSpeciesKey] = static_cast<int64_t>( species );
                    typed             = true;
                    continue;
                }

                kept[key] = value;
            }

            if ( dropped == 0 )
                continue; // already raised, or authored after the move - leave the tree byte-identical

            entity.Components["VolumetricCloud"] = rfl::Generic( std::move( kept ) );
            report.Entities += 1;
            report.FieldsDropped += dropped;
            report.SpeciesSet += typed ? 1 : 0;
        }

        return report;
    }

    CloudTypeMigrationReport MigrateCloudTypeV4ToV5( std::vector<Assets::EntityData>& entities )
    {
        static constexpr const char* kSpeciesKey = "Species";
        static constexpr const char* kNoiseKey   = "NoiseVolume";
        static constexpr const char* kTypeKey    = "CloudType";

        // THE FOUR KINDS T0 COMPILED IN, IN ITS ORDER, and the only place that order is written down now
        // that the enumerator is gone. The integer a v4 file carries in "Species" indexes this array, and
        // each name is the stem of a shipped `.decloudtype` carrying the same twelve numbers T0 compiled
        // in. Two statements of one library, and Desert/Tests/Engine/CloudType is what keeps them equal —
        // it opens each file this array names and compares it against what T0 shipped.
        static constexpr const char* kSpeciesOrder[] = {
             Assets::kCloudTypeStratus,
             Assets::kCloudTypeCumulusMediocris,
             Assets::kCloudTypeCumulusCongestus,
             Assets::kCloudTypeCumulonimbus,
        };
        constexpr int kSpeciesCount = static_cast<int>( std::size( kSpeciesOrder ) );

        CloudTypeMigrationReport report;

        for ( auto& entity : entities )
        {
            const auto clouds = entity.Components.get( "VolumetricCloud" );
            if ( !clouds.has_value() )
                continue;

            const auto fields = clouds.value().to_object();
            if ( !fields.has_value() )
            {
                LOG_WARN( "[SceneMigration] entity '{0}': the VolumetricCloud payload is {1}, not an object - "
                          "its species could not be turned into a cloud type asset",
                          entity.Tag.value_or( "Entity" ), Describe( clouds.value() ) );
                continue;
            }

            // Rebuilt rather than erased in place, like the two migrations above it: rfl::Object is an
            // ordered vector of pairs with no erase, and copying every key except the ones that move states
            // the intent more plainly than an index dance. Order is preserved.
            rfl::Generic::Object kept;
            bool                 touched = false;

            for ( const auto& [key, value] : fields.value() )
            {
                if ( key == kNoiseKey )
                {
                    touched = true;

                    // NAMED, NOT SWALLOWED. The slot moved onto the cloud type and a pure function cannot
                    // create the file it would have to move it into, so the artist is told exactly which
                    // layer lost which volume and where to put it back. A value dropped without a message
                    // is a value nobody will ever find again (§1.4).
                    const auto handle = value.to_int();
                    if ( handle.has_value() && handle.value() != 0 )
                    {
                        report.VolumesLost += 1;
                        LOG_WARN( "[SceneMigration] entity '{0}': the layer named noise volume {1}, which "
                                  "is now a field of the CLOUD TYPE rather than of the layer. Open the "
                                  "type in Window > Cloud Type and point its Noise Volume at that .dcnv; "
                                  "until then the layer uses the built-in default volume.",
                                  entity.Tag.value_or( "Entity" ), handle.value() );
                    }
                    continue;
                }

                if ( key == kSpeciesKey )
                {
                    touched = true;

                    const auto index = value.to_int();
                    if ( !index.has_value() )
                    {
                        // A species that is not an integer says nothing about what the author wanted, and
                        // the empty slot is a kind of cloud in its own right - the built-in congestus.
                        report.FieldsBroken += 1;
                        LOG_WARN( "[SceneMigration] entity '{0}': Species is {1}, not an integer - the "
                                  "layer keeps the built-in default cloud type",
                                  entity.Tag.value_or( "Entity" ), Describe( value ) );
                        continue;
                    }

                    const int64_t species = index.value();
                    if ( species < 0 || species >= kSpeciesCount )
                    {
                        // A hand-edited file can carry any integer. T0's own reader clamped this to the
                        // first species; here it becomes the EMPTY slot instead, because "the value is not
                        // one of the four" and "the author chose stratus" are different statements and only
                        // the first one is true.
                        report.FieldsBroken += 1;
                        LOG_WARN( "[SceneMigration] entity '{0}': Species is {1}, which is not one of the {2} "
                                  "kinds this file could name - the layer keeps the built-in default cloud "
                                  "type",
                                  entity.Tag.value_or( "Entity" ), species, kSpeciesCount );
                        continue;
                    }

                    // A PATH AND NOT A HANDLE, because that is what a reflected asset field is written as
                    // in this engine: Core::MakeAssetResolver turns every one of them into a path on save
                    // and back into a handle on load, so a scene carrying a raw integer here would be read
                    // through the resolver's string branch, fail it, and land on the empty slot. It is
                    // relative to the assets root for the reason CloudTypeAssetRelativePath states — a
                    // pure function cannot know where the project is installed, and the library ships
                    // inside it.
                    kept[kTypeKey] = Assets::CloudTypeAssetRelativePath( kSpeciesOrder[species] );
                    report.TypesSet += 1;
                    continue;
                }

                kept[key] = value;
            }

            if ( !touched )
                continue; // already raised, or authored after the move - leave the tree byte-identical

            entity.Components["VolumetricCloud"] = rfl::Generic( std::move( kept ) );
            report.Entities += 1;
        }

        return report;
    }

    CloudSetMigrationReport MigrateCloudSetV5ToV6( std::vector<Assets::EntityData>& entities )
    {
        static constexpr const char* kTypeKey = "CloudType";
        static constexpr const char* kSlotKey = "CloudType1";

        CloudSetMigrationReport report;

        for ( auto& entity : entities )
        {
            const auto clouds = entity.Components.get( "VolumetricCloud" );
            if ( !clouds.has_value() )
                continue;

            const auto fields = clouds.value().to_object();
            if ( !fields.has_value() )
            {
                LOG_WARN( "[SceneMigration] entity '{0}': the VolumetricCloud payload is {1}, not an object - "
                          "its cloud type could not be moved into the first slot of the set",
                          entity.Tag.value_or( "Entity" ), Describe( clouds.value() ) );
                continue;
            }

            // Rebuilt rather than renamed in place, like every migration above it: rfl::Object is an
            // ordered vector of pairs with no rename, and copying every key while changing one name states
            // the intent more plainly than an index dance. Order is preserved, so the slot lands exactly
            // where the single type used to be.
            rfl::Generic::Object kept;
            bool                 touched = false;

            for ( const auto& [key, value] : fields.value() )
            {
                if ( key != kTypeKey )
                {
                    kept[key] = value;
                    continue;
                }

                touched = true;
                report.SlotsCarried += 1;

                // NOT INSPECTED BEYOND THIS. Whatever the value is — a path to a `.decloudtype`, the empty
                // handle, or something a hand-edit put there — it meant "the kind of cloud this layer is
                // made of" and it still does; the key it lives under is the only thing that changed. A
                // migration that also validated would be answering a question the loader answers next, and
                // answering it twice is how two readers of one field end up disagreeing.
                const auto text = value.to_string();
                if ( ( text.has_value() && text.value().empty() ) ||
                     ( value.to_int().has_value() && value.to_int().value() == 0 ) )
                    report.SlotsEmpty += 1;

                kept[kSlotKey] = value;
            }

            if ( !touched )
                continue; // already raised, or authored after the move - leave the tree byte-identical

            entity.Components["VolumetricCloud"] = rfl::Generic( std::move( kept ) );
            report.Entities += 1;
        }

        return report;
    }

    TerrainMaterialMigrationReport MigrateTerrainMaterialV6ToV7( std::vector<Assets::EntityData>& entities )
    {
        static constexpr const char* kTerrainKey  = "Terrain";
        static constexpr const char* kMaterialKey = "Material";

        TerrainMaterialMigrationReport report;

        for ( auto& entity : entities )
        {
            // BOTH keys, and that pairing is the whole gate. A "Material" on an entity with no terrain is
            // the runtime/script override channel and stays exactly where it is; only the terrain's copy of
            // it was ever an AUTHORING surface, and only the terrain's copy goes.
            if ( !entity.Components.get( kTerrainKey ).has_value() )
                continue;

            const auto material = entity.Components.get( kMaterialKey );
            if ( !material.has_value() )
                continue; // already raised, or a terrain that never had one - leave the tree byte-identical

            // Counted before it is dropped. A malformed payload (a number, a string, a hand-edit) still
            // COUNTS as a removal and is still reported: the entity carried something under that key, and
            // saying "nothing was there" because it could not be read would be the quiet substitution this
            // whole clause exists to forbid.
            const auto fields = material.value().to_object();
            if ( !fields.has_value() )
            {
                LOG_WARN( "[SceneMigration] entity '{0}': the terrain's Material payload is {1}, not an "
                          "object - it is removed with the authoring path it belonged to, and nothing "
                          "could be read out of it to name here",
                          entity.Tag.value_or( "Entity" ), Describe( material.value() ) );
            }
            else
            {
                for ( const auto& [key, value] : fields.value() )
                {
                    // The two vectors of values. "ShaderName" is deliberately not among them: the new model
                    // has exactly one Terrain-domain program and a terrain material is created on it, so the
                    // name was never a value anybody has to re-author.
                    if ( key != "Params" && key != "Textures" )
                        continue;

                    const auto rows = value.to_array();
                    if ( !rows.has_value() )
                    {
                        LOG_WARN( "[SceneMigration] entity '{0}': the terrain's Material.{1} is {2}, not an "
                                  "array - it is removed and its contents cannot be named",
                                  entity.Tag.value_or( "Entity" ), key, Describe( value ) );
                        continue;
                    }

                    for ( const auto& row : rows.value() )
                    {
                        // The name is what makes this reportable at all, so a row without one is still
                        // counted and reported under a placeholder rather than skipped.
                        std::string name = "<unnamed>";
                        if ( const auto rowFields = row.to_object(); rowFields.has_value() )
                        {
                            if ( const auto named = rowFields.value().get( "Name" ); named.has_value() )
                            {
                                if ( const auto text = named.value().to_string(); text.has_value() )
                                    name = text.value();
                            }
                        }

                        if ( key == "Params" )
                            report.Params += 1;
                        else
                            report.Textures += 1;
                        report.DroppedNames.push_back( std::move( name ) );
                    }
                }
            }

            // Rebuilt rather than erased, exactly like the payload migrations above: rfl::Object is an
            // ordered vector of pairs with no erase, so every OTHER component is copied across in order and
            // the one being retired is simply not.
            rfl::ExtraFields<rfl::Generic> kept;
            for ( const auto& [key, value] : entity.Components )
            {
                if ( key != kMaterialKey )
                    kept[key] = value;
            }
            entity.Components = std::move( kept );
            report.Entities += 1;
        }

        return report;
    }

    namespace
    {
        // The components that can name a material, and the key each names it under. `MaterialPaths` is a
        // LIST (one per mesh slot); `Terrain.Material` is a single string. Kept as one table so a fifth
        // component that gains a material slot is one line here rather than a fourth copy of the loop.
        struct MaterialPathSite
        {
            const char* Component;
            const char* Key;
            bool        IsList;
        };

        constexpr MaterialPathSite kMaterialPathSites[] = {
             { "StaticMesh", "MaterialPaths", true },
             { "InstancedStaticMesh", "MaterialPaths", true },
             { "SkinnedMesh", "MaterialPaths", true },
             { "Terrain", "Material", false },
        };

        // The path `stored` names, expressed relative to `assetsRoot`, or nullopt when it already is (or
        // lies outside the root, or is empty).
        //
        // Lexical on purpose - see the header. The root's own components are matched as a contiguous run
        // inside the stored path and the LAST match wins, so the answer does not depend on whether the
        // root arrived spelled relatively or absolutely.
        std::optional<std::string> RelativeToAssetsRoot( const std::string&           stored,
                                                         const std::filesystem::path& assetsRoot )
        {
            if ( stored.empty() )
                return std::nullopt; // "no material in this slot" - not a path to rewrite

            std::vector<std::string> rootParts;
            for ( const auto& part : assetsRoot.lexically_normal() )
            {
                // A trailing separator makes the last component an empty string ("Resources/Assets/" ->
                // {"Resources","Assets",""}), and matching on it would match everywhere.
                if ( !part.empty() && part != "." )
                    rootParts.push_back( part.generic_string() );
            }
            if ( rootParts.empty() )
                return std::nullopt;

            std::vector<std::string> pathParts;
            for ( const auto& part : std::filesystem::path( stored ).lexically_normal() )
                pathParts.push_back( part.generic_string() );

            if ( pathParts.size() <= rootParts.size() )
                return std::nullopt;

            std::size_t bestEnd = 0; // one past the last component of the best match, 0 = no match
            for ( std::size_t start = 0; start + rootParts.size() < pathParts.size(); ++start )
            {
                if ( std::equal( rootParts.begin(), rootParts.end(), pathParts.begin() + start ) )
                    bestEnd = start + rootParts.size();
            }
            if ( bestEnd == 0 )
                return std::nullopt; // not under this root - the caller reports it rather than guessing

            std::string relative;
            for ( std::size_t i = bestEnd; i < pathParts.size(); ++i )
            {
                if ( !relative.empty() )
                    relative += '/';
                relative += pathParts[i];
            }
            if ( relative.empty() || relative == stored )
                return std::nullopt;
            return relative;
        }
    } // namespace

    MaterialPathMigrationReport MigrateMaterialPathV7ToV8( std::vector<Assets::EntityData>& entities,
                                                           const std::filesystem::path&     assetsRoot )
    {
        MaterialPathMigrationReport report;

        for ( auto& entity : entities )
        {
            const std::string tag     = entity.Tag.value_or( "Entity" );
            bool              touched = false;

            for ( const auto& site : kMaterialPathSites )
            {
                const auto payload = entity.Components.get( site.Component );
                if ( !payload.has_value() )
                    continue;

                const auto fields = payload.value().to_object();
                if ( !fields.has_value() )
                {
                    LOG_WARN( "[SceneMigration] entity '{0}': the {1} payload is {2}, not an object - the "
                              "material path(s) in it could not be made relative and stay as they are",
                              tag, site.Component, Describe( payload.value() ) );
                    continue;
                }

                const auto named = fields.value().get( site.Key );
                if ( !named.has_value() )
                    continue; // this component names no material - nothing to do, tree untouched

                // Rebuilt rather than assigned into: rfl::Object is an ordered vector of pairs, and copying
                // every key while replacing one value is what every migration above this one does. Order is
                // preserved, so a scene that changes nothing round-trips byte-identically.
                rfl::Generic::Object kept;
                bool                 rewroteHere = false;

                for ( const auto& [key, value] : fields.value() )
                {
                    if ( key != site.Key )
                    {
                        kept[key] = value;
                        continue;
                    }

                    if ( !site.IsList )
                    {
                        const auto text = value.to_string();
                        if ( !text.has_value() )
                        {
                            LOG_WARN( "[SceneMigration] entity '{0}': {1}.{2} is {3}, not a string - it is "
                                      "left exactly as it is and still names no material relative to the "
                                      "assets root",
                                      tag, site.Component, site.Key, Describe( value ) );
                            kept[key] = value;
                            continue;
                        }

                        if ( const auto rel = RelativeToAssetsRoot( text.value(), assetsRoot ) )
                        {
                            kept[key] = *rel;
                            report.Paths += 1;
                            rewroteHere = true;
                        }
                        else
                        {
                            if ( std::filesystem::path( text.value() ).is_absolute() )
                                report.OutsideNames.push_back( tag + " > " + site.Component + "." + site.Key +
                                                               " = " + text.value() );
                            kept[key] = value;
                        }
                        continue;
                    }

                    const auto rows = value.to_array();
                    if ( !rows.has_value() )
                    {
                        LOG_WARN( "[SceneMigration] entity '{0}': {1}.{2} is {3}, not an array - the slot "
                                  "paths in it are left exactly as they are",
                                  tag, site.Component, site.Key, Describe( value ) );
                        kept[key] = value;
                        continue;
                    }

                    rfl::Generic::Array slots;
                    for ( const auto& row : rows.value() )
                    {
                        const auto text = row.to_string();
                        if ( !text.has_value() )
                        {
                            LOG_WARN( "[SceneMigration] entity '{0}': a slot of {1}.{2} is {3}, not a "
                                      "string - it is left exactly as it is",
                                      tag, site.Component, site.Key, Describe( row ) );
                            slots.push_back( row );
                            continue;
                        }

                        if ( const auto rel = RelativeToAssetsRoot( text.value(), assetsRoot ) )
                        {
                            slots.push_back( rfl::Generic( *rel ) );
                            report.Paths += 1;
                            rewroteHere = true;
                            continue;
                        }

                        if ( std::filesystem::path( text.value() ).is_absolute() )
                            report.OutsideNames.push_back( tag + " > " + site.Component + "." + site.Key + " = " +
                                                           text.value() );
                        slots.push_back( row );
                    }
                    kept[key] = std::move( slots );
                }

                if ( !rewroteHere )
                    continue; // already relative, or nothing usable - leave the tree byte-identical

                entity.Components[site.Component] = rfl::Generic( std::move( kept ) );
                touched                           = true;
            }

            if ( touched )
                report.Entities += 1;
        }

        return report;
    }

    SceneMigrationReport MigrateScene( SceneSerialized& scene, const std::filesystem::path& assetsRoot )
    {
        SceneMigrationReport report;

        if ( scene.SceneVersion.value_or( 0 ) < kSceneVersionSky )
        {
            report.SkyRaised = true;
            report.Sky       = MigrateSkyV0ToV1( scene.Entities );
        }

        if ( scene.UnitVersion.value_or( 0 ) < kUnitVersion )
        {
            report.UnitsRaised = true;
            report.Units       = MigrateMetresToUnits( scene.Entities, scene.Settings );
        }

        if ( scene.SceneVersion.value_or( 0 ) < kSceneVersionTonemap )
        {
            report.TonemapperRaised = true;
            report.Tonemap          = MigrateTonemapperV1ToV2( scene.Settings );
        }

        if ( scene.SceneVersion.value_or( 0 ) < kSceneVersionCloudNoise )
        {
            report.CloudNoiseRaised = true;
            report.CloudNoise       = MigrateCloudNoiseV2ToV3( scene.Entities );
        }

        if ( scene.SceneVersion.value_or( 0 ) < kSceneVersionCloudSpecies )
        {
            report.CloudSpeciesRaised = true;
            report.CloudSpecies       = MigrateCloudSpeciesV3ToV4( scene.Entities );
        }

        // AFTER the step above and not beside it: v3 -> v4 WRITES the "Species" key that this one reads.
        // The two are the only pair in this function with an order that matters, and it is stated here
        // rather than left to the sequence they happen to be written in.
        if ( scene.SceneVersion.value_or( 0 ) < kSceneVersionCloudType )
        {
            report.CloudTypeRaised = true;
            report.CloudType       = MigrateCloudTypeV4ToV5( scene.Entities );
        }

        // AFTER the step above for the same reason that one follows its own: v4 -> v5 WRITES the
        // "CloudType" key that this one renames. Three of the six steps in this function now form one
        // chain — Species integer, then CloudType path, then CloudType1 slot — and each is gated on its own
        // version integer precisely so that a file entering at any point along it comes out at the end.
        if ( scene.SceneVersion.value_or( 0 ) < kSceneVersionCloudSet )
        {
            report.CloudSetRaised = true;
            report.CloudSet       = MigrateCloudSetV5ToV6( scene.Entities );
        }

        // Independent of the cloud chain above it and of everything else in this function: no terrain field
        // is a length the unit migration scales, and no sky or cloud step reads either of the two keys this
        // one pairs. It sits last because it is newest, not because anything requires it to.
        if ( scene.SceneVersion.value_or( 0 ) < kSceneVersionTerrainMaterial )
        {
            report.TerrainMaterialRaised = true;
            report.TerrainMaterial       = MigrateTerrainMaterialV6ToV7( scene.Entities );
        }

        // AFTER the step above, and this pair's order does matter: v6 -> v7 REMOVES the inline Material
        // component from terrain entities, and this step reads `Terrain.Material` - a different key on a
        // different payload, but running it first would rewrite paths inside a component that is about to
        // be deleted and report work that did not survive.
        if ( scene.SceneVersion.value_or( 0 ) < kSceneVersionMaterialPath )
        {
            report.MaterialPathRaised = true;
            report.MaterialPath       = MigrateMaterialPathV7ToV8( scene.Entities, assetsRoot );
        }

        // Stamped whether or not anything moved: an empty scene at version 0 is still a scene at version 0,
        // and leaving it unstamped is how every load ends up re-running a migration that already happened.
        scene.SceneVersion = kSceneVersion;
        scene.UnitVersion  = kUnitVersion;

        return report;
    }

} // namespace Desert::Core
