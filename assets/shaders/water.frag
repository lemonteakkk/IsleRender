#include "common.glsl"
#include "pbr.glsl"
#include "shadow.glsl"

// Forward-shaded water: screen-space refraction with depth-based absorption,
// Fresnel sky reflection, sun glint and shoreline foam.
layout(binding = 0) uniform sampler2D uSceneColor;
layout(binding = 1) uniform sampler2D uSceneDepth;

in vec3 vWorldPos;
in vec3 vNormal;
out vec4 fragColor;

layout(binding = 2) uniform sampler2D uWaterDetail;  // RG = slope, B = height of a tileable fBm
uniform int uProceduralDetail;                        // 1 = old per-pixel noise path (A/B testing)

vec3 detailNormalProcedural(vec2 xz, float t) {
    float e = 0.15;
    vec2 p1 = xz * 0.35 + vec2(t * 0.25, t * 0.18);
    vec2 p2 = xz * 0.8 - vec2(t * 0.3, -t * 0.22);
    float h = fbm2(p1) + 0.5 * fbm2(p2);
    float hx = fbm2(p1 + vec2(e, 0)) + 0.5 * fbm2(p2 + vec2(e, 0));
    float hz = fbm2(p1 + vec2(0, e)) + 0.5 * fbm2(p2 + vec2(0, e));
    return normalize(vec3(h - hx, e * 1.6, h - hz));
}

// Same two scrolling noise layers, read from a precomputed tileable slope texture:
// 2 filtered fetches instead of 24 value-noise evaluations.
vec3 detailNormal(vec2 xz, float t) {
    if (uProceduralDetail != 0) return detailNormalProcedural(xz, t);
    vec2 g1 = texture(uWaterDetail, (xz * 0.35 + vec2(t * 0.25, t * 0.18)) / 8.0).rg * 8.0;  // d/dp1
    vec2 g2 = texture(uWaterDetail, (xz * 0.8 - vec2(t * 0.3, -t * 0.22)) / 8.0).rg * 8.0;   // d/dp2
    vec2 dhdw = g1 * 0.35 + 0.5 * g2 * 0.8;  // slope in world units
    return normalize(vec3(-dhdw.x / 0.56, 1.0, -dhdw.y / 0.56));
}

float foamPattern(vec2 p) {
    if (uProceduralDetail != 0) return fbm2(p);
    return texture(uWaterDetail, p / 8.0).b;
}

void main() {
    vec2 uv = gl_FragCoord.xy * uScreen.zw;
    float t = uCamPos.w;
    float dist = length(vWorldPos - uCamPos.xyz);
    vec3 dn = detailNormal(vWorldPos.xz, t);
    float detailFade = 1.0 - smoothstep(60.0, 300.0, dist);
    vec3 N = normalize(vNormal + vec3(dn.x, 0.0, dn.z) * detailFade * 1.2);
    vec3 V = normalize(uCamPos.xyz - vWorldPos);

    // water thickness along the view ray (distance from surface to the seabed behind it)
    vec3 bed = reconstructWorldPos(uv, texture(uSceneDepth, uv).r);
    float thickness = max(length(bed - uCamPos.xyz) - dist, 0.0);

    vec2 refrUV = uv + N.xz * 0.035 * clamp(thickness / 3.0, 0.0, 1.0);
    vec3 bedR = reconstructWorldPos(refrUV, texture(uSceneDepth, refrUV).r);
    if (length(bedR - uCamPos.xyz) < dist) refrUV = uv;  // refracted sample is in front of water
    vec3 refr = texture(uSceneColor, refrUV).rgb;

    float night = uLightDir.w;
    vec3 ambient = mix(uHorizon.rgb, uZenith.rgb, 0.5);
    vec3 deep = vec3(0.004, 0.035, 0.05) * (ambient * 2.0 + uLightColor.rgb * 0.1 * max(uLightDir.y, 0.0));
    vec3 absorb = exp(-thickness * vec3(0.38, 0.09, 0.07));
    vec3 scatterColor = vec3(0.02, 0.16, 0.16) * (ambient + uLightColor.rgb * 0.06);
    vec3 under = refr * absorb + mix(scatterColor, deep, 1.0 - exp(-thickness * 0.08)) * (1.0 - absorb);

    float NdotV = max(dot(N, V), 0.0);
    float F = 0.02 + 0.98 * pow(1.0 - NdotV, 5.0);
    vec3 R = reflect(-V, N);
    R.y = abs(R.y);
    vec3 refl = skyColor(R, false);

    float shadow = shadowFactor(vWorldPos, vec3(0, 1, 0));
    vec3 L = normalize(uLightDir.xyz);
    vec3 H = normalize(L + V);
    float spec = D_GGX(max(dot(N, H), 0.0), 0.08) * F_Schlick(max(dot(H, V), 0.0), vec3(0.02)).x;
    vec3 glint = uLightColor.rgb * spec * max(dot(N, L), 0.0) * shadow * 0.25;

    vec3 color = mix(under, refl, F) + glint;

    // shoreline foam: thin water + animated noise bands
    float foamBand = 1.0 - smoothstep(0.0, 1.1, thickness);
    float foamNoise = foamPattern(vWorldPos.xz * 0.9 + vec2(t * 0.4, -t * 0.3));
    float waves = 0.5 + 0.5 * sin(thickness * 6.0 - t * 2.2 + foamNoise * 4.0);
    float foam = foamBand * smoothstep(0.35, 0.75, foamNoise * 0.7 + waves * 0.5);
    foam += smoothstep(0.55, 0.9, vWorldPos.y) * 0.25 * foamNoise;  // crest foam
    vec3 foamLight = uLightColor.rgb * max(dot(N, L), 0.0) * shadow * 0.25 + ambient;
    color = mix(color, vec3(0.9) * foamLight, clamp(foam, 0.0, 1.0));

    color = applyFog(color, vWorldPos);
    fragColor = vec4(color, 1.0);
}
