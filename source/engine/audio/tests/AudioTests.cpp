#include <jettison/audio/AudioSystem.h>

#include <jettison/core/Assert.h>

#include <doctest/doctest.h>

TEST_CASE("Audio: the system fails loudly until phase 8")
{
	Jettison::Audio::AudioSystem audio;
	CHECK_THROWS_AS(audio.Update(), Jettison::Core::NotImplementedError);
	CHECK_THROWS_AS(audio.BusVolume(Jettison::Audio::Bus::Music), Jettison::Core::NotImplementedError);
}
