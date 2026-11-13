#include <jettison/render/ColourGrade.h>

#include <charconv>
#include <cmath>
#include <cstddef>
#include <format>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <vector>

#include <jettison/resource/ResourceManager.h>

namespace Jettison::Render
{
namespace
{
[[noreturn]] void Refuse(std::string_view file, std::size_t line, const std::string &what)
{
	throw std::runtime_error(std::format("Colour grade '{}', line {}: {}", file, line, what));
}

std::vector<std::string_view> Words(std::string_view line)
{
	std::vector<std::string_view> words;
	std::size_t at = 0;
	while (at < line.size())
	{
		while (at < line.size() && (line[at] == ' ' || line[at] == '\t'))
			++at;
		std::size_t end = at;
		while (end < line.size() && line[end] != ' ' && line[end] != '\t')
			++end;
		if (end > at)
			words.push_back(line.substr(at, end - at));
		at = end;
	}
	return words;
}

std::optional<float> Number(std::string_view word)
{
	float value = 0.0f;
	const auto [end, error] = std::from_chars(word.data(), word.data() + word.size(), value);
	if (error != std::errc{} || end != word.data() + word.size() || !std::isfinite(value))
		return std::nullopt;
	return value;
}

// The three numbers after a keyword, or of an entry.
glm::vec3 Triple(
    std::span<const std::string_view> words, std::string_view file, std::size_t line,
    std::string_view what)
{
	if (words.size() != 3)
		Refuse(file, line, std::format("{} takes three numbers", what));
	glm::vec3 value{0.0f};
	for (std::size_t channel = 0; channel < 3; ++channel)
	{
		const std::optional<float> number = Number(words[channel]);
		if (!number)
			Refuse(
			    file, line,
			    std::format("{} has '{}', which is not a finite number", what, words[channel]));
		value[static_cast<glm::length_t>(channel)] = *number;
	}
	return value;
}
} // namespace

ColourLut ReadCubeLut(std::string_view text, std::string_view name)
{
	ColourLut lut;
	std::size_t lineNumber = 0;
	std::size_t at = 0;
	bool sized = false;
	while (at <= text.size())
	{
		std::size_t end = text.find('\n', at);
		if (end == std::string_view::npos)
			end = text.size();
		std::string_view line = text.substr(at, end - at);
		at = end + 1;
		++lineNumber;
		if (const std::size_t comment = line.find('#'); comment != std::string_view::npos)
			line = line.substr(0, comment);
		if (!line.empty() && line.back() == '\r')
			line.remove_suffix(1);
		const std::vector<std::string_view> words = Words(line);
		if (words.empty())
			continue;
		const std::string_view keyword = words.front();
		const std::span<const std::string_view> rest{words.begin() + 1, words.end()};
		if (keyword == "TITLE")
		{
			const std::size_t open = line.find('"');
			const std::size_t close = line.rfind('"');
			if (open == std::string_view::npos || close == open)
				Refuse(name, lineNumber, "TITLE takes a quoted title");
			lut.title = std::string{line.substr(open + 1, close - open - 1)};
		}
		else if (keyword == "LUT_1D_SIZE")
			Refuse(name, lineNumber, "it is a 1D table, and a grade is a 3D one");
		else if (keyword == "LUT_3D_SIZE")
		{
			const std::optional<float> size = rest.size() == 1 ? Number(rest[0]) : std::nullopt;
			if (sized || !size || *size != std::floor(*size) || *size < float{kMinLutSize} ||
			    *size > float{kMaxLutSize})
				Refuse(
				    name, lineNumber,
				    std::format(
				        "LUT_3D_SIZE is given once, before the entries, as a whole number from {} to "
				        "{}",
				        kMinLutSize, kMaxLutSize));
			lut.size = static_cast<std::uint32_t>(*size);
			lut.values.reserve(std::size_t{lut.size} * lut.size * lut.size);
			sized = true;
		}
		else if (keyword == "DOMAIN_MIN" || keyword == "DOMAIN_MAX")
		{
			const glm::vec3 bound = Triple(rest, name, lineNumber, keyword);
			if (bound != glm::vec3{keyword == "DOMAIN_MIN" ? 0.0f : 1.0f})
				Refuse(name, lineNumber, "a grade's domain is 0 to 1, as the display's is");
		}
		else if (keyword == "LUT_3D_INPUT_RANGE")
		{
			const std::optional<float> low = rest.size() == 2 ? Number(rest[0]) : std::nullopt;
			const std::optional<float> high = rest.size() == 2 ? Number(rest[1]) : std::nullopt;
			if (low != 0.0f || high != 1.0f)
				Refuse(name, lineNumber, "a grade's input range is 0 to 1, as the display's is");
		}
		else if (Number(keyword))
		{
			if (!sized)
				Refuse(name, lineNumber, "an entry comes before LUT_3D_SIZE");
			if (lut.values.size() == std::size_t{lut.size} * lut.size * lut.size)
				Refuse(
				    name, lineNumber,
				    std::format(
				        "more entries than the {} a table of size {} has", lut.values.size(),
				        lut.size));
			lut.values.push_back(Triple(words, name, lineNumber, "an entry"));
		}
		else
			Refuse(name, lineNumber, std::format("'{}' is not a keyword of a .cube", keyword));
	}
	if (!sized)
		Refuse(name, lineNumber, "there is no LUT_3D_SIZE");
	const std::size_t count = std::size_t{lut.size} * lut.size * lut.size;
	if (lut.values.size() != count)
		Refuse(
		    name, lineNumber,
		    std::format(
		        "{} entries, where a table of size {} has {}", lut.values.size(), lut.size, count));
	return lut;
}

ColourLut IdentityLut(std::uint32_t size)
{
	ColourLut lut;
	lut.size = size;
	lut.title = "identity";
	const auto last = static_cast<float>(size - 1);
	for (std::uint32_t b = 0; b < size; ++b)
		for (std::uint32_t g = 0; g < size; ++g)
			for (std::uint32_t r = 0; r < size; ++r)
				lut.values.emplace_back(
				    static_cast<float>(r) / last, static_cast<float>(g) / last,
				    static_cast<float>(b) / last);
	return lut;
}

glm::vec3 SampleLut(const ColourLut &lut, glm::vec3 colour)
{
	const auto last = static_cast<float>(lut.size - 1);
	const glm::vec3 at = glm::clamp(colour, 0.0f, 1.0f) * last;
	const glm::uvec3 low = glm::min(glm::uvec3{glm::floor(at)}, glm::uvec3{lut.size - 2});
	const glm::vec3 fraction = at - glm::vec3{low};
	const auto entry = [&](std::uint32_t r, std::uint32_t g, std::uint32_t b)
	{ return lut.values[r + lut.size * (g + std::size_t{lut.size} * b)]; };
	glm::vec3 result{0.0f};
	for (std::uint32_t corner = 0; corner < 8; ++corner)
	{
		const glm::uvec3 step{corner & 1u, (corner >> 1) & 1u, (corner >> 2) & 1u};
		const glm::vec3 weight = glm::mix(1.0f - fraction, fraction, glm::vec3{step});
		result +=
		    weight.x * weight.y * weight.z * entry(low.x + step.x, low.y + step.y, low.z + step.z);
	}
	return result;
}

void RegisterColourLutLoader(Resource::ResourceManager &resources)
{
	resources.RegisterLoader(
	    Resource::Loader<ColourLut>{
	        [](std::span<const std::byte> bytes, std::string_view path)
	        {
		        return std::make_unique<ColourLut>(ReadCubeLut(
		            std::string_view{reinterpret_cast<const char *>(bytes.data()), bytes.size()},
		            path));
	        },
	        {},
	        {}});
}
} // namespace Jettison::Render
