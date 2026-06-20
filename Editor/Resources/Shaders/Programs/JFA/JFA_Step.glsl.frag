#version 450

layout(location = 0) in vec2 v_TexCoord;

layout(location = 0) out vec4 o_Result;

layout(set = 0, binding = 0) uniform sampler2D u_InputTexture;

layout(push_constant) uniform PushConstants
{
    int u_StepLength;
};

void main()
{
    ivec2 texSize = textureSize(u_InputTexture, 0);
    vec2 pixelCoord = v_TexCoord * vec2(texSize);
    
    vec4 bestSeed = texture(u_InputTexture, v_TexCoord);
    float bestDist = 1e20;
    
    if (bestSeed.x >= 0.0)
    {
        bestDist = distance(bestSeed.xy, pixelCoord);
    }
    
    for (int y = -1; y <= 1; y++)
    {
        for (int x = -1; x <= 1; x++)
        {
            if (x == 0 && y == 0)
                continue;
            
            vec2 offset = vec2(float(x), float(y)) * float(u_StepLength) / vec2(texSize);
            vec4 neighborSeed = texture(u_InputTexture, v_TexCoord + offset);
            
            if (neighborSeed.x >= 0.0)
            {
                float d = distance(neighborSeed.xy, pixelCoord);
                if (d < bestDist)
                {
                    bestDist = d;
                    bestSeed = neighborSeed;
                }
            }
        }
    }
    
    o_Result = bestSeed;
}