#pragma once

#include <string>
#include <string_view>

namespace Jettison::Net
{
// A server's key pair for the handshake, Curve25519, each key 32 bytes as 64 lower-case hex
// characters. The secret stays with the server (ServerConfig::secretKey) and every client is given
// the public one to pin (ClientConfig::serverKey), so a client connects only to the server that holds
// the secret. A secret lost or leaked is a new pair, and every client given its public key.
struct ServerKey
{
	std::string publicKey;
	std::string secretKey;
};

ServerKey GenerateServerKey();

// Throws std::invalid_argument for text that is not a secret key.
std::string PublicKeyOf(std::string_view secretKey);
} // namespace Jettison::Net
