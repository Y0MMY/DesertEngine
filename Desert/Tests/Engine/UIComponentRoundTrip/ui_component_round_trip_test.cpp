// DOES A UI COMPONENT SURVIVE BEING WRITTEN TO A `.desce` AND READ BACK?
//
// Nothing asked that until now. The UI components are serialized generically — ComponentRegistry's
// MakeReflected walks the reflected field table — so there was no per-component serializer for anyone
// to test, and the generic walker was only ever exercised on a synthetic three-field struct
// (Desert/Tests/Engine/ReflectionSerializer). The result was three defects living together in the one
// path a canvas background travels, all of them shipped, and one of them the reason the background
// looked like a dead setting: it could not be authored so that it survived a save.
//
// This suite is that missing round trip, on the REAL reflected types out of Reflection.gen.cpp, and it
// goes through JSON TEXT rather than staying in rfl::Generic. The text is not decoration: the handle
// corruption that the third defect was about happened in the conversion between the tree and a number,
// and a test that never leaves the tree cannot see it.
//
// The asset resolver is a stub. That is the point of the seam — MakeAssetResolver reaches the
// ResourceRegistry and through it the whole renderer, so no suite can build it; what a scene actually
// needs from it is a string in and a handle out, and the two stubs here are exactly that contract.

#include <gtest/gtest.h>

#include <Engine/ECS/Components.hpp>
#include <Engine/Reflection/ReflectionRegistry.hpp>
#include <Engine/Reflection/ReflectionSerializer.hpp>

#include <rflcpp/rfl/json.hpp>

#include <cstdint>
#include <string>

using Desert::Reflection::AssetResolver;
using Desert::Reflection::DeserializeReflected;
using Desert::Reflection::ReflectionRegistry;
using Desert::Reflection::SerializeReflected;
using Desert::Reflection::TypeInfo;

namespace ECS = Desert::ECS;

namespace
{
    // The handle the third defect was measured on: a texture id is a 64-bit FNV-1a of the asset's
    // project-relative path, so it lands above 2^53 essentially always. Through a double it came back
    // as 5355760296319879168 — 328 out.
    constexpr uint64_t kMeasuredHandle = 5355760296319878840ull;

    // The first integer a double cannot count past, and the first one it gets wrong. 2^53 itself is
    // exactly representable; 2^53 + 1 is not, and rounds to 2^53.
    constexpr uint64_t kDoubleExactLimit  = 1ull << 53; // 9 007 199 254 740 992
    constexpr uint64_t kFirstLostByDouble = ( 1ull << 53 ) + 1ull;

    // A handle above 2^63, which the path hash produces about half the time. It has to survive the
    // int64 the file carries being REINTERPRETED rather than converted.
    constexpr uint64_t kAboveInt64 = 0xF0E1D2C3B4A59687ull;

    const TypeInfo& Type( const std::string& name )
    {
        const TypeInfo* type = ReflectionRegistry::Get().Find( name );
        EXPECT_NE( type, nullptr ) << "the reflected type '" << name
                                   << "' is not registered, so no scene can carry it";
        return *type;
    }

    // What a `.desce` actually holds between the two halves of the trip: JSON text.
    rfl::Generic::Object ThroughJsonText( const rfl::Generic::Object& written )
    {
        const std::string text   = rfl::json::write( rfl::Generic( written ) );
        const auto        parsed = rfl::json::read<rfl::Generic>( text );
        EXPECT_TRUE( parsed.has_value() ) << "what the serializer wrote is not valid JSON: " << text;
        if ( !parsed.has_value() )
            return {};
        const auto object = parsed.value().to_object();
        EXPECT_TRUE( object.has_value() );
        return object.has_value() ? object.value() : rfl::Generic::Object{};
    }

    // THE ONE PLACE A TEXTURE REFERENCE BECOMES A STRING AND BACK, stubbed. The real resolver stores
    // the root-tagged stable key (`cooked:Textures/T.tex`), which is the form that survives being
    // carried to another machine; this stub stands in for the registry lookup with a fixed pair, so
    // the suite asserts the SHAPE of the round trip rather than re-testing TextureSlotRoundTrip.
    constexpr uint64_t kResolvedHandle = kMeasuredHandle;
    const std::string  kResolvedKey    = "cooked:Textures/T_Checker.tex";

    // Only the texture type is mapped: UIPanelData also declares a `Video` slot (VideoAsset), and a
    // resolver that answered every type alike would let a sprite pass on a video's branch.
    AssetResolver KeyResolver()
    {
        AssetResolver r;
        r.ToPath = []( uint64_t handle, const std::string& type ) -> std::string
        { return ( type == "TextureAsset" && handle == kResolvedHandle ) ? kResolvedKey : std::string(); };
        r.FromPath = []( const std::string& key, const std::string& type ) -> uint64_t
        { return ( type == "TextureAsset" && key == kResolvedKey ) ? kResolvedHandle : 0ull; };
        return r;
    }
} // namespace

