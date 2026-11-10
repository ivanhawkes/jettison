#pragma once

#include <cstdint>
#include <string_view>

namespace Jettison::Core
{
// 64-bit FNV-1a. It is what StringId hashes with, so its output is part of the save format and of
// every data file that stores an ID: the tests pin it with the reference vectors published with
// the algorithm, and it is never changed in place. Chosen over xxHash because it is a few lines
// that run the same at compile time and at run time; the strings it hashes are short names, where
// xxHash's throughput does not show.
constexpr std::uint64_t Fnv1a64(std::string_view text)
{
	std::uint64_t hash = 0xcbf29ce484222325ULL;
	for (const char character : text)
	{
		hash ^= static_cast<std::uint8_t>(character);
		hash *= 0x100000001b3ULL;
	}
	return hash;
}

// Folds value into seed, for hashing a composite key from its parts. The order matters:
// HashCombine(HashCombine(s, a), b) and HashCombine(HashCombine(s, b), a) differ. The mixing is
// boost's hash_combine widened to 64 bits; like Fnv1a64 it is pinned by recorded values, because
// a cache or a save keyed on a combined hash depends on it.
constexpr std::uint64_t HashCombine(std::uint64_t seed, std::uint64_t value)
{
	return seed ^ (value + 0x9e3779b97f4a7c15ULL + (seed << 12) + (seed >> 4));
}
} // namespace Jettison::Core
