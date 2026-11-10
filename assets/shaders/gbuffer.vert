#include "common.glsl"
#include "skinning.glsl"

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec3 aColor;
#ifdef INSTANCED
layout(location = 4) in mat4 aInstance;
#endif

uniform mat4 uModel;

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUV;
out vec3 vColor;
flat out float vVariation;

void main() {
    mat4 M = uModel;
#ifdef INSTANCED
    M = aInstance * uModel;
#endif
#ifdef SKINNED
    M = skinMatrix();
    vVariation = 0.5;
#else
    vVariation = hash12(M[3].xz);  // per-instance tint seed
#endif
    vec4 wp = M * vec4(aPos, 1.0);
    vWorldPos = wp.xyz;
    vNormal = normalize(mat3(M) * aNormal);
    vUV = aUV;
    vColor = aColor;
    gl_Position = uViewProj * wp;
}
