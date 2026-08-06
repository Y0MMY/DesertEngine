#pragma once

#include <Common/Core/Units.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace Desert::Graphic
{
    // Cascaded-shadow-map cascade fitting, as PURE MATH — no renderer, no GPU, no state.
    //
    // It lived inside MeshRenderer, where it could only be exercised by running the editor and looking at
    // the screen. That is how a metre-era constant survived the switch to centimetres and quietly capped
    // every shadow at 150 cm: nothing could assert on it. Out here it is a function of numbers that a test
    // can pin down (see Tests/Engine/ShadowCascades).
    //
    // EVERY distance is in WORLD UNITS, and a world unit is a centimetre (Common::Units).

    inline constexpr uint32_t kMaxShadowCascades = 4;

    struct CascadeFit
    {
        glm::mat4 ViewProj      = glm::mat4( 1.0f ); // light-space matrix for this cascade
        glm::vec3 Center        = glm::vec3( 0.0f ); // world centre of the fitted sphere (texel-snapped)
        float     Radius        = 0.0f;              // world radius of the slice's bounding sphere
        float     WorldPerTexel = 0.0f;              // 2 * Radius / shadowMapSize — drives the normal-offset
        float     SplitFar      = 0.0f;              // view-space far distance this cascade covers
    };

    struct CascadeSetup
    {
        glm::mat4 CameraView       = glm::mat4( 1.0f );
        glm::mat4 CameraProjection = glm::mat4( 1.0f );
        float     CameraNear       = Common::Units::Cm( 10.0f );
        float     CameraFar        = Common::Units::Metres( 1000.0f );
        glm::vec3 LightDirection   = glm::vec3( 0.0f, -1.0f, 0.0f ); // direction the light TRAVELS
        // How far from the camera shadows are computed at all. Anything past this is simply unshadowed,
        // so it is the single number that decides whether a scene "has shadows".
        float    MaxDistance   = Common::Units::Metres( 150.0f );
        float    SplitLambda   = 0.6f; // 0 = uniform splits, 1 = logarithmic
        uint32_t CascadeCount  = kMaxShadowCascades;
        uint32_t ShadowMapSize = 2048;
    };

    // Fills @p out with CascadeCount fitted cascades. Returns how many were written (0 when the setup is
    // degenerate: no light direction, or a far plane behind the near one).
    inline uint32_t ComputeShadowCascades( const CascadeSetup& setup, CascadeFit* out )
    {
        const uint32_t count = std::min( setup.CascadeCount, kMaxShadowCascades );
        if ( count == 0 || !out )
            return 0;
        if ( glm::length( setup.LightDirection ) < 1e-4f )
            return 0;

        const glm::vec3 lightDir  = glm::normalize( setup.LightDirection );
        const float     camNear   = setup.CameraNear;
        const float     shadowFar = glm::min( setup.CameraFar, setup.MaxDistance );
        if ( shadowFar <= camNear )
            return 0;

        const float splitRange = shadowFar - camNear;
        const float ratio      = shadowFar / glm::max( camNear, 1e-4f );

        // Practical split scheme: blend uniform and logarithmic distributions (lambda).
        float splitFar[kMaxShadowCascades];
        for ( uint32_t i = 0; i < count; ++i )
        {
            const float p   = static_cast<float>( i + 1 ) / static_cast<float>( count );
            const float log = camNear * std::pow( ratio, p );
            const float uni = camNear + splitRange * p;
            splitFar[i]     = glm::mix( uni, log, setup.SplitLambda );
        }

        // Frustum corners (GL NDC z in [-1,1]) give the eye + view axis; the slice bounding spheres are then
        // computed analytically from scalars, which is what keeps the radius bit-stable as the camera turns
        // (a centroid + max-corner-distance is analytically rotation-invariant but accumulates FP noise, and
        // the quantized radius then flip-flops and the texel snap stops hiding the crawl).
        const glm::mat4 invVP = glm::inverse( setup.CameraProjection * setup.CameraView );
        glm::vec3       nearCorners[4];
        glm::vec3       farCorners[4];
        int             ci = 0;
        for ( int x = 0; x < 2; ++x )
        {
            for ( int y = 0; y < 2; ++y )
            {
                const glm::vec4 nc = invVP * glm::vec4( 2.0f * x - 1.0f, 2.0f * y - 1.0f, -1.0f, 1.0f );
                const glm::vec4 fc = invVP * glm::vec4( 2.0f * x - 1.0f, 2.0f * y - 1.0f, 1.0f, 1.0f );
                nearCorners[ci]    = glm::vec3( nc ) / nc.w;
                farCorners[ci]     = glm::vec3( fc ) / fc.w;
                ++ci;
            }
        }

        const glm::vec3 nearRingCenter =
             0.25f * ( nearCorners[0] + nearCorners[1] + nearCorners[2] + nearCorners[3] );
        const glm::vec3 farRingCenter = 0.25f * ( farCorners[0] + farCorners[1] + farCorners[2] + farCorners[3] );
        const glm::vec3 viewFwd       = glm::normalize( farRingCenter - nearRingCenter );
        const glm::vec3 eye           = nearRingCenter - viewFwd * camNear; // frustum apex

        // k = the frustum's angular half-slope, taken from the PROJECTION matrix (rotation-independent).
        const float tanHalfY = 1.0f / glm::max( std::abs( setup.CameraProjection[1][1] ), 1e-6f );
        const float aspect   = std::abs( setup.CameraProjection[1][1] / setup.CameraProjection[0][0] );
        const float kSlope   = tanHalfY * std::sqrt( 1.0f + aspect * aspect );
        const float k2       = kSlope * kSlope;

        float lastFar = camNear;
        for ( uint32_t c = 0; c < count; ++c )
        {
            const float zNear = lastFar;
            const float zFar  = splitFar[c];

            float sphereZ = 0.0f;
            float radius  = 0.0f;
            if ( k2 * ( zFar + zNear ) >= ( zFar - zNear ) )
            {
                // The near ring is already inside the far ring's sphere.
                sphereZ = zFar;
                radius  = kSlope * zFar;
            }
            else
            {
                sphereZ        = 0.5f * ( zFar + zNear ) * ( 1.0f + k2 );
                const float dz = sphereZ - zFar;
                radius         = std::sqrt( dz * dz + k2 * zFar * zFar );
            }

            const glm::vec3 center = eye + viewFwd * sphereZ;
            radius                 = std::ceil( radius * 16.0f ) / 16.0f; // quantize a bit for stability

            const glm::vec3 up = glm::abs( lightDir.y ) > 0.99f ? glm::vec3( 0, 0, 1 ) : glm::vec3( 0, 1, 0 );

            // Texel-snap: the cascade centre may only move in WHOLE shadow-map texels along the light's
            // axes, so the sampling grid stays world-locked while the camera moves.
            const float     worldPerTexel = ( 2.0f * radius ) / static_cast<float>( setup.ShadowMapSize );
            const glm::mat4 lightBasis    = glm::lookAt( -lightDir, glm::vec3( 0.0f ), up );
            glm::vec3       centerLS      = glm::vec3( lightBasis * glm::vec4( center, 1.0f ) );
            centerLS.x                    = std::floor( centerLS.x / worldPerTexel ) * worldPerTexel;
            centerLS.y                    = std::floor( centerLS.y / worldPerTexel ) * worldPerTexel;
            const glm::vec3 snapped       = glm::vec3( glm::inverse( lightBasis ) * glm::vec4( centerLS, 1.0f ) );

            // The light eye is pushed back by 2*radius so casters between it and the slice still cast.
            const glm::mat4 view = glm::lookAt( snapped - lightDir * ( radius * 2.0f ), snapped, up );
            const glm::mat4 proj =
                 glm::orthoRH_ZO( -radius, radius, -radius, radius, Common::Units::Cm( 10.0f ), radius * 4.0f );

            out[c].ViewProj      = proj * view;
            out[c].Center        = snapped;
            out[c].Radius        = radius;
            out[c].WorldPerTexel = worldPerTexel;
            out[c].SplitFar      = zFar;

            lastFar = zFar;
        }
        return count;
    }
} // namespace Desert::Graphic
