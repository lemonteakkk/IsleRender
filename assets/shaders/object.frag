#include "common.glsl"
#include "gbuffer_out.glsl"

in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV;
in vec3 vColor;
flat in float vVariation;

uniform float uRoughness;
uniform float uMetallic;
uniform float uEmissive;

void main() {
    vec3 N = normalize(vNormal);
    if (!gl_FrontFacing) N = -N;
    // subtle per-instance hue/brightness variation so forests don't look cloned
    vec3 tint = mix(vec3(0.85, 0.95, 0.85), vec3(1.15, 1.05, 0.9), vVariation);
    vec3 albedo = vColor * tint;
    float emissive = vUV.x * uEmissive * uLightColor.a;  // lamps glow only when lanterns are on
    float roughness = uRoughness >= 0.0 ? uRoughness : vUV.y;  // < 0: per-vertex roughness (merged props)
    writeGBuffer(albedo, N, roughness, uMetallic, emissive, 2.0);
}
