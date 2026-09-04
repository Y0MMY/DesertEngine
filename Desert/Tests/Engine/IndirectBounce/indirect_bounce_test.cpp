// The one-bounce estimate the deferred screen-space GI is built from, tested as the RELATION it has to
// the direct light it is a second view of — not as a function that returns plausible numbers.
//
// What this suite exists for. Programs/Deferred/DeferredLighting.shader shaded the neighbouring surface
// that supplies a bounce with `nAlb * max(0.0, dot(nN, sunL)) * sunRadiance`: the un-normalized Lambert
// form the forward mesh shaders shipped until Mesh/DirectLighting.glslh replaced it, PI times too
// bright. The sweep that removed that form everywhere else did not remove it here, and the suite that
// was meant to stop it coming back — Desert/Tests/Engine/DirectLighting — could not see it, because it
// asks whether each shader FILE reaches the shared BRDF. DeferredLighting.shader does, for its sun, one
// function below the gather. The middle link kept a copy of its own and nothing asserted about the
// middle link.
//
// The defect was invisible in every frame in the repository for a reason that is itself a finding and
// is recorded at the site: the estimate's distance softening is `1.0 + d2` in SQUARE WORLD UNITS, and
// the world switched to centimetres on 2026-08-04 without that constant moving. Measured on CornellDemo
// at the shipped GIIntensity of 2.0, the whole gather moves the final frame by at most 1/255 on 0.21%
// of pixels. A factor of PI on a term four orders of magnitude below the frame is not visible, which is
// exactly why it needs a test rather than an eye.
//
// Every assertion below is about the SHIPPED TEXT (Mesh/IndirectBounce.glslh compiled as C++), and each
// picks a property that a missing 1/PI cannot have.

#include <gtest/gtest.h>

#include "IndirectBounceReference.hpp"

#include <Common/Core/Units.hpp>

#include <glm/gtc/epsilon.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace Desert::Tests::IndirectBounceRef;

namespace
{
    // The emitter: a surface at the origin facing +Z, lit by a sun 45 degrees off its normal.
    const glm::vec3 kEmitterPos( 0.0f );
    const glm::vec3 kEmitterN( 0.0f, 0.0f, 1.0f );
    const glm::vec3 kSunL     = glm::normalize( glm::vec3( 0.0f, 0.7071f, 0.7071f ) );
    const glm::vec3 kRadiance = glm::vec3( 1.0f );
    const glm::vec3 kAlbedo   = glm::vec3( 0.5f );
    const glm::vec3 kF0       = glm::vec3( 0.04f ); // Fdielectric, as the gather builds it at metalness 0

    constexpr float kRoughness = 0.5f;

    glm::vec3 Bounce( const glm::vec3& receiverPos, const glm::vec3& receiverN, const glm::vec3& albedo = kAlbedo,
                      float metalness = 0.0f, const glm::vec3& F0 = kF0, float roughness = kRoughness,
                      const glm::vec3& radiance = kRadiance )
    {
        return EvaluateBounceSample( receiverPos, receiverN, kEmitterPos, kEmitterN, albedo, F0, metalness,
                                     roughness, kSunL, radiance );
    }

    // A receiver placed at distance `r` in direction `wo` from the emitter, aimed straight back at it.
    // Aiming it back is what pins the receiving cosine at exactly 1, so the only cosine left in the
    // coupling is the EMITTING one — which is the outgoing cosine a hemisphere integral needs.
    glm::vec3 ReceiverAt( const glm::vec3& wo, float r )
    {
        return kEmitterPos + wo * r;
    }

