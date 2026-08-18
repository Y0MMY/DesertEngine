#pragma once

#include <Engine/ECS/SkyAtmosphereComponent.hpp>

namespace Desert::Graphic
{
    // Sky palettes: one constexpr table of VALUES, in this one file. Adding a preset is one enumerator on
    // ECS::SkyPreset plus one row here.
    //
    // SkyPresetValues carries EXACTLY the thirteen palette fields. Everything else on the component -
    // the time-of-day block, the environment-bake knobs, the planet radius and ActivePreset itself - is
    // structurally out of reach of a preset rather than merely left alone by convention. That is what
    // makes "applying a preset does not touch your quality settings or your time of day" a property of
    // the type instead of a promise in a comment.
    //
    // ActivePreset is deliberately NOT written by ApplySkyPreset: the caller records which preset it
    // applied. A function that stamped its own name into the data could never be asserted against
    // "the authored fields are unchanged", which is the whole point of the split.

    // The palette field set, written ONCE - the value struct, the read and the write below all derive
    // from it, so a field cannot be in the struct and forgotten by the apply loop.
#define DESERT_SKY_PRESET_FIELDS( X )                                                                             \
    X( float, SkyBrightness )                                                                                     \
    X( float, HorizonFalloff )                                                                                    \
    X( glm::vec3, ZenithColor )                                                                                   \
    X( glm::vec3, HorizonColor )                                                                                  \
    X( glm::vec3, GroundColor )                                                                                   \
    X( glm::vec3, NightColor )                                                                                    \
    X( float, SunIntensity )                                                                                      \
    X( glm::vec3, SunColor )                                                                                      \
    X( float, SunAngularDiameter )                                                                                \
    X( float, SunGlow )                                                                                           \
    X( glm::vec3, SunsetColor )                                                                                   \
    X( float, SunsetIntensity )                                                                                   \
    X( float, StarIntensity )

    // The thirteen fields a sky preset drives.
    struct SkyPresetValues
    {
#define DESERT_SKY_PRESET_DECLARE( Type, Name ) Type Name{};
        DESERT_SKY_PRESET_FIELDS( DESERT_SKY_PRESET_DECLARE )
#undef DESERT_SKY_PRESET_DECLARE

        bool operator==( const SkyPresetValues& ) const = default;
    };

    struct SkyPresetEntry
    {
        ECS::SkyPreset  Id;
        const char*     Name;
        SkyPresetValues Values;
    };

    // One row per enumerator of ECS::SkyPreset except Custom, which is the absence of a preset and so has
    // no values. Colours are LINEAR RGB, matching the component.
    inline constexpr SkyPresetEntry kSkyPresets[] = {
         // The component's own defaults. Kept identical so a freshly added Sky Atmosphere is a palette
         // with a name rather than an anonymous one.
         { ECS::SkyPreset::ClearNoon, "Clear Noon",
           SkyPresetValues{
                .SkyBrightness      = 1.0f,
                .HorizonFalloff     = 0.85f,
                .ZenithColor        = { 0.08f, 0.26f, 0.70f },
                .HorizonColor       = { 0.50f, 0.66f, 0.92f },
                .GroundColor        = { 0.16f, 0.19f, 0.24f },
                .NightColor         = { 0.010f, 0.020f, 0.050f },
                .SunIntensity       = 22.0f,
                .SunColor           = { 1.00f, 0.96f, 0.88f },
                .SunAngularDiameter = 2.2918f,
                .SunGlow            = 1.0f,
                .SunsetColor        = { 1.00f, 0.42f, 0.18f },
                .SunsetIntensity    = 1.0f,
                .StarIntensity      = 1.0f,
           } },
         // Low sun: the disk is dimmer because its light crosses far more atmosphere, while the halo and
         // the horizon reddening are what carry the look.
         { ECS::SkyPreset::GoldenHour, "Golden Hour",
           SkyPresetValues{
                .SkyBrightness      = 1.10f,
                .HorizonFalloff     = 1.30f,
                .ZenithColor        = { 0.05f, 0.15f, 0.42f },
                .HorizonColor       = { 0.85f, 0.52f, 0.28f },
                .GroundColor        = { 0.18f, 0.14f, 0.12f },
                .NightColor         = { 0.010f, 0.020f, 0.050f },
                .SunIntensity       = 14.0f,
                .SunColor           = { 1.00f, 0.72f, 0.42f },
                .SunAngularDiameter = 2.2918f,
                .SunGlow            = 2.20f,
                .SunsetColor        = { 1.00f, 0.35f, 0.12f },
                .SunsetIntensity    = 2.40f,
                .StarIntensity      = 0.0f,
           } },
         // A flat grey dome: the sun is a diffuse smear rather than a disk, so its angular size is widened
         // and its glow nearly removed instead of pretending an overcast deck exists in the sky shader.
         { ECS::SkyPreset::OvercastGrey, "Overcast Grey",
           SkyPresetValues{
                .SkyBrightness      = 0.75f,
                .HorizonFalloff     = 0.45f,
                .ZenithColor        = { 0.32f, 0.34f, 0.37f },
                .HorizonColor       = { 0.46f, 0.47f, 0.50f },
                .GroundColor        = { 0.20f, 0.21f, 0.22f },
                .NightColor         = { 0.010f, 0.012f, 0.015f },
                .SunIntensity       = 3.0f,
                .SunColor           = { 0.85f, 0.86f, 0.88f },
                .SunAngularDiameter = 4.0f,
                .SunGlow            = 0.30f,
                .SunsetColor        = { 0.55f, 0.48f, 0.44f },
                .SunsetIntensity    = 0.25f,
                .StarIntensity      = 0.0f,
           } },
         // Moonlit rather than pitch black: the "sun" is the moon, which is why its colour is cool and its
         // intensity sits at the bottom of the field's range.
         { ECS::SkyPreset::Night, "Night",
           SkyPresetValues{
                .SkyBrightness      = 0.35f,
                .HorizonFalloff     = 0.70f,
                .ZenithColor        = { 0.010f, 0.020f, 0.060f },
                .HorizonColor       = { 0.030f, 0.045f, 0.085f },
                .GroundColor        = { 0.012f, 0.014f, 0.020f },
                .NightColor         = { 0.008f, 0.016f, 0.040f },
                .SunIntensity       = 1.0f,
                .SunColor           = { 0.62f, 0.70f, 0.90f },
                .SunAngularDiameter = 2.0f,
                .SunGlow            = 0.40f,
                .SunsetColor        = { 0.10f, 0.12f, 0.22f },
                .SunsetIntensity    = 0.15f,
                .StarIntensity      = 3.50f,
           } },
         // The asset-preview backdrop: dark, cohesive and deliberately uninteresting, so nothing in the
         // dome competes with the mesh being inspected. These are the numbers PreviewViewport used to
         // hand-author; it now applies this preset, so there is one copy of them instead of two.
         { ECS::SkyPreset::StudioNeutral, "Studio Neutral",
           SkyPresetValues{
                .SkyBrightness      = 1.0f,
                .HorizonFalloff     = 0.60f,
                .ZenithColor        = { 0.10f, 0.13f, 0.19f },
                .HorizonColor       = { 0.22f, 0.25f, 0.31f },
                .GroundColor        = { 0.13f, 0.14f, 0.17f },
                .NightColor         = { 0.010f, 0.020f, 0.050f },
                .SunIntensity       = 10.0f,
                .SunColor           = { 1.00f, 0.95f, 0.85f },
                .SunAngularDiameter = 2.2918f,
                .SunGlow            = 0.50f,
                .SunsetColor        = { 1.00f, 0.42f, 0.18f },
                .SunsetIntensity    = 1.0f,
                .StarIntensity      = 0.0f,
           } },
    };

