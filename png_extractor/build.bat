IF %1 == system_build (
cl png_extractor.cpp
)
IF %1 == application_run (
png_extractor
)