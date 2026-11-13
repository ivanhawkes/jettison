#include <jettison/ui/UiSystem.h>

#include <RmlUi/Core.h>

#include <format>
#include <stdexcept>
#include <string>
#include <utility>

#include <jettison/core/Log.h>
#include <jettison/render/Overlay.h>

#include "UiState.h"

namespace Jettison::Ui
{
namespace
{
constexpr Core::Category kUi{"ui"};

// RmlUi is one instance a process.
bool g_live = false;

int KeyModifiers(Modifiers modifiers)
{
	return (modifiers.shift ? Rml::Input::KM_SHIFT : 0) |
	       (modifiers.control ? Rml::Input::KM_CTRL : 0) | (modifiers.alt ? Rml::Input::KM_ALT : 0);
}

// GLFW's key numbers, as Input::InputCode has them (the shell checks a sample against GLFW's own
// header), to RmlUi's: the keys a document's text fields and navigation read.
Rml::Input::KeyIdentifier ToRmlKey(std::int32_t code)
{
	using namespace Rml::Input;
	if (code >= 'A' && code <= 'Z')
		return static_cast<KeyIdentifier>(KI_A + (code - 'A'));
	if (code >= '0' && code <= '9')
		return static_cast<KeyIdentifier>(KI_0 + (code - '0'));
	if (code >= 290 && code <= 301)
		return static_cast<KeyIdentifier>(KI_F1 + (code - 290));
	switch (code)
	{
		case 32: return KI_SPACE;
		case 256: return KI_ESCAPE;
		case 257: return KI_RETURN;
		case 258: return KI_TAB;
		case 259: return KI_BACK;
		case 260: return KI_INSERT;
		case 261: return KI_DELETE;
		case 262: return KI_RIGHT;
		case 263: return KI_LEFT;
		case 264: return KI_DOWN;
		case 265: return KI_UP;
		case 266: return KI_PRIOR;
		case 267: return KI_NEXT;
		case 268: return KI_HOME;
		case 269: return KI_END;
		case 335: return KI_NUMPADENTER;
		case 340: return KI_LSHIFT;
		case 341: return KI_LCONTROL;
		case 342: return KI_LMENU;
		case 344: return KI_RSHIFT;
		case 345: return KI_RCONTROL;
		case 346: return KI_RMENU;
		default: return KI_UNKNOWN;
	}
}
} // namespace

Rml::ElementDocument *Detail::UiState::Find(DocumentHandle document) const
{
	if (document.index >= documents.size())
		return nullptr;
	const DocumentSlot &slot = documents[document.index];
	return slot.generation == document.generation ? slot.pDocument : nullptr;
}


UiSystem::UiSystem(const Vfs::FileSystem &files, int width, int height)
{
	if (g_live)
		throw std::logic_error("UiSystem: there is one UI at a time, and one is already made");
	m_pState = std::make_unique<Detail::UiState>(files);
	Rml::SetSystemInterface(&m_pState->system);
	Rml::SetFileInterface(&m_pState->fileInterface);
	Rml::SetRenderInterface(&m_pState->render);
	if (!Rml::Initialise())
		throw std::runtime_error("UiSystem: RmlUi did not initialise");
	g_live = true;
	m_pState->pContext = Rml::CreateContext("main", {width, height});
	if (!m_pState->pContext)
	{
		Rml::Shutdown();
		g_live = false;
		throw std::runtime_error("UiSystem: RmlUi did not make its context");
	}
}

UiSystem::~UiSystem()
{
	// The documents and the data models go with the context, before the interfaces and the
	// definitions the models read through.
	Rml::Shutdown();
	g_live = false;
}

void UiSystem::LoadFont(std::string_view virtualPath, bool fallback)
{
	if (!Rml::LoadFontFace(std::string{virtualPath}, fallback))
		throw std::runtime_error(std::format("UiSystem: the font '{}' did not load", virtualPath));
}

DataModel &UiSystem::CreateDataModel(std::string_view name)
{
	const std::string key{name};
	if (m_pState->models.contains(key))
		throw std::logic_error(std::format("UiSystem: the data model '{}' is made already", name));
	Rml::DataModelConstructor constructor = m_pState->pContext->CreateDataModel(key);
	if (!constructor)
		throw std::logic_error(std::format("UiSystem: RmlUi refused the data model '{}'", name));
	if (m_pState->models.empty())
		Detail::RegisterTransforms(constructor, m_pState->system.Strings());
	auto pModel =
	    std::make_unique<DataModel>(std::make_unique<Detail::DataModelState>(key, constructor));
	DataModel &model = *pModel;
	m_pState->models.emplace(key, std::move(pModel));
	return model;
}

void UiSystem::SetLanguage(std::string_view language)
{
	StringTable strings;
	strings.Load(m_pState->files, language);
	m_pState->system.Strings() = std::move(strings);
	for (Detail::DocumentSlot &slot : m_pState->documents)
	{
		if (slot.pDocument == nullptr)
			continue;
		const Rml::String source = slot.pDocument->GetSourceURL();
		const bool shown = slot.pDocument->IsVisible();
		Rml::ElementDocument *pDocument = m_pState->pContext->LoadDocument(source);
		if (!pDocument)
			throw std::runtime_error(
			    std::format("UiSystem: the document '{}' did not load again", source));
		slot.pDocument->Close();
		slot.pDocument = pDocument;
		if (shown)
			pDocument->Show();
	}
}

const StringTable &UiSystem::Strings() const
{
	return m_pState->system.Strings();
}

DocumentHandle UiSystem::LoadDocument(std::string_view virtualPath)
{
	const std::size_t problems = m_pState->system.Problems();
	Rml::ElementDocument *pDocument = m_pState->pContext->LoadDocument(std::string{virtualPath});
	if (!pDocument)
		throw std::runtime_error(std::format("UiSystem: the document '{}' did not load", virtualPath));
	if (m_pState->system.Problems() != problems)
		Core::Log(
		    Core::Severity::Warning, kUi, "'{}' loaded with {} problems, which the log has",
		    virtualPath, m_pState->system.Problems() - problems);
	for (std::uint32_t index = 0; index < m_pState->documents.size(); ++index)
	{
		Detail::DocumentSlot &slot = m_pState->documents[index];
		if (slot.pDocument == nullptr)
		{
			slot.pDocument = pDocument;
			return {index, slot.generation};
		}
	}
	m_pState->documents.push_back({pDocument, 0});
	return {static_cast<std::uint32_t>(m_pState->documents.size() - 1), 0};
}

void UiSystem::Show(DocumentHandle document)
{
	if (Rml::ElementDocument *pDocument = m_pState->Find(document))
		pDocument->Show();
}

void UiSystem::Hide(DocumentHandle document)
{
	if (Rml::ElementDocument *pDocument = m_pState->Find(document))
		pDocument->Hide();
}

void UiSystem::Close(DocumentHandle document)
{
	Rml::ElementDocument *pDocument = m_pState->Find(document);
	if (!pDocument)
		return;
	pDocument->Close();
	Detail::DocumentSlot &slot = m_pState->documents[document.index];
	slot.pDocument = nullptr;
	++slot.generation;
}

bool UiSystem::IsShown(DocumentHandle document) const
{
	const Rml::ElementDocument *pDocument = m_pState->Find(document);
	return pDocument != nullptr && pDocument->IsVisible();
}

std::size_t UiSystem::OpenDocuments() const
{
	std::size_t open = 0;
	for (const Detail::DocumentSlot &slot : m_pState->documents)
		open += slot.pDocument != nullptr ? 1 : 0;
	return open;
}

void UiSystem::SetSize(int width, int height, float pixelsPerDp)
{
	m_pState->pContext->SetDimensions({width, height});
	m_pState->pContext->SetDensityIndependentPixelRatio(pixelsPerDp);
}

bool UiSystem::MouseMove(const glm::vec2 &position, Modifiers modifiers)
{
	return !m_pState->pContext->ProcessMouseMove(
	    static_cast<int>(position.x), static_cast<int>(position.y), KeyModifiers(modifiers));
}

bool UiSystem::MouseButton(int button, bool pressed, Modifiers modifiers)
{
	return pressed ? !m_pState->pContext->ProcessMouseButtonDown(button, KeyModifiers(modifiers))
	               : !m_pState->pContext->ProcessMouseButtonUp(button, KeyModifiers(modifiers));
}

bool UiSystem::MouseWheel(float steps, Modifiers modifiers)
{
	// RmlUi scrolls down for a positive delta, and GLFW's wheel is positive away from the user.
	return !m_pState->pContext->ProcessMouseWheel(-steps, KeyModifiers(modifiers));
}

void UiSystem::MouseLeave()
{
	m_pState->pContext->ProcessMouseLeave();
}

bool UiSystem::Key(Input::InputCode key, bool pressed, Modifiers modifiers)
{
	if (key.device != Input::Device::Keyboard)
		return false;
	const Rml::Input::KeyIdentifier identifier = ToRmlKey(key.code);
	if (identifier == Rml::Input::KI_UNKNOWN)
		return false;
	// RmlUi answers whether the key went on to the context's default handling, so a key that a
	// focused element consumed reads false; what the game asks is whether the UI took it.
	const bool propagated =
	    pressed ? m_pState->pContext->ProcessKeyDown(identifier, KeyModifiers(modifiers))
	            : m_pState->pContext->ProcessKeyUp(identifier, KeyModifiers(modifiers));
	return !propagated || WantsKeyboard();
}

bool UiSystem::Text(char32_t character)
{
	if (!WantsKeyboard())
		return false;
	m_pState->pContext->ProcessTextInput(static_cast<Rml::Character>(character));
	return true;
}

bool UiSystem::WantsMouse() const
{
	const Rml::Element *pHover = m_pState->pContext->GetHoverElement();
	// The context's root is hovered when no document is under the mouse.
	return pHover != nullptr && pHover != m_pState->pContext->GetRootElement();
}

bool UiSystem::WantsKeyboard() const
{
	const Rml::Element *pFocus = m_pState->pContext->GetFocusElement();
	if (pFocus == nullptr)
		return false;
	const Rml::String &tag = pFocus->GetTagName();
	return tag == "input" || tag == "textarea" || tag == "select";
}

void UiSystem::Update(double presentationSeconds)
{
	m_pState->system.SetTime(presentationSeconds);
	m_pState->pContext->Update();
	m_pState->overlay.Clear();
	m_pState->pContext->Render();
}

const Render::Overlay &UiSystem::Overlay() const
{
	return m_pState->overlay;
}

std::string UiSystem::ElementText(DocumentHandle document, std::string_view id) const
{
	Rml::ElementDocument *pDocument = m_pState->Find(document);
	Rml::Element *pElement = pDocument ? pDocument->GetElementById(std::string{id}) : nullptr;
	return pElement ? pElement->GetInnerRML() : std::string{};
}

bool UiSystem::Click(DocumentHandle document, std::string_view id)
{
	Rml::ElementDocument *pDocument = m_pState->Find(document);
	Rml::Element *pElement = pDocument ? pDocument->GetElementById(std::string{id}) : nullptr;
	if (!pElement)
		return false;
	pElement->Click();
	return true;
}

std::size_t UiSystem::Problems() const
{
	return m_pState->system.Problems();
}
} // namespace Jettison::Ui