// --- (1) The canvas background, end to end -------------------------------------------------------
//
// The setting the whole task is about. A background sprite set in Details must come back as the same
// texture after the scene is written and reopened, and it must be written as a form that names a place
// in the PROJECT rather than on the machine that saved it.
TEST( UIComponentRoundTrip, ACanvasBackgroundSurvivesTheTripAndIsStoredByProjectRelativeKey )
{
    ECS::UICanvasData written;
    written.Sprite = Desert::Assets::AssetHandle( kResolvedHandle );

    const AssetResolver resolver = KeyResolver();
    const auto          object   = SerializeReflected( Type( "UICanvasData" ), &written, &resolver );

    const auto stored = object.get( "Sprite" );
    ASSERT_TRUE( stored.has_value() ) << "the canvas wrote no Sprite field at all";
    const auto asString = stored.value().to_string();
    ASSERT_TRUE( asString.has_value() ) << "the sprite was written as something other than a reference "
                                           "string — a raw id names nothing after a restart";
    EXPECT_EQ( asString.value(), kResolvedKey );
    EXPECT_NE( asString.value().find( ':' ), std::string::npos )
         << "the stored form carries no root tag, so it is a bare path and the reader cannot tell which "
            "of the project's roots it is relative to";
    EXPECT_NE( asString.value().front(), '/' )
         << "the stored form is an absolute path, i.e. a directory that exists on one machine only";

    ECS::UICanvasData read;
    DeserializeReflected( Type( "UICanvasData" ), &read, ThroughJsonText( object ), &resolver );

    EXPECT_EQ( static_cast<uint64_t>( read.Sprite ), kResolvedHandle )
         << "the canvas background did not come back: the setting is dead in the only way that matters, "
            "which is that it cannot be authored so that it survives a save";
}

// --- (2) The rest of the canvas, so the fix is not one field wide --------------------------------
TEST( UIComponentRoundTrip, EveryAuthoredCanvasFieldComesBack )
{
    ECS::UICanvasData written;
    written.ScaleMode        = ECS::UICanvasScaleMode::Letterbox;
    written.RenderMode       = ECS::UICanvasRenderMode::WorldSpace;
    written.WorldScale       = 1234.5f;
    written.ReferenceWidth   = 1920.0f;
    written.ReferenceHeight  = 1080.0f;
    written.MatchWidthHeight = 0.25f;
    written.Sprite           = Desert::Assets::AssetHandle( kResolvedHandle );
    written.Visible          = false;
    written.SafeArea         = glm::vec4( 4.0f, 8.0f, 12.0f, 16.0f );

    const AssetResolver resolver = KeyResolver();
    const auto          object   = SerializeReflected( Type( "UICanvasData" ), &written, &resolver );

    ECS::UICanvasData read;
    DeserializeReflected( Type( "UICanvasData" ), &read, ThroughJsonText( object ), &resolver );

    EXPECT_EQ( read.ScaleMode, written.ScaleMode );
    EXPECT_EQ( read.RenderMode, written.RenderMode );
    EXPECT_FLOAT_EQ( read.WorldScale, written.WorldScale );
    EXPECT_FLOAT_EQ( read.ReferenceWidth, written.ReferenceWidth );
    EXPECT_FLOAT_EQ( read.ReferenceHeight, written.ReferenceHeight );
    EXPECT_FLOAT_EQ( read.MatchWidthHeight, written.MatchWidthHeight );
    EXPECT_EQ( static_cast<uint64_t>( read.Sprite ), kResolvedHandle );
    EXPECT_EQ( read.Visible, written.Visible );
    EXPECT_EQ( read.SafeArea, written.SafeArea );
}

// --- (3) The other two UI slots that carry an asset ----------------------------------------------
//
// UIImage's Sprite and UIPanel's Sprite go down the same branch. Asserting them here is not repetition:
// the defect was in the SHARED path, so a fix that only reached the canvas would be a fix in exactly
// one of the three places a UI asset reference lives.
TEST( UIComponentRoundTrip, TheImageAndPanelSpriteSlotsTakeTheSameRoute )
{
    const AssetResolver resolver = KeyResolver();

    {
        ECS::UIImageData written;
        written.Sprite    = Desert::Assets::AssetHandle( kResolvedHandle );
        const auto object = SerializeReflected( Type( "UIImageData" ), &written, &resolver );

        ECS::UIImageData read;
        DeserializeReflected( Type( "UIImageData" ), &read, ThroughJsonText( object ), &resolver );
        EXPECT_EQ( static_cast<uint64_t>( read.Sprite ), kResolvedHandle );
    }
    {
        ECS::UIPanelData written;
        written.Sprite    = Desert::Assets::AssetHandle( kResolvedHandle );
        const auto object = SerializeReflected( Type( "UIPanelData" ), &written, &resolver );

        ECS::UIPanelData read;
        DeserializeReflected( Type( "UIPanelData" ), &read, ThroughJsonText( object ), &resolver );
        EXPECT_EQ( static_cast<uint64_t>( read.Sprite ), kResolvedHandle );
    }
}

