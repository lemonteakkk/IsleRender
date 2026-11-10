#include "common.glsl"
#include "pbr.glsl"
#include "shadow.glsl"

layout(binding = 0) uniform sampler2D gAlbedoTex;
layout(binding = 1) uniform sampler2D gNormalTex;
layout(binding = 2) uniform sampler2D gMaterialTex;
layout(binding = 3) uniform sampler2D gDepthTex;
layout(binding = 4) uniform sampler2D uSSAO;

layout(std140, binding = 1) uniform Lights {
    vec4 uLightPosRadius[64];
    vec4 uLightColorIntensity[64];
};

in vec2 vUV;
out vec4 fragColor;

vec3 cascadeTint(vec3 worldPos) {
    float viewDepth = -(uView * vec4(worldPos, 1.0)).z;
    int c = selectCascade(viewDepth);
    if (c == 0) return vec3(1.0, 0.35, 0.35);
    if (c == 1) return vec3(0.35, 1.0, 0.35);
    if (c == 2) return vec3(0.35, 0.5, 1.0);
    return vec3(0.6);
}

void main() {
    float depth = texture(gDepthTex, vUV).r;
    vec3 worldPos = reconstructWorldPos(vUV, depth);
    int debugView = int(uMisc.x);

    if (depth >= 1.0) {
        vec3 dir = normalize(worldPos - uCamPos.xyz);
        fragColor = vec4(debugView == 0 ? skyColor(dir, true) : vec3(0.0), 1.0);
        return;
    }

    vec4 a = texture(gAlbedoTex, vUV);
    vec4 n = texture(gNormalTex, vUV);
    vec4 m = texture(gMaterialTex, vUV);
    vec3 albedo = a.rgb;
    vec3 N = normalize(n.xyz);
    float roughness = clamp(n.w, 0.04, 1.0);
    float metallic = m.r;
    float emissive = m.g * 8.0;
    float ao = texture(uSSAO, vUV).r;
    // LOD skirts: pixels seen through T-junction cracks sit below the surface; shade them as the
    // surface above so the seams stay invisible.
    float skirtDrop = m.a * 16.0;
    vec3 shadePos = worldPos + vec3(0.0, skirtDrop, 0.0);
    if (skirtDrop > 0.01) ao = 1.0;
    vec3 V = normalize(uCamPos.xyz - worldPos);

    if (debugView != 0) {
        vec3 c = vec3(0.0);
        if (debugView == 1) c = albedo;
        else if (debugView == 2) c = N * 0.5 + 0.5;
        else if (debugView == 3) c = vec3(roughness);
        else if (debugView == 4) c = vec3(metallic, emissive / 8.0, 0.0);
        else if (debugView == 5) c = vec3(ao);
        else if (debugView == 6) c = cascadeTint(worldPos) * (0.35 + 0.65 * shadowFactor(worldPos, N));
        else if (debugView == 7) c = vec3(1.0 - exp(-length(worldPos - uCamPos.xyz) * 0.004));
        fragColor = vec4(c, 1.0);
        return;
    }

    // --- directional light (sun / moon) with cascaded shadows
    vec3 L = normalize(uLightDir.xyz);
    float shadow = shadowFactor(shadePos, N);
    vec3 color = evalBRDF(N, V, L, albedo, roughness, metallic, uLightColor.rgb) * shadow;

    // --- point lights (lanterns)
    int count = int(uMisc.w);
    for (int i = 0; i < count; ++i) {
        vec3 toL = uLightPosRadius[i].xyz - worldPos;
        float d = length(toL);
        float r = uLightPosRadius[i].w;
        if (d > r) continue;
        float window = pow(clamp(1.0 - pow(d / r, 4.0), 0.0, 1.0), 2.0);
        float atten = window / (d * d + 1.0);
        vec3 radiance = uLightColorIntensity[i].rgb * uLightColorIntensity[i].w * atten;
        color += evalBRDF(N, V, toL / d, albedo, roughness, metallic, radiance);
    }

    // --- ambient: hemisphere irradiance + sky reflection, both occluded by SSAO
    vec3 skyAvg = mix(uHorizon.rgb, uZenith.rgb, 0.5);
    vec3 irradiance = mix(uGround.rgb, skyAvg, N.y * 0.5 + 0.5);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    vec3 F = F_Schlick(max(dot(N, V), 0.0), F0);
    vec3 kd = (1.0 - F) * (1.0 - metallic);
    vec3 R = reflect(-V, N);
    vec3 specAmbient = skyColor(R, false) * F * (1.0 - roughness) * (1.0 - roughness);
    color += (kd * albedo * irradiance * 0.6 + specAmbient * 0.4) * ao;

    color += albedo * emissive;
    color = applyFog(color, worldPos);
    fragColor = vec4(color, 1.0);
}
