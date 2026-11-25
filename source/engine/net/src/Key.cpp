#include <jettison/net/Key.h>

#include "Seal.h"

namespace Jettison::Net
{
ServerKey GenerateServerKey()
{
	const Seal::KeyPair pair;
	return {Seal::Hex(pair.publicKey), Seal::Hex(pair.secretKey)};
}


std::string PublicKeyOf(std::string_view secretKey)
{
	return Seal::Hex(Seal::KeyPair{secretKey}.publicKey);
}
} // namespace Jettison::Net
