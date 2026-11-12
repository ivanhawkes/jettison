#version 450

// The opaque depth prepass's colour (docs/architecture.md, "The frame"): beside the depth, each
// surface's view-space normal, which ambient occlusion reads, and its object's ID, which the outlines
// read. The normal is the mesh's, interpolated, without its material's normal map: occlusion is of the
// shape. Its multisampled targets are read at sample zero, as the depth is, rather than resolved.
layout(location = 0) in vec3 fragViewNormal;
layout(location = 1) flat in uint fragObject;

// A2B10G10R10: the unit normal from [-1, 1] to [0, 1].
layout(location = 0) out vec4 outNormal;
// A float that holds the ID as a whole number, exact to 2^24: a device need not have multisampled
// integer targets, and lavapipe does not.
layout(location = 1) out float outObject;

void main()
{
	vec3 normal = normalize(fragViewNormal);
	// Facing the camera, as the shading bends it: a back face seen through a gap is still a surface.
	if (!gl_FrontFacing)
		normal = -normal;
	outNormal = vec4(normal * 0.5 + 0.5, 1.0);
	outObject = float(fragObject);
}