    // THE DIRECTIONAL ALBEDO OF THE BOUNCE: of the sunlight arriving at the emitter, how much leaves it
    // toward all the surfaces that could receive it, integrated over the whole outgoing hemisphere. This
    // is the quantity a missing 1/PI multiplies by PI, and the one number that says "energy" rather than
    // "brightness".
    //
    // The estimate's own geometry supplies every factor the integral needs: `emit` IS cos(theta_o), and
    // the softened inverse square is a constant at fixed distance, so multiplying it back out leaves
    // exactly `BRDF * cos(theta_o)` under the integral. F0 is zero — which is NOT the specular lobe
    // switched off, since Schlick still returns 1 at grazing from a zero base reflectance — so the
    // assertion is about the whole BRDF and not a half of it isolated by a trick.
    float BounceDirectionalAlbedo( float albedo, float roughness, float distance )
    {
        const float cosLi = glm::max( glm::dot( kEmitterN, kSunL ), 0.0f );

        // ASKED OF THE SHIPPED TEXT, NOT RETYPED. This used to read `1.0f + distance * distance`, a
        // second copy of the shader's softening living in the test — and the moment that softening's
        // units were repaired (a bare 1.0 meant one square CENTIMETRE in a centimetre world), three
        // assertions here started dividing by a constant the shader no longer used and were measuring
        // nothing. Both sides now call one function, so they cannot disagree about it again.
        const float falloff = BounceFalloff( distance * distance );

        constexpr int   kTheta = 128;
        constexpr int   kPhi   = 512;
        constexpr float kPi    = 3.14159265358979f;

        float integral = 0.0f;
        for ( int i = 0; i < kTheta; ++i )
        {
            const float theta = ( i + 0.5f ) / kTheta * ( kPi * 0.5f );
            for ( int j = 0; j < kPhi; ++j )
            {
                const float     phi = ( j + 0.5f ) / kPhi * ( 2.0f * kPi );
                const glm::vec3 wo( std::sin( theta ) * std::cos( phi ), std::sin( theta ) * std::sin( phi ),
                                    std::cos( theta ) );

                const glm::vec3 sample = Bounce( ReceiverAt( wo, distance ), -wo, glm::vec3( albedo ), 0.0f,
                                                 glm::vec3( 0.0f ), roughness );

                integral += ( sample.r * falloff / cosLi ) * std::sin( theta ) * ( kPi * 0.5f / kTheta ) *
                            ( 2.0f * kPi / kPhi );
            }
        }
        return integral;
    }

    std::filesystem::path ShaderRoot()
    {
        // The test binary lives in build/Bin/Tests/<config>; walk up to the repository root.
        std::filesystem::path root = std::filesystem::current_path();
        for ( int up = 0; up < 8 && !std::filesystem::exists( root / "Editor" / "Resources" / "Shaders" ); ++up )
            root = root.parent_path();
        return root / "Editor" / "Resources" / "Shaders";
    }

    std::string Read( const std::filesystem::path& file )
    {
        std::ifstream      in( file, std::ios::binary );
        std::ostringstream buffer;
        buffer << in.rdbuf();
        return buffer.str();
    }
} // namespace

// ---------------------------------------------------------------------------------------------------
// THE SOFTENING IS A LENGTH, AND THE LENGTH IS A METRE.
// ---------------------------------------------------------------------------------------------------

// Every other assertion in this file divides BounceFalloff back out, so all of them are blind to what it
// actually is — which is how a bare `1.0` survived the 2026-08-04 switch to centimetre world units and
// left screen-space GI four orders of magnitude below anything a frame could show, with GIIntensity a
// dead setting in 49 scenes. That defect could not fail a single test here, and the mutation proves it:
// restoring `1.0 + d2` leaves the other nine green.
//
// So the softening gets its own assertion, and it is about MEANING rather than about a constant: the
// bounce is at half strength one METRE from its emitter. Written through Common::Units so that the next
// unit migration has to walk past it.
TEST( IndirectBounce, TheSofteningLengthIsOneMetreAndNotOneWorldUnit )
{
    EXPECT_NEAR( BounceFalloff( 0.0f ), 1.0f, 1e-6f ) << "a coincident emitter must stay finite at 1x";

    const float oneMetre = Common::Units::Metres( 1.0f );
    EXPECT_NEAR( BounceFalloff( oneMetre * oneMetre ), 2.0f, 1e-4f )
         << "the bounce is supposed to be at HALF strength one metre away; it is at half strength "
         << std::sqrt( 1.0f )
         << " world units instead, which is what a bare `1.0 + d2` gives in a "
            "centimetre world";

    // And the direction of the mistake, stated so it cannot come back as the opposite error: one
    // centimetre must be nearly free, not half.
    const float oneCm = Common::Units::Cm( 1.0f );
    EXPECT_LT( BounceFalloff( oneCm * oneCm ), 1.001f )
         << "a centimetre of separation is attenuating the bounce as if it were a metre";
}

// ---------------------------------------------------------------------------------------------------
// THE bound. One assertion that catches a spurious factor of anything, and this one was PI.
// ---------------------------------------------------------------------------------------------------

