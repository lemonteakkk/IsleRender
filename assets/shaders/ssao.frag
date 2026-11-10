#include "common.glsl"

// Normal-oriented hemisphere SSAO, evaluated at half resolution.
layout(binding = 0) uniform sampler2D uDepth;
layout(binding = 1) uniform sampler2D uNormal;
layout(binding = 2) uniform sampler2D uNoise;

uniform vec3 uKernel[16];
uniform float uRadius;
uniform float uPower;

in vec2 vUV;
out float fragAO;

vec3 viewPos(vec2 uv) {
    float d = textureLod(uDepth, uv, 0.0).r;
    vec4 p = uInvProj * vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
    return p.xyz / p.w;
}

void main() {
    float depth = textureLod(uDepth, vUV, 0.0).r;
    if (depth >= 1.0) { fragAO = 1.0; return; }

    vec3 P = viewPos(vUV);
    vec3 N = normalize(mat3(uView) * textureLod(uNormal, vUV, 0.0).xyz);
    vec2 noiseScale = uScreen.xy * 0.5 / 4.0;
    vec3 rnd = vec3(texture(uNoise, vUV * noiseScale).xy, 0.0);
    vec3 T = normalize(rnd - N * dot(rnd, N));
    vec3 B = cross(N, T);
    mat3 TBN = mat3(T, B, N);

    // scale radius with distance a bit so far terrain still gets contact shading
    float radius = uRadius * (1.0 + max(-P.z - 40.0, 0.0) * 0.02);
    float occlusion = 0.0;
    for (int i = 0; i < 16; ++i) {
        vec3 S = P + TBN * uKernel[i] * radius;
        vec4 o = uProj * vec4(S, 1.0);
        vec2 suv = o.xy / o.w * 0.5 + 0.5;
        if (any(lessThan(suv, vec2(0.0))) || any(greaterThan(suv, vec2(1.0)))) continue;
        float sceneZ = viewPos(suv).z;
        float range = smoothstep(0.0, 1.0, radius / abs(P.z - sceneZ));
        occlusion += (sceneZ >= S.z + 0.03 * radius ? 1.0 : 0.0) * range;
    }
    fragAO = pow(clamp(1.0 - occlusion / 16.0, 0.0, 1.0), uPower);
}
