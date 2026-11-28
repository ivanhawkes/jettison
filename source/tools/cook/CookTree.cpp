#include "CookTree.h"

#include <fastgltf/core.hpp>
#include <fastgltf/tools.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <format>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>

#include "Bake.h"
#include "Settings.h"
#include "Texture.h"
#include <jettison/core/File.h>
#include <jettison/core/Hash.h>
#include <jettison/core/Log.h>
#include <jettison/geometry/Cook.h>
#include <jettison/geometry/GltfFile.h>
#include <jettison/physics/CollisionFile.h>
#include <jettison/physics/Components.h>
#include <jettison/reflect/Reflect.h>
#include <jettison/serialise/Archive.h>

namespace Jettison::Cook
{
namespace
{
namespace fs = std::filesystem;

using Bytes = std::vector<std::byte>;

constexpr Core::Category kCook{"cook"};

constexpr Serialise::FormatVersion kRecordVersion{1};

constexpr std::string_view kSettingsExtension = ".yaml";

constexpr std::string_view kModelOutputExtension = ".glb";

constexpr std::string_view kTextureOutputExtension = ".ktx2";

// A model's collision file is at its path with collision/ for models/ and this extension
// (docs/physics.md, "A model's collision shape"); one outside models/ is under collision/ whole.
constexpr std::string_view kCollisionExtension = ".collision";

constexpr std::string_view kModelsDirectory = "models";

constexpr std::string_view kCollisionDirectory = "collision";

constexpr std::array kModelExtensions{std::string_view{".gltf"}, std::string_view{".glb"}};

constexpr std::array kImageExtensions{
    std::string_view{".png"}, std::string_view{".jpg"}, std::string_view{".jpeg"},
    std::string_view{".tga"}};

// Fonts keep their upstream names (docs/coding-standards.md, "Content files").
constexpr std::array kFontExtensions{std::string_view{".ttf"}, std::string_view{".otf"}};

// A loose image's map, the last word of its name, that is colour (docs/coding-standards.md, "Content
// files"); a detail layer's colour is data, as the import script encodes it.
constexpr std::array kColourMaps{std::string_view{"-color"}, std::string_view{"-emissive"}};

constexpr std::string_view kDetailMap = "-detail";

// What geometry's reader enables (GltfFile.cpp), so a source it reads lists its references too.
constexpr auto kGltfExtensions = fastgltf::Extensions::EXT_meshopt_compression |
                                 fastgltf::Extensions::KHR_mesh_quantization |
                                 fastgltf::Extensions::KHR_texture_basisu;

// Folded into a key for a referenced file that is not there, which no file's hash stands for.
constexpr std::uint64_t kMissingFile = 0;

struct RecordEntry
{
	std::string key;
	std::vector<std::string> outputs;
	std::vector<std::string> references;
};

struct Record
{
	std::map<std::string, RecordEntry> sources;
};

enum class Kind
{
	Model,
	Image,
	Copy
};

struct Source
{
	std::string path;
	Kind kind{Kind::Copy};
	std::vector<std::string> outputs;
};

enum class Result
{
	Cooked,
	Skipped,
	Refused
};

// What became of a source on the cook's threads, taken into the report and the record in the sources'
// order, so neither depends on which thread finished first.
struct Outcome
{
	Result result{Result::Refused};
	RecordEntry entry;
	std::string refusal;