TEST( IndirectBounce, TheEmitterSendsOnAtMostItsAlbedoAndNotPiTimesIt )
{
    // A surface that bounces light is still a surface, not a light source. Integrated over the outgoing
    // hemisphere it passes on exactly `albedo` of what fell on it — the identity the 1/PI IS, restated
    // for the estimate that reads that surface a second time.
    //
    // The deleted form (`albedo * N·L * radiance`, no BRDF at all) returns PI * albedo here, so every
    // wall in every frame handed on 3.14 times the sunlight it received before the gather even started
    // counting geometry. No setting of GIIntensity can satisfy this assertion, because the knob scales
    // the whole gather and this integral is normalized by the emitter's own irradiance.
    //
    // The tolerance is the one Desert/Tests/Engine/DirectLighting measured for the same integrand and
    // the same 128x512 midpoint grid: the model's own worst departure from perfect conservation is
    // +0.0028 (a rough dielectric at albedo 0, where Schlick's grazing reflectance adds a little) and
    // -0.0009 (a very rough white surface, where the Smith visibility takes a little).
    float worst = 0.0f;
    for ( float roughness = 0.1f; roughness <= 1.0f; roughness += 0.1f )
        for ( float a = 0.0f; a <= 1.0f; a += 0.1f )
        {
            const float passedOn = BounceDirectionalAlbedo( a, roughness, 2.0f );

            EXPECT_NEAR( passedOn, a, 4e-3f )
                 << "bounce directional albedo at albedo " << a << ", roughness " << roughness
                 << " — a factor of PI here is the defect this suite exists for";
            EXPECT_LE( passedOn, 1.01f ) << "the emitter passed on more light than reached it";

            worst = glm::max( worst, std::abs( passedOn - a ) );
        }

    EXPECT_LT( worst, 4e-3f ) << "energy imbalance grew past the model's known one: " << worst;
}

TEST( IndirectBounce, TheEnergyBoundDoesNotDependOnHowFarApartTheSurfacesAre )
{
    // The softened inverse square is a COUPLING, not a second BRDF: dividing it back out has to recover
    // the same albedo whatever the separation. This is what says the distance term and the reflectance
    // term are separable — and it is the assertion that would have to be re-measured, not deleted, if
    // the softening's scale is ever corrected for the centimetre world (see IndirectBounce.glslh).
    for ( float distance : { 0.25f, 1.0f, 4.0f, 40.0f } )
        EXPECT_NEAR( BounceDirectionalAlbedo( 0.8f, 0.4f, distance ), 0.8f, 4e-3f )
             << "at separation " << distance;
}

TEST( IndirectBounce, TheBounceNeverExceedsWhatTheSharedBRDFSaysTheEmitterIsSending )
{
    // THE relation to Mesh/DirectLighting.glslh, stated as an inequality so it is not a comparison of a
    // function with itself: the coupling is two cosines over one-plus-a-square, every factor of which
    // lies in [0, 1], so the estimate can only ever be a FRACTION of the emitter's outgoing radiance
    // toward the receiver. The old inline form was PI times that radiance, which fails here at every
    // geometry rather than at a lucky one.
    for ( float theta = 0.05f; theta < 1.55f; theta += 0.1f )
        for ( float distance : { 0.5f, 2.0f, 9.0f } )
        {
            const glm::vec3 wo( std::sin( theta ), 0.0f, std::cos( theta ) );
            const glm::vec3 receiverPos = ReceiverAt( wo, distance );
            const glm::vec3 receiverN   = -wo;

            const glm::vec3 bounce = Bounce( receiverPos, receiverN );

            // What the emitter is sending toward the receiver, straight from the shared text. `wo` is
            // the emitter's view vector: the direction from it toward the surface being shaded.
            const glm::vec3 outgoing =
                 EvaluateDirectLight( kSunL, kRadiance, wo, kEmitterN, kF0, 0.0f, kRoughness, kAlbedo );

            EXPECT_LE( bounce.r, outgoing.r + 1e-6f ) << "theta " << theta << ", distance " << distance;
            EXPECT_GT( bounce.r, 0.0f ) << "the bounce vanished where both surfaces face each other";
        }
}

// ---------------------------------------------------------------------------------------------------
// The coupling: what the geometry is allowed to do.
// ---------------------------------------------------------------------------------------------------

