/* The hook WAMR prints through (print.cpp). */
#pragma once

#include <stdarg.h>

#ifdef __cplusplus
extern "C"
{
#endif

	typedef int (*jettison_wamr_print_t)(const char *format, va_list arguments);

	/* Set it before the runtime starts, and leave it set while it runs. */
	void jettison_wamr_set_print(jettison_wamr_print_t print);
	int jettison_wamr_vprintf(const char *format, va_list arguments);

#ifdef __cplusplus
}
#endif
