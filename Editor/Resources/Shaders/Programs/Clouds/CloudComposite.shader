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

        In(0) vec2 v_TexCoord;

        Out(0) vec4 o_Color;

        // The raymarch output, at Resolution Scale. Magnified by the sampler's own bilinear filter: the
        // depth-aware bilateral upsample belongs with the temporal resolve, which is the stage that also
        // needs the low-resolution depth it would read.
        Uniform(0) sampler2D u_CloudScatter;

        void main()
        {
            vec4 cloud = texture(u_CloudScatter, v_TexCoord);
            o_Color    = vec4(cloud.rgb, clamp(cloud.a, 0.0, 1.0));
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
