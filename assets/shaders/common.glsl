// Shared per-frame data. Layout must match FrameData in Renderer.cpp.
layout(std140, binding = 0) uniform Frame {
    mat4 uView;
    mat4 uProj;
    mat4 uViewProj;
    mat4 uInvViewProj;
    mat4 uInvView;
    mat4 uInvProj;
    mat4 uLightVP[4];
    vec4 uCamPos;      // xyz, time
    vec4 uLightDir;    // xyz (toward light), night factor
    vec4 uLightColor;  // rgb, lantern fade
    vec4 uZenith;
    vec4 uHorizon;
    vec4 uGround;
    vec4 uCascadeFar;  // xyz cascade far planes, w shadows on
    vec4 uFog;         // x density
    vec4 uScreen;      // w, h, 1/w, 1/h
    vec4 uMisc;        // debug view, shadow bias, normal offset, light count
};

const float PI = 3.14159265359;

float hash13(vec3 p) {
    p = fract(p * 0.1031);
    p += dot(p, p.zyx + 31.32);
    return fract((p.x + p.y) * p.z);
}

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float valueNoise2(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash12(i), hash12(i + vec2(1, 0)), u.x), mix(hash12(i + vec2(0, 1)), hash12(i + vec2(1, 1)), u.x), u.y);
}

float fbm2(vec2 p) {
    float s = 0.0, a = 0.5;
    for (int i = 0; i < 4; ++i) {
        s += a * valueNoise2(p);
        p = mat2(1.6, 1.2, -1.2, 1.6) * p;
        a *= 0.5;
    }
    return s;
}

// Analytic sky: zenith/horizon gradient + sun (or moon) disc and glow + stars at night.
vec3 skyColor(vec3 dir, bool withDisc) {
    float night = uLightDir.w;
    float y = dir.y;
    vec3 col = mix(uHorizon.rgb, uZenith.rgb, pow(clamp(y, 0.0, 1.0), 0.45));
    // below the horizon: fade toward a dark sea/ground tone
    col = mix(col, uHorizon.rgb * 0.35 + uGround.rgb * 0.2, smoothstep(0.0, -0.25, y));

    float mu = max(dot(dir, uLightDir.xyz), 0.0);
    vec3 lc = uLightColor.rgb;
    col += lc * (pow(mu, 8.0) * 0.06 + pow(mu, 64.0) * 0.12) * (1.0 - night * 0.6);
    if (withDisc) {
        float disc = smoothstep(0.99935, 0.99965, mu);
        col += lc * disc * mix(25.0, 12.0, night);
        if (night > 0.0 && y > 0.0) {
            vec3 cell = floor(dir * 320.0);
            float h = hash13(cell);
            float star = step(0.9975, h) * (0.6 + 0.4 * sin(uCamPos.w * 2.0 + h * 50.0));
            col += vec3(star) * night * smoothstep(0.0, 0.25, y) * 1.5;
        }
    }
    return col;
}

vec3 applyFog(vec3 color, vec3 worldPos) {
    vec3 toP = worldPos - uCamPos.xyz;
    float dist = length(toP);
    vec3 dir = toP / max(dist, 1e-4);
    // height fog: denser near sea level
    float heightFactor = exp(-max(worldPos.y, 0.0) * 0.035);
    float fog = 1.0 - exp(-dist * uFog.x * mix(0.4, 1.0, heightFactor));
    vec3 fogDir = normalize(vec3(dir.x, max(dir.y, 0.02), dir.z));
    return mix(color, skyColor(fogDir, false), clamp(fog, 0.0, 1.0));
}

vec3 reconstructWorldPos(vec2 uv, float depth) {
    vec4 p = uInvViewProj * vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    return p.xyz / p.w;
}
