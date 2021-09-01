echo off
cls
IF %1 == system_build (
cl /ZI font_extractor.cpp /link D2d1.lib User32.lib
)
IF %1 ==  application_run (
font_extractor
)