    // The preset row for @p id, or nullptr for Custom (and for any enumerator added without a row - the
    // completeness test turns that into a failing test rather than a menu entry that quietly does
    // nothing).
    inline constexpr const SkyPresetEntry* FindSkyPreset( ECS::SkyPreset id )
    {
        for ( const SkyPresetEntry& entry : kSkyPresets )
            if ( entry.Id == id )
                return &entry;
        return nullptr;
    }

    // The palette half of a component, lifted out. This is the value the editor compares before and after
    // drawing the Details block to decide whether a PALETTE field was edited - the check that keeps
    // moving the time of day or the bake resolution from clearing the preset name.
    //
    // It is a lossless copy rather than the 64-bit hash the requirement suggested: two different palettes
    // can share a hash, and the consequence of that collision is precisely the failure the mechanism
    // exists to prevent - a preset name that keeps claiming "Clear Noon" after the zenith went purple.
    // Comparing 52 bytes with == is also cheaper than hashing them.
    inline SkyPresetValues ExtractSkyPresetValues( const ECS::SkyAtmosphereData& data )
    {
        SkyPresetValues values;
#define DESERT_SKY_PRESET_READ( Type, Name ) values.Name = data.Name;
        DESERT_SKY_PRESET_FIELDS( DESERT_SKY_PRESET_READ )
#undef DESERT_SKY_PRESET_READ
        return values;
    }

    // Overwrites the thirteen palette fields of @p data with the preset's. Pure: no logging, no GPU, no
    // globals, and no write to ActivePreset. Custom applies nothing - it is a label for hand-authored
    // values, not a set of them.
    inline void ApplySkyPreset( ECS::SkyPreset id, ECS::SkyAtmosphereData& data )
    {
        const SkyPresetEntry* entry = FindSkyPreset( id );
        if ( !entry )
            return;

#define DESERT_SKY_PRESET_WRITE( Type, Name ) data.Name = entry->Values.Name;
        DESERT_SKY_PRESET_FIELDS( DESERT_SKY_PRESET_WRITE )
#undef DESERT_SKY_PRESET_WRITE
    }

    // Which preset this palette IS, or Custom when it is none of them. Only palette fields are consulted,
    // so the time of day and the bake settings never affect the answer.
    inline ECS::SkyPreset MatchSkyPreset( const ECS::SkyAtmosphereData& data )
    {
        const SkyPresetValues values = ExtractSkyPresetValues( data );
        for ( const SkyPresetEntry& entry : kSkyPresets )
            if ( entry.Values == values )
                return entry.Id;
        return ECS::SkyPreset::Custom;
    }

    // Display name for a combo box. Custom has no row, so it is named here and nowhere else.
    inline const char* SkyPresetName( ECS::SkyPreset id )
    {
        const SkyPresetEntry* entry = FindSkyPreset( id );
        return entry ? entry->Name : "Custom";
    }
} // namespace Desert::Graphic
