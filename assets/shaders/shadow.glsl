// Cascaded shadow lookup with normal-offset bias and 3x3 hardware-PCF taps.
layout(binding = 5) uniform sampler2DArrayShadow uShadowMap;

int selectCascade(float viewDepth) {
    int c = 0;
    if (viewDepth > uCascadeFar.x) c = 1;
    if (viewDepth > uCascadeFar.y) c = 2;
    if (viewDepth > uCascadeFar.z) c = 3;
    return c;
}

float sampleCascade(int c, vec3 worldPos, vec3 N) {
    float texel = 1.0 / float(textureSize(uShadowMap, 0).x);
    vec3 p = worldPos + N * uMisc.z * float(c + 1) * 2.0;
    vec4 lp = uLightVP[c] * vec4(p, 1.0);
    vec3 s = lp.xyz / lp.w * 0.5 + 0.5;
    if (any(lessThan(s.xy, vec2(0.0))) || any(greaterThan(s.xy, vec2(1.0))) || s.z > 1.0) return 1.0;
    float ref = s.z - uMisc.y * float(c + 1);
    float sum = 0.0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x)
            sum += texture(uShadowMap, vec4(s.xy + vec2(x, y) * texel * 1.25, float(c), ref));
    return sum / 9.0;
}

float shadowFactor(vec3 worldPos, vec3 N) {
    if (uCascadeFar.w < 0.5) return 1.0;
    float viewDepth = -(uView * vec4(worldPos, 1.0)).z;
    int c = selectCascade(viewDepth);
    if (c >= 3) return 1.0;
    float s = sampleCascade(c, worldPos, N);
    // blend into the next cascade near the split to hide the seam
    float cFar = uCascadeFar[c];
    float cNear = c == 0 ? 0.0 : uCascadeFar[c - 1];
    float blend = smoothstep(cFar - (cFar - cNear) * 0.12, cFar, viewDepth);
    if (blend > 0.0) {
        float next = c + 1 < 3 ? sampleCascade(c + 1, worldPos, N) : 1.0;
        s = mix(s, next, blend);
    }
    return s;
}
