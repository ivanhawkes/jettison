# DETERMINISTIC: the simulation reads its time from SimClock and its step count from the
# accumulator, and the calendar's dates are shown to the player from saved simulation time, so
# none of their arithmetic may depend on whether the compiler fused a multiply and an add.
jettison_add_module(time
	DETERMINISTIC
	DEPENDS
		core
	HEADERS
		include/jettison/time/Clock.h
		include/jettison/time/FixedStep.h
		include/jettison/time/Calendar.h
	SOURCES
		src/Calendar.cpp
		src/Clock.cpp
		src/FixedStep.cpp
	TESTS
		tests/CalendarTests.cpp
		tests/ClockTests.cpp
		tests/FixedStepTests.cpp
)
