#include "JoltRuntime.h"

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/IssueReporting.h>
#include <Jolt/Core/Memory.h>
#include <Jolt/RegisterTypes.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <format>
#include <memory>
#include <source_location>
#include <string_view>

#include <jettison/core/Assert.h>
#include <jettison/core/Log.h>

namespace Jettison::Physics::Detail
{
namespace
{
constexpr Core::Category kJolt{"physics"};

void Trace(const char *format, ...)
{
	char line[1024];
	va_list arguments;
	va_start(arguments, format);
	std::vsnprintf(line, sizeof line, format, arguments);
	va_end(arguments);
	Core::Log(Core::Severity::Info, kJolt, "{}", line);
}

#ifdef JPH_ENABLE_ASSERTS
// A failed assertion in Jolt is a defect, as one of the engine's is: it is reported the same way,
// with Jolt's message, and aborts there rather than returning to break into a debugger.
bool AssertFailed(const char *expression, const char *message, const char *file, JPH::uint line)
{
	Core::LogMessage(
	    Core::Severity::Error, kJolt,
	    std::format("Jolt: {} ({}:{})", message ? message : expression, file, line),
	    std::source_location::current());
	Core::AssertionFailed(expression, std::source_location::current());
}
#endif

class Runtime
{
  public:
	Runtime()
	{
		JPH::RegisterDefaultAllocator();
		m_pFactory = std::make_unique<JPH::Factory>();
		JPH::Trace = &Trace;
		JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = &AssertFailed;)
		JPH::Factory::sInstance = m_pFactory.get();
		JPH::RegisterTypes();
	}
	~Runtime()
	{
		JPH::UnregisterTypes();
		JPH::Factory::sInstance = nullptr;
	}
	Runtime(const Runtime &) = delete;
	Runtime &operator=(const Runtime &) = delete;

  private:
	// Made after the allocator is registered, since Jolt allocates through it.
	std::unique_ptr<JPH::Factory> m_pFactory;
};
} // namespace

void EnsureJolt()
{
	static const Runtime runtime;
	(void)runtime;
}

void ByteRecorder::WriteBytes(const void *data, std::size_t size)
{
	const auto *bytes = static_cast<const std::uint8_t *>(data);
	m_bytes.insert(m_bytes.end(), bytes, bytes + size);
}

void ByteRecorder::ReadBytes(void *data, std::size_t size)
{
	if (m_failed || size > m_bytes.size() - m_read)
	{
		m_failed = true;
		std::memset(data, 0, size);
		return;
	}
	std::memcpy(data, m_bytes.data() + m_read, size);
	m_read += size;
}
} // namespace Jettison::Physics::Detail
