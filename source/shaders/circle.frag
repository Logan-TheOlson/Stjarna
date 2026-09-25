#version 450

layout(location = 0) in  vec2  localPos;
layout(location = 1) in  vec4  fragColor;
layout(location = 2) in  float fragRadius;
layout(location = 0) out vec4  outColor;

// A wider-than-strictly-necessary feather (see circle.vert's kOverlapFactor comment) softens each
// circle's edge into more of a blob than a hard-edged disk, so overlapping neighbors blend into a
// continuous field rather than tiling as visibly separate circles.
const float kEdgeFeather = 2.5;

// Written into a float accumulation buffer (see kDensityFormat's comment in VulkanContext.h) with
// additive blending, premultiplied by coverage — density.rgb sums each contributing particle's
// color weighted by how much it covers this pixel, density.a sums that coverage on its own. The
// composite pass (resolve.frag) later divides rgb by a to recover the coverage-weighted *average*
// color wherever several particles' feathered edges overlap, which is what makes clustered ("high
// pressure") particles blend into each other instead of layering as discrete translucent disks —
// while a single isolated particle's own alpha is unaffected by any of this and still reads solid.
void main() {
    float dist  = length(localPos);
    float edge  = fwidth(dist) * kEdgeFeather;
    float alpha = 1.0 - smoothstep(fragRadius - edge, fragRadius, dist);
    float a     = fragColor.a * alpha;
    outColor = vec4(fragColor.rgb * a, a);
}
