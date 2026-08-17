// Cascaded-shadow-map fitting, tested as numbers.
//
// This exists because of a bug that shipped and could not have been caught by anything we had: the
// cascade coverage was a bare `150.0f` written when a world unit was a METRE. The switch to centimetres
// left the number alone, so shadows were computed for the first metre and a half in front of the camera
// and nothing beyond it was ever shadowed. Every test below is about a distance in world units; several
// of them fail outright against the old constant.

#include <Engine/Core/Projection.hpp>
#include <Engine/Graphic/ShadowCascades.hpp>

#include <Common/Core/Units.hpp>

#include <glm/gtc/matrix_transform.hpp>
#include <gtest/gtest.h>

#include <cmath>

using Desert::Graphic::CascadeFit;
using Desert::Graphic::CascadeSetup;
using Desert::Graphic::ComputeShadowCascades;
using Desert::Graphic::kMaxShadowCascades;

namespace Units = Common::Units;

namespace
{
    // A camera looking down -Z from the origin with the engine's own defaults (near 10 cm, far 50 km).
    //
    // THE PROJECTION MUST COME FROM THE ENGINE'S OWN FACTORY. ComputeShadowCascades unprojects NDC
    // corners, so it reads the camera's depth convention; a glm::perspective here would hand it an
    // OpenGL [-1,1] matrix, the near and far rings would swap, and the test would happily pin cascades
    // fitted behind the observer.
    CascadeSetup DefaultSetup()
    {
        CascadeSetup s;
        s.CameraNear = Desert::Core::kDefaultNearPlane;
        s.CameraFar  = Desert::Core::kDefaultFarPlane;
        s.CameraView = glm::lookAt( glm::vec3( 0.0f ), glm::vec3( 0.0f, 0.0f, -1.0f ), glm::vec3( 0, 1, 0 ) );
        s.CameraProjection =
             Desert::Core::MakePerspective( glm::radians( 45.0f ), 16.0f / 9.0f, s.CameraNear, s.CameraFar );
        s.LightDirection   = glm::normalize( glm::vec3( -0.4f, -1.0f, -0.3f ) ); // where the light TRAVELS
        return s;
    }

    // Is @p worldPos inside cascade @p c's shadow map (i.e. would it receive a shadow)?
    bool CoveredBy( const CascadeFit& fit, const glm::vec3& worldPos )
    {
        const glm::vec4 clip = fit.ViewProj * glm::vec4( worldPos, 1.0f );
        const glm::vec3 ndc  = glm::vec3( clip ) / clip.w;
        return ndc.x >= -1.0f && ndc.x <= 1.0f && ndc.y >= -1.0f && ndc.y <= 1.0f && ndc.z >= 0.0f &&
               ndc.z <= 1.0f;
    }
} // namespace

// THE regression test. A character-sized object 40 m down the view axis is ordinary scene content; with
// the metre-era constant the cascades stopped at 1.5 m and nothing here was covered.
TEST( ShadowCascades, CoversOrdinaryWorldDistances )
{
    const CascadeSetup setup = DefaultSetup();
    CascadeFit         fits[kMaxShadowCascades];
    const uint32_t     n = ComputeShadowCascades( setup, fits );
    ASSERT_EQ( n, kMaxShadowCascades );

    for ( const float metres : { 1.0f, 5.0f, 20.0f, 40.0f, 100.0f } )
    {
        const glm::vec3 p( 0.0f, 0.0f, -Units::Metres( metres ) );
        bool            covered = false;
        for ( uint32_t c = 0; c < n; ++c )
            covered = covered || CoveredBy( fits[c], p );
        EXPECT_TRUE( covered ) << "nothing shadows a point " << metres << " m in front of the camera";
    }
}

// The coverage cap is the number that decides whether a scene has shadows at all, so it is stated in
// metres and asserted in metres.
TEST( ShadowCascades, LastCascadeReachesTheRequestedDistance )
{
    CascadeSetup setup = DefaultSetup();
    setup.MaxDistance  = Units::Metres( 150.0f );

    CascadeFit     fits[kMaxShadowCascades];
    const uint32_t n = ComputeShadowCascades( setup, fits );
    ASSERT_EQ( n, kMaxShadowCascades );

    EXPECT_NEAR( fits[n - 1].SplitFar, setup.MaxDistance, setup.MaxDistance * 0.01f );
    EXPECT_GT( fits[n - 1].SplitFar, Units::Metres( 100.0f ) );
}

