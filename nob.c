#define NOB_IMPLEMENTATION
#include "thirdparty/external/nob.h"

#if _WIN32
#define EXE_POSTFIX ".exe"
#define LIB_POSTFIX ".lib"
#else
#define EXE_POSTFIX ""
#define LIB_POSTFIX ".a"
#endif

int main(int argc, char **argv)
{
    NOB_GO_REBUILD_URSELF(argc, argv);
    if (nob_file_exists("nob.obj")) nob_delete_file("nob.obj");

    nob_minimal_log_level = NOB_WARNING;

    Nob_Cmd CommonIncludesCmd = {0};
    {
        const char* CommonIncludes[] = {
            "-Isrc",
            "-Igenerated",
            "-Ithirdparty/Tracy/public/",
            "-Ithirdparty/EASTL/include/",
            "-Ithirdparty/directstorage/native/include",
            "-Ithirdparty/minilzo",
            "-Ithirdparty/winpixeventruntime/include",
            "-Ithirdparty/imgui",
            "-Ithirdparty/imnodes",
            "-Ithirdparty/external",
        };
        for (int i = 0; i < NOB_ARRAY_LEN(CommonIncludes); ++i)
        {
            nob_cmd_append(&CommonIncludesCmd, CommonIncludes[i]);
        }
    }

    Nob_Cmd CommonLibsCmd = {0};
    {
        const char* CommonLibs[] = {
#if _WIN32
            "Winmm.lib", "onecore.lib", "dxcompiler.lib",
            "thirdparty/directstorage/native/lib/x64/dstorage.lib",
#endif
            "thirdparty/EASTL/EASTL"LIB_POSTFIX,
            "thirdparty/lib/ThirdParty"LIB_POSTFIX,
        };
        for (int i = 0; i < NOB_ARRAY_LEN(CommonLibs); ++i)
        {
            nob_cmd_append(&CommonLibsCmd, CommonLibs[i]);
        }
    }

    Nob_Cmd CommonFlagsCmd = {0};
    {
        const char* CommonFlags[] = {
            "-std:c++17", "-MD", "-Od",
            "-nologo", "-fp:fast",
            "-fp:except-",
            "-Gm-", "-GR-", "-EHa-", "-Zo", "-Oi", "-WX", "-Z7", "-GS-",
            "-DEASTL_CUSTOM_FLOAT_CONSTANTS_REQUIRED",
            "-D_HAS_EXCEPTIONS=0",

            //"-DTRACY_ENABLE=1",
            //"-DDEBUG"
            //"-fsanitize=address",
            //"-showIncludes",
        };
        for (int i = 0; i < NOB_ARRAY_LEN(CommonFlags); ++i)
        {
            nob_cmd_append(&CommonFlagsCmd, CommonFlags[i]);
        }
    }

    Nob_Cmd CommonLinkerFlagsCmd = {0};
    {
        const char* CommonLinkerFlags[] ={
            "-incremental:no", "-opt:ref",
        };
        for (int i = 0; i < NOB_ARRAY_LEN(CommonLinkerFlags); ++i) {
            nob_cmd_append(&CommonLinkerFlagsCmd, CommonLinkerFlags[i]);
        }
    }

    nob_mkdir_if_not_exists("bin");
    nob_mkdir_if_not_exists("bin-int");
    nob_mkdir_if_not_exists("generated");

    nob_copy_directory_recursively("./thirdparty/glfw/lib-vc2022/", "./bin/");
    nob_copy_directory_recursively("./thirdparty/directstorage/native/bin/x64/", "./bin/");
    nob_copy_directory_recursively("./thirdparty/winpixeventruntime/bin/x64/", "./bin/");
    nob_copy_directory_recursively("./thirdparty/dxc/bin/x64/", "./bin/");
    nob_copy_directory_recursively("./thirdparty/assimp/bin/Release/", "./bin/");

    {
        Nob_Cmd CodeParserCmd = {0};
        nob_cc(&CodeParserCmd);
        nob_cc_inputs(&CodeParserCmd, "SingleFileBuilds/CodeParser.cpp");
        nob_cc_output(&CodeParserCmd, "bin/CodeParser"EXE_POSTFIX);

        nob_cmd_append(&CodeParserCmd, "-Fo:", "./bin-int/");
        nob_cmd_extend(&CodeParserCmd, &CommonIncludesCmd);
        nob_cmd_extend(&CodeParserCmd, &CommonFlagsCmd);

        nob_cmd_append(&CodeParserCmd, "/link");
        nob_cmd_append(&CodeParserCmd, "-PDB:bin/CoreParser.pdb");
        nob_cmd_extend(&CodeParserCmd, &CommonLibsCmd);
        nob_cmd_extend(&CodeParserCmd, &CommonLinkerFlagsCmd);

        if (!nob_cmd_run_sync(CodeParserCmd)) return 1;
    }

    {
        Nob_Cmd CodeParserRunCmd = {0};
        nob_cmd_append(&CodeParserRunCmd, "bin/CodeParser"EXE_POSTFIX);
        if (!nob_cmd_run_sync(CodeParserRunCmd)) return 1;
    }

    {
        const char* OvenLibs = "./thirdparty/assimp/assimp-vc142-mt.lib";
        const char* OvenIncludes ="-I./thirdparty/assimp/include/";

        Nob_Cmd OvenCmd = {0};
        nob_cc(&OvenCmd);
        nob_cc_output(&OvenCmd, "bin/Oven"EXE_POSTFIX);
        nob_cc_inputs(&OvenCmd, "SingleFileBuilds/Oven.cpp");

        nob_cmd_append(&OvenCmd, "-Fo:", "bin-int/");
        nob_cmd_append(&OvenCmd, OvenIncludes);
        nob_cmd_extend(&OvenCmd, &CommonIncludesCmd);
        nob_cmd_extend(&OvenCmd, &CommonFlagsCmd);

        nob_cmd_append(&OvenCmd, "/link");
        nob_cmd_append(&OvenCmd, "-PDB:bin/Oven.pdb");
        nob_cmd_append(&OvenCmd, OvenLibs);
        nob_cmd_extend(&OvenCmd, &CommonLibsCmd);
        nob_cmd_extend(&OvenCmd, &CommonLinkerFlagsCmd);

        if (!nob_cmd_run_sync(OvenCmd)) return 1;
    }

    {
        Nob_Cmd OvenRunCmd = {0};
        nob_cmd_append(&OvenRunCmd, "bin/Oven"EXE_POSTFIX, "compile_shaders");
        if (!nob_cmd_run_sync(OvenRunCmd)) return 1;
    }

    {
        const char* PbrtrrLibs[] = {
            "dxguid.lib", "d3d12.lib", "dxgi.lib",
            "./thirdparty/glfw/lib-vc2022/glfw3dll.lib",
        };
        const char* PbrtrrIncludes = "-I./thirdparty/glfw/include/";

        Nob_Cmd PbrtrrCmd = {0};
        nob_cc(&PbrtrrCmd);
        nob_cc_inputs(&PbrtrrCmd, "SingleFileBuilds/pbrtrr.cpp");
        nob_cc_output(&PbrtrrCmd, "bin/pbrtrr"EXE_POSTFIX);

        nob_cmd_append(&PbrtrrCmd, "-Fo:", "./bin-int/");
        nob_cmd_append(&PbrtrrCmd, PbrtrrIncludes);
        nob_cmd_extend(&PbrtrrCmd, &CommonIncludesCmd);
        nob_cmd_extend(&PbrtrrCmd, &CommonFlagsCmd);

        nob_cmd_append(&PbrtrrCmd, "/link");
        nob_cmd_append(&PbrtrrCmd, "-PDB:bin/pbrtrr.pdb");
        nob_cmd_extend(&PbrtrrCmd, &CommonLibsCmd);
        nob_cmd_extend(&PbrtrrCmd, &CommonLinkerFlagsCmd);
        for (int i = 0; i < NOB_ARRAY_LEN(PbrtrrLibs); ++i) { nob_cmd_append(&PbrtrrCmd, PbrtrrLibs[i]); }

        if (!nob_cmd_run_sync(PbrtrrCmd)) return 1;
    }
    return 0;
}