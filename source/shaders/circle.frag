#version 450

layout(location = 0) in  vec2  localPos;
layout(location = 1) in  vec4  fragColor;
layout(location = 2) in  float fragRadius;
layout(location = 0) out vec4  outColor;

// Feather width (in pixels) for the circle's edge anti-aliasing.
const float kEdgeFeather = 1.0;

void main() {
    float dist  = length(localPos);
    float edge  = fwidth(dist) * kEdgeFeather;
    float alpha = 1.0 - smoothstep(fragRadius - edge, fragRadius, dist);
    outColor = vec4(fragColor.rgb, fragColor.a * alpha);
}
