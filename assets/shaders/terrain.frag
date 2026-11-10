#include "common.glsl"
#include "gbuffer_out.glsl"

in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV;
in vec3 vColor;
flat in float vVariation;

vec3 lin(vec3 c) { return pow(c, vec3(2.2)); }

void main() {
    vec3 N = normalize(vNormal);
    // heightfield: always seen from above, so never flip (skirt back faces must keep the surface normal)
    vec2 xz = vWorldPos.xz;
    float h = vWorldPos.y;
    float slope = 1.0 - N.y;

    float large = fbm2(xz * 0.025);
    float detail = fbm2(xz * 0.35);

    vec3 grassA = lin(vec3(0.27, 0.45, 0.14));
    vec3 grassB = lin(vec3(0.42, 0.52, 0.18));
    vec3 dryGrass = lin(vec3(0.55, 0.52, 0.28));
    vec3 sand = lin(vec3(0.82, 0.74, 0.55));
    vec3 wetSand = lin(vec3(0.52, 0.45, 0.33));
    vec3 rock = lin(vec3(0.42, 0.39, 0.36));
    vec3 rockDark = lin(vec3(0.26, 0.25, 0.24));
    vec3 snow = lin(vec3(0.92, 0.94, 0.97));

    vec3 grass = mix(grassA, grassB, smoothstep(0.35, 0.65, large));
    grass = mix(grass, dryGrass, smoothstep(0.55, 0.8, large) * 0.6);
    vec3 col = grass * (0.85 + 0.3 * detail);
    float roughness = 0.92;

    float sandMask = 1.0 - smoothstep(1.6, 3.2, h + (large - 0.5) * 2.0 + (detail - 0.5));
    col = mix(col, sand * (0.9 + 0.2 * detail), sandMask);

    float wet = 1.0 - smoothstep(-0.2, 0.9, h);
    col = mix(col, wetSand, wet);
    roughness = mix(roughness, 0.45, wet);

    float rockMask = smoothstep(0.32, 0.5, slope + (detail - 0.5) * 0.15);
    vec3 rockCol = mix(rockDark, rock, smoothstep(0.3, 0.7, fbm2(xz * 0.12 + h * 0.3)));
    col = mix(col, rockCol, rockMask);
    roughness = mix(roughness, 0.8, rockMask);

    float snowMask = smoothstep(50.0, 58.0, h + (large - 0.5) * 8.0) * (1.0 - smoothstep(0.45, 0.6, slope));
    col = mix(col, snow, snowMask);
    roughness = mix(roughness, 0.6, snowMask);

    writeGBuffer(col, N, roughness, 0.0, 0.0, 1.0);
    gMaterial.a = clamp(vColor.x / 16.0, 0.0, 1.0);  // skirt drop below the surface (m), 0 elsewhere
}
