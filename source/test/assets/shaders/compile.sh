#!/bin/sh
glslc shader.vert -o shader.vert.spv
glslc shader.frag -o shader.frag.spv
glslc imgui.vert -o imgui.vert.spv
glslc imgui.frag -o imgui.frag.spv
