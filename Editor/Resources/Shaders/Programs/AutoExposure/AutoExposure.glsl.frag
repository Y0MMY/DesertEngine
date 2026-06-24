#version 450 core

// Eye-adaptation: outputs (into a 1x1 target) the temporally-adapted average scene luminance.
// Average = geometric mean of an NxN grid of the HDR scene. Smoothly lerped from the previous frame's
// value (read from u_PrevLuminance, also 1x1) so exposure eases in rather than popping.

layout(location = 0) in  vec2 v_TexCoord;
layout(set = 0, binding = 0) uniform sampler2D u_SceneTexture;
layout(set = 0, binding = 1) uniform sampler2D u_PrevLuminance; // 1x1
layout(location = 0) out vec4 oLuminance;

layout(push_constant) uniform PushConstants
{
    float u_DeltaTime;
    float u_AdaptSpeed;
    float u_MinLuma;
    float u_MaxLuma;
};

float luma( vec3 c )
{
    return dot( c, vec3( 0.2126, 0.7152, 0.0722 ) );
}

void main()
{
    const int N      = 8;
    float     logSum = 0.0;
    for ( int y = 0; y < N; ++y )
    {
        for ( int x = 0; x < N; ++x )
        {
            vec2  uv = ( vec2( x, y ) + 0.5 ) / float( N );
            float l  = luma( texture( u_SceneTexture, uv ).rgb );
            logSum += log( max( l, 1e-4 ) );
        }
    }
    float avg = exp( logSum / float( N * N ) );
    avg = clamp( avg, u_MinLuma, u_MaxLuma );

    float prev = texture( u_PrevLuminance, vec2( 0.5 ) ).r;
    if ( prev <= 0.0 )
        prev = avg; // first frame / uninitialised

    float adapted = mix( prev, avg, 1.0 - exp( -u_DeltaTime * u_AdaptSpeed ) );
    oLuminance = vec4( adapted, 0.0, 0.0, 1.0 );
}
