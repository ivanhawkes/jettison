#include "Seal.h"

#include <sodium.h>

#include <algorithm>
#include <limits>
#include <stdexcept>

#include <jettison/core/Assert.h>

namespace Jettison::Net::Seal
{
namespace
{
static_assert(kKeyBytes == crypto_kx_PUBLICKEYBYTES && kKeyBytes == crypto_kx_SECRETKEYBYTES);
static_assert(kKeyBytes == crypto_kx_SESSIONKEYBYTES && kKeyBytes == crypto_generichash_BYTES);
static_assert(kKeyBytes == crypto_aead_xchacha20poly1305_ietf_KEYBYTES);
static_assert(kTagBytes == crypto_aead_xchacha20poly1305_ietf_ABYTES);
static_assert(kCounterBytes <= crypto_aead_xchacha20poly1305_ietf_NPUBBYTES);

using Nonce = std::array<unsigned char, crypto_aead_xchacha20poly1305_ietf_NPUBBYTES>;


auto *Bytes(Key &key)
{
	return reinterpret_cast<unsigned char *>(key.data());
}


const auto *Bytes(const Key &key)
{
	return reinterpret_cast<const unsigned char *>(key.data());
}


void PutCounter(std::byte *pOut, std::uint64_t counter)
{
	for (std::size_t index = 0; index < kCounterBytes; ++index)
		pOut[index] = static_cast<std::byte>(counter >> (8 * index));
}


std::uint64_t GetCounter(const std::byte *pIn)
{
	std::uint64_t counter = 0;
	for (std::size_t index = 0; index < kCounterBytes; ++index)
		counter |= static_cast<std::uint64_t>(pIn[index]) << (8 * index);
	return counter;
}


Nonce NonceOf(const std::byte *pCounter)
{
	Nonce nonce{};
	std::copy_n(reinterpret_cast<const unsigned char *>(pCounter), kCounterBytes, nonce.begin());
	return nonce;
}


// One key of the pair from two exchanges: keyed BLAKE2b of the static exchange's key under the
// ephemeral one's, so it needs both secrets.
Key Mix(const Key &ephemeral, const Key &fixed)
{
	Key mixed{};
	crypto_generichash(
	    Bytes(mixed), mixed.size(), Bytes(fixed), fixed.size(), Bytes(ephemeral), ephemeral.size());
	return mixed;
}
} // namespace


void StartSodium()
{
	static const bool started = []
	{
		if (sodium_init() < 0)
			throw std::runtime_error("net: libsodium did not initialise");
		return true;
	}();
	(void)started;
}


std::string Hex(std::span<const std::byte> bytes)
{
	std::string text(bytes.size() * 2 + 1, '\0');
	sodium_bin2hex(
	    text.data(), text.size(), reinterpret_cast<const unsigned char *>(bytes.data()), bytes.size());
	text.pop_back();
	return text;
}


std::optional<Key> ParseKey(std::string_view hex)
{
	Key key{};
	std::size_t length = 0;
	const char *pEnd = nullptr;
	if (hex.size() != kKeyBytes * 2 ||
	    sodium_hex2bin(Bytes(key), key.size(), hex.data(), hex.size(), nullptr, &length, &pEnd) != 0 ||
	    length != key.size() || pEnd != hex.data() + hex.size())
		return std::nullopt;
	return key;
}


KeyPair::KeyPair()
{
	StartSodium();
	crypto_kx_keypair(Bytes(publicKey), Bytes(secretKey));
}


void Wipe(std::string &secret)
{
	sodium_memzero(secret.data(), secret.size());
	secret.clear();
}


void Wipe(std::vector<std::byte> &secret)
{
	sodium_memzero(secret.data(), secret.size());
	secret.clear();
}


KeyPair::KeyPair(std::string_view secretHex)
{
	StartSodium();
	std::optional<Key> secret = ParseKey(secretHex);
	if (!secret)
		throw std::invalid_argument("net: a secret key is 64 hex characters");
	secretKey = *secret;
	sodium_memzero(secret->data(), secret->size());
	crypto_scalarmult_base(Bytes(publicKey), Bytes(secretKey));
}


KeyPair::~KeyPair()
{
	sodium_memzero(secretKey.data(), secretKey.size());
}


std::optional<Keys> Keys::ForClient(
    const KeyPair &ephemeral, const Key &serverStatic, const Key &serverEphemeral)
{
	Keys fixed;
	Keys fresh;
	if (crypto_kx_client_session_keys(
	        Bytes(fixed.m_receive), Bytes(fixed.m_send), Bytes(ephemeral.publicKey),
	        Bytes(ephemeral.secretKey), Bytes(serverStatic)) != 0 ||
	    crypto_kx_client_session_keys(
	        Bytes(fresh.m_receive), Bytes(fresh.m_send), Bytes(ephemeral.publicKey),
	        Bytes(ephemeral.secretKey), Bytes(serverEphemeral)) != 0)
		return std::nullopt;
	Keys keys;
	keys.m_send = Mix(fresh.m_send, fixed.m_send);
	keys.m_receive = Mix(fresh.m_receive, fixed.m_receive);
	return keys;
}


std::optional<Keys> Keys::ForServer(
    const KeyPair &serverStatic, const KeyPair &ephemeral, const Key &clientEphemeral)
{
	Keys fixed;
	Keys fresh;
	if (crypto_kx_server_session_keys(
	        Bytes(fixed.m_receive), Bytes(fixed.m_send), Bytes(serverStatic.publicKey),
	        Bytes(serverStatic.secretKey), Bytes(clientEphemeral)) != 0 ||
	    crypto_kx_server_session_keys(
	        Bytes(fresh.m_receive), Bytes(fresh.m_send), Bytes(ephemeral.publicKey),
	        Bytes(ephemeral.secretKey), Bytes(clientEphemeral)) != 0)
		return std::nullopt;
	Keys keys;
	keys.m_send = Mix(fresh.m_send, fixed.m_send);
	keys.m_receive = Mix(fresh.m_receive, fixed.m_receive);
	return keys;
}


Keys::~Keys()
{
	sodium_memzero(m_send.data(), m_send.size());
	sodium_memzero(m_receive.data(), m_receive.size());
}


bool Keys::Seal(Channel channel, std::span<std::byte> packet)
{
	JETTISON_VERIFY(packet.size() >= kOverhead);
	if (m_spent)
		return false;
	const std::uint64_t counter = m_nextSent;

	// The last counter is used once, and then nothing more is sealed, rather than the count wrapping.
	if (counter == std::numeric_limits<std::uint64_t>::max())
		m_spent = true;
	else
		++m_nextSent;
	PutCounter(packet.data(), counter);
	const Nonce nonce = NonceOf(packet.data());
	const auto additional = static_cast<unsigned char>(channel);
	auto *pMessage = reinterpret_cast<unsigned char *>(packet.data() + kCounterBytes);
	crypto_aead_xchacha20poly1305_ietf_encrypt(
	    pMessage, nullptr, pMessage, packet.size() - kOverhead, &additional, 1, nullptr, nonce.data(),
	    Bytes(m_send));
	return true;
}


void Keys::SendFrom(std::uint64_t counter)
{
	m_nextSent = counter;
	m_spent = false;
}


std::optional<std::vector<std::byte>> Keys::Open(Channel channel, std::span<const std::byte> sealed)
{
	const auto index = static_cast<std::size_t>(channel);
	if (sealed.size() < kOverhead || index >= m_lastOpened.size())
		return std::nullopt;
	const std::uint64_t counter = GetCounter(sealed.data());
	std::optional<std::uint64_t> &last = m_lastOpened[index];
	if (last && counter <= *last)
		return std::nullopt;
	const Nonce nonce = NonceOf(sealed.data());
	const auto additional = static_cast<unsigned char>(channel);
	std::vector<std::byte> plain(sealed.size() - kOverhead);
	if (crypto_aead_xchacha20poly1305_ietf_decrypt(
	        reinterpret_cast<unsigned char *>(plain.data()), nullptr, nullptr,
	        reinterpret_cast<const unsigned char *>(sealed.data() + kCounterBytes),
	        sealed.size() - kCounterBytes, &additional, 1, nonce.data(), Bytes(m_receive)) != 0)
		return std::nullopt;
	last = counter;
	return plain;
}
} // namespace Jettison::Net::Seal
