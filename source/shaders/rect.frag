#version 450

layout(location = 0) in  vec2 localPos;
layout(location = 1) in  vec4 fragColor;
layout(location = 2) in  vec2 fragPixHalfSize;
layout(location = 0) out vec4 outColor;

void main() {
    vec2  d     = abs(localPos) - fragPixHalfSize;
    float dist  = length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);
    float edge  = fwidth(dist);
    float alpha = 1.0 - smoothstep(0.0, edge, dist);
    outColor = vec4(fragColor.rgb, fragColor.a * alpha);
}
