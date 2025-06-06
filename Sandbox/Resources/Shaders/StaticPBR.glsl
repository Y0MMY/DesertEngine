#pragma stage : vertex

#version 450 

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aTangent;
layout(location = 3) in vec3 aBitangent;
layout(location = 4) in vec2 aTextureCoord;

layout( push_constant ) uniform constants
{
	mat4 project;
	mat4 view;
} m_PushConstants;


layout(location=0) out Vertex
{
	vec3 Position;
	vec3 Normal;
	vec2 Texcoord;
	mat4 View;
} outVertex;

void main()
{
	outVertex.Position = aPosition;
	outVertex.Texcoord = aTextureCoord;
	outVertex.Normal = 	 aNormal;
	outVertex.View = m_PushConstants.view;

	gl_Position =  m_PushConstants.project * m_PushConstants.view * vec4(aPosition, 1.0);
}

#pragma stage : fragment

#version 450 core

layout(location=0) in Vertex
{
	vec3 Position;
	vec3 Normal;
	vec2 Texcoord;
	mat4 View;
} inVertex;

const float PI = 3.141592;
const float Epsilon = 0.00001;

const vec3 Fdielectric = vec3(0.04);


layout(location = 0) out vec4 oColor;

// layout(binding = 0) uniform PBRData {
// 	vec3 		Albedo;
// 	float     	Metallic;
// 	float     	Roughness;
// } pbr;

// layout(binding = 1) uniform LightningUB {
// 	vec3 		Direction;
// } lights;

// layout(binding = 2) uniform GlobalUB {
// 	vec3 CameraPosition;
// } global;

float ndfGGX(float cosLh, float roughness)
{
	float alpha   = roughness * roughness;
	float alphaSq = alpha * alpha;

	float denom = (cosLh * cosLh) * (alphaSq - 1.0) + 1.0;
	return alphaSq / (PI * denom * denom);
}

float gaSchlickG1(float cosTheta, float k)
{
	return cosTheta / (cosTheta * (1.0 - k) + k);
}

float gaSchlickGGX(float cosLi, float cosLo, float roughness)
{
	float r = roughness + 1.0;
	float k = (r * r) / 8.0; // Epic suggests using this roughness remapping for analytic lights.
	return gaSchlickG1(cosLi, k) * gaSchlickG1(cosLo, k);
}

vec3 fresnelSchlick(vec3 F0, float cosTheta)
{
	return F0 + (vec3(1.0) - F0) * pow(1.0 - cosTheta, 5.0);
}

// vec3 Lightning(vec3 View, vec3 N, vec3 F0, float metalness, float roughness, vec3 albedo)
// {
// 	vec3 color;

// 	for(uint i = 0; i < 1; i++)
// 	{
// 		vec3 Li = - lights.Direction;
// 		 vec3 Lradiance = vec3(1.0, 1.0, 1.0);

// 		vec3 Lh = normalize(Li + View);

// 		float cosLi = max(0.0, dot(N, Li));
// 		float cosLh = max(0.0, dot(N, Lh));
// 		float cosLo = max(0.0, dot(N, View));

// 		vec3 F  = fresnelSchlick(F0, max(0.0, dot(Lh, View))); // todo: view
// 		// Calculate normal distribution for specular BRDF.
// 		float D = ndfGGX(cosLh, roughness);
// 		// Calculate geometric attenuation for specular BRDF.
// 		float G = gaSchlickGGX(cosLi, cosLo, roughness);

// 		vec3 kd = mix(vec3(1.0) - F, vec3(0.0), metalness);
// 		vec3 diffuseBRDF = kd * albedo;
// 		vec3 specularBRDF  = (F * D * G) / max(Epsilon, 4.0 * cosLi * cosLo);

// 		color += (diffuseBRDF + specularBRDF) * Lradiance * cosLi;
// 	}

// 	return color;
// }

void main() {

	// const vec3 albedo = pbr.Albedo;
	// const float metalness = pbr.Metallic;
	// const float roughness  = pbr.Roughness;

	// const vec3 View = normalize(global.CameraPosition - inVertex.Position);

	// vec3 F0 = mix(Fdielectric, albedo, metalness);
	// vec3 light = Lightning(View, inVertex.Normal, F0, metalness, roughness, albedo );
    oColor = vec4(vec3(inVertex.Position * 0.5 + 0.5) , 1.0);  // [-1,1] → [0,1]
}