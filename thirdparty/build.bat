mkdir lib
mkdir bin-int

@set CommonIncludes=/I .\Tracy\public\
@set CommonIncludes=/I .\glfw\include\ %CommonIncludes%
@set CommonIncludes=/I .\EASTL\include\ %CommonIncludes%
@set CommonIncludes=/I .\directstorage\native\include\ %CommonIncludes%
@set CommonIncludes=/I .\minilzo\ %CommonIncludes%
@set CommonIncludes=/I .\winpixeventruntime\include\ %CommonIncludes%
@set CommonIncludes=/I .\imgui\ %CommonIncludes%
@set CommonIncludes=/I .\imnodes\ %CommonIncludes%

@set CommonFlags=-DTRACY_ENABLE=1 -std:c++17 -MD -Od -nologo -fp:fast -fp:except- -Gm- -GR- -EHa- -Zo -Oi -WX -Z7 -GS-
@set CommonFlags=-D_HAS_EXCEPTIONS=0 %CommonFlags%
@rem set CommonFlags=/fsanitize=address %CommonFlags%

@cl -nologo -c -EHa- /Fo: .\bin-int\ .\ThirdParty.cpp tracy\public\TracyClient.cpp %CommonIncludes% %CommonFlags%
@lib -nologo /OUT:.\lib\ThirdParty.lib .\bin-int\ThirdParty.obj .\bin-int\TracyClient.obj