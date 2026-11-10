find_package(Threads REQUIRED)

jettison_add_module(jobs
	DEPENDS
		core
	HEADERS
		include/jettison/jobs/IoWorker.h
	SOURCES
		src/IoWorker.cpp
	TESTS
		tests/JobsTests.cpp
	PRIVATE_LIBRARIES
		Threads::Threads
)