// --- (4) The handle itself, through the text, at the sizes that break -----------------------------
//
// With no resolver a handle is written as a raw integer, which is the form a `.desce` carries for any
// slot whose reference could not be turned into a path. Through a double that number is destroyed
// above 2^53, and a UI handle is above 2^53 essentially always. Asserted on the value it was MEASURED
// wrong on, and on both sides of the boundary, because a fix that rounds correctly at the limit and
// wrongly one past it is the failure this catches.
TEST( UIComponentRoundTrip, ARawHandleSurvivesTheJsonTextExactlyAtEverySize )
{
    for ( const uint64_t handle :
          { kMeasuredHandle, kDoubleExactLimit, kFirstLostByDouble, kAboveInt64, 1ull, 0ull } )
    {
        ECS::UICanvasData written;
        written.Sprite = Desert::Assets::AssetHandle( handle );

        // No resolver: the AssetHandle field takes the raw-integer route on both sides.
        const auto object = SerializeReflected( Type( "UICanvasData" ), &written, nullptr );

        ECS::UICanvasData read;
        read.Sprite = Desert::Assets::AssetHandle( 0xDEADBEEFull ); // so "unchanged" cannot pass as "read"
        DeserializeReflected( Type( "UICanvasData" ), &read, ThroughJsonText( object ), nullptr );

        EXPECT_EQ( static_cast<uint64_t>( read.Sprite ), handle )
             << "handle " << handle
             << " did not survive the JSON text; a double holds 53 bits and a "
                "texture id is 64";
    }
}

// --- (5) The documented behaviour of a missing key ------------------------------------------------
//
// A record that does not mention a field leaves it at what it already held. Asserted rather than
// assumed, because the two halves of a migration depend on it in opposite directions: an old file
// that has no `Sprite` must not zero a default, and a new field added tomorrow must not read as 0
// from every scene written before it.
TEST( UIComponentRoundTrip, AFieldTheRecordDoesNotMentionKeepsWhatItHad )
{
    rfl::Generic::Object partial;
    partial["Visible"] = rfl::Generic( false );

    ECS::UICanvasData read;
    read.Sprite         = Desert::Assets::AssetHandle( kMeasuredHandle );
    read.ReferenceWidth = 640.0f;

    DeserializeReflected( Type( "UICanvasData" ), &read, ThroughJsonText( partial ), nullptr );

    EXPECT_FALSE( read.Visible ) << "the one field the record DID state was not read";
    EXPECT_EQ( static_cast<uint64_t>( read.Sprite ), kMeasuredHandle )
         << "an absent key cleared a field instead of leaving it alone";
    EXPECT_FLOAT_EQ( read.ReferenceWidth, 640.0f );
}

// --- (6) An unresolvable reference is refused, not silently zeroed --------------------------------
//
// The read side used to answer 0 and say nothing, which is indistinguishable from "this canvas has no
// background" — so a scene whose texture had moved opened looking correct and saved the loss back out.
// The stub answers 0 for an unknown key exactly as the real resolver does for a missing file; what
// this asserts is that the serializer ASKS rather than skipping the field, so the refusal reaches the
// resolver where the log lives.
TEST( UIComponentRoundTrip, AReferenceThatResolvesToNothingReachesTheResolverRatherThanBeingSkipped )
{
    int asked = 0;

    AssetResolver counting;
    counting.ToPath = []( uint64_t, const std::string& type ) -> std::string
    { return type == "TextureAsset" ? "cooked:Textures/Gone.tex" : std::string(); };
    counting.FromPath = [&asked]( const std::string& key, const std::string& type ) -> uint64_t
    {
        if ( type != "TextureAsset" )
            return 0ull;
        ++asked;
        EXPECT_EQ( key, "cooked:Textures/Gone.tex" );
        return 0ull;
    };

    ECS::UICanvasData written;
    written.Sprite    = Desert::Assets::AssetHandle( kMeasuredHandle );
    const auto object = SerializeReflected( Type( "UICanvasData" ), &written, &counting );

    ECS::UICanvasData read;
    read.Sprite = Desert::Assets::AssetHandle( 7ull );
    DeserializeReflected( Type( "UICanvasData" ), &read, ThroughJsonText( object ), &counting );

    EXPECT_EQ( asked, 1 ) << "the stored reference never reached the resolver, so nothing could report "
                             "that it did not resolve";
    EXPECT_EQ( static_cast<uint64_t>( read.Sprite ), 0ull );
}

int main( int argc, char** argv )
{
    ReflectionRegistry::Get().ResolveStructLinks();
    ::testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
