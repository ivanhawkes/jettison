/* The script suite's module for reading the world: a handler that reads a component from the newest
 * snapshot, and an input that may not. */
#include "jettison_script.h"

/* Reads the pinged ship's hold, and its own, which it has none of, and sends what it read. */
JETTISON_EXPORT("on_look") void on_look(const Test_Ping *ping)
{
	const Test_Hold *hold = jettison_read_Test_Hold(ping->ship);
	const Test_Hold *none = jettison_read_Test_Hold(JETTISON_NO_ENTITY);
	Test_Pong pong = {.ship = ping->ship, .total = -1};
	if (hold != 0 && none == 0)
	{
		pong.total = hold->cargo;
		pong.text = hold->owner;
	}
	jettison_send_Test_Pong(&pong);
}

/* Reads while answering an input, which only an option's inputs may inform. */
JETTISON_EXPORT("reads") double reads(void)
{
	return jettison_read_Test_Hold(0) == 0 ? 0.0 : 1.0;
}


/* A tree's task that asks the game for an input, which may be one this script answers, and then
 * reads, which a task may not, however it was called. */
JETTISON_EXPORT("peek") void peek(Script_TaskContext *context)
{
	jettison_input("reads", 5);
	context->memory = jettison_read_Test_Hold(0) == 0 ? 1 : 2;
	context->status = Script_TaskStatus_Success;
}
