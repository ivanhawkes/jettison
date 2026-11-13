#include <jettison/platform/Crash.h>

#include <elf.h>
#include <execinfo.h>
#include <fcntl.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <ucontext.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <format>
#include <stdexcept>
#include <string_view>
#include <system_error>

#include <jettison/core/Log.h>

namespace Jettison::Platform
{
namespace
{
constexpr Core::Category kCrash{"crash"};

constexpr std::array kSignals{SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT};
constexpr std::size_t kMaxFrames = 64;
constexpr std::size_t kMaxModules = 16;
constexpr std::size_t kPathBytes = 1024;
// The handler's stack: its own buffers are statics, so it needs little, but the symboliser's vfork
// child borrows it until it execs.
constexpr std::size_t kStackBytes = std::size_t{128} * 1024;
constexpr unsigned kTimeLimitSeconds = 30;

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
constexpr bool kSanitised = true;
#else
constexpr bool kSanitised = false;
#endif
#elif defined(__SANITIZE_ADDRESS__)
constexpr bool kSanitised = true;
#else
constexpr bool kSanitised = false;
#endif

// What the handler reads, set as it is installed and before any signal can reach it. Plain arrays,
// so reading them needs nothing a handler may not call.
struct Settings
{
	std::array<char, kPathBytes> directory{};
	std::array<char, 256> build{};
	std::array<char, kPathBytes> symboliser{};
};
Settings g_settings;
bool g_installed = false;
std::array<struct sigaction, kSignals.size()> g_previous{};
std::terminate_handler g_previousTerminate = nullptr;
alignas(16) std::array<std::byte, kStackBytes> g_stack{};
// The thread writing a report: a second thread's fatal signal waits for the first to end the
// process, and the same thread's, a fault inside the handler, ends it at once.
std::atomic<pid_t> g_reporting{0};

void Copy(std::array<char, kPathBytes> &to, const std::string &from, std::string_view what)
{
	if (from.size() >= to.size())
		throw std::runtime_error(
		    std::format(
		        "Crash handler: the {} '{}' is longer than {} bytes", what, from, to.size() - 1));
	std::memcpy(to.data(), from.c_str(), from.size() + 1);
}

std::string FindOnPath(std::string_view name)
{
	const char *path = std::getenv("PATH");
	if (!path)
		return {};
	std::string_view directories{path};
	while (!directories.empty())
	{
		const std::size_t colon = directories.find(':');
		const std::string_view directory = directories.substr(0, colon);
		directories =
		    colon == std::string_view::npos ? std::string_view{} : directories.substr(colon + 1);
		if (directory.empty())
			continue;
		std::string candidate = std::string{directory} + "/" + std::string{name};
		if (::access(candidate.c_str(), X_OK) == 0)
			return candidate;
	}
	return {};
}

// --- Everything below until OnTerminate runs inside the signal handler. ---

std::size_t Length(const char *text) noexcept
{
	std::size_t length = 0;
	while (text[length] != '\0')
		++length;
	return length;
}

void WriteAll(int descriptor, const char *bytes, std::size_t count) noexcept
{
	while (count > 0)
	{
		const ssize_t wrote = ::write(descriptor, bytes, count);
		if (wrote < 0 && errno == EINTR)
			continue;
		if (wrote <= 0)
			return;
		bytes += wrote;
		count -= static_cast<std::size_t>(wrote);
	}
}

// Text into a buffer, then to the report and to stderr, with no allocation.
class Writer
{
  public:
	Writer(int report, bool echo) noexcept : m_report{report}, m_echo{echo}
	{
	}
	~Writer()
	{
		Flush();
	}
	Writer(const Writer &) = delete;
	Writer &operator=(const Writer &) = delete;

