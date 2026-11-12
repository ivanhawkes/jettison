#include <jettison/render/Overlay.h>

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <stdexcept>
#include <vector>

using Jettison::Render::kNoOverlayTexture;
using Jettison::Render::Overlay;
using Jettison::Render::OverlayRect;
using Jettison::Render::OverlayTexture;
using Jettison::Render::OverlayVertex;

namespace
{
const std::array<OverlayVertex, 4> kQuad{{
    {{0.0f, 0.0f}},
    {{10.0f, 0.0f}},
    {{10.0f, 10.0f}},
    {{0.0f, 10.0f}},
}};
const std::array<std::uint32_t, 6> kQuadIndices{0, 1, 2, 0, 2, 3};

OverlayTexture Texels(std::uint32_t width, std::uint32_t height)
{
	return {width, height, std::vector<std::byte>(std::size_t{width} * height * 4, std::byte{255})};
}
} // namespace

TEST_CASE("Overlay: each draw runs over its own indices, counted from its own vertices")
{
	Overlay overlay;
	overlay.Draw(kQuad, kQuadIndices, {1.0f, 2.0f}, kNoOverlayTexture);
	overlay.Draw(kQuad, kQuadIndices, {3.0f, 4.0f}, kNoOverlayTexture);

	REQUIRE(overlay.Draws().size() == 2);
	CHECK(overlay.Vertices().size() == 8);
	CHECK(overlay.Indices().size() == 12);
	CHECK(overlay.Draws()[1].firstIndex == 6);
	CHECK(overlay.Draws()[1].indexCount == 6);
	CHECK(overlay.Draws()[1].vertexOffset == 4);
	CHECK(overlay.Draws()[1].translation.x == 3.0f);
	// The indices are kept as given: the draw's vertex offset is what moves them.
	CHECK(overlay.Indices()[6] == 0);

	overlay.Clear();
	CHECK(overlay.Draws().empty());
	CHECK(overlay.Vertices().empty());
	CHECK(overlay.Indices().empty());
}

TEST_CASE("Overlay: a draw keeps the scissor and the transform in force when it was made")
{
	Overlay overlay;
	overlay.SetScissor(OverlayRect{5, 6, 7, 8});
	overlay.SetTransform(glm::mat4{2.0f});
	overlay.Draw(kQuad, kQuadIndices, {}, kNoOverlayTexture);
	overlay.SetScissor(std::nullopt);
	overlay.SetTransform(std::nullopt);
	overlay.Draw(kQuad, kQuadIndices, {}, kNoOverlayTexture);

	REQUIRE(overlay.Draws().size() == 2);
	CHECK(overlay.Draws()[0].scissor == OverlayRect{5, 6, 7, 8});
	REQUIRE(overlay.Draws()[0].transform.has_value());
	CHECK(overlay.Transforms()[*overlay.Draws()[0].transform][0][0] == 2.0f);
	CHECK_FALSE(overlay.Draws()[1].scissor.has_value());
	CHECK_FALSE(overlay.Draws()[1].transform.has_value());

	// Clearing goes back to the whole frame, unmoved.
	overlay.SetScissor(OverlayRect{1, 1, 1, 1});
	overlay.Clear();
	overlay.Draw(kQuad, kQuadIndices, {}, kNoOverlayTexture);
	CHECK_FALSE(overlay.Draws()[0].scissor.has_value());
}

TEST_CASE("Overlay: a draw that reads past its vertices, or is not whole triangles, is refused")
{
	Overlay overlay;
	const std::array<std::uint32_t, 3> past{0, 1, 4};
	CHECK_THROWS_AS(overlay.Draw(kQuad, past, {}, kNoOverlayTexture), std::invalid_argument);
	const std::array<std::uint32_t, 2> partial{0, 1};
	CHECK_THROWS_AS(overlay.Draw(kQuad, partial, {}, kNoOverlayTexture), std::invalid_argument);
	CHECK(overlay.Draws().empty());
	CHECK(overlay.Vertices().empty());
}

TEST_CASE("Overlay: textures outlive the frame's draws, and an identifier is never given twice")
{
	Overlay overlay;
	const auto first = overlay.AddTexture(Texels(2, 2));
	const auto second = overlay.AddTexture(Texels(1, 1));
	CHECK(first != kNoOverlayTexture);
	CHECK(first != second);

	overlay.Clear();
	REQUIRE(overlay.FindTexture(first) != nullptr);
	CHECK(overlay.FindTexture(first)->width == 2);

	overlay.RemoveTexture(first);
	CHECK(overlay.FindTexture(first) == nullptr);
	const auto third = overlay.AddTexture(Texels(1, 1));
	CHECK(third != first);
	CHECK(third != second);
	CHECK(overlay.Textures().size() == 2);

	// The texels have to be the size the texture says.
	CHECK_THROWS_AS(overlay.AddTexture({2, 2, std::vector<std::byte>(15)}), std::invalid_argument);
	CHECK_THROWS_AS(overlay.AddTexture({0, 0, {}}), std::invalid_argument);
}
