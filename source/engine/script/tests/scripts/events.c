/* The script suite's module for the host API: a handler that sends a command made from the event it
 * is given, inputs that read the option's, and calls the host refuses. */
#include "jettison_script.h"

static int32_t s_pings;

JETTISON_EXPORT("on_ping") void on_ping(const Test_Ping *ping)
{
	++s_pings;
	int64_t total = ping->count;
	for (uint32_t index = 0; index < ping->numbers.size; ++index)
		total += ping->numbers.data[index];
	Test_Inner items[2] = {ping->inner, {.mood = Test_Mood_Cross, .weight = s_pings}};
	Test_Pong pong = {.ship = ping->ship, .total = total, .text = ping->text, .items = {items, 2}};
	if (ping->flag)
		pong.total += (int64_t)(ping->where.x + ping->where.y + ping->where.z);
	jettison_send_Test_Pong(&pong);
	jettison_log(JETTISON_INFO, ping->text.data, ping->text.size);
}

JETTISON_EXPORT("pings") int32_t pings(void)
{
	return s_pings;
}

/* Sends a command whose text points past the end of memory. */
JETTISON_EXPORT("bad_send") void bad_send(const Test_Ping *ping)
{
	Test_Pong pong = {.ship = ping->ship, .text = {(const char *)0x7ffffff0, 100}};
	jettison_send_Test_Pong(&pong);
}

JETTISON_EXPORT("doubled") double doubled(void)
{
	return 2.0 * jettison_input("base", 4);
}

/* What it has answered before, which a fresh instance each call keeps at one. */
static int32_t s_answers;
JETTISON_EXPORT("answers") double answers(void)
{
	return ++s_answers;
}

JETTISON_EXPORT("unknown") double unknown(void)
{
	return jettison_input("nothing", 7);
}

JETTISON_EXPORT("sends") double sends(void)
{
	Test_Pong pong = {.ship = JETTISON_NO_ENTITY};
	jettison_send_Test_Pong(&pong);
	return 1.0;
}

JETTISON_EXPORT("not_a_number") double not_a_number(void)
{
	return __builtin_nan("");
}
