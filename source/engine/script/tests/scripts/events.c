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
	Test_Order orders[1] = {{.by = {.kind = Sim_ControllerKind_Player, .slot = 3}, .amount = 2}};
	Sim_ReasonArgument why[1] = {
	    {.kind = Sim_ReasonValue_Integer, .integer = ping->count, .entity = JETTISON_NO_ENTITY}};
	/* As players, which the host does not let it be, at the top and nested. */
	Test_Pong pong = {
	    .ship = ping->ship,
	    .total = total,
	    .text = ping->text,
	    .items = {items, 2},
	    .sender = {.kind = Sim_ControllerKind_Player, .slot = 1},
	    .order = {.by = {.kind = Sim_ControllerKind_Player, .slot = 2}, .amount = 1},
	    .orders = {orders, 1},
	    .why = {.key = {"too_far", 7}, .arguments = {why, 1}}};
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

/* Gives a reason under a name that is not one, as if it were the engine's. */
JETTISON_EXPORT("bad_reason") void bad_reason(const Test_Ping *ping)
{
	Test_Pong pong = {.ship = ping->ship, .why = {.key = {"reason.dock.not_controller", 26}}};
	jettison_send_Test_Pong(&pong);
}

/* A tree's task: counts its ticks in the tree's memory, and once the game's price is past ten sends
 * what it counted and succeeds; it fails at its third tick without. */
JETTISON_EXPORT("haggle") void haggle(Script_TaskContext *context)
{
	if (context->starting)
		context->memory = 0;
	++context->memory;
	if (jettison_input("price", 5) > 10.0)
	{
		Test_Pong pong = {.ship = context->entity, .total = (int64_t)context->memory};
		if (context->arguments.size > 0)
			pong.text = context->arguments.data[0];
		jettison_send_Test_Pong(&pong);
		context->status = Script_TaskStatus_Success;
	}
	else if (context->memory >= 3)
		context->status = Script_TaskStatus_Failure;
}

/* Its halt, as its tree leaves it running: says how far it had counted. */
JETTISON_EXPORT("let_go") void let_go(const Script_TaskContext *context)
{
	Test_Pong pong = {.ship = context->entity, .total = -(int64_t)context->memory};
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