// A cascade that is not capped follows the CAMERA's far plane instead, so a short-range camera does not
// get cascades stretched over a kilometre of nothing.
TEST( ShadowCascades, CoverageIsCappedByTheNearerOfFarPlaneAndMaxDistance )
{
    CascadeSetup setup = DefaultSetup();
    setup.CameraFar    = Units::Metres( 30.0f );
    setup.CameraProjection =
         Desert::Core::MakePerspective( glm::radians( 45.0f ), 16.0f / 9.0f, setup.CameraNear, setup.CameraFar );
    setup.MaxDistance = Units::Metres( 150.0f );

    CascadeFit     fits[kMaxShadowCascades];
    const uint32_t n = ComputeShadowCascades( setup, fits );
    ASSERT_EQ( n, kMaxShadowCascades );
    EXPECT_NEAR( fits[n - 1].SplitFar, Units::Metres( 30.0f ), Units::Metres( 0.5f ) );
}

TEST( ShadowCascades, SplitsAndRadiiGrowOutward )
{
    CascadeFit     fits[kMaxShadowCascades];
    const uint32_t n = ComputeShadowCascades( DefaultSetup(), fits );
    ASSERT_EQ( n, kMaxShadowCascades );

    for ( uint32_t c = 1; c < n; ++c )
    {
        EXPECT_GT( fits[c].SplitFar, fits[c - 1].SplitFar ) << "cascade " << c << " must reach further";
        EXPECT_GE( fits[c].Radius, fits[c - 1].Radius ) << "a further slice cannot bound smaller";
    }
    EXPECT_GT( fits[0].Radius, 0.0f );
}

// The normal-offset bias in the shader is expressed in these units; if the relation breaks, shadow acne
// scales wrongly and the failure looks like "shadows are noisy" rather than anything about texels.
TEST( ShadowCascades, WorldPerTexelMatchesRadiusAndMapSize )
{
    CascadeSetup setup  = DefaultSetup();
    setup.ShadowMapSize = 2048;

    CascadeFit     fits[kMaxShadowCascades];
    const uint32_t n = ComputeShadowCascades( setup, fits );
    ASSERT_EQ( n, kMaxShadowCascades );

    for ( uint32_t c = 0; c < n; ++c )
        EXPECT_FLOAT_EQ( fits[c].WorldPerTexel, ( 2.0f * fits[c].Radius ) / 2048.0f );
}

// The texel snap is what stops shadows crawling as the camera moves: the cascade centre may only travel
// in WHOLE shadow-map texels along the light's own axes, so the sampling grid stays world-locked. It does
// NOT mean the centre never moves — it follows the camera along the view axis, and the light-space depth
// axis is deliberately not snapped. So the assertion is about the light-space X/Y being on the grid.
TEST( ShadowCascades, CentreMovesInWholeTexelsAlongTheLightAxes )
{
    CascadeSetup setup = DefaultSetup();
    CascadeFit   before[kMaxShadowCascades];
    ASSERT_EQ( ComputeShadowCascades( setup, before ), kMaxShadowCascades );

    const glm::vec3 lightDir   = glm::normalize( setup.LightDirection );
    const glm::vec3 up         = glm::abs( lightDir.y ) > 0.99f ? glm::vec3( 0, 0, 1 ) : glm::vec3( 0, 1, 0 );
    const glm::mat4 lightBasis = glm::lookAt( -lightDir, glm::vec3( 0.0f ), up );

    // Creep the camera sideways in tenths of a texel and check every intermediate position.
    for ( int step = 1; step <= 12; ++step )
    {
        const float nudge = before[0].WorldPerTexel * 0.1f * static_cast<float>( step );
        setup.CameraView =
             glm::lookAt( glm::vec3( nudge, 0.0f, 0.0f ), glm::vec3( nudge, 0.0f, -1.0f ), glm::vec3( 0, 1, 0 ) );

        CascadeFit after[kMaxShadowCascades];
        ASSERT_EQ( ComputeShadowCascades( setup, after ), kMaxShadowCascades );

        const glm::vec3 a = glm::vec3( lightBasis * glm::vec4( before[0].Center, 1.0f ) );
        const glm::vec3 b = glm::vec3( lightBasis * glm::vec4( after[0].Center, 1.0f ) );

        for ( int axis = 0; axis < 2; ++axis ) // x, y — the two axes the snap owns
        {
            const float texels = ( b[axis] - a[axis] ) / before[0].WorldPerTexel;
            EXPECT_NEAR( texels, std::round( texels ), 1e-2f )
                 << "cascade slid " << texels << " texels on axis " << axis << " at step " << step;
        }
    }
}

TEST( ShadowCascades, DegenerateSetupsProduceNothing )
{
    CascadeFit fits[kMaxShadowCascades];

    CascadeSetup noLight   = DefaultSetup();
    noLight.LightDirection = glm::vec3( 0.0f );
    EXPECT_EQ( ComputeShadowCascades( noLight, fits ), 0u );

    CascadeSetup inverted = DefaultSetup();
    inverted.MaxDistance  = inverted.CameraNear * 0.5f;
    EXPECT_EQ( ComputeShadowCascades( inverted, fits ), 0u );
}

int main( int argc, char** argv )
{
    testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
