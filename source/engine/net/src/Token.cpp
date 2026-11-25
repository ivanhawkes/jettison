#include <jettison/net/Token.h>

#include <sodium.h>

#include <array>
#include <span>

#include "Seal.h"

namespace Jettison::Net
{
namespace
{
constexpr std::size_t kTokenBytes = 32;
constexpr std::size_t kDigestBytes = crypto_generichash_BYTES;

static_assert(kDigestBytes == 32);


std::string Hex(std::span<const unsigned char> bytes)
{
	return Seal::Hex(std::as_bytes(bytes));
}
} // namespace


std::string GenerateToken()
{
	Seal::StartSodium();
	std::array<unsigned char, kTokenBytes> bytes{};
	randombytes_buf(bytes.data(), bytes.size());
	std::string token = Hex(bytes);
	sodium_memzero(bytes.data(), bytes.size());
	return token;
}


std::string DigestToken(std::string_view token)
{
	Seal::StartSodium();
	std::array<unsigned char, kDigestBytes> digest{};
	crypto_generichash(
	    digest.data(), digest.size(), reinterpret_cast<const unsigned char *>(token.data()),
	    token.size(), nullptr, 0);
	return Hex(digest);
}


bool TokenMatches(std::string_view token, std::string_view digest)
{
	const std::string mine = DigestToken(token);
	return digest.size() == mine.size() && sodium_memcmp(mine.data(), digest.data(), mine.size()) == 0;
}
} // namespace Jettison::Net
