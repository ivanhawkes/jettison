#version 450

// One triangle that covers the screen, from the vertex index alone: no vertex buffer.
layout(location = 0) out vec2 fragUv;

void main()
{
	fragUv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
	gl_Position = vec4(fragUv * 2.0 - 1.0, 0.0, 1.0);
}
