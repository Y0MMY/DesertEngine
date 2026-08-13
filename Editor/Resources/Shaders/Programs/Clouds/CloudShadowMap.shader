Shader "CloudShadowMap"
{
    Compute
    {
        // Stage S1b of the volumetric clouds: the SHADOW MAP.
        //
        // One texel per sun ray through the layer. Marches that ray from where it enters the layer top to
        // where it leaves the bottom, accumulating density-length, and records the running total at four
        // heights on the way down. The raymarch then answers "how much cloud is between this sample and
        // the sun" with ONE texture fetch instead of a six-sample cone march — twelve fetches saved on
        // every shaded sample, which is two thirds of what a shaded sample used to cost.
        //
        // The projection, the slice heights and the read-out all live in Common/CloudShadow.glslh, which
        // the CloudMath tests compile as C++: this pass and the march MUST agree about where a world
        // point lands, and a disagreement would put every shadow somewhere other than the cloud that
        // cast it — a thing that looks like a lighting bug and is a coordinate bug.
        //
        // WHAT IT COSTS. 512x512 texels times CLOUD_SHADOW_STEPS samples of CloudDensityCheap (two
        // fetches each), once per frame. That is a FIXED cost — it does not grow with how much cloud is
        // on screen, which is the opposite of the cone march it replaces and the reason the two cross
        // over so heavily in favour of this one.
        //
        // WHAT IT GIVES UP. The cone march samples a widening CONE and so softens a self-shadow
        // terminator; a column is a line and does not. The softening the march keeps is the four-slice
        // interpolation in height plus the map's own texel footprint, which at the default extent is
        // around 120 m — comparable to the cone's own radius. Expect a slightly crisper terminator.

        #include <Common/CloudNoise.glslh>
        #include <Common/CloudGeometry.glslh>
        #include <Common/CloudParams.glslh>
        #include <Common/CloudShadow.glslh>
        #include <Common/CloudDensityProcedural.glslh>

        // rgba16f: density-length is unbounded in principle and a half carries three decimal digits,
        // which is far more than a term that saturates the Beer curve above ~10 will ever need.
        layout(binding = 0, rgba16f) restrict writeonly uniform image2D u_CloudShadowOut;

        PushConstant CloudShadowPush
        {
            // xyz = the world point the map is built around (the camera), w = the map's half-width in
            // world units. Both must match what the raymarch projects with, so both are pushed to the
            // two passes from the same place on the CPU in the same frame.
            vec4 u_ShadowCentre;
        };

        LocalSize(8, 8, 1);

        // Samples taken down the column. Fixed rather than authored: this is not a quality knob an artist
        // would know how to set, and the pass is a fixed cost that the frame budget has to be able to
        // rely on.
        const int CLOUD_SHADOW_STEPS = 24;

        void main()
        {
            ivec2 size  = imageSize(u_CloudShadowOut);
            ivec2 coord = ivec2(gl_GlobalInvocationID.xy);
            if (coord.x >= size.x || coord.y >= size.y)
                return;

            // Empty is the answer to every early-out below, and it has to be WRITTEN: the image is not
            // cleared between frames, so a texel that returned without storing would shadow this frame's
            // clouds with the sun position of some earlier one.
            vec4 result = vec4(0.0f, 0.0f, 0.0f, 0.0f);

            vec2 uv = (vec2(coord) + vec2(0.5f, 0.5f)) / vec2(size);

            vec3  centre = vec3(u_ShadowCentre.x, u_ShadowCentre.y, u_ShadowCentre.z);
            float extent = u_ShadowCentre.w;
            vec3  sunDir = u_SunDirection.xyz;

            // The sun ray for this texel: a point on the plane through the centre, extended along the sun.
            vec3 planePoint = CloudShadowPlanePoint(uv, centre, sunDir, extent);

            float planetRadiusKm = CloudKmFromWorld(u_PlanetRadius);
            float bottomKm       = CloudKmFromWorld(u_LayerBottomAltitude);
            float thicknessKm    = CloudKmFromWorld(u_LayerThickness);

            // The layer segment of THIS TEXEL'S SUN RAY, in signed distances along +sunDir from the plane
            // point. Signed because the plane the map is built on runs through the camera, which is
            // normally below the layer: the segment sits ahead of some texels and behind others, and a
            // clamped entry would throw the latter away — see CloudShadowColumn.
            vec3 originKm = planePoint * (1.0f / CLOUD_WORLD_UNITS_PER_KM);

            CloudShellHit column = CloudShadowColumn(originKm, sunDir, planetRadiusKm, bottomKm, thicknessKm);
            if (!column.Hit)
            {
                imageStore(u_CloudShadowOut, coord, result);
                return;
            }

            // Walked from the TOP down: heights fall monotonically, which is what lets the slices be
            // recorded in one pass with a single comparison and no search.
            float tTop    = CloudWorldFromKm(column.TExit);
            float tBottom = CloudWorldFromKm(column.TEnter);
            float span    = tTop - tBottom;
            if (span <= 0.0f)
            {
                imageStore(u_CloudShadowOut, coord, result);
                return;
            }

            float dt            = span / float(CLOUD_SHADOW_STEPS);
            float densityLength = 0.0f;

            // The slice this step is still filling. The march runs from the layer TOP down, so the height
            // fraction falls monotonically and the slices are crossed in order — no search, one compare.
            int slice = 3;

            for (int i = 0; i < CLOUD_SHADOW_STEPS; ++i)
            {
                float t        = tTop - ( float(i) + 0.5f ) * dt;
                vec3  worldPos = planePoint + sunDir * t;
                vec3  posKm    = worldPos * (1.0f / CLOUD_WORLD_UNITS_PER_KM);
                float height   = CloudHeightFraction(posKm, planetRadiusKm, bottomKm, thicknessKm);

                // Record the running total the moment the march drops past a slice's height, BEFORE this
                // step's own contribution: the slice means "what lies above this height".
                while (slice >= 0 && height <= CloudShadowSliceHeight(slice))
                {
                    if (slice == 3)
                        result.w = densityLength;
                    else if (slice == 2)
                        result.z = densityLength;
                    else if (slice == 1)
                        result.y = densityLength;
                    else
                        result.x = densityLength;
                    --slice;
                }

                densityLength += CloudDensityCheap(worldPos, height) * dt;
            }

            // Any slice the march never dropped past — a ray that left the layer sideways before reaching
            // the base, which every low sun produces — gets the total. It is the honest answer: all of the
            // cloud this ray met lies above that height.
            while (slice >= 0)
            {
                if (slice == 3)
                    result.w = densityLength;
                else if (slice == 2)
                    result.z = densityLength;
                else if (slice == 1)
                    result.y = densityLength;
                else
                    result.x = densityLength;
                --slice;
            }

            imageStore(u_CloudShadowOut, coord, result);
        }
    }
}
