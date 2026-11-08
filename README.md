# Jettison Engine

A toy 2D / 3D game engine project (very experimental).

I'm looking into the possibility of making a basic engine using primarily open source software. The goal is to find the minimum viable product which is able to be used to make simple games. The libraries will be selected based on fitness for purpose.

Criteria will include:

* elegance
* efficiency
* modern c++ usage
* interoperability
* maintenance

All attempts will be made to observe best practice.

## Cloning
This repository contains submodules for external dependencies, so when doing a fresh clone you need to clone recursively:

```
git clone --recursive git@github.com:ivanhawkes/Jettison.git
```

Existing repositories can be updated manually:

```
git submodule init
git submodule update
```

## Building

There's a few steps to get the project to build. First, you need to get all of the dependencies installed.
That includes Vulkan, SPIRV development libraries, etc. At a minimum, follow the instructions on installing
Vulkan. I had to follow a few additional steps.

```bash
sudo apt install libxcb-xinput0 libxcb-xinerama0 libxinerama-dev libxcb-cursor-dev libxcursor-dev libxi-dev spirv-cross spirv-headers spirv-tools

# Compile the shaders by hand.
cd source/test/assets/shaders
./compile.sh

# Change back to the root of the project first.
mkdir build
cd build
cmake .. -B .
ninja
```
