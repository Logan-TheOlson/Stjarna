#version 450

layout(location = 0) in  vec2  localPos;
layout(location = 1) in  vec4  fragColor;
layout(location = 2) in  vec2  fragHalfExtent;
layout(location = 3) in  float fragBorder;
layout(location = 0) out vec4  outColor;

// Hollow frame: opaque within `fragBorder` pixels of the nearest edge, transparent past that, so
// this draws an outline rather than a filled block.
void main() {
    vec2  distToEdge = fragHalfExtent - abs(localPos);
    float dist       = min(distToEdge.x, distToEdge.y);
    float edge       = fwidth(dist);
    float alpha      = 1.0 - smoothstep(fragBorder - edge, fragBorder, dist);
    outColor = vec4(fragColor.rgb, fragColor.a * alpha);
}
