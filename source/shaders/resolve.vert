#version 450

// Fullscreen triangle covering the whole viewport, generated purely from gl_VertexIndex — no
// vertex buffer needed, this pipeline only ever draws 3 vertices (see RecordCompositePass).
void main() {
    vec2 pos = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2) * 2.0 - 1.0;
    gl_Position = vec4(pos, 0.0, 1.0);
}
