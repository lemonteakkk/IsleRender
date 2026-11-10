#include "skinning.glsl"

layout(location = 0) in vec3 aPos;
#ifdef INSTANCED
layout(location = 4) in mat4 aInstance;
#endif

uniform mat4 uModel;
uniform mat4 uLightMatrix;

void main() {
    mat4 M = uModel;
#ifdef INSTANCED
    M = aInstance * uModel;
#endif
#ifdef SKINNED
    M = skinMatrix();
#endif
    gl_Position = uLightMatrix * M * vec4(aPos, 1.0);
}
