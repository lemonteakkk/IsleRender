#include "common.glsl"

// 4x4 depth-aware blur (matches the 4x4 noise tile) to remove SSAO noise without halos.
layout(binding = 0) uniform sampler2D uAO;
layout(binding = 1) uniform sampler2D uDepth;

in vec2 vUV;
out float fragAO;

float linearDepth(float d) {
    vec4 p = uInvProj * vec4(0.0, 0.0, d * 2.0 - 1.0, 1.0);
    return -p.z / p.w;
}

void main() {
    vec2 texel = 1.0 / vec2(textureSize(uAO, 0));
    float center = linearDepth(textureLod(uDepth, vUV, 0.0).r);
    float sum = 0.0, wsum = 0.0;
    for (int y = -2; y < 2; ++y)
        for (int x = -2; x < 2; ++x) {
            vec2 uv = vUV + (vec2(x, y) + 0.5) * texel;
            float d = linearDepth(textureLod(uDepth, uv, 0.0).r);
            float w = 1.0 / (1e-3 + abs(d - center) / max(center, 1.0) * 40.0);
            sum += textureLod(uAO, uv, 0.0).r * w;
            wsum += w;
        }
    fragAO = sum / wsum;
}
