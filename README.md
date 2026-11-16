# Jettison Engine

A C++20 3D game engine intended for multiple game types. Manifest is the first production
consumer and owns its game rules and content. Jettison provides reusable capabilities,
with optional space services and a small space-trading example for automated tests.
Separating that example from the current engine app is planned work; see the
[game integration contract](docs/game-integration.md) and [roadmap](docs/roadmap.md).

The engine is assembled primarily from open source libraries, each selected for fitness for
purpose. The criteria are:

- elegance
- efficiency
- modern C++ usage
- interoperability
- maintenance

All attempts will be made to observe best practice.

## Cloning

```
git clone git@github.com:ivanhawkes/jettison.git
```

There are no submodules. Dependencies are pinned in `source/external/CMakeLists.txt` and fetched
by CMake on the first configure. Large media is in Git LFS, so run `git lfs install` before the
first checkout. The `devenv` shell does this for you.

## Building

The provided `devenv.nix` supplies the toolchain: Clang, CMake 3.28+, Ninja, the Vulkan SDK
pieces, `glslc` and the Wayland libraries.

```bash
cmake --preset clang-debug
cmake --build --preset clang-debug

# Run from the root folder.
build/clang-debug/source/test/render_test

# The full gate: format, builds, tidy, headless render test on software Vulkan.
./scripts/ci.sh --llvmpipe --werror
```

## Using the engine

The game fetches the engine with CMake's `FetchContent` and links `Jettison::Engine`.
`source/test/consumer` is the smallest such project; [build and
gates](docs/build-and-gates.md#consuming-the-engine) shows how to run it.

## Documentation

Everything else is in [`docs/`](docs/README.md):

- [Guide](docs/guide.md): how to work with the engine, step by step, starting with releasing a mod
- [Scope](docs/scope.md): what is in and out, and why
- [Architecture](docs/architecture.md): layout, module dependencies, the simulation split,
  coordinate frames
- [Roadmap](docs/roadmap.md): the phases and a brief for every system
- [Coding standards](docs/coding-standards.md)
- [Build and gates](docs/build-and-gates.md)
- [History](docs/history.md)
