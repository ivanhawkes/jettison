#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <jettison/net/Protocol.h>

namespace Jettison::Net::Seal
{
// The handshake and the packets sealed after it, on libsodium. Each end offers an ephemeral
// Curve25519 key; a connection's keys come from two exchanges, the client's ephemeral key with the
// server's static one (which the client pins, so only that server can derive them) and with the
// server's ephemeral one (so a static key stolen later opens no recorded session). A sealed packet is
// its counter, eight bytes little-endian, then XChaCha20-Poly1305 over the message, with the channel
// as additional data. Changing any of this breaks every client of the build before.
inline constexpr std::size_t kKeyBytes = 32;
inline constexpr std::size_t kCounterBytes = 8;
inline constexpr std::size_t kTagBytes = 16;
inline constexpr std::size_t kOverhead = kCounterBytes + kTagBytes;

// Channel's values, Reliable and Unreliable: each has its own last counter opened, and ENet its own
// channel (Wire::kChannelCount).
inline constexpr std::size_t kChannelCount = 2;

using Key = std::array<std::byte, kKeyBytes>;

// Throws std::runtime_error when libsodium cannot start; once per process.
void StartSodium();

std::string Hex(std::span<const std::byte> bytes);

// Nullopt for text that is not 64 hex characters.
std::optional<Key> ParseKey(std::string_view hex);

// Overwrites a secret, then empties it.
void Wipe(std::string &secret);

void Wipe(std::vector<std::byte> &secret);

// The secret is wiped when the pair is destroyed.
struct KeyPair
{
	KeyPair();

	// Throws std::invalid_argument for text that is not a secret key.
	explicit KeyPair(std::string_view secretHex);

	~KeyPair();

	KeyPair(const KeyPair &) = delete;

	KeyPair &operator=(const KeyPair &) = delete;

	Key publicKey{};
	Key secretKey{};
};

// One connection's keys, a key each way, and the counters that keep every nonce unique: one count of
// what was sent, over both channels, and per channel the last counter opened, so a packet replayed,
// or moved to the other channel, does not open.
class Keys
{
  public:
	// Nullopt when a public key is one no exchange can use (a point of small order).
	static std::optional<Keys> ForClient(
	    const KeyPair &ephemeral, const Key &serverStatic, const Key &serverEphemeral);

	static std::optional<Keys> ForServer(
	    const KeyPair &serverStatic, const KeyPair &ephemeral, const Key &clientEphemeral);

	~Keys();

	Keys(Keys &&) noexcept = default;

	Keys &operator=(Keys &&) noexcept = default;

	Keys(const Keys &) = delete;

	Keys &operator=(const Keys &) = delete;

	// Seals in place a packet of kOverhead bytes more than its message: the counter is written first,
	// the message, which follows it, is encrypted where it lies, and the tag fills the last bytes.
	// False, changing nothing, once every counter has been used, so no nonce is used twice.
	[[nodiscard]] bool Seal(Channel channel, std::span<std::byte> packet);

	// Nullopt for a packet forged, damaged, replayed, older than one already opened on its channel, or
	// sealed with other keys.
	std::optional<std::vector<std::byte>> Open(Channel channel, std::span<const std::byte> sealed);

	// For the suite: the next message is sent with this counter.
	void SendFrom(std::uint64_t counter);

  private:
	Keys() = default;

	Key m_send{};
	Key m_receive{};
	std::uint64_t m_nextSent = 0;
	bool m_spent = false;
	std::array<std::optional<std::uint64_t>, kChannelCount> m_lastOpened{};
};
} // namespace Jettison::Net::Seal
