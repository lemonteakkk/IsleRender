// Linear blend skinning: palette = jointWorld * inverseBind, up to 4 influences per vertex.
#ifdef SKINNED
layout(location = 5) in vec4 aJoints;  // float-encoded indices
layout(location = 6) in vec4 aWeights;
layout(std140, binding = 2) uniform Bones {
    mat4 uBones[32];
};

mat4 skinMatrix() {
    ivec4 j = ivec4(aJoints + 0.5);
    return uBones[j.x] * aWeights.x + uBones[j.y] * aWeights.y + uBones[j.z] * aWeights.z +
           uBones[j.w] * aWeights.w;
}
#endif
