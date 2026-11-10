#include <jettison/profile/Profile.h>

namespace Jettison::Profile
{
bool IsEnabled()
{
#ifdef TRACY_ENABLE
	return true;
#else
	return false;
#endif
}

bool IsConnected()
{
	return TracyIsConnected;
}
} // namespace Jettison::Profile
