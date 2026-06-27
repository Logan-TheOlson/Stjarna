#version 450

layout(push_constant) uniform PC {
    vec4  color;
    vec2  center;
    vec2  screenSize;
    float radius;
} pc;

layout(location = 0) in  vec2 localPos;
layout(location = 0) out vec4 outColor;

void main() {
    float dist = length(localPos);
    float edge = fwidth(dist);
    float alpha = 1.0 - smoothstep(pc.radius - edge, pc.radius, dist);
    outColor = vec4(pc.color.rgb, pc.color.a * alpha);
}