TEST( IndirectBounce, NeitherSurfaceBouncesThroughItsOwnBack )
{
    // Exactly zero, not a small number. A receiver turned away must gain nothing, and an emitter turned
    // away must give nothing — the two halves fail in opposite directions and one alone is not evidence.
    const glm::vec3 above = ReceiverAt( glm::vec3( 0.0f, 0.0f, 1.0f ), 2.0f );

    EXPECT_TRUE( glm::all( glm::epsilonEqual( Bounce( above, kEmitterN ), glm::vec3( 0.0f ), 1e-9f ) ) )
         << "a receiver facing away from the emitter still collected a bounce";

    const glm::vec3 behind = ReceiverAt( glm::vec3( 0.0f, 0.0f, -1.0f ), 2.0f );
    EXPECT_TRUE( glm::all(
         glm::epsilonEqual( Bounce( behind, glm::vec3( 0.0f, 0.0f, 1.0f ) ), glm::vec3( 0.0f ), 1e-9f ) ) )
         << "an emitter bounced light out of its own back face";
}

TEST( IndirectBounce, TheBounceFallsMonotonicallyWithSeparation )
{
    // Monotonicity along one ray: only the softened inverse square moves, so a term that grew, plateaued
    // or inverted would show here where a single spot value never could.
    const glm::vec3 wo = glm::normalize( glm::vec3( 0.3f, 0.0f, 1.0f ) );

    float previous = 1e30f;
    for ( float distance = 0.1f; distance < 20.0f; distance *= 1.4f )
    {
        const glm::vec3 bounce = Bounce( ReceiverAt( wo, distance ), -wo );

        EXPECT_LT( bounce.r, previous ) << "the bounce did not fall at separation " << distance;
        previous = bounce.r;
    }
}

TEST( IndirectBounce, TheResponseToTheSunIsExactlyLinear )
{
    // The gain is a constant of the two surfaces, not of the light. A stray multiplier, a square, or a
    // light counted twice all break this while leaving one spot value looking entirely reasonable.
    const glm::vec3 wo          = glm::normalize( glm::vec3( 0.2f, 0.1f, 1.0f ) );
    const glm::vec3 receiverPos = ReceiverAt( wo, 3.0f );

    const glm::vec3 unit = Bounce( receiverPos, -wo, kAlbedo, 0.0f, kF0, kRoughness, glm::vec3( 1.0f ) );
    ASSERT_GT( unit.r, 0.0f );

    for ( float scale = 0.5f; scale <= 64.0f; scale *= 2.0f )
    {
        const glm::vec3 scaled = Bounce( receiverPos, -wo, kAlbedo, 0.0f, kF0, kRoughness, glm::vec3( scale ) );
        EXPECT_NEAR( scaled.r, unit.r * scale, unit.r * scale * 1e-5f ) << "sun radiance scale " << scale;
    }
}

// ---------------------------------------------------------------------------------------------------
// The material of the EMITTER, which the deleted form read as nothing but a colour.
// ---------------------------------------------------------------------------------------------------

TEST( IndirectBounce, AMetalBouncesNoPaint )
{
    // The old form bounced the raw G-buffer albedo whatever the surface was made of, so a gold panel
    // threw its base colour around the room as though it were a diffuse wall. A conductor has no diffuse
    // lobe: at metalness 1 the albedo must reach the bounce only through F0, which the caller builds and
    // passes separately.
    const glm::vec3 wo          = glm::normalize( glm::vec3( 0.25f, 0.0f, 1.0f ) );
    const glm::vec3 receiverPos = ReceiverAt( wo, 2.0f );

    const glm::vec3 dark   = Bounce( receiverPos, -wo, glm::vec3( 0.05f ), 1.0f, glm::vec3( 1.0f ), kRoughness );
    const glm::vec3 bright = Bounce( receiverPos, -wo, glm::vec3( 0.95f ), 1.0f, glm::vec3( 1.0f ), kRoughness );

    EXPECT_TRUE( glm::all( glm::epsilonEqual( dark, bright, 1e-9f ) ) )
         << "albedo leaked into a conductor's bounce";
}

