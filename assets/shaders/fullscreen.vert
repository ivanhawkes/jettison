#version 450

// One triangle that covers the target, from the vertex index alone: no vertex buffer. Every
// full-screen program is this file built under its own name (assets/CMakeLists.txt): the output
// pass, the sky, and the environment's bake passes. The depth is 0, the far end of the reversed-Z
// range, which is where the sky is drawn; the passes without a depth attachment ignore it.
layout(location = 0) out vec2 fragUv;

void main()
{
	fragUv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
	gl_Position = vec4(fragUv * 2.0 - 1.0, 0.0, 1.0);
}
