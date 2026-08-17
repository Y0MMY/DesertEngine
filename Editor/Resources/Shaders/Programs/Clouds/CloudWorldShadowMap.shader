Shader "CloudWorldShadowMap"
{
    Compute
    {
        // Stage S1c of the volumetric clouds: THE SHADOW THE SKY CASTS ON THE WORLD.
        //
        // One texel per sun ray, exactly like Programs/Clouds/CloudShadowMap.shader and through exactly the
        // same projection (Common/CloudShadowProjection.glslh). What differs is what a texel MEANS, and why
        // this is a second pass rather than two more channels on the first one:
        //
        //   * The four-slice map is PER LAYER — one volume slice per shell, each with its own extent — and
        //     answers "how much of layer L's mass is above a sample at height fraction h INSIDE L". The
        //     ground is under every deck at once and is inside none of them, so none of that question
        //     applies to it. This map marches the WHOLE sky in one column and stores one answer.
        //   * The four-slice map is gated by each layer's own Cloud Shadow Map quality setting; this one is
        //     gated by a directional light's Cast Cloud Shadows. Two independent switches cannot share one
        //     dispatch without one of them silently turning the other on.
        //   * The four-slice map stores density-length BEFORE ExtinctionScale, because the march multiplies
        //     it in per layer. A terrain shader has no layer to take an ExtinctionScale from, so this map
        //     stores true optical quantities with every layer's own scale already folded in.
        //
        // The encoding is UE's (VolumetricCloudCommon.ush:54-68), and the read-out that inverts it is
        // Common/CloudWorldShadow.glslh, which the CloudMath tests compile as C++:
        //
        //     r = front depth (km along the sun ray, from the map plane, increasing away from the sun)
        //     g = mean extinction per km over the stretch that had cloud in it
        //     b = the whole column's optical depth — the cap
        //
        // WHAT IT COSTS. 512x512 texels, CLOUD_WORLD_SHADOW_STEPS samples down the column, and at each
        // sample one CloudDensityCheap per live layer. That is a FIXED cost per frame, independent of how
        // much cloud is on screen and of how many pixels the march visits — and it is paid only while some
        // directional light in the scene has Cast Cloud Shadows on, which ships OFF exactly as UE ships it.

        #include <Common/CloudNoise.glslh>
        #include <Common/CloudGeometry.glslh>
        #include <Common/CloudParams.glslh>
        #include <Common/CloudShadow.glslh>
        #include <Common/CloudWorldShadow.glslh>

        // ONLY the hero clouds whose Casts Cloud Shadow is on, the same prefix of the instance buffer the
        // four-slice pass marches — a hero cloud that does not shadow the sky does not shadow the ground.
        #define CLOUD_VOXEL_INSTANCE_COUNT u_VoxelShadowCount
        #include <Common/CloudDensityCompose.glslh>

        // rgba16f, and a 2D image rather than a volume: this map is the WHOLE sky's column, so there is
        // nothing to put in a second slice. Half floats carry three decimal digits, and the quantity whose
        // precision actually matters — the difference between a receiver's depth and the column's front
        // depth — is kilometres wide against a quantisation step of tens of metres.
        layout(binding = 0, rgba16f) restrict writeonly uniform image2D u_CloudWorldShadowOut;

        PushConstant CloudWorldShadowPush
        {
            // xyz = the world point the map is built around (the camera); w = the half-width in world
            // units. Both ride here and NOT in the parameter block, unlike the four-slice pass's extent:
            // the consumers of this map are the terrain and the lit meshes, which never see
            // CloudLayerPayload, so the two numbers they need travel to them through their own uniform
            // block — and the C++ that fills that block is the same struct that fills this push constant
            // (Graphic::CloudWorldShadowInput), so the writer and the readers cannot disagree.
            vec4 u_WorldShadowCentreExtent;
        };

        LocalSize(8, 8, 1);

        // Samples taken down the column. More than the four-slice pass's 24 because this column spans the
        // WHOLE sky — from the top of the highest layer to the base of the lowest, including the empty
        // kilometres between a deck and a sheet above it, which those 24 never had to cross.
        const int CLOUD_WORLD_SHADOW_STEPS = 32;

        void main()
        {
            ivec2 size  = imageSize(u_CloudWorldShadowOut);
            ivec2 coord = ivec2(gl_GlobalInvocationID.xy);
            if (coord.x >= size.x || coord.y >= size.y)
                return;

            // Empty is the answer to every early-out below, and it has to be WRITTEN: the image is not
            // cleared between frames, so a texel that returned without storing would shadow this frame's
            // ground with the sun position of some earlier one.
            vec4 result = vec4(0.0f, 0.0f, 0.0f, 0.0f);

            vec2 uv = (vec2(float(coord.x), float(coord.y)) + vec2(0.5f, 0.5f)) /
                      vec2(float(size.x), float(size.y));

            vec3  centre = u_WorldShadowCentreExtent.xyz;
            float extent = u_WorldShadowCentreExtent.w;
            vec3  sunDir = u_SunDirection.xyz;

            vec3 planePoint = CloudShadowPlanePoint(uv, centre, sunDir, extent);

            float planetRadiusKm = CloudKmFromWorld(u_PlanetRadius);

            // THE UNION SHELL. One column has to cross every layer, so it is bounded by the lowest base and
            // the highest top in the scene; the empty air between two decks simply contributes no density.
            // Marching each layer's own shell separately would be cheaper per layer and would produce two
            // front depths, and the encoding has room for one — the whole point of it is that a receiver
            // asks one question and gets one number.
            int   liveLayers = max(u_LayerCount, 1);
            float bottomKm   = 1.0e9f;
            float topKm      = -1.0e9f;
            for (int i = 0; i < CLOUD_MAX_LAYERS; ++i)
            {
                if (i >= liveLayers)
                    continue;
                CloudSelectLayer(i);
                bottomKm = min(bottomKm, CloudKmFromWorld(u_LayerBottomAltitude));
                topKm    = max(topKm, CloudKmFromWorld(u_LayerBottomAltitude + u_LayerThickness));
            }

            if (topKm <= bottomKm)
            {
                imageStore(u_CloudWorldShadowOut, coord, result);
                return;
            }

            vec3 originKm = planePoint * (1.0f / CLOUD_WORLD_UNITS_PER_KM);

            // Signed distances along +sunDir from the plane point, for the same reason CloudShadowColumn is
            // signed: the plane runs through the CAMERA, which normally sits below the layer, so the segment
            // is ahead of some texels and behind others — and a clamped entry is how the four-slice pass
            // once ended up marching the antipodal side of the planet.
            CloudShellHit column =
                 CloudShadowColumn(originKm, sunDir, planetRadiusKm, bottomKm, topKm - bottomKm);
            if (!column.Hit)
            {
                imageStore(u_CloudWorldShadowOut, coord, result);
                return;
            }

            float tTop    = CloudWorldFromKm(column.TExit);
            float tBottom = CloudWorldFromKm(column.TEnter);
            float span    = tTop - tBottom;
            if (span <= 0.0f)
            {
                imageStore(u_CloudWorldShadowOut, coord, result);
                return;
            }

            float dt   = span / float(CLOUD_WORLD_SHADOW_STEPS);
            float dtKm = CloudKmFromWorld(dt);

            // Marched from the TOP down, so the depth along the sun grows monotonically and the FIRST
            // sample with medium in it is the front of the cloud, with no search and no second pass.
            float opticalDepth = 0.0f;
            float frontKm      = 0.0f;
            float backKm       = 0.0f;
            bool  metCloud     = false;

            for (int i = 0; i < CLOUD_WORLD_SHADOW_STEPS; ++i)
            {
                float t        = tTop - (float(i) + 0.5f) * dt;
                vec3  worldPos = planePoint + sunDir * t;
                vec3  posKm    = worldPos * (1.0f / CLOUD_WORLD_UNITS_PER_KM);

                // The plane point sits ON the plane, so its component along the sun is zero and this
                // sample's depth (Common/CloudShadowProjection.glslh's convention: increasing AWAY from the
                // sun) is simply -t. Written out rather than called, because the call would recompute a dot
                // product whose answer is already in hand.
                float depthKm = -CloudKmFromWorld(t);

                // EVERY LAYER AT THIS POINT. A sample outside a shell contributes nothing — the unclamped
                // height is what says so; the clamped one would report a sample a kilometre above the tops
                // as sitting exactly on them and shadow the ground with cloud that is not there.
                float sigma = 0.0f;
                for (int layer = 0; layer < CLOUD_MAX_LAYERS; ++layer)
                {
                    if (layer >= liveLayers)
                        continue;

                    CloudSelectLayer(layer);

                    float layerBottomKm = CloudKmFromWorld(u_LayerBottomAltitude);
                    float layerThickKm  = CloudKmFromWorld(u_LayerThickness);
                    float height = CloudLayerHeight(posKm, planetRadiusKm, layerBottomKm, layerThickKm);
                    if (height < 0.0f || height > 1.0f)
                        continue;

                    // ExtinctionScale folded in HERE, per layer, because the receiver has no layer to take
                    // it from. This is the one thing the four-slice map deliberately does not do.
                    sigma += CloudDensityCheap(worldPos, height) * u_ExtinctionTint.w;
                }
                sigma *= CLOUD_EXTINCTION_PER_WORLD_UNIT;

                if (sigma > 0.0f)
                {
                    // Half a step above the first sample and half a step below the last: the samples are at
                    // step CENTRES, and the medium they stand for fills the step. Taking the centres
                    // themselves would make a one-sample column zero kilometres thick and its mean
                    // extinction infinite.
                    if (!metCloud)
                    {
                        frontKm  = depthKm - 0.5f * dtKm;
                        metCloud = true;
                    }
                    backKm = depthKm + 0.5f * dtKm;
                }

                opticalDepth += sigma * dt;
            }

            if (metCloud)
            {
                result.x = frontKm;
                // Spread over the stretch that actually had cloud in it, which is what makes the read-out's
                // linear ramp land on the deck's own base and top rather than on the whole sky.
                result.y = opticalDepth / max(backKm - frontKm, dtKm);
                result.z = opticalDepth;
            }

            imageStore(u_CloudWorldShadowOut, coord, result);
        }
    }
}
