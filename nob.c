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

    //nob_minimal_log_level = NOB_WARNING;

#if _WIN32
    #define SYSTEM_INCLUDES "-Ithirdparty/directstorage/native/include"
#else
    #define SYSTEM_INCLUDES 
#endif

    Nob_Cmd CommonIncludes = {0};
    nob_cmd_append(&CommonIncludes,
        "-Isrc",
        "-Igenerated",
        "-Ithirdparty/Tracy/public",
        "-Ithirdparty/EASTL/include",
        "-Ithirdparty/minilzo",
        "-Ithirdparty/winpixeventruntime/include",
        "-Ithirdparty/imgui",
        "-Ithirdparty/imnodes",
        "-Ithirdparty/external",
        SYSTEM_INCLUDES
    );

#if _WIN32
    #define SYSTEM_LIBS "Winmm.lib", "onecore.lib", "dxcompiler.lib", "thirdparty/directstorage/native/lib/x64/dstorage.lib"
#else
    #define SYSTEM_LIBS
#endif

    Nob_Cmd CommonLibs = {0};
    nob_cmd_append(&CommonLibs,
        "thirdparty/EASTL/EASTL" LIB_POSTFIX,
        "thirdparty/lib/ThirdParty" LIB_POSTFIX,
        SYSTEM_LIBS
    );

    Nob_Cmd CommonFlagsCmd = {0};
    nob_cmd_append(&CommonFlagsCmd,
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
    );

    Nob_Cmd CommonLinkerFlagsCmd = {0};
    nob_cmd_append(&CommonLinkerFlagsCmd, 
        "-incremental:no", "-opt:ref",
    );

    nob_mkdir_if_not_exists("bin");
    nob_mkdir_if_not_exists("bin-int");
    nob_mkdir_if_not_exists("generated");

    nob_copy_directory_recursively("thirdparty/glfw/lib-vc2022", "bin");
    nob_copy_directory_recursively("thirdparty/directstorage/native/bin/x64", "bin");
    nob_copy_directory_recursively("thirdparty/winpixeventruntime/bin/x64", "bin");
    nob_copy_directory_recursively("thirdparty/dxc/bin/x64", "bin");
    nob_copy_directory_recursively("thirdparty/assimp/bin/Release", "bin");

    {
        Nob_Cmd CodeParserCmd = {0};
        nob_cc(&CodeParserCmd);
        nob_cc_inputs(&CodeParserCmd, "SingleFileBuilds/CodeParser.cpp");
        nob_cc_output(&CodeParserCmd, "bin/CodeParser"EXE_POSTFIX);

        nob_cmd_append(&CodeParserCmd, "-Fo:", "./bin-int/");
        nob_cmd_extend(&CodeParserCmd, &CommonIncludes);
        nob_cmd_extend(&CodeParserCmd, &CommonFlagsCmd);

        nob_cmd_append(&CodeParserCmd, "/link");
        nob_cmd_append(&CodeParserCmd, "-PDB:bin/CodeParser.pdb");
        nob_cmd_extend(&CodeParserCmd, &CommonLibs);
        nob_cmd_extend(&CodeParserCmd, &CommonLinkerFlagsCmd);

        if (!nob_cmd_run_sync(CodeParserCmd)) return 1;
    }

    {
        Nob_Cmd CodeParserRunCmd = {0};
        nob_cmd_append(&CodeParserRunCmd, "bin/CodeParser"EXE_POSTFIX);
        if (!nob_cmd_run_sync(CodeParserRunCmd)) return 1;
    }

    {
        Nob_Cmd OvenCmd = {0};
        nob_cc(&OvenCmd);
        nob_cc_output(&OvenCmd, "bin/Oven"EXE_POSTFIX);
        nob_cc_inputs(&OvenCmd, "SingleFileBuilds/Oven.cpp");

        nob_cmd_append(&OvenCmd, "-Fo:", "bin-int");
        nob_cmd_append(&OvenCmd, "-Ithirdparty/assimp/include");
        nob_cmd_extend(&OvenCmd, &CommonIncludes);
        nob_cmd_extend(&OvenCmd, &CommonFlagsCmd);

        nob_cmd_append(&OvenCmd, "/link");
        nob_cmd_append(&OvenCmd, "-PDB:bin/Oven.pdb");
        nob_cmd_append(&OvenCmd, "./thirdparty/assimp/assimp-vc142-mt.lib");
        nob_cmd_extend(&OvenCmd, &CommonLibs);
        nob_cmd_extend(&OvenCmd, &CommonLinkerFlagsCmd);

        if (!nob_cmd_run_sync(OvenCmd)) return 1;
    }

    {
        Nob_Cmd OvenRunCmd = {0};
        nob_cmd_append(&OvenRunCmd, "bin/Oven"EXE_POSTFIX, "compile_shaders");
        if (!nob_cmd_run_sync(OvenRunCmd)) return 1;
    }

    {
        Nob_Cmd PbrtrrCmd = {0};
        nob_cc(&PbrtrrCmd);
        nob_cc_inputs(&PbrtrrCmd, "SingleFileBuilds/pbrtrr.cpp");
        nob_cc_output(&PbrtrrCmd, "bin/pbrtrr"EXE_POSTFIX);

        nob_cmd_append(&PbrtrrCmd, "-Fo:", "./bin-int/");
        nob_cmd_append(&PbrtrrCmd, "-I./thirdparty/glfw/include/");
        nob_cmd_extend(&PbrtrrCmd, &CommonIncludes);
        nob_cmd_extend(&PbrtrrCmd, &CommonFlagsCmd);

        nob_cmd_append(&PbrtrrCmd, "/link");
        nob_cmd_append(&PbrtrrCmd, "-PDB:bin/pbrtrr.pdb");
        nob_cmd_extend(&PbrtrrCmd, &CommonLibs);
        nob_cmd_extend(&PbrtrrCmd, &CommonLinkerFlagsCmd);
        nob_cmd_append(&PbrtrrCmd, 
            "dxguid.lib", "d3d12.lib", "dxgi.lib",
            "./thirdparty/glfw/lib-vc2022/glfw3dll.lib"
        );

        if (!nob_cmd_run_sync(PbrtrrCmd)) return 1;
    }
    return 0;
}