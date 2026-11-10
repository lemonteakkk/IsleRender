// G-buffer layout
//   RT0 RGBA8   : albedo.rgb (linear), unused
//   RT1 RGBA16F : world normal.xyz, roughness
//   RT2 RGBA8   : metallic, emissive / 8, material id / 255, unused
layout(location = 0) out vec4 gAlbedo;
layout(location = 1) out vec4 gNormal;
layout(location = 2) out vec4 gMaterial;

void writeGBuffer(vec3 albedo, vec3 N, float roughness, float metallic, float emissive, float materialId) {
    gAlbedo = vec4(albedo, 1.0);
    gNormal = vec4(normalize(N), roughness);
    gMaterial = vec4(metallic, clamp(emissive / 8.0, 0.0, 1.0), materialId / 255.0, 0.0);
}
