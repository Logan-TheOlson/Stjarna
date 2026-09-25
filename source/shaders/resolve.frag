#version 450

layout(set = 0, binding = 0) uniform sampler2D densityTex;
layout(location = 0) out vec4 outColor;

// Unpremultiplies the density buffer circle.frag accumulated into: density.rgb is the sum of each
// covering particle's color weighted by its coverage at this pixel, density.a is that coverage
// summed on its own. Dividing recovers the coverage-weighted average color — a single particle's
// own color where only it covers a pixel, a genuine blend of colors where several overlapping
// ("high pressure") particles do.
//
// A single particle's own coverage already peaks at ~1.0 at its center (see circle.frag), so
// `total` alone can't distinguish "one particle here" from "five particles stacked here" once it's
// clamped for use as alpha — both would read as fully opaque and only differ by hue. kPressureGain
// instead treats density *beyond* that single-particle peak as a "pressure" signal and brightens
// color with it, so a lightly-populated area keeps its particles' normal color while a dense
// cluster visibly intensifies — density becomes visible as brightness, not just average color.
const float kPressureGain = 0.6;

void main() {
    vec4  density  = texelFetch(densityTex, ivec2(gl_FragCoord.xy), 0);
    float total    = density.a;
    vec3  avgColor = density.rgb / max(total, 1e-4);
    float pressure = max(total - 1.0, 0.0);
    vec3  outRGB   = avgColor * (1.0 + pressure * kPressureGain);
    outColor = vec4(outRGB, min(total, 1.0));
}
