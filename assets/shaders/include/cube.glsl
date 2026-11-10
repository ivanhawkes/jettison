// Cube map faces, in Vulkan's order (+X, -X, +Y, -Y, +Z, -Z) and orientation: the direction a
// texel of a face is sampled with, from the face and the texel's position on it, 0 to 1 from the
// top-left corner. Rendering a face at that direction, texel for texel, writes what sampling the
// cube along it reads (the Vulkan specification's cube map face selection table, inverted).

#ifndef JETTISON_CUBE_GLSL
#define JETTISON_CUBE_GLSL

vec3 CubeDirection(uint face, vec2 uv)
{
	vec2 st = uv * 2.0 - 1.0;
	vec3 direction;
	if (face == 0u)
		direction = vec3(1.0, -st.y, -st.x);
	else if (face == 1u)
		direction = vec3(-1.0, -st.y, st.x);
	else if (face == 2u)
		direction = vec3(st.x, 1.0, st.y);
	else if (face == 3u)
		direction = vec3(st.x, -1.0, -st.y);
	else if (face == 4u)
		direction = vec3(st.x, -st.y, 1.0);
	else
		direction = vec3(-st.x, -st.y, -1.0);
	return normalize(direction);
}

#endif
