#include "common.glsl"

layout(location = 0) in vec3 aPos;

uniform vec2 uWaterOffset;

out vec3 vWorldPos;
out vec3 vNormal;

// Gerstner wave: (dir.x, dir.y, steepness, wavelength)
vec3 gerstner(vec4 wave, vec3 p, inout vec3 tangent, inout vec3 binormal) {
    float k = 2.0 * PI / wave.w;
    float c = sqrt(9.8 / k);
    vec2 d = normalize(wave.xy);
    float f = k * (dot(d, p.xz) - c * uCamPos.w);
    float a = wave.z / k;
    tangent += vec3(-d.x * d.x * wave.z * sin(f), d.x * wave.z * cos(f), -d.x * d.y * wave.z * sin(f));
    binormal += vec3(-d.x * d.y * wave.z * sin(f), d.y * wave.z * cos(f), -d.y * d.y * wave.z * sin(f));
    return vec3(d.x * a * cos(f), a * sin(f), d.y * a * cos(f));
}

void main() {
    vec2 local = aPos.xz;
    // stretch the outermost ring of the grid toward the horizon so the sea never ends
    if (max(abs(local.x), abs(local.y)) > 799.0) local *= 30.0;
    vec3 p = vec3(local.x + uWaterOffset.x, 0.0, local.y + uWaterOffset.y);
    vec3 tangent = vec3(1, 0, 0), binormal = vec3(0, 0, 1);
    vec3 grid = p;
    // fade waves out far away so the coarse grid does not alias
    float fade = 1.0 - smoothstep(120.0, 380.0, length(p.xz - uCamPos.xz));
    vec3 offset = vec3(0.0);
    offset += gerstner(vec4(1.0, 0.6, 0.10, 38.0), grid, tangent, binormal);
    offset += gerstner(vec4(-0.4, 1.0, 0.08, 22.0), grid, tangent, binormal);
    offset += gerstner(vec4(0.7, -0.5, 0.06, 13.0), grid, tangent, binormal);
    offset += gerstner(vec4(-0.9, -0.3, 0.05, 8.0), grid, tangent, binormal);
    p += offset * fade;
    vWorldPos = p;
    vNormal = normalize(mix(vec3(0, 1, 0), normalize(cross(binormal, tangent)), fade));
    gl_Position = uViewProj * vec4(p, 1.0);
}
