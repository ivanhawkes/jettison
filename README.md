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

