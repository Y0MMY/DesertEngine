Shader "CloudComposite"
{
    Fragment
    {
        // Stage S4 of the volumetric clouds: the COMPOSITE.
        //
        // A fullscreen quad in RenderPhase::Transparency, drawn with a LOAD begin over the finished
        // scene colour, before bloom and before tonemap — so the cloudscape is exposed, glowed and
        // tonemapped exactly like everything else in the frame.
        //
        // The pipeline supplies the blend: source factor One, destination factor SrcAlpha, i.e.
        //
        //     scene = cloud.rgb + scene * cloud.a
        //
        // which is the premultiplied over-operator with .a carrying TRANSMITTANCE rather than opacity.
        // The raymarch produced exactly that pair, so this shader has nothing to reconstruct.
        //
        // NO DEPTH TEST, and none is wanted: a fullscreen quad has no meaningful depth, and occlusion by
        // scene geometry was already resolved inside the march, which clamped every ray to the distance
        // the depth attachment reported. That is the one path that works in Forward and in Deferred.
        //
        // NO TONEMAP, NO GAMMA, NO EXPOSURE. This shader moves linear HDR from one linear HDR target to
        // another.
        //
        // THE MAGNIFICATION IS BILATERAL, not bilinear. The cloud image was produced at Resolution Scale,
        // and the low-resolution texel that straddles a geometry silhouette marched only as far as that
        // geometry (CLD-29 clamps every ray to the scene depth), so it carries almost no cloud. Bilinear
        // magnification smears that emptiness one or two full-resolution pixels into the open sky beside
        // the silhouette — a dark halo that traces every foreground edge. Each tap is therefore weighted
        // by how well its own stop distance agrees with the tap this pixel sits closest to, read from the
        // guide image the raymarch writes alongside its colour. Where the guide agrees — open sky, a flat
        // surface, and every pixel at Resolution Scale = Full — the weights are exactly bilinear again.
        //
        // The guide exists because THIS pass cannot read the scene depth: it runs inside a render pass
        // that has the depth attachment bound, and sampling a bound attachment is a feedback loop. That is
        // the same constraint that put the depth read of CLD-29 in compute.

        #include <Common/CloudGeometry.glslh>
        #include <Common/CloudTemporal.glslh>

        In(0) vec2 v_TexCoord;

        Out(0) vec4 o_Color;

        // The resolved cloud image at Resolution Scale: the temporal resolve's output when Temporal Mode
        // is Reprojection, and the raymarch target itself when it is Off. Which one is bound is the
        // renderer's decision and this shader does not need to know — both carry premultiplied radiance
        // in .rgb and transmittance in .a.
        Uniform(0) sampler2D u_CloudResolved;

        // Per cloud texel, the distance the march was allowed to run to, packed by
        // CloudEncodeGuideDistance. Fetched, never filtered: a filtered packed value decodes to a
        // distance where nothing is.
        Uniform(1) sampler2D u_CloudDepthGuide;

        void main()
        {
            vec2 cloudSize = vec2(textureSize(u_CloudResolved, 0));

            // The texel quad this pixel falls in, in the sampler's own convention: texel centres sit at
            // half-integers, so subtracting 0.5 turns the coordinate into "which four texels and how far
            // between them". At Resolution Scale = Full this lands exactly on a texel centre and the
            // fraction is zero, which is why full resolution costs nothing here.
            vec2  scaled = v_TexCoord * cloudSize - vec2(0.5, 0.5);
            vec2  base   = floor(scaled);
            vec2  frac   = scaled - base;
            ivec2 lo     = ivec2(base);
            ivec2 last   = ivec2(cloudSize) - ivec2(1, 1);

            ivec2 c00 = clamp(lo + ivec2(0, 0), ivec2(0, 0), last);
            ivec2 c10 = clamp(lo + ivec2(1, 0), ivec2(0, 0), last);
            ivec2 c01 = clamp(lo + ivec2(0, 1), ivec2(0, 0), last);
            ivec2 c11 = clamp(lo + ivec2(1, 1), ivec2(0, 0), last);

            float d00 = CloudDecodeGuideDistance(texelFetch(u_CloudDepthGuide, c00, 0));
            float d10 = CloudDecodeGuideDistance(texelFetch(u_CloudDepthGuide, c10, 0));
            float d01 = CloudDecodeGuideDistance(texelFetch(u_CloudDepthGuide, c01, 0));
            float d11 = CloudDecodeGuideDistance(texelFetch(u_CloudDepthGuide, c11, 0));

            CloudUpsampleWeights weights =
                 CloudBilateralUpsampleWeights(frac, d00, d10, d01, d11, CLOUD_GUIDE_RELATIVE_TOLERANCE);

            vec4 cloud = texelFetch(u_CloudResolved, c00, 0) * weights.W00 +
                         texelFetch(u_CloudResolved, c10, 0) * weights.W10 +
                         texelFetch(u_CloudResolved, c01, 0) * weights.W01 +
                         texelFetch(u_CloudResolved, c11, 0) * weights.W11;

            o_Color = vec4(cloud.rgb, clamp(cloud.a, 0.0, 1.0));
        }
    }

    Vertex
    {
        #include <Common/QuadPositions.glslh>
        #include <Common/QuadTextureCoords.glslh>

        Out(0) vec2 v_TexCoord;

        void main()
        {
            v_TexCoord  = QUAD_TEXTURE_COORDINATES[gl_VertexIndex];
            gl_Position = vec4(QUAD_POSITIONS[gl_VertexIndex], 0.0, 1.0);
        }
    }
}
