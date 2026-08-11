Shader "CloudTemporalResolve"
{
    Compute
    {
        // Stage S3 of the volumetric clouds: THE TEMPORAL RESOLVE.
        //
        // One dispatch over the raymarch target. Reads the frame that was just marched and the frame this
        // stage resolved last time, reprojects the history by the CAMERA's motion alone, clamps it to what
        // the current neighbourhood can vouch for, and blends. The result is written to the other half of
        // a two-image ping-pong and is what the composite magnifies.
        //
        // WHY IT EXISTS. At Resolution Scale = Half with 64..128 steps and a per-pixel jitter of the ray
        // origin, a single marched frame BOILS: the dither that removes the march's banding is a different
        // dither every frame. Accumulating over frames is what turns that dither back into detail. The
        // reference project has nothing to copy here — its "temporal upscaling" is a spatial near/far
        // split with no history at all, and the one reprojection it contains is commented out
        // (RESEARCH_REFERENCE D.2, D.3) — so this is designed rather than ported.
        //
        // CAMERA-ONLY REPROJECTION, and no motion vectors (CLD-32a constraint 1). The clouds' own motion
        // is wind, which the march already integrates into the sample position; a motion-vector target
        // would be new engine infrastructure for an effect that is already there.
        //
        // TEMPORAL MODE = OFF DOES NOT REACH THIS SHADER. Off is not a branch in here: the renderer does
        // not dispatch this stage at all, allocates no history, and points the composite straight at the
        // raymarch target. That is what makes "Off equals the march bit for bit" a fact about the pipeline
        // rather than a claim about a code path.
        //
        // ---- THE ARTEFACTS THIS STAGE BUYS (CLD-32b) -------------------------------------------------
        // None of these can be looked at in the development environment, so they are named in advance.
        // Each row says what triggers it and which knob trades it away.
        //
        //   Disocclusion trails - a smear behind geometry that uncovers new cloud.
        //     Trigger: translating past a foreground object; flying out of a canyon.
        //     Knob:    lower Temporal Clamp Scale. Lowering Temporal Blend Factor makes it WORSE (it
        //              keeps more of the history that is wrong); the neighbourhood clamp is the fix.
        //
        //   Inertia on fast rotation - clouds "catch up" after a whip pan.
        //     Trigger: angular velocity high enough that most reprojected UVs leave the screen.
        //     Knob:    raise Temporal Blend Factor, or Temporal Mode = Off. Note that the pixels which
        //              left the screen carry no history at all; the inertia is in the pixels that stayed.
        //
        //   Softness in the band that a turn uncovers - one frame of 3x3 blur along the leading edge.
        //     Trigger: any camera rotation, strongest at the screen edge it turns toward.
        //     Knob:    none, and deliberately. This is what the edge costs instead of BOILING: a pixel
        //              with no history resolves to the neighbourhood mean rather than to one jittered
        //              half-resolution sample. It lasts exactly until that pixel has a history of its
        //              own, which is the next frame.
        //
        //   Shell-parallax error - the reprojection puts every pixel on the shell mid-surface, so a cloud
        //              much nearer than that reprojects slightly wrong.
        //     Trigger: camera inside or just below the layer.
        //     Knob:    none. Inherent to camera-only reprojection, and bounded because near clouds are
        //              also where NearFadeMinDensity thins them out.
        //
        //   Ghosting on wind-driven silhouette change - an edge that moved because the CLOUD moved, not
        //              because the camera did.
        //     Trigger: high Animation Speed / Wind Influence, e.g. the Storm preset.
        //     Knob:    the neighbourhood clamp - lower Temporal Clamp Scale. This is the case the clamp
        //              exists for: the reprojection cannot see wind, and the clamp is what notices that
        //              the history no longer resembles its own neighbourhood.
        //
        //   Sun-glint flicker - a bright forward-scatter pixel enters and leaves the clamp box.
        //     Trigger: looking near the sun with a high Silver Lining Intensity.
        //     Knob:    raise Temporal Clamp Scale, accepting more ghosting in exchange.

        #include <Common/CloudGeometry.glslh>
        #include <Common/CloudParams.glslh>
        #include <Common/CloudTemporal.glslh>

        // The resolved image, which is also the history the NEXT frame reads. rgba16f, the same format
        // and the same contents as the raymarch target: premultiplied radiance in .rgb, transmittance
        // in .a.
        layout(binding = 0, rgba16f) restrict writeonly uniform image2D u_CloudResolved;

        // The frame that was just marched. Point-sampled with texelFetch only - this stage never
        // magnifies, it resolves one texel onto the same texel.
        Uniform(3) sampler2D u_CloudCurrent;

        // What this stage produced last frame, sampled BILINEARLY at the reprojected coordinate. Bilinear
        // is the point: the reprojected position lands between texels, and snapping it to the nearest one
        // would re-introduce, once per frame, exactly the quantisation the accumulation is removing.
        Uniform(4) sampler2D u_CloudHistory;

        PushConstant CloudTemporalPush
        {
            // NDC to a CAMERA-RELATIVE world point. The eye translation is removed on the CPU BEFORE the
            // matrix is inverted, because inverting the absolute one in single precision tilts every
            // reconstructed ray by an angle that grows with the camera's distance from the world origin —
            // measured at a full degree 30 km out, which is ten pixels of history sampled from the wrong
            // place. See CloudPayload.hpp.
            mat4 u_InverseViewProjection;
            // Rows 0, 1 and 3 of (previous view-projection x translate(current camera position)). Three
            // rows because clip z is never used, and premultiplied by the camera translation because a
            // camera-relative point keeps the arithmetic away from planet-scale magnitudes.
            vec4 u_PreviousRow0;
            vec4 u_PreviousRow1;
            vec4 u_PreviousRow3;
            // xyz = camera position in world units; w = 1 when the history image already holds a resolved
            // frame, 0 on the first dispatch after the images are allocated.
            vec4 u_CameraPosition;
        };

        LocalSize(8, 8, 1);

        void main()
        {
            ivec2 size  = imageSize(u_CloudResolved);
            ivec2 coord = ivec2(gl_GlobalInvocationID.xy);
            if (coord.x >= size.x || coord.y >= size.y)
                return;

            ivec2 last    = size - ivec2(1, 1);
            vec4  current = texelFetch(u_CloudCurrent, coord, 0);

            // The 3x3 min/max of the current frame, clamped at the image border rather than wrapped: a
            // wrapped neighbour is a sample from the opposite edge of the sky and would authorise a
            // history value that nothing near this pixel supports.
            vec4 neighbourhoodMin = current;
            vec4 neighbourhoodMax = current;
            // Summed in the same loop: the mean is what a pixel with no history resolves to, and taking
            // it here means the nine taps are fetched once and used twice.
            vec4 neighbourhoodSum = vec4(0.0f, 0.0f, 0.0f, 0.0f);
            for (int dy = -1; dy <= 1; ++dy)
            {
                for (int dx = -1; dx <= 1; ++dx)
                {
                    ivec2 tap = clamp(coord + ivec2(dx, dy), ivec2(0, 0), last);
                    vec4  c   = texelFetch(u_CloudCurrent, tap, 0);
                    neighbourhoodMin = min(neighbourhoodMin, c);
                    neighbourhoodMax = max(neighbourhoodMax, c);
                    neighbourhoodSum = neighbourhoodSum + c;
                }
            }

            // Nine taps always, because the border ones are CLAMPED into range rather than skipped — the
            // divisor is a constant and there is no edge case to get wrong.
            vec4 neighbourhoodMean = neighbourhoodSum * (1.0f / 9.0f);

            CloudTemporalBox box = CloudNeighbourhoodBox(neighbourhoodMin, neighbourhoodMax,
                                                         u_TemporalClampScale);

            vec2 uv = (vec2(coord) + vec2(0.5f, 0.5f)) / vec2(size);

            vec3 cameraPositionKm = vec3(u_CameraPosition.x, u_CameraPosition.y, u_CameraPosition.z) *
                                    (1.0f / CLOUD_WORLD_UNITS_PER_KM);

            CloudReprojection reprojection =
                 CloudReprojectThroughShell(uv, u_InverseViewProjection, cameraPositionKm,
                                            CloudKmFromWorld(u_PlanetRadius),
                                            CloudKmFromWorld(u_LayerBottomAltitude),
                                            CloudKmFromWorld(u_LayerThickness), u_PreviousRow0,
                                            u_PreviousRow1, u_PreviousRow3);

            bool historyUsable = u_CameraPosition.w > 0.5f && reprojection.Valid;

            // textureLod, not texture(): a compute shader has no derivatives, so an implicit level of
            // detail is undefined there. The image has one mip, and this says so.
            vec4 history = textureLod(u_CloudHistory, reprojection.Uv, 0.0f);

            vec4 resolved = CloudTemporalResolve(current, neighbourhoodMean, history, historyUsable,
                                                  u_TemporalBlendFactor, box);

            imageStore(u_CloudResolved, coord, resolved);
        }
    }
}
