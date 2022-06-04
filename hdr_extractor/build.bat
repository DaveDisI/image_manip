echo off
cls

IF %1 == system_build (
g++ -g hdr_extractor.cpp -o hdr_extractor
)
IF %1 == application_run (
hdr_extractor
)