TEST( IndirectBounce, ALambertianEmitterHandsOnItsAlbedoOverPiAndNotItsAlbedo )
{
    // The spot value the integral above generalizes, arranged so every cosine and every Fresnel term is
    // exactly 1 and the only number left is the normalization: emitter facing the sun head-on, receiver
    // directly in front of it facing back, dielectric with F0 = 0 so Schlick contributes nothing at
    // normal incidence.
    //
    //   outgoing radiance = albedo / PI          coupling = 1 * 1 / BounceFalloff(d2)
    //
    // THE COUPLING IS ASKED FOR, NOT ASSUMED. This assertion used to read `1/(2*PI)`, which is the answer
    // only while the softening happens to evaluate to 2 at unit distance — true of `1.0 + d*d` and of
    // nothing else. Repairing that softening's units (a bare 1.0 meant one square CENTIMETRE in a
    // centimetre world) left this expectation measuring the old constant instead of the property it
    // names, and it failed by exactly the ratio of the two softenings. The property is the 1/PI; the
    // coupling is BounceFalloff's business and is divided out rather than predicted.
    const float     kPi = 3.14159265358979f;
    const glm::vec3 straightUp( 0.0f, 0.0f, 1.0f );
    const float     kDistance = 1.0f;

    const glm::vec3 bounce = EvaluateBounceSample( ReceiverAt( straightUp, kDistance ), -straightUp, kEmitterPos,
                                                   kEmitterN, glm::vec3( 1.0f ), glm::vec3( 0.0f ), 0.0f,
                                                   kRoughness, straightUp, glm::vec3( 1.0f ) );

    // Lambert's 1/PI, and nothing else, once the coupling is removed. The deleted form hands on 1.0 here
    // — PI times too much — which is what this test exists to catch.
    EXPECT_NEAR( bounce.r * BounceFalloff( kDistance * kDistance ), 1.0f / kPi, 1e-5f );
}

// ---------------------------------------------------------------------------------------------------
// The census, at the CALL SITE this time.
// ---------------------------------------------------------------------------------------------------

// Desert/Tests/Engine/DirectLighting proves that every shader FILE with a direct light in it reaches
// Mesh/DirectLighting.glslh. That is true of DeferredLighting.shader and always was, and the bounce
// inside it was still PI times too bright — the file-level census cannot see a second evaluation living
// in a helper. This is the same census one level down: the GI gather's own estimate is the shared text,
// by construction, and does not spell out a cosine-times-albedo of its own.
TEST( IndirectBounce, TheDeferredGIGatherReachesTheOneSharedBRDFToo )
{
    const std::filesystem::path root = ShaderRoot();
    ASSERT_TRUE( std::filesystem::exists( root ) )
         << "could not find Editor/Resources/Shaders above " << std::filesystem::current_path();

    const std::string gather = Read( root / "Programs" / "Deferred" / "DeferredLighting.shader" );
    ASSERT_FALSE( gather.empty() );

    EXPECT_NE( gather.find( "IndirectBounce.glslh" ), std::string::npos )
         << "the deferred pass does not compile the shared bounce estimate";
    EXPECT_NE( gather.find( "EvaluateBounceSample(" ), std::string::npos )
         << "the deferred pass does not evaluate its bounce through EvaluateBounceSample";

    const std::string bounce = Read( root / "Mesh" / "IndirectBounce.glslh" );
    ASSERT_FALSE( bounce.empty() );

    EXPECT_NE( bounce.find( "DirectLighting.glslh" ), std::string::npos )
         << "the bounce estimate does not name the shared direct-light BRDF";
    EXPECT_NE( bounce.find( "EvaluateDirectLight(" ), std::string::npos )
         << "the bounce estimate does not shade its emitter through the shared BRDF";
}

TEST( IndirectBounce, NoShaderShadesABounceWithARawCosine )
{
    // The exact expression that was deleted, and its spelling in the text that replaced it: an emitter's
    // normal dotted against the sun, outside the one BRDF that is allowed to do that. Both names are
    // listed because the defect is a shape, not a string, and it came back once already under a new one
    // — DeferredLighting.shader called it `nN`, Mesh/IndirectBounce.glslh calls it `emitterN`.
    //
    // Comments are stripped first because this codebase records its defects at the site and several of
    // those notes QUOTE the deleted line.
    const std::filesystem::path root = ShaderRoot();
    ASSERT_TRUE( std::filesystem::exists( root ) );

    const char* kRawCosines[] = { "dot(nN, sunL)", "dot(emitterN, sunL)" };

    for ( const auto& entry : std::filesystem::recursive_directory_iterator( root ) )
    {
        if ( !entry.is_regular_file() )
            continue;

        const std::string ext = entry.path().extension().string();
        if ( ext != ".shader" && ext != ".glslh" )
            continue;

        std::istringstream in( Read( entry.path() ) );
        std::string        line;
        while ( std::getline( in, line ) )
        {
            const std::size_t comment = line.find( "//" );
            if ( comment != std::string::npos )
                line.erase( comment );

            for ( const char* raw : kRawCosines )
                EXPECT_EQ( line.find( raw ), std::string::npos )
                     << entry.path().filename().string()
                     << " shades a bouncing neighbour with a raw cosine again: " << line;
        }
    }
}

int main( int argc, char** argv )
{
    testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
