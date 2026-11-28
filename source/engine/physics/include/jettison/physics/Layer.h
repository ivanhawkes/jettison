#pragma once

#include <string>
#include <vector>

namespace Jettison::Physics
{
// A collision layer of the game's, which a body is put in (Body::layer) and a cast or an overlap is
// cast as (CastOptions::layer): a definition the game loads with the definition registry, from a
// directory of its choosing, and defines in the physics world with all its other layers at once
// (PhysicsWorld::DefineLayers), or defines in code. Registered with Reflect as "Physics.Layer", its
// field Saved and Moddable, by RegisterTypes.
//
// Two layers meet if either names the other, so a mod's layer can meet the base game's without
// editing theirs, and a layer meets itself only if it names itself. A body in no layer meets every
// body, so a game's layers narrow only what its own layered bodies meet: a body an engine module makes
// in none (a dock's platform) is met by every layer.
struct LayerDefinition
{
	// The layers it meets, by name, each one defined.
	std::vector<std::string> meets;
};
} // namespace Jettison::Physics
