#include "common.glsl"

layout(binding = 0) uniform sampler2D uHDR;
layout(binding = 1) uniform sampler2D uBloom;
uniform float uExposure;
uniform float uBloomStrength;

in vec2 vUV;
out vec4 fragColor;

// Narkowicz ACES filmic fit.
vec3 aces(vec3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

void main() {
    vec3 hdr = texture(uHDR, vUV).rgb;
    vec3 color;
    if (int(uMisc.x) != 0) {
        color = hdr;  // debug views are already display-referred
    } else {
        hdr = mix(hdr, texture(uBloom, vUV).rgb, uBloomStrength);
        color = aces(hdr * uExposure);
        vec2 q = vUV - 0.5;
        color *= 1.0 - dot(q, q) * 0.35;  // gentle vignette
    }
    color = pow(color, vec3(1.0 / 2.2));
    float luma = dot(color, vec3(0.299, 0.587, 0.114));
    fragColor = vec4(color, luma);  // luma in alpha for FXAA
}