	Writer &Text(std::string_view text) noexcept
	{
		for (const char character : text)
		{
			if (m_used == m_buffer.size())
				Flush();
			m_buffer[m_used++] = character;
		}
		return *this;
	}
	Writer &Text(const char *text) noexcept
	{
		return Text(std::string_view{text, Length(text)});
	}
	Writer &Decimal(std::uint64_t value, int width = 0) noexcept
	{
		std::array<char, 20> digits{};
		int count = 0;
		do
		{
			digits[static_cast<std::size_t>(count++)] = static_cast<char>('0' + value % 10);
			value /= 10;
		} while (value != 0);
		for (int pad = count; pad < width; ++pad)
			Text("0");
		while (count > 0)
			Text(std::string_view{&digits[static_cast<std::size_t>(--count)], 1});
		return *this;
	}
	Writer &Hex(std::uint64_t value, int width = 1) noexcept
	{
		constexpr std::string_view kDigits = "0123456789abcdef";
		Text("0x");
		int count = 16;
		while (count > width && ((value >> ((count - 1) * 4)) & 0xF) == 0)
			--count;
		for (int digit = count - 1; digit >= 0; --digit)
			Text(kDigits.substr((value >> (digit * 4)) & 0xF, 1));
		return *this;
	}
	void Flush() noexcept
	{
		if (m_report >= 0)
			WriteAll(m_report, m_buffer.data(), m_used);
		if (m_echo)
			WriteAll(STDERR_FILENO, m_buffer.data(), m_used);
		m_used = 0;
	}
	void Echo(bool echo) noexcept
	{
		Flush();
		m_echo = echo;
	}

  private:
	int m_report;
	bool m_echo;
	std::array<char, 1024> m_buffer{};
	std::size_t m_used{0};
};

// Text into a fixed array, for a file's name and a symboliser's arguments.
class Builder
{
  public:
	explicit Builder(char *pBuffer, std::size_t size) noexcept : m_pBuffer{pBuffer}, m_size{size}
	{
		m_pBuffer[0] = '\0';
	}
	Builder &Text(std::string_view text) noexcept
	{
		for (const char character : text)
			if (m_used + 1 < m_size)
				m_pBuffer[m_used++] = character;
		m_pBuffer[m_used] = '\0';
		return *this;
	}
	Builder &Decimal(std::uint64_t value, int width = 0) noexcept
	{
		std::array<char, 20> digits{};
		int count = 0;
		do
		{
			digits[static_cast<std::size_t>(count++)] = static_cast<char>('0' + value % 10);
			value /= 10;
		} while (value != 0);
		for (int pad = count; pad < width; ++pad)
			Text("0");
		while (count > 0)
			Text(std::string_view{&digits[static_cast<std::size_t>(--count)], 1});
		return *this;
	}

