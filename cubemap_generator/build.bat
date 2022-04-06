@echo off
IF %1 == system_build (
cl cubemap_generator.cpp /Zi
)
IF %1 == application_run (
cubemap_generator.exe
)