@echo off
set argone=%1
if defined argone cd %1
glslc.exe shader.vert -o shader.vert.spv
glslc.exe shader.frag -o shader.frag.spv