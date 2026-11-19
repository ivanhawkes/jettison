#pragma once

#include <string>
#include <string_view>

namespace Jettison::Net
{
// Bearer tokens on libsodium. A token is 32 random bytes as 64 lower-case hex characters, which the
// server's operator hands to a principal; the server keeps only its digest, BLAKE2b-256 of the
// token's text as hex, so a leaked store gives no token. A token has 256 bits of entropy, so a fast
// digest suffices where a password would want a slow one.

std::string GenerateToken();

std::string DigestToken(std::string_view token);

// Compares the token's digest with the one given in time that does not depend on where they differ.
// False for a digest of the wrong length, which a stored digest's length does not give away.
bool TokenMatches(std::string_view token, std::string_view digest);
} // namespace Jettison::Net
