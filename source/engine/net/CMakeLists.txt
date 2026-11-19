# Low-level networking (N02): a server and a client on ENet over UDP, the handshake with a game's
# verifier, bearer tokens, framing under a size limit, bounded inbound queues and snapshots at a
# bounded rate. A game's protocol, its accounts and what a session may see stay in the game.
jettison_add_module(net
	DEPENDS
		core
		serialise
	HEADERS
		include/jettison/net/Client.h
		include/jettison/net/Protocol.h
		include/jettison/net/Server.h
		include/jettison/net/Token.h
	SOURCES
		src/Client.cpp
		src/Server.cpp
		src/Token.cpp
		src/Wire.cpp
		src/Wire.h
	TESTS
		tests/NetTests.cpp
	PRIVATE_LIBRARIES
		enet
		sodium
)

# The suite speaks raw ENet to the server, as a client that is not this engine's would.
if(TARGET jettison_net_tests)
	target_link_libraries(jettison_net_tests PRIVATE enet)
endif()
