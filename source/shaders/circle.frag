#version 450

layout(location = 0) in  vec2  localPos;
layout(location = 1) in  vec4  fragColor;
layout(location = 2) in  float fragRadius;
layout(location = 0) out vec4  outColor;

// A wider-than-strictly-necessary feather (see circle.vert's kOverlapFactor comment) softens each
// circle's edge into more of a blob than a hard-edged disk, so overlapping neighbors blend into a
// continuous field rather than tiling as visibly separate circles.
const float kEdgeFeather = 2.5;

void main() {
    float dist  = length(localPos);
    float edge  = fwidth(dist) * kEdgeFeather;
    float alpha = 1.0 - smoothstep(fragRadius - edge, fragRadius, dist);
    outColor = vec4(fragColor.rgb, fragColor.a * alpha);
}
