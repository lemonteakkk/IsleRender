// Cook-Torrance BRDF: GGX distribution, Smith-Schlick geometry, Schlick Fresnel.
float D_GGX(float NdotH, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d + 1e-7);
}

float G_SchlickGGX(float NdotX, float roughness) {
    float r = roughness + 1.0;
    float k = r * r / 8.0;
    return NdotX / (NdotX * (1.0 - k) + k);
}

vec3 F_Schlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Radiance reflected toward V from a light of radiance `radiance` arriving along L.
vec3 evalBRDF(vec3 N, vec3 V, vec3 L, vec3 albedo, float roughness, float metallic, vec3 radiance) {
    vec3 H = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 1e-4);
    float NdotH = max(dot(N, H), 0.0);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    vec3 F = F_Schlick(max(dot(H, V), 0.0), F0);
    float D = D_GGX(NdotH, roughness);
    float G = G_SchlickGGX(NdotV, roughness) * G_SchlickGGX(NdotL, roughness);
    vec3 spec = D * G * F / (4.0 * NdotV * NdotL + 1e-4);
    vec3 kd = (1.0 - F) * (1.0 - metallic);
    return (kd * albedo / PI + spec) * radiance * NdotL;
}
