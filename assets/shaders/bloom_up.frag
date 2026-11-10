// 3x3 tent upsample, additively blended onto the next larger mip.
layout(binding = 0) uniform sampler2D uSrc;
uniform vec2 uSrcTexel;

in vec2 vUV;
out vec3 fragColor;

void main() {
    vec2 t = uSrcTexel;
    vec3 s = texture(uSrc, vUV).rgb * 4.0;
    s += (texture(uSrc, vUV + vec2(-t.x, 0)).rgb + texture(uSrc, vUV + vec2(t.x, 0)).rgb +
          texture(uSrc, vUV + vec2(0, -t.y)).rgb + texture(uSrc, vUV + vec2(0, t.y)).rgb) * 2.0;
    s += texture(uSrc, vUV + vec2(-t.x, -t.y)).rgb + texture(uSrc, vUV + vec2(t.x, -t.y)).rgb +
         texture(uSrc, vUV + vec2(-t.x, t.y)).rgb + texture(uSrc, vUV + vec2(t.x, t.y)).rgb;
    fragColor = s / 16.0;
}
