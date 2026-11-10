// Colour-space helpers. Lighting is done in linear light, the HDR target holds linear values, and
// sRGB encoding happens once, on the way to the swapchain: by the format when the swapchain is an
// sRGB format, and by LinearToSrgb when only a UNORM one was offered.

vec3 LinearToSrgb(vec3 linear)
{
	vec3 low = linear * 12.92;
	vec3 high = 1.055 * pow(linear, vec3(1.0 / 2.4)) - 0.055;
	return mix(high, low, lessThanEqual(linear, vec3(0.0031308)));
}

float Luminance(vec3 linear)
{
	return dot(linear, vec3(0.2126, 0.7152, 0.0722));
}
