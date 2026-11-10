#include "common.glsl"

// Compact FXAA (Lottes) operating on luma stored in alpha by the tonemap pass.
layout(binding = 0) uniform sampler2D uLDR;

in vec2 vUV;
out vec4 fragColor;

const float FXAA_SPAN_MAX = 8.0;
const float FXAA_REDUCE_MUL = 1.0 / 8.0;
const float FXAA_REDUCE_MIN = 1.0 / 128.0;

void main() {
    vec2 px = uScreen.zw;
    float lNW = texture(uLDR, vUV + vec2(-1, -1) * px).a;
    float lNE = texture(uLDR, vUV + vec2(1, -1) * px).a;
    float lSW = texture(uLDR, vUV + vec2(-1, 1) * px).a;
    float lSE = texture(uLDR, vUV + vec2(1, 1) * px).a;
    vec4 rgbM = texture(uLDR, vUV);
    float lM = rgbM.a;
    float lMin = min(lM, min(min(lNW, lNE), min(lSW, lSE)));
    float lMax = max(lM, max(max(lNW, lNE), max(lSW, lSE)));
    if (lMax - lMin < max(0.0312, lMax * 0.125)) { fragColor = vec4(rgbM.rgb, 1.0); return; }

    vec2 dir = vec2(-((lNW + lNE) - (lSW + lSE)), (lNW + lSW) - (lNE + lSE));
    float reduce = max((lNW + lNE + lSW + lSE) * 0.25 * FXAA_REDUCE_MUL, FXAA_REDUCE_MIN);
    float rcpMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + reduce);
    dir = clamp(dir * rcpMin, vec2(-FXAA_SPAN_MAX), vec2(FXAA_SPAN_MAX)) * px;

    vec3 a = 0.5 * (texture(uLDR, vUV + dir * (1.0 / 3.0 - 0.5)).rgb + texture(uLDR, vUV + dir * (2.0 / 3.0 - 0.5)).rgb);
    vec3 b = a * 0.5 + 0.25 * (texture(uLDR, vUV - dir * 0.5).rgb + texture(uLDR, vUV + dir * 0.5).rgb);
    float lB = dot(b, vec3(0.299, 0.587, 0.114));
    fragColor = vec4((lB < lMin || lB > lMax) ? a : b, 1.0);
}
