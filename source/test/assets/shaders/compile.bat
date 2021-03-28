@echo off

set argone=%1
if defined argone cd %1

REM TEST
glslc shader.vert -o shader.vert.spv
glslc shader.frag -o shader.frag.spv

REM IMGUI
glslc imgui.vert -o imgui.vert.spv
glslc imgui.frag -o imgui.frag.spv
