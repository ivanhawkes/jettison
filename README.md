# Jettison Engine

A 3D game engine, built for a space trading simulation: trucking logistics, trading and empire
building across star systems, with stations orbiting planets. The game lives in its own
repository and consumes this engine as a CMake package.

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
build/clang-debug/source/test/test

# The full gate: format, builds, tidy, headless render test on software Vulkan.
./scripts/ci.sh --llvmpipe --werror
```

## Documentation

Everything else is in [`docs/`](docs/README.md):

- [Scope](docs/scope.md): what is in and out, and why
- [Architecture](docs/architecture.md): layout, module dependencies, the simulation split,
  coordinate frames
- [Roadmap](docs/roadmap.md): the phases and a brief for every system
- [Coding standards](docs/coding-standards.md)
- [Build and gates](docs/build-and-gates.md)
- [History](docs/history.md)
