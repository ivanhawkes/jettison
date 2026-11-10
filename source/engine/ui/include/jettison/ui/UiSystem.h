#pragma once

#include <cstdint>
#include <string>

namespace Jettison::Ui
{
// RmlUi behind an engine interface. Documents and stylesheets load through the VFS so mods can
// restyle; data models bind to simulation snapshots, never to simulation state. The Vulkan render
// interface for it lives in render, and the console and dev tools stay on ImGui (render/vulkan/
// imgui today). Filled in phase 8.
struct DocumentHandle
{
	std::uint32_t index{0};
	std::uint32_t generation{0};
};

class UiSystem
{
  public:
	DocumentHandle LoadDocument(const std::string &virtualPath);
	void Show(DocumentHandle document);
	void Hide(DocumentHandle document);
	void Close(DocumentHandle document);

	// Once per frame: input, layout, then the render module draws what Render() returned.
	void Update(double deltaTime);
	std::size_t OpenDocuments() const
	{
		return m_openDocuments;
	}

  private:
	std::size_t m_openDocuments{0};
};
} // namespace Jettison::Ui
