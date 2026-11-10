// The mesh vertex, as Render::Vertex lays it out (render/vulkan/Model.h).

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inNormal;
layout(location = 4) in uvec4 inJoints;
layout(location = 5) in vec4 inWeights;
layout(location = 6) in uvec4 inJoints1;
layout(location = 7) in vec4 inWeights1;
