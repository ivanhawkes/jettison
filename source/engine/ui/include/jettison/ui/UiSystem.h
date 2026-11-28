#pragma once

#include <glm/vec2.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include <jettison/input/InputCode.h>
#include <jettison/ui/DataModel.h>
#include <jettison/ui/StringTable.h>

namespace Jettison::Render
{
class Overlay;
}
namespace Jettison::Resource
{
class ResourceManager;
}
namespace Jettison::Vfs
{
class FileSystem;
}

namespace Jettison::Ui
{
// A document the UI has loaded; generational, so a handle to one closed names nothing.
struct DocumentHandle
{
	std::uint32_t index{0};
	std::uint32_t generation{0};

	friend bool operator==(const DocumentHandle &, const DocumentHandle &) = default;
};

// The keys held with an input.
struct Modifiers
{
	bool shift{false};
	bool control{false};
	bool alt{false};
};

namespace Detail
{
class UiState;
}

// The game UI (docs/ui.md): RmlUi behind an engine interface, so nothing above it
// includes RmlUi. Documents, stylesheets, templates, fonts and images are read through the VFS, so a
// mod's mount over the base restyles a screen by shadowing its stylesheet. What it draws goes into a
// Render::Overlay, which the renderer draws over the tonemapped scene and under the dev tools; the
// dev tools and the console stay on ImGui. Documents read the game's data through data models
// (DataModel.h), bound to presentation's copies of the simulation's snapshots.
//
// RmlUi is one library-wide instance, so there is one UiSystem at a time in a process; a second
// throws std::logic_error. Main thread only. Its messages go to the log under the "ui" category, and
// the warnings and errors among them are counted (Problems), which the smoke test holds at none.
class UiSystem
{
  public:
	// A UI over a frame of that size in pixels. The file system outlives it, and so does the resource
	// manager, which reads and decodes a document's images on its I/O worker: until one is there its
	// element draws without it and is laid out as if the image had no size, so an element whose style
	// sets its size does not move when it arrives. The manager is given the loader of them, so it
	// serves one UI; with none, a test's, they are decoded on the main thread as the document lays
	// out. Throws std::logic_error for a manager that has served a UI already.
	UiSystem(
	    const Vfs::FileSystem &files, int width, int height,
	    Resource::ResourceManager *pResources = nullptr);

	~UiSystem();

	UiSystem(const UiSystem &) = delete;

	UiSystem &operator=(const UiSystem &) = delete;

	// A TrueType or OpenType face by virtual path, under the family name the file gives; a fallback
	// face is searched, in the order loaded, for a glyph no other face has. Throws std::runtime_error
	// naming the file for one that is missing or that FreeType cannot read. Once a face is loaded, a
	// character in a document's text that no face has is logged, once, and counted a problem.
	void LoadFont(std::string_view virtualPath, bool fallback = false);

	// Every TrueType and OpenType file under the directory whose topmost file is in the mount of that
	// index or one above it, as a fallback face, in path order: the faces a game or a mod supplies for
	// the scripts the engine's fonts lack, loaded after the engine's. Returns how many; throws as
	// LoadFont does.
	std::size_t LoadFallbackFonts(std::string_view virtualDirectory, std::size_t firstMount);

	// A data model, which every document that names it (data-model="market") reads. Before the
	// documents that read it are loaded; throws std::logic_error for a name already made.
	DataModel &CreateDataModel(std::string_view name);

	// The language the documents are read in and the game's code formats its text in (StringTable),
	// its tables read through the file system. Documents already open are read again, each keeping
	// its handle and whether it is shown, since RmlUi translates a document's text as it reads it.
	// Throws as StringTable::Load does, and the language before it stays. Until it is called the
	// table is empty, and every [[key]] reads as its key.
	void SetLanguage(std::string_view language);

	const StringTable &Strings() const;

	// Loads a document, hidden. Throws std::runtime_error naming it for one that is missing or that
	// RmlUi cannot parse.
	DocumentHandle LoadDocument(std::string_view virtualPath);

	// Do nothing for a handle to a document that is closed.
	void Show(DocumentHandle document);

	void Hide(DocumentHandle document);

	void Close(DocumentHandle document);

	// Reads every open document again, each keeping its handle and whether it is shown, when a file
	// RmlUi has read for one has changed since (its document, a stylesheet or its patch, a template),
	// or another mount's file now shadows it: for a UI author iterating, in development, as the
	// resource manager reloads a file. The data models stay as they are, so a document read again
	// reads the values and raises the events it did before. A document that no longer loads stays as
	// it was, logged, and is tried again once a file changes again. Returns how many were read again.
	std::size_t ReloadChanged();

	// RmlUi's debugger over the documents: its menu, an element's styles, the event log and the data
	// models, drawn into the overlay with them. Made the first time it is shown, so a run that never
	// shows it draws and reads nothing of it.
	void ShowDebugger(bool shown);

	bool IsShown(DocumentHandle document) const;

	std::size_t OpenDocuments() const;

	// The frame's size in pixels, and how many pixels a density-independent pixel (dp) is.
	void SetSize(int width, int height, float pixelsPerDp = 1.0f);

	// Input, in the frame's pixels from its top left. Each says whether the UI took it: the mouse
	// over a shown document's element, a key into the element with focus. A key is GLFW's, as
	// Input::InputCode has it; a button 0 for the left, 1 the right, 2 the middle.
	bool MouseMove(const glm::vec2 &position, Modifiers modifiers);

	bool MouseButton(int button, bool pressed, Modifiers modifiers);

	bool MouseWheel(float steps, Modifiers modifiers);

	void MouseLeave();

	bool Key(Input::InputCode key, bool pressed, Modifiers modifiers);

	bool Text(char32_t character);

	// Whether the mouse is over a shown document's element, so a click there is not a click on the
	// scene behind it.
	bool WantsMouse() const;

	// Whether an element that takes text has focus, so keys typed are not the game's bindings.
	bool WantsKeyboard() const;

	// Once a frame, in the presentation phase: the data models' changes, layout, and the documents
	// drawn into the overlay, which is cleared first. The time drives transitions and animations.
	void Update(double presentationSeconds);

	const Render::Overlay &Overlay() const;

	// An element's text as the document shows it now, by its id: what a test and the smoke sequence
	// read. Empty for an element the document does not have.
	std::string ElementText(DocumentHandle document, std::string_view id) const;

	// An element's attribute as the document holds it now, a [[key]] in it translated: empty for an
	// element or an attribute the document does not have.
	std::string ElementAttribute(
	    DocumentHandle document, std::string_view id, std::string_view name) const;

	// A click on the element, as the mouse would make one, and false for an element the document does
	// not have: the smoke sequence presses a screen's buttons with it.
	bool Click(DocumentHandle document, std::string_view id);

	// The warnings and errors RmlUi has logged, the strings asked for that no table has, the
	// translations refused, and the characters a text has that no face loaded has.
	std::size_t Problems() const;

  private:
	std::unique_ptr<Detail::UiState> m_pState;
};
} // namespace Jettison::Ui
