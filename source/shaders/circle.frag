#version 450

layout(location = 0) in  vec2  localPos;
layout(location = 1) in  vec4  fragColor;
layout(location = 2) in  float fragRadius;
layout(location = 0) out vec4  outColor;

void main() {
    float dist  = length(localPos);
    float edge  = fwidth(dist);
    float alpha = 1.0 - smoothstep(fragRadius - edge, fragRadius, dist);
    outColor = vec4(fragColor.rgb, fragColor.a * alpha);
}
