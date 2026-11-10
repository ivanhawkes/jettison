#include <jettison/ui/UiSystem.h>

#include <jettison/core/Assert.h>

#include <doctest/doctest.h>

TEST_CASE("Ui: no documents are open; loading one fails loudly until phase 8")
{
	Jettison::Ui::UiSystem ui;
	CHECK(ui.OpenDocuments() == 0);
	CHECK_THROWS_AS(ui.LoadDocument("ui/market.rml"), Jettison::Core::NotImplementedError);
}
