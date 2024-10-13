@echo off
set CommonIncludes=/I .\src
set CommonIncludes=/I .\generated %CommonIncludes%
set CommonIncludes=/I .\thirdparty\Tracy\public\ %CommonIncludes%
set CommonIncludes=/I .\thirdparty\EASTL\include\ %CommonIncludes%
set CommonIncludes=/I .\thirdparty\directstorage\native\include\ %CommonIncludes%
set CommonIncludes=/I .\thirdparty\minilzo\ %CommonIncludes%
set CommonIncludes=/I .\thirdparty\winpixeventruntime\include\ %CommonIncludes%
set CommonIncludes=/I .\thirdparty\imgui\ %CommonIncludes%
set CommonIncludes=/I .\thirdparty\imnodes\ %CommonIncludes%
set CommonIncludes=/I .\thirdparty\external\ %CommonIncludes%

set CommonLibs=Winmm.lib onecore.lib dxcompiler.lib
set CommonLibs=.\thirdparty\directstorage\native\lib\x64\dstorage.lib %CommonLibs%
set CommonLibs=.\thirdparty\EASTL\EASTL.lib %CommonLibs%
set CommonLibs=.\thirdparty\lib\ThirdParty.lib %CommonLibs%

set CommonFlags=-std:c++17 -MD -Od -nologo -fp:fast -fp:except- -Gm- -GR- -EHa- -Zo -Oi -WX -Z7 -GS-
rem set CommonFlags=-DTRACY_ENABLE=1 %CommonFlags%
set CommonFlags=-DEASTL_CUSTOM_FLOAT_CONSTANTS_REQUIRED %CommonFlags%
set CommonFlags=-D_HAS_EXCEPTIONS=0 %CommonFlags%
rem set CommonFlags=-DDEBUG %CommonFlags%
rem set CommonFlags=/fsanitize=address %CommonFlags%
rem set CommonFlags=-showIncludes %CommonFlags%

set CommonLinkerFlags=-incremental:no -opt:ref

rmdir /S /Q .\bin .\bin-int .\generated
mkdir .\bin
mkdir .\bin-int
mkdir .\generated

copy /b thirdparty\\glfw\\lib-vc2022\\glfw3.dll bin
copy /b thirdparty\\directstorage\\native\\bin\\x64\\dstorage.dll bin
copy /b thirdparty\\directstorage\\native\\bin\\x64\\dstoragecore.dll bin
copy /b thirdparty\\winpixeventruntime\\bin\\x64\\WinPixEventRuntime.dll bin
copy /b thirdparty\\dxc\\bin\\x64\\dxil.dll bin
copy /b thirdparty\\dxc\\bin\\x64\\dxcompiler.dll bin
copy /b thirdparty\\assimp\\bin\\Release\\assimp-vc142-mt.dll bin

set PbrtrrLibs=dxguid.lib d3d12.lib dxgi.lib %CommonLibs%
set PbrtrrLibs=.\thirdparty\glfw\lib-vc2022\glfw3dll.lib %PbrtrrLibs%
set PbrtrrLinkerFlags=%PbrtrrLibs% %CommonLinkerFlags%

set PbrtrrIncludes=/I .\thirdparty\glfw\include\ %CommonIncludes%

set OvenLibs=.\thirdparty\assimp\assimp-vc142-mt.lib %CommonLibs%
set OvenIncludes=/I .\thirdparty\assimp\include\ %CommonIncludes%
set OvenLinkerFlags=%OvenLibs% %CommonLinkerFlags%

cl /Fo: .\bin-int\ /Fe: .\bin\CodeParser.exe .\SingleFileBuilds\CodeParser.cpp %OvenIncludes% %CommonFlags% /link -PDB:.\bin\CodeParser.pdb %OvenLinkerFlags%
if %ERRORLEVEL% GEQ 1 EXIT /B %ERRORLEVEL%
if "%~1" == "OnlyCodeParser" EXIT

.\bin\CodeParser.exe
if %ERRORLEVEL% GEQ 1 EXIT /B %ERRORLEVEL%

cl /Fo: .\bin-int\ /Fe: .\bin\Oven.exe .\SingleFileBuilds\Oven.cpp %OvenIncludes% %CommonFlags% /link -PDB:.\bin\Oven.pdb %OvenLinkerFlags%
if %ERRORLEVEL% GEQ 1 EXIT /B %ERRORLEVEL%

.\bin\Oven.exe compile_shaders
if %ERRORLEVEL% GEQ 1 EXIT /B %ERRORLEVEL%

cl /Fo: .\bin-int\ /Fe: .\bin\pbrtrr.exe .\SingleFileBuilds\pbrtrr.cpp %PbrtrrIncludes% %CommonFlags% /link -PDB:.\bin\pbrtrr.pdb %PbrtrrLinkerFlags%
if %ERRORLEVEL% GEQ 1 EXIT /B %ERRORLEVEL%
