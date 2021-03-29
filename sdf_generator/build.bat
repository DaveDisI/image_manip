C:\"Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build"\vcvarsall x64 && ^
cl sdf_generator.cpp /Zi /volatile:iso /Fesdf_generator /link d2d1.lib User32.lib 
