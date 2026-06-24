#version 450 core

// Separable Gaussian blur. Direction (in texels) comes via push constant: (spread,0) horizontal,
// (0,spread) vertical. Run H then V (and repeat) to widen the bloom.

layout(location = 0) in  vec2 v_TexCoord;
layout(set = 0, binding = 0) uniform sampler2D u_InputTexture;
layout(location = 0) out vec4 oColor;

layout(push_constant) uniform PushConstants
{
    vec2 u_Direction;
};

void main()
{
    vec2 texel  = 1.0 / vec2( textureSize( u_InputTexture, 0 ) );
    vec2 offset = u_Direction * texel;

    const float w[5] = float[]( 0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216 );

    vec3 result = texture( u_InputTexture, v_TexCoord ).rgb * w[0];
    for ( int i = 1; i < 5; ++i )
    {
        result += texture( u_InputTexture, v_TexCoord + offset * float( i ) ).rgb * w[i];
        result += texture( u_InputTexture, v_TexCoord - offset * float( i ) ).rgb * w[i];
    }
    oColor = vec4( result, 1.0 );
}