  private:
	char *m_pBuffer;
	std::size_t m_size;
	std::size_t m_used{0};
};

const char *SignalName(int signal) noexcept
{
	switch (signal)
	{
		case SIGSEGV: return "SIGSEGV";
		case SIGBUS: return "SIGBUS";
		case SIGFPE: return "SIGFPE";
		case SIGILL: return "SIGILL";
		case SIGABRT: return "SIGABRT";
		default: return "a signal";
	}
}

// What the kernel says caused it, where the code says more than the signal.
const char *Cause(const siginfo_t &info) noexcept
{
	if (info.si_code == SI_USER || info.si_code == SI_TKILL || info.si_code == SI_QUEUE)
		return "sent by a process";
	switch (info.si_signo)
	{
		case SIGSEGV:
			return info.si_code == SEGV_MAPERR   ? "an address that is not mapped"
			       : info.si_code == SEGV_ACCERR ? "an access the mapping does not permit"
			                                     : "a fault";
		case SIGBUS:
			return info.si_code == BUS_ADRALN   ? "an unaligned address"
			       : info.si_code == BUS_ADRERR ? "an address with nothing behind it"
			                                    : "a bus error";
		case SIGFPE:
			return info.si_code == FPE_INTDIV   ? "an integer divided by zero"
			       : info.si_code == FPE_INTOVF ? "an integer overflow"
			                                    : "an arithmetic fault";
		case SIGILL: return "an illegal instruction";
		default: return "";
	}
}

bool HasAddress(int signal) noexcept
{
	return signal == SIGSEGV || signal == SIGBUS || signal == SIGFPE || signal == SIGILL;
}

// Days since 1970-01-01 to a civil date (Howard Hinnant's civil_from_days).
void CivilFromDays(std::int64_t days, std::int64_t &year, unsigned &month, unsigned &day) noexcept
{
	days += 719468;
	const std::int64_t era = (days >= 0 ? days : days - 146096) / 146097;
	const auto dayOfEra = static_cast<unsigned>(days - era * 146097);
	const unsigned yearOfEra =
	    (dayOfEra - dayOfEra / 1460 + dayOfEra / 36524 - dayOfEra / 146096) / 365;
	const unsigned dayOfYear = dayOfEra - (365 * yearOfEra + yearOfEra / 4 - yearOfEra / 100);
	const unsigned monthIndex = (5 * dayOfYear + 2) / 153;
	day = dayOfYear - (153 * monthIndex + 2) / 5 + 1;
	month = monthIndex < 10 ? monthIndex + 3 : monthIndex - 9;
	year = static_cast<std::int64_t>(yearOfEra) + era * 400 + (month <= 2 ? 1 : 0);
}

// 2026-11-13T10:22:05Z, or 20261113T102205Z for a file's name.
template <typename Out> void WriteTime(Out &writer, std::int64_t seconds, bool compact) noexcept
{
	const std::int64_t days = (seconds >= 0 ? seconds : seconds - 86399) / 86400;
	const std::int64_t inDay = seconds - days * 86400;
	std::int64_t year = 0;
	unsigned month = 0;
	unsigned day = 0;
	CivilFromDays(days, year, month, day);
	writer.Decimal(static_cast<std::uint64_t>(year), 4).Text(compact ? "" : "-");
	writer.Decimal(month, 2).Text(compact ? "" : "-").Decimal(day, 2).Text("T");
	writer.Decimal(static_cast<std::uint64_t>(inDay / 3600), 2).Text(compact ? "" : ":");
	writer.Decimal(static_cast<std::uint64_t>(inDay / 60 % 60), 2).Text(compact ? "" : ":");
	writer.Decimal(static_cast<std::uint64_t>(inDay % 60), 2).Text("Z");
}

struct Module
{
	std::array<char, kPathBytes> path{};
	// What a frame's address is less to give the symboliser the address in the file: where the
	// module is loaded, or zero for an executable that is not position-independent.
	std::uintptr_t bias{0};
	std::size_t frames{0};
};

struct Frame
{
	std::uintptr_t address{0};
	int module{-1};
	// The address in the module's file, as its symbols have it.
	std::uintptr_t offset{0};
};

// The handler's working state, a static so that its size is nothing on the handler's stack.
struct Scratch
{
	std::array<void *, kMaxFrames> raw{};
	std::array<Frame, kMaxFrames> frames{};
	std::size_t frameCount{0};
	std::array<Module, kMaxModules> modules{};
	std::size_t moduleCount{0};
	std::array<char, 8192> read{};
	std::array<char, kPathBytes + 256> line{};
	std::array<char, kPathBytes> reportPath{};
	std::array<char, kMaxFrames * 20> addressText{};
	std::array<char *, kMaxFrames + 16> arguments{};
	std::array<char, 2048> command{};
};
Scratch g_scratch;

std::uintptr_t ParseHex(const char *&cursor) noexcept
{
	std::uintptr_t value = 0;
	for (;; ++cursor)
	{
		const char c = *cursor;
		if (c >= '0' && c <= '9')
			value = value * 16 + static_cast<std::uintptr_t>(c - '0');
		else if (c >= 'a' && c <= 'f')
			value = value * 16 + static_cast<std::uintptr_t>(c - 'a' + 10);
		else
			return value;
	}
}

void SkipField(const char *&cursor) noexcept
{
	while (*cursor != ' ' && *cursor != '\0')
		++cursor;
	while (*cursor == ' ')
		++cursor;
}

bool SamePath(const char *a, const char *b) noexcept
{
	return std::strcmp(a, b) == 0;
}

int FindModule(const char *path, std::uintptr_t bias) noexcept
{
	for (std::size_t index = 0; index < g_scratch.moduleCount; ++index)
		if (SamePath(g_scratch.modules[index].path.data(), path))
			return static_cast<int>(index);
	if (g_scratch.moduleCount == kMaxModules)
		return -1;
	Module &module = g_scratch.modules[g_scratch.moduleCount];
	Builder{module.path.data(), module.path.size()}.Text({path, Length(path)});
	module.bias = bias;
	module.frames = 0;
	return static_cast<int>(g_scratch.moduleCount++);
}

// One line of /proc/self/maps: "start-end perms offset device inode   path". The mappings are in
// address order, and a file's are together, the one at its offset 0 first: where it is loaded.
void ReadMapping(const char *text, const char *&currentPath, std::uintptr_t &currentBase) noexcept
{
	const char *cursor = text;
	const std::uintptr_t start = ParseHex(cursor);
	if (*cursor++ != '-')
		return;
	const std::uintptr_t end = ParseHex(cursor);
	SkipField(cursor);
	const bool readable = *cursor == 'r';
	SkipField(cursor);
	const std::uintptr_t offset = ParseHex(cursor);
	SkipField(cursor); // to the device
	SkipField(cursor); // to the inode
	SkipField(cursor); // to the path
	const char *path = cursor;
	if (*path != '/')
		return;
	if (offset == 0)
	{
		// An executable that is not position-independent has its symbols at its addresses.
		bool fixed = false;
		if (readable)
		{
			std::uint16_t type = 0;
			// NOLINTNEXTLINE(performance-no-int-to-ptr): the address is the mapping's, from the kernel.
			std::memcpy(&type, reinterpret_cast<const void *>(start + offsetof(Elf64_Ehdr, e_type)), 2);
			fixed = type == ET_EXEC;
		}
		currentPath = path;
		currentBase = fixed ? 0 : start;
	}
	for (std::size_t index = 0; index < g_scratch.frameCount; ++index)
	{
		Frame &frame = g_scratch.frames[index];
		if (frame.module >= 0 || frame.address < start || frame.address >= end)
			continue;
		const bool known = currentPath != nullptr && SamePath(currentPath, path);
		const std::uintptr_t bias = known ? currentBase : start - offset;
		frame.module = FindModule(path, bias);
		if (frame.module >= 0)
		{
			frame.offset =
			    frame.address - g_scratch.modules[static_cast<std::size_t>(frame.module)].bias;
			++g_scratch.modules[static_cast<std::size_t>(frame.module)].frames;
		}
	}
}

void FindModules() noexcept
{
	const int maps = ::open("/proc/self/maps", O_RDONLY | O_CLOEXEC);
	if (maps < 0)
		return;
	std::size_t lineLength = 0;
	// The current mapping at offset 0's path is in the line buffer only while its line is, so it is
	// kept here, apart from it.
	static std::array<char, kPathBytes> s_currentPath{};
	const char *currentPath = nullptr;
	std::uintptr_t currentBase = 0;
	for (;;)
	{
		const ssize_t count = ::read(maps, g_scratch.read.data(), g_scratch.read.size());
		if (count < 0 && errno == EINTR)
			continue;
		if (count <= 0)
			break;
		for (ssize_t at = 0; at < count; ++at)
		{
			const char character = g_scratch.read[static_cast<std::size_t>(at)];
			if (character != '\n')
			{
				if (lineLength + 1 < g_scratch.line.size())
					g_scratch.line[lineLength++] = character;
				continue;
			}
			g_scratch.line[lineLength] = '\0';
			const char *before = currentPath;
			ReadMapping(g_scratch.line.data(), currentPath, currentBase);
			if (currentPath != before && currentPath != nullptr)
			{
				Builder{s_currentPath.data(), s_currentPath.size()}.Text(
				    {currentPath, Length(currentPath)});
				currentPath = s_currentPath.data();
			}
			lineLength = 0;
		}
	}
	::close(maps);
}

// The symboliser over one module's frames, its output into the report: "0x...: function at
// file:line", with "(inlined by)" lines for the calls inlined into it. A return address is the
// instruction after its call, so the address named is the one before it, in the call.
void Symbolise(int report, std::size_t moduleIndex) noexcept
{
	Module &module = g_scratch.modules[moduleIndex];
	std::size_t argument = 0;
	g_scratch.arguments[argument++] = g_settings.symboliser.data();
	static std::array<char, 16> s_options{"-Cfipa"};
	static std::array<char, 4> s_file{"-e"};
	g_scratch.arguments[argument++] = s_options.data();
	g_scratch.arguments[argument++] = s_file.data();
	g_scratch.arguments[argument++] = module.path.data();
	char *pText = g_scratch.addressText.data();
	for (std::size_t index = 0; index < g_scratch.frameCount; ++index)
	{
		const Frame &frame = g_scratch.frames[index];
		if (frame.module != static_cast<int>(moduleIndex))
			continue;
		const std::uintptr_t address = index == 0 ? frame.offset : frame.offset - 1;
		constexpr std::string_view kDigits = "0123456789abcdef";
		Builder builder{pText, 20};
		builder.Text("0x");
		bool leading = true;
		for (int digit = 15; digit >= 0; --digit)
		{
			const std::uintptr_t nibble = (address >> (digit * 4)) & 0xF;
			if (leading && nibble == 0 && digit != 0)
				continue;
			leading = false;
			builder.Text(kDigits.substr(nibble, 1));
		}
		g_scratch.arguments[argument++] = pText;
		pText += 20;
	}
	g_scratch.arguments[argument] = nullptr;

	// vfork, not fork: fork runs the process's fork handlers, malloc's among them, which take the
	// locks a crash may be holding. The child only moves its descriptors and execs.
	// NOLINTNEXTLINE(bugprone-unsafe-functions, clang-analyzer-security.insecureAPI.vfork)
	const pid_t child = ::vfork();
	if (child == 0)
	{
		// What it says of the debug information it reads is not the report's.
		const int quiet = ::open("/dev/null", O_WRONLY);
		::dup2(report, STDOUT_FILENO);
		::dup2(quiet >= 0 ? quiet : report, STDERR_FILENO);
		::execv(g_settings.symboliser.data(), g_scratch.arguments.data());
		::_exit(127);
	}
	if (child < 0)
		return;
	int status = 0;
	while (::waitpid(child, &status, 0) < 0 && errno == EINTR) {}
}

void CopyToStderr(int report, off_t from) noexcept
{
	for (;;)
	{
		const ssize_t count = ::pread(report, g_scratch.read.data(), g_scratch.read.size(), from);
		if (count < 0 && errno == EINTR)
			continue;
		if (count <= 0)
			return;
		WriteAll(STDERR_FILENO, g_scratch.read.data(), static_cast<std::size_t>(count));
		from += count;
	}
}

void WriteCommandLine(Writer &writer) noexcept
{
	const int file = ::open("/proc/self/cmdline", O_RDONLY | O_CLOEXEC);
	if (file < 0)
		return;
	const ssize_t count = ::read(file, g_scratch.command.data(), g_scratch.command.size());
	::close(file);
	for (ssize_t at = 0; at < count; ++at)
	{
		const char character = g_scratch.command[static_cast<std::size_t>(at)];
		const bool last = at + 1 == count;
		if (character == '\0')
		{
			if (!last)
				writer.Text(" ");
		}
		else
			writer.Text(std::string_view{&character, 1});
	}
}

void WriteReport(int signal, const siginfo_t &info, const ucontext_t &context) noexcept
{
	const pid_t process = ::getpid();
	const auto thread = static_cast<pid_t>(::syscall(SYS_gettid));
	timespec now{};
	::clock_gettime(CLOCK_REALTIME, &now);

	// crash-20261113T102205Z-12345.txt, in the directory made as the handler was installed.
	Builder{g_scratch.reportPath.data(), g_scratch.reportPath.size()}
	    .Text({g_settings.directory.data(), Length(g_settings.directory.data())})
	    .Text("/crash-");
	{
		Builder file{
		    g_scratch.reportPath.data() + Length(g_scratch.reportPath.data()),
		    g_scratch.reportPath.size() - Length(g_scratch.reportPath.data())};
		WriteTime(file, now.tv_sec, true);
		file.Text("-").Decimal(static_cast<std::uint64_t>(process)).Text(".txt");
	}
	const int report =
	    ::open(g_scratch.reportPath.data(), O_RDWR | O_CREAT | O_TRUNC | O_CLOEXEC, S_IRUSR | S_IWUSR);

	Writer writer{report, true};
	writer.Text("\nFatal signal ")
	    .Text(SignalName(signal))
	    .Text(" (")
	    .Decimal(static_cast<unsigned>(signal));
	writer.Text(")");
	if (const char *cause = Cause(info); *cause != '\0')
		writer.Text(", ").Text(cause);
	if (HasAddress(signal) && info.si_code != SI_USER && info.si_code != SI_TKILL)
		writer.Text(", at ").Hex(reinterpret_cast<std::uintptr_t>(info.si_addr));
	writer.Text("\nThread ").Decimal(static_cast<std::uint64_t>(thread));
	std::array<char, 17> threadName{};
	if (::prctl(PR_GET_NAME, threadName.data(), 0, 0, 0) == 0)
		writer.Text(" '").Text(threadName.data()).Text("'");
	writer.Text(thread == process ? ", the main thread" : "").Text(", of process ");
	writer.Decimal(static_cast<std::uint64_t>(process)).Text("\nTime ");
	WriteTime(writer, now.tv_sec, false);
	writer.Text("\nBuild ").Text(g_settings.build.data()).Text("\nCommand ");
	WriteCommandLine(writer);
	writer.Text("\n");

	// The frames from the handler's own up to the signal's are the handler's and the kernel's
	// trampoline: the report starts at the instruction the signal stopped, its frame 0. Where the
	// unwinder did not reach it, that instruction comes first, then every frame it found.
	const auto pc = static_cast<std::uintptr_t>(context.uc_mcontext.gregs[REG_RIP]);
	const int count = ::backtrace(g_scratch.raw.data(), static_cast<int>(g_scratch.raw.size()));
	auto first = static_cast<std::size_t>(count);
	for (std::size_t index = 0; index < static_cast<std::size_t>(count); ++index)
		if (reinterpret_cast<std::uintptr_t>(g_scratch.raw[index]) == pc)
		{
			first = index;
			break;
		}
	g_scratch.frameCount = 0;
	g_scratch.moduleCount = 0;
	if (first == static_cast<std::size_t>(count))
	{
		g_scratch.frames[g_scratch.frameCount++] = Frame{pc, -1, 0};
		first = 0;
	}
	for (std::size_t index = first; index < static_cast<std::size_t>(count); ++index)
		if (g_scratch.frameCount < kMaxFrames)
			g_scratch.frames[g_scratch.frameCount++] =
			    Frame{reinterpret_cast<std::uintptr_t>(g_scratch.raw[index]), -1, 0};
	FindModules();

	writer.Text("\nFrames, as module+offset:\n");
	for (std::size_t index = 0; index < g_scratch.frameCount; ++index)
	{
		const Frame &frame = g_scratch.frames[index];
		writer.Text("  #").Decimal(index, 2).Text(" ").Hex(frame.address, 12).Text("  ");
		if (frame.module >= 0)
			writer.Text(g_scratch.modules[static_cast<std::size_t>(frame.module)].path.data())
			    .Text("+")
			    .Hex(frame.offset);
		else
			writer.Text("?");
		writer.Text("\n");
	}

	if (g_settings.symboliser[0] != '\0' && report >= 0)
	{
		writer.Text("\nFrames, named by ").Text(g_settings.symboliser.data()).Text(":\n");
		writer.Flush();
		const off_t named = ::lseek(report, 0, SEEK_END);
		for (std::size_t module = 0; module < g_scratch.moduleCount; ++module)
			if (g_scratch.modules[module].frames != 0)
				Symbolise(report, module);
		CopyToStderr(report, named);
	}

	writer.Echo(false);
	writer.Text("\nThe log's tail:\n");
	writer.Flush();
	if (report >= 0)
	{
		Core::WriteLogTail(report);
		::fsync(report);
		::close(report);
		writer.Echo(true);
		writer.Text("The crash report is ").Text(g_scratch.reportPath.data()).Text("\n");
	}
	else
	{
		writer.Echo(true);
		writer.Text("The crash report could not be written to ").Text(g_scratch.reportPath.data());
		writer.Text("\n");
	}
}

void OnSignal(int signal, siginfo_t *pInfo, void *pContext)
{
	const auto thread = static_cast<pid_t>(::syscall(SYS_gettid));
	pid_t idle = 0;
	if (!g_reporting.compare_exchange_strong(idle, thread))
	{
		// A fault inside the handler ends the process as the signal would have; another thread's
		// waits for the report to end it.
		if (idle != thread)
			for (;;)
				::pause();
	}
	else
	{
		// A handler that hangs, a symboliser stuck on a lock, still lets the process end.
		struct sigaction alarm{};
		alarm.sa_handler = SIG_DFL;
		::sigaction(SIGALRM, &alarm, nullptr);
		::alarm(kTimeLimitSeconds);
		WriteReport(signal, *pInfo, *static_cast<const ucontext_t *>(pContext));
	}

	// The default action, raised as the handler returns: a fault that returns runs its instruction
	// again, and an abort's raise did not return, so both end as they would have without a handler.
	struct sigaction fallback{};
	fallback.sa_handler = SIG_DFL;
	::sigaction(signal, &fallback, nullptr);
	::raise(signal);
}

// --- The signal handler ends here. ---

[[noreturn]] void OnTerminate() noexcept
{
	try
	{
		if (const std::exception_ptr exception = std::current_exception())
			std::rethrow_exception(exception);
		Core::Log(Core::Severity::Fatal, kCrash, "std::terminate was called with no exception");
	}
	catch (const std::exception &error)
	{
		Core::Log(Core::Severity::Fatal, kCrash, "An exception went uncaught: {}", error.what());
	}
	catch (...)
	{
		Core::Log(
		    Core::Severity::Fatal, kCrash,
		    "An exception of a type that is not std::exception went uncaught");
	}
	Core::FlushLogging();
	std::abort();
}
} // namespace

bool InstallCrashHandler(const CrashOptions &options)
{
	if (kSanitised)
	{
		Core::Log(
		    Core::Severity::Info, kCrash,
		    "No crash handler: the address sanitiser reports fatal signals in this build");
		return false;
	}

	std::error_code error;
	std::filesystem::create_directories(options.directory, error);
	if (error)
		throw std::runtime_error(
		    std::format(
		        "Crash handler: the directory '{}' could not be made: {}", options.directory.string(),
		        error.message()));
	std::string symboliser;
	if (options.symbolise)
		symboliser = options.symboliser.empty() ? FindOnPath("addr2line") : options.symboliser.string();

	// The new settings are copied in with the signals blocked, so a crash on another thread meanwhile
	// does not read them half written.
	sigset_t all{};
	sigset_t before{};
	sigfillset(&all);
	pthread_sigmask(SIG_BLOCK, &all, &before);
	try
	{
		Copy(g_settings.directory, std::filesystem::absolute(options.directory).string(), "directory");
		Copy(g_settings.symboliser, symboliser, "symboliser");
		const std::string build = options.build.substr(0, g_settings.build.size() - 1);
		std::memcpy(g_settings.build.data(), build.c_str(), build.size() + 1);
	}
	catch (...)
	{
		pthread_sigmask(SIG_SETMASK, &before, nullptr);
		throw;
	}

	if (!g_installed)
	{
		// The first backtrace loads libgcc's unwinder, which allocates: done here, not in a handler.
		std::array<void *, 4> prime{};
		::backtrace(prime.data(), static_cast<int>(prime.size()));

		stack_t stack{};
		stack.ss_sp = g_stack.data();
		stack.ss_size = g_stack.size();
		::sigaltstack(&stack, nullptr);

		struct sigaction action{};
		action.sa_sigaction = OnSignal;
		action.sa_flags = SA_SIGINFO | SA_ONSTACK;
		sigemptyset(&action.sa_mask);
		for (std::size_t index = 0; index < kSignals.size(); ++index)
			::sigaction(kSignals[index], &action, &g_previous[index]);
		g_previousTerminate = std::set_terminate(OnTerminate);
		g_installed = true;
	}
	pthread_sigmask(SIG_SETMASK, &before, nullptr);
	Core::Log(
	    Core::Severity::Debug, kCrash, "Crash reports go to {}{}", g_settings.directory.data(),
	    symboliser.empty() ? ", their frames unnamed" : ", their frames named by " + symboliser);
	return true;
}

void UninstallCrashHandler() noexcept
{
	if (!g_installed)
		return;
	for (std::size_t index = 0; index < kSignals.size(); ++index)
		::sigaction(kSignals[index], &g_previous[index], nullptr);
	std::set_terminate(g_previousTerminate);
	stack_t stack{};
	stack.ss_flags = SS_DISABLE;
	::sigaltstack(&stack, nullptr);
	g_installed = false;
}
} // namespace Jettison::Platform
