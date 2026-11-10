#include <jettison/audio/AudioSystem.h>

#include <jettison/core/Assert.h>

namespace Jettison::Audio
{
void AudioSystem::SetBusVolume(Bus /*unused*/, float /*unused*/)
{
	JETTISON_NOT_IMPLEMENTED();
}
float AudioSystem::BusVolume(Bus /*unused*/) const
{
	JETTISON_NOT_IMPLEMENTED();
}
SourceHandle AudioSystem::Play(
    const std::string & /*unused*/, Bus /*unused*/, const glm::vec3 & /*unused*/, bool /*unused*/)
{
	JETTISON_NOT_IMPLEMENTED();
}
void AudioSystem::Stop(SourceHandle /*unused*/)
{
	JETTISON_NOT_IMPLEMENTED();
}
void AudioSystem::SetListener(
    const glm::vec3 & /*unused*/, const glm::vec3 & /*unused*/, const glm::vec3 & /*unused*/)
{
	JETTISON_NOT_IMPLEMENTED();
}
void AudioSystem::Update()
{
	JETTISON_NOT_IMPLEMENTED();
}
} // namespace Jettison::Audio