	// The files the source references, which are not sources of their own even when it is refused.
	std::vector<std::string> references;
};


void RegisterTypes()
{
	static const bool registered = []
	{
		using namespace Reflect;
		RegisterType<RecordEntry>("Cook.RecordEntry")
		    .Field<&RecordEntry::key>("key", Attribute::Saved)
		    .Field<&RecordEntry::outputs>("outputs", Attribute::Saved)
		    .Field<&RecordEntry::references>("references", Attribute::Saved);
		RegisterType<Record>("Cook.Record").Field<&Record::sources>("sources", Attribute::Saved);
		return true;
	}();
	(void)registered;
}


std::optional<Bytes> TryReadFile(const fs::path &path)
{
	std::ifstream stream{path, std::ios::binary};
	if (!stream)
		return std::nullopt;
	const std::vector<char> chars{
	    std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
	const auto *pBytes = reinterpret_cast<const std::byte *>(chars.data());
	return Bytes{pBytes, pBytes + chars.size()};
}


Bytes ReadFile(const fs::path &path)
{
	std::optional<Bytes> bytes = TryReadFile(path);
	if (!bytes)
		throw std::runtime_error("Cannot read " + path.string());
	return std::move(*bytes);
}


std::string AsText(const Bytes &bytes)
{
	return {reinterpret_cast<const char *>(bytes.data()), bytes.size()};
}


bool HasExtension(std::string_view path, std::span<const std::string_view> extensions)
{
	return std::ranges::any_of(
	    extensions, [&](std::string_view extension) { return path.ends_with(extension); });
}


bool IsKebab(std::string_view word)
{
	const bool characters = std::ranges::all_of(
	    word,
	    [](char ch) { return (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-'; });
	return characters && !word.empty() && word.front() != '-' && word.back() != '-' &&
	       word.find("--") == std::string_view::npos;
}


// Every directory, and each part of the file's name between dots, is kebab-case, so
// big-ship.gltf.yaml and arm.material.yaml follow it. A font's own name is exempt, as the rule says.
bool FollowsNamingRule(const std::string &relative)
{
	const fs::path path{relative};
	const bool font = HasExtension(relative, kFontExtensions);
	for (const fs::path &part : font ? path.parent_path() : path)
	{
		const std::string text = part.string();
		std::size_t start = 0;
		for (std::size_t dot = text.find('.');; dot = text.find('.', start))
		{
			if (!IsKebab(std::string_view{text}.substr(start, dot - start)))
				return false;
			if (dot == std::string::npos)
				break;
			start = dot + 1;
		}
	}
	return true;
}


// A relative path that stays where it starts: no root and no "..", as the record's outputs do within
// the output.
bool StaysWithin(const fs::path &path)
{
	return !path.empty() && path.is_relative() && !path.has_root_name() &&
	       std::ranges::none_of(path, [](const fs::path &part) { return part == ".."; });
}


// The first symbolic link on relative's way down from root, relative itself included.
std::optional<std::string> LinkWithin(const fs::path &root, const fs::path &relative)
{
	fs::path walked;
	for (const fs::path &part : relative)
	{
		walked /= part;
		std::error_code error;
		if (fs::is_symlink(root / walked, error))
			return walked.generic_string();
	}
	return std::nullopt;
}


// The source-relative path of a file a model at modelPath names by uri, which is beside it or below
// it by the rule geometry's reader holds its buffers to (Geometry::FileWithin).
std::string ReferencePath(const fs::path &root, const std::string &modelPath, const fastgltf::URI &uri)
{
	const fs::path relative = uri.fspath();
	const fs::path directory = fs::path{modelPath}.parent_path();
	if (!uri.isLocalPath() || !Geometry::FileWithin(root / directory, relative))
		throw std::runtime_error(
		    std::format(
		        "{}: it references '{}', which is not a file beside it or below it", modelPath,
		        uri.string()));
	return (directory / relative).lexically_normal().generic_string();
}


// The files a glTF's buffers and images are in, sorted.
std::vector<std::string> References(const Bytes &bytes, const fs::path &root, const std::string &path)
{
	auto data = fastgltf::GltfDataBuffer::FromBytes(bytes.data(), bytes.size());
	if (data.error() != fastgltf::Error::None)
		throw std::runtime_error(std::format("{}: it is not a glTF model the cook can read", path));
	fastgltf::Parser parser{kGltfExtensions};
	auto parsed = parser.loadGltf(data.get(), {});
	if (parsed.error() != fastgltf::Error::None)
		throw std::runtime_error(
		    std::format(
		        "{}: it is not a glTF model the cook can read ({})", path,
		        fastgltf::getErrorMessage(parsed.error())));
	std::set<std::string> references;
	const auto add = [&](const auto &data)
	{
		if (const auto *pUri = std::get_if<fastgltf::sources::URI>(&data))
			references.insert(ReferencePath(root, path, *pUri->uri));
	};
	for (const auto &buffer : parsed->buffers)
		add(buffer.data);
	for (const auto &image : parsed->images)
		add(image.data);
	return {references.begin(), references.end()};
}


std::uint64_t HashBytes(std::span<const std::byte> bytes)
{
	return Core::Fnv1a64({reinterpret_cast<const char *>(bytes.data()), bytes.size()});
}


std::string CookKey(
    const Bytes &bytes, const fs::path &root, std::span<const std::string> references,
    std::span<const SettingsLayer> layers)
{
	std::uint64_t hash = Core::HashCombine(Core::Fnv1a64("jettison_cook"), kCookVersion);
	hash = Core::HashCombine(hash, HashBytes(bytes));
	for (const std::string &reference : references)
	{
		hash = Core::HashCombine(hash, Core::Fnv1a64(reference));

		// The record's list is not parsed again, so a link changed since to lead out of the tree is
		// not read: it hashes as missing, and the model, parsed again, is refused.
		const std::optional<fs::path> within = Geometry::FileWithin(root, reference);
		const std::optional<Bytes> referenced = within ? TryReadFile(*within) : std::nullopt;
		hash = Core::HashCombine(hash, referenced ? HashBytes(*referenced) : kMissingFile);
	}
	for (const SettingsLayer &layer : layers)
	{
		hash = Core::HashCombine(hash, Core::Fnv1a64(layer.name));
		hash = Core::HashCombine(hash, Core::Fnv1a64(layer.text));
	}
	return std::format("{:016x}", hash);
}


std::string WithExtension(const std::string &path, std::string_view extension)
{
	return fs::path{path}.replace_extension(extension).generic_string();
}


std::string CollisionPath(const std::string &modelPath)
{
	const fs::path model{modelPath};
	fs::path under = model;
	if (std::distance(model.begin(), model.end()) > 1 && model.begin()->string() == kModelsDirectory)
		under = model.lexically_relative(kModelsDirectory);
	return (fs::path{kCollisionDirectory} / under)
	    .replace_extension(kCollisionExtension)
	    .generic_string();
}


ColourSpace LooseColourSpace(const std::string &path)
{
	const std::string name = fs::path{path}.filename().string();
	const std::string_view stem = std::string_view{name}.substr(0, name.find('.'));
	for (const std::string_view map : kColourMaps)
		if (stem.ends_with(map) && !stem.substr(0, stem.size() - map.size()).ends_with(kDetailMap))
			return ColourSpace::Srgb;
	return ColourSpace::Linear;
}


// Each image's colour space by the material slots that name it: base colour and emission are colour,
// the rest data, as is an image no material names.
std::vector<ColourSpace> ImageColourSpaces(const fastgltf::Asset &asset, const std::string &path)
{
	std::vector<std::optional<ColourSpace>> spaces(asset.images.size());
	const auto use = [&](const auto &info, ColourSpace space)
	{
		if (!info.has_value())
			return;
		const fastgltf::Texture &texture = asset.textures.at(info->textureIndex);
		const auto image = texture.imageIndex ? texture.imageIndex : texture.basisuImageIndex;
		if (!image)
			return;
		auto &chosen = spaces.at(*image);
		if (chosen && *chosen != space)
			throw std::runtime_error(
			    std::format(
			        "{}: image {} is used as colour and as data, which need two encodings", path,
			        *image));
		chosen = space;
	};
	for (const fastgltf::Material &material : asset.materials)
	{
		use(material.pbrData.baseColorTexture, ColourSpace::Srgb);
		use(material.emissiveTexture, ColourSpace::Srgb);
		use(material.pbrData.metallicRoughnessTexture, ColourSpace::Linear);
		use(material.normalTexture, ColourSpace::Linear);
		use(material.occlusionTexture, ColourSpace::Linear);
	}
	std::vector<ColourSpace> chosen;
	chosen.reserve(spaces.size());
	for (const auto &space : spaces)
		chosen.push_back(space.value_or(ColourSpace::Linear));
	return chosen;
}


Bytes ImageBytes(
    const fastgltf::Asset &asset, const fastgltf::Image &image, const fs::path &root,
    const std::string &path)
{
	return std::visit(
	    [&](const auto &data) -> Bytes
	    {
		    using Data = std::decay_t<decltype(data)>;
		    if constexpr (std::is_same_v<Data, fastgltf::sources::URI>)
			    return ReadFile(root / ReferencePath(root, path, *data.uri));
		    else if constexpr (std::is_same_v<Data, fastgltf::sources::BufferView>)
		    {
			    const auto bytes = fastgltf::DefaultBufferDataAdapter{}(asset, data.bufferViewIndex);
			    return {bytes.begin(), bytes.end()};
		    }
		    else if constexpr (std::is_same_v<Data, fastgltf::sources::Array>)
			    return {data.bytes.begin(), data.bytes.end()};
		    else if constexpr (std::is_same_v<Data, fastgltf::sources::Vector>)
			    return data.bytes;
		    else
			    throw std::runtime_error(
			        std::format("{}: image '{}' has no data the cook can read", path, image.name));
	    },
	    image.data);
}


class TreeCook
{
  public:
	TreeCook(fs::path source, fs::path output)
	    : m_source{std::move(source)}, m_output{std::move(output)}
	{
	}


	CookReport Run()
	{
		CheckTrees(m_source, m_output);
		if (!fs::is_directory(m_source))
			throw std::runtime_error("The cook's source is not a directory: " + m_source.string());
		ReadRecord();
		const std::vector<Source> sources = FindSources();

		// The reflection tables are global and not locked, so every type the threads read or write is
		// registered before they start.
		m_defaults = DefaultSettingsLayer();
		Physics::RegisterTypes();

		// A model goes first, since the files it references are not sources of their own, so the rest
		// clash with the models' outputs and each other's once those are known.
		std::vector<const Source *> models;
		for (const Source &source : sources)
			if (source.kind == Kind::Model)
				models.push_back(&source);
		models = WithoutClashes(std::move(models), {});
		std::set<std::string> referenced;
		for (Outcome &outcome : ProcessAll(models))
			referenced.insert(outcome.references.begin(), outcome.references.end());
		std::vector<const Source *> others;
		for (const Source &source : sources)
			if (source.kind != Kind::Model && !referenced.contains(source.path))
				others.push_back(&source);
		ProcessAll(WithoutClashes(std::move(others), models));

		DeleteLostOutputs();
		Core::WriteFileDurably(
		    m_output / kRecordName, Serialise::YamlArchive::Write(m_next, kRecordVersion));
		Core::Log(
		    Core::Severity::Info, kCook, "{} cooked, {} unchanged, {} deleted, {} refused",
		    m_report.cooked.size(), m_report.skipped.size(), m_report.deleted.size(),
		    m_report.refusals.size());
		return std::move(m_report);
	}

  private:
	void ReadRecord()
	{
		RegisterTypes();
		const std::optional<Bytes> bytes = TryReadFile(m_output / kRecordName);
		if (!bytes)
			return;
		try
		{
			m_previous = Serialise::YamlArchive::Read<Record>(AsText(*bytes), kRecordVersion);
		}
		catch (const std::exception &error)
		{
			throw std::runtime_error(
			    std::format(
			        "The cook record {} does not read: {}", (m_output / kRecordName).string(),
			        error.what()));
		}
	}


	// Every file in the source, sorted, hidden ones left out, refusing a name that breaks the rule.
	std::vector<Source> FindSources()
	{
		std::vector<std::string> files;
		for (auto entry = fs::recursive_directory_iterator{m_source};
		     entry != fs::recursive_directory_iterator{}; ++entry)
		{
			if (entry->path().filename().string().starts_with('.'))
			{
				entry.disable_recursion_pending();
				continue;
			}
			std::string path = entry->path().lexically_relative(m_source).generic_string();

			// The watch cannot see a change to what a link out of the tree leads to, so the record
			// would go stale. A directory's link within it is not walked, its files being sources
			// at their own paths.
			if (entry->is_symlink() && !Geometry::FileWithin(m_source, path))
			{
				Refuse(
				    path,
				    std::format("{}: it is a symbolic link that leads out of the source tree", path));
				continue;
			}
			if (entry->is_regular_file())
				files.push_back(std::move(path));
		}
		std::ranges::sort(files);
		m_present = {files.begin(), files.end()};
		if (m_present.contains(std::string{kGameSettingsName}))
			m_gameSettings = SettingsLayer{
			    std::string{kGameSettingsName}, AsText(ReadFile(m_source / kGameSettingsName))};

		std::vector<Source> sources;
		for (const std::string &path : files)
		{
			if (!FollowsNamingRule(path))
			{
				Refuse(
				    path, std::format(
				              "{}: its name breaks the content-file rule (kebab-case: lower-case words "
				              "and digits joined by hyphens)",
				              path));
				continue;
			}
			if (IsSettingsFile(path))
				continue;
			if (path == kRecordName)
			{
				Refuse(path, std::format("{}: it has the cook record's name", path));
				continue;
			}
			Source source{path, Kind::Copy, {path}};
			if (HasExtension(path, kModelExtensions))
				source = {
				    path,
				    Kind::Model,
				    {WithExtension(path, kModelOutputExtension), CollisionPath(path)}};
			else if (HasExtension(path, kImageExtensions))
				source = {path, Kind::Image, {WithExtension(path, kTextureOutputExtension)}};
			sources.push_back(std::move(source));
		}
		return sources;
	}


	// The sources, less those refused for writing an output that another of them or one of the models
	// writes. The models have been processed already, so they keep their outputs.
	std::vector<const Source *> WithoutClashes(
	    std::vector<const Source *> sources, std::span<const Source *const> models)
	{
		std::map<std::string, std::vector<const Source *>> writers;
		const auto add = [&](std::span<const Source *const> list)
		{
			for (const Source *pSource : list)
				for (const std::string &output : pSource->outputs)
					writers[output].push_back(pSource);
		};
		add(sources);
		add(models);
		std::set<const Source *> clashing;
		for (const auto &[output, writing] : writers)
			if (writing.size() > 1)
				for (const Source *pSource : writing)
					if (std::ranges::find(models, pSource) == models.end() &&
					    clashing.insert(pSource).second)
						Refuse(
						    pSource->path, std::format(
						                       "{}: it and another source would both write {}",
						                       pSource->path, output));
		std::erase_if(sources, [&](const Source *pSource) { return clashing.contains(pSource); });
		return sources;
	}


	bool IsSettingsFile(const std::string &path) const
	{
		return path == kGameSettingsName ||
		       (path.ends_with(kSettingsExtension) &&
		        m_present.contains(path.substr(0, path.size() - kSettingsExtension.size())));
	}


	std::vector<SettingsLayer> LayersFor(const std::string &path) const
	{
		std::vector<SettingsLayer> layers{m_defaults};
		if (m_gameSettings)
			layers.push_back(*m_gameSettings);
		const std::string own = path + std::string{kSettingsExtension};
		if (m_present.contains(own))
			layers.push_back({own, AsText(ReadFile(m_source / own))});
		return layers;
	}


	void Refuse(const std::string &path, const std::string &message)
	{
		Core::Log(Core::Severity::Error, kCook, "{}", message);
		m_report.refusals.push_back(message);
		if (const auto found = m_previous.sources.find(path); found != m_previous.sources.end())
			m_next.sources.insert(*found);
	}


	bool Unchanged(const std::string &path, const std::string &key) const
	{
		const auto found = m_previous.sources.find(path);
		return found != m_previous.sources.end() && found->second.key == key &&
		       std::ranges::all_of(
		           found->second.outputs, [&](const std::string &output)
		           { return StaysWithin(output) && fs::exists(m_output / output); });
	}


	// Each source processed on the cook's threads, as many as the machine runs at once, its outcome
	// taken in the order given.
	std::vector<Outcome> ProcessAll(std::span<const Source *const> sources)
	{
		std::vector<Outcome> outcomes(sources.size());
		std::atomic<std::size_t> next{0};
		const auto work = [&]
		{
			for (std::size_t index = next++; index < sources.size(); index = next++)
				outcomes[index] = Process(*sources[index]);
		};
		{
			const std::size_t threads = std::min<std::size_t>(
			    std::max(1u, std::thread::hardware_concurrency()), sources.size());
			std::vector<std::jthread> pool;
			for (std::size_t thread = 1; thread < threads; ++thread)
				pool.emplace_back(work);
			work();
		}
		for (std::size_t index = 0; index < sources.size(); ++index)
			Take(*sources[index], outcomes[index]);
		return outcomes;
	}


	void Take(const Source &source, const Outcome &outcome)
	{
		switch (outcome.result)
		{
			case Result::Cooked:
				Core::Log(Core::Severity::Info, kCook, "cooked {}", source.path);
				m_report.cooked.push_back(source.path);
				m_next.sources[source.path] = outcome.entry;
				break;
			case Result::Skipped:
				m_report.skipped.push_back(source.path);
				m_next.sources[source.path] = outcome.entry;
				break;
			case Result::Refused: Refuse(source.path, outcome.refusal); break;
		}
	}


	// Reads only what no thread writes: the record read, the trees and the settings found.
	Outcome Process(const Source &source) const
	{
		Outcome outcome;
		std::vector<std::string> &references = outcome.references;
		try
		{
			const Bytes bytes = ReadFile(m_source / source.path);
			const std::vector<SettingsLayer> layers =
			    source.kind == Kind::Copy ? std::vector<SettingsLayer>{} : LayersFor(source.path);

			// While its bytes are the same a model references the same files, so the record's list
			// stands for them, and an unchanged model is not parsed.
			if (const auto found = m_previous.sources.find(source.path);
			    found != m_previous.sources.end())
				references = found->second.references;
			std::string key = CookKey(bytes, m_source, references, layers);
			if (!Unchanged(source.path, key) && source.kind == Kind::Model)
			{
				references = References(bytes, m_source, source.path);
				key = CookKey(bytes, m_source, references, layers);
			}
			if (Unchanged(source.path, key))
			{
				outcome.result = Result::Skipped;
				outcome.entry = m_previous.sources.at(source.path);
				return outcome;
			}

			// A link in the output could lead anywhere, so nothing is written through one.
			for (const std::string &output : source.outputs)
				if (const auto link = LinkWithin(m_output, output))
					throw std::runtime_error(
					    std::format(
					        "{}: its output {} would be written through {}, a symbolic link in the "
					        "output",
					        source.path, output, *link));

			switch (source.kind)
			{
				case Kind::Model: CookModel(source, bytes, layers, key); break;
				case Kind::Image:
					Core::WriteFileDurably(
					    m_output / source.outputs.front(),
					    EncodeTexture(
					        bytes, source.path, LooseColourSpace(source.path),
					        ReadSettings(layers).textures));
					break;
				case Kind::Copy:
					Core::WriteFileDurably(m_output / source.outputs.front(), bytes);
					break;
			}
			outcome.result = Result::Cooked;
			outcome.entry = RecordEntry{key, source.outputs, references};
			return outcome;
		}
		catch (const std::exception &error)
		{
			outcome.result = Result::Refused;
			outcome.refusal = error.what();
			return outcome;
		}
	}


	// The model's collision file is written before its GLB, which names the cook key the pair shares.
	void CookModel(
	    const Source &source, const Bytes &bytes, std::span<const SettingsLayer> layers,
	    const std::string &key) const
	{
		const Settings settings = ReadSettings(layers);
		const auto pFile =
		    Geometry::GltfFile::Parse(bytes, source.path, (m_source / source.path).parent_path());
		const fastgltf::Asset &asset = pFile->Asset();
		const std::vector<ColourSpace> spaces = ImageColourSpaces(asset, source.path);
		std::vector<Bytes> images;
		images.reserve(asset.images.size());
		for (std::size_t index = 0; index < asset.images.size(); ++index)
			images.push_back(EncodeTexture(
			    ImageBytes(asset, asset.images[index], m_source, source.path),
			    std::format("{} image {}", source.path, index), spaces[index], settings.textures));
		const Bytes model = Geometry::CookModel(*pFile, settings.lod, key, images);

		const std::string &collision = source.outputs[1];
		Core::WriteFileDurably(
		    m_output / collision,
		    Physics::WriteCollisionFile(Bake::CollisionShape(m_source / source.path), collision));
		Core::WriteFileDurably(m_output / source.outputs[0], model);
	}


	// Only outputs the last record names, and only within the output. One below a symbolic link there
	// is not deleted but reported, its record entry kept so the next run tries again.
	void DeleteLostOutputs()
	{
		std::set<std::string> kept;
		for (const auto &[path, entry] : m_next.sources)
			kept.insert(entry.outputs.begin(), entry.outputs.end());
		for (const auto &[path, entry] : m_previous.sources)
			for (const std::string &output : entry.outputs)
			{
				if (kept.contains(output) || !StaysWithin(output))
					continue;
				if (const auto link = LinkWithin(m_output, fs::path{output}.parent_path()))
				{
					const std::string message = std::format(
					    "{}: not deleted, since {} in the output is a symbolic link", output, *link);
					Core::Log(Core::Severity::Error, kCook, "{}", message);
					m_report.refusals.push_back(message);
					m_next.sources.try_emplace(path, entry);
					continue;
				}
				if (fs::remove(m_output / output))
				{
					Core::Log(
					    Core::Severity::Info, kCook, "deleted {}, which {} no longer makes", output,
					    path);
					m_report.deleted.push_back(output);
				}
			}
	}

	fs::path m_source;
	fs::path m_output;
	Record m_previous;
	Record m_next;
	std::set<std::string> m_present;
	std::optional<SettingsLayer> m_gameSettings;
	SettingsLayer m_defaults;
	CookReport m_report;
};
} // namespace


void CheckTrees(const std::filesystem::path &source, const std::filesystem::path &output)
{
	// Without a trailing separator, which leaves an empty last part that no path is within.
	const auto canonical = [](const fs::path &path)
	{
		fs::path resolved = fs::weakly_canonical(path);
		return resolved.has_filename() ? resolved : resolved.parent_path();
	};
	const auto within = [](const fs::path &path, const fs::path &directory)
	{
		const fs::path relative = path.lexically_relative(directory);
		return !relative.empty() && *relative.begin() != "..";
	};
	const fs::path from = canonical(source);
	const fs::path to = canonical(output);
	if (within(to, from) || within(from, to))
		throw std::runtime_error(
		    std::format(
		        "The cook's output {} and source {} overlap: the output is the source, lies inside it "
		        "or holds it",
		        to.string(), from.string()));
}


CookReport CookTree(const std::filesystem::path &source, const std::filesystem::path &output)
{
	return TreeCook{source, output}.Run();
}
} // namespace Jettison::Cook
