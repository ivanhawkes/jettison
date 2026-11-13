/* The script suite's module for what a script keeps by key: a count stored and read back, a key
 * taken away, as many keys of a size as it is asked for, and stores the host refuses. */
#include "jettison_script.h"

/* What it has remembered in this instance, which a new instance starts again from nothing. */
static int32_t s_held;
static uint8_t s_bytes[4096];

JETTISON_EXPORT("remember") void remember(int32_t value)
{
	++s_held;
	jettison_store("count", 5, &value, sizeof(value));
}

/* The count kept, -1 for none and -2 for bytes that are not a count. */
JETTISON_EXPORT("recall") int32_t recall(void)
{
	int32_t value = 0;
	const int32_t kept = jettison_load("count", 5, &value, sizeof(value));
	return kept < 0 ? -1 : kept != (int32_t)sizeof(value) ? -2 : value;
}

JETTISON_EXPORT("forget") void forget(void)
{
	jettison_store("count", 5, s_bytes, 0);
}

JETTISON_EXPORT("held") int32_t held(void)
{
	return s_held;
}

/* Keys "k" and the index's two bytes, each of size bytes. */
JETTISON_EXPORT("hoard") void hoard(int32_t keys, int32_t size)
{
	for (int32_t index = 0; index < keys; ++index)
	{
		const char key[3] = {'k', (char)(index & 0xff), (char)((index >> 8) & 0xff)};
		jettison_store(key, sizeof(key), s_bytes, (uint32_t)size);
	}
}

JETTISON_EXPORT("long_key") void long_key(void)
{
	jettison_store((const char *)s_bytes, 65, s_bytes, 1);
}

JETTISON_EXPORT("stores") double stores(void)
{
	int32_t value = 1;
	jettison_store("count", 5, &value, sizeof(value));
	return 1.0;
}
