workspace "pbrtrr"
	architecture "x64"
    startproject "pbrtrr"

	configurations
	{
		"Debug",
		"Development",
		"Profile",
		"Release"
    }

outputdir = "Pbrtrr"

filter "configurations:Debug"
    defines { "_DEBUG", "DEBUG" }
    targetsuffix ("_Debug")
    symbols "On"

filter "configurations:Development"
    defines { "DEVELOPMENT" }
    targetsuffix ("_Development")
    symbols "On"
    optimize "On"

filter "configurations:Profile"
    defines { "PROFILE" }
    targetsuffix ("_Profile")
    symbols "On"
    optimize "On"

filter "configurations:Release"
    defines { "NDEBUG", "RELEASE" }
    flags { "LinkTimeOptimization" }
    optimize "On"

filter ""

include ("thirdparty/premake5.lua")

project "pbrtrr"
if _OPTIONS["SHIPPING"] == "1" then
    kind "WindowedApp"
    defines { "SHIPPING" }
else
    kind "ConsoleApp"
end
    vectorextensions "AVX2"
    floatingpoint "Fast"
    language "C++"
    staticruntime "Off"
    stringpooling "on"
	cppdialect "C++20"
    location "."
    warnings "Extra"
    objdir "./bin-int"
    targetdir ("./bin")

	prebuildcommands { "GenerateProjects.bat" }

    libdirs {"./thirdparty/glfw/lib-vc2022"}
    postbuildcommands {
        "copy thirdparty\\glfw\\lib-vc2022\\glfw3.dll bin",
    }

    postbuildcommands {
        "copy thirdparty\\directstorage\\native\\bin\\x64\\dstorage.dll bin",
        "copy thirdparty\\directstorage\\native\\bin\\x64\\dstoragecore.dll bin",
    }

    postbuildcommands {
        "copy thirdparty\\winpixeventruntime\\bin\\x64\\WinPixEventRuntime.dll bin",
        "copy thirdparty\\dxc\\bin\\x64\\dxil.dll bin",
        "copy thirdparty\\dxc\\bin\\x64\\dxcompiler.dll bin",
    }
    
    libdirs {
        -- thirdparty
        "./thirdparty/EASTL",
        "./thirdparty/dxc/lib/x64",
        "./thirdparty/winpixeventruntime/bin/x64",
        "./thirdparty/directstorage/native/lib/x64",
    }

    files {
        "./thirdparty/minilzo/minilzo.c",
        "./thirdparty/imgui/backends/imgui_impl_glfw.cpp",
        "./thirdparty/imgui/backends/imgui_impl_glfw.h",
        "./thirdparty/tracy/public/TracyClient.cpp",
        "./thirdparty/EASTL/EASTL.natvis",
        "./src/**.h",   "./src/**.cpp",
        "./src/**.hpp", "./src/**.tpp" }

    links {
        -- d3d
        "dxguid",
        "d3d12",
        "dxgi",
        "dstorage",
        -- windows
        "Winmm",
        "onecore",
        "dxcompiler",
        -- thirdparty
        "CityHash",
        "imgui",
        "glfw3",
        "WinPixEventRuntime",
    }

    includedirs {   
        "./src/",
        "./thirdparty/glfw/include",
        "./thirdparty/minilzo",
        "./thirdparty/imgui",
        "./thirdparty/imnodes",
        "./thirdparty/CityHash/src",
        "./thirdparty/tracy/public/tracy",
        "./thirdparty/tracy/public/client",
        "./thirdparty/EASTL/include",
        "./thirdparty/winpixeventruntime/include",
        "./thirdparty/directstorage/native/include",
    }

    defines {
        "EASTL_CUSTOM_FLOAT_CONSTANTS_REQUIRED",
        "_CRT_SECURE_NO_WARNINGS",
        "WIN32_LEAN_AND_MEAN",
        "NOMINMAX",
        "WIN32",
        "_WINDOWS",
        -- "GLFW_EXPOSE_NATIVE_WIN32"
    }

    filter "Debug"
        defines {
            "GLFW_INCLUDE_NONE",
            "EA_DEBUG",
            "USE_PIX",
        }
        links {
            "EASTLd",
        }

    filter "Profile"
        runtime "Release"
        defines {
            "TRACY_ENABLE",
            "USE_PIX",
        }
        links {
            "EASTL",
        }
    
    filter "Development"
        runtime "Release"
        defines {
            "USE_PIX",
        }
        links {
            "EASTL",
        }

    filter "Release"
        runtime "Release"
        links {
            "EASTL",
        }

    --[[ This stuff is just for reference, it doesn't really work that well

    systemversion "latest"

    shadermodel "6.0"

    shaderoptions({"/WX"}) -- Warnings as errors

    local shader_dir = "./content/shaders/"
    files(shader_dir.."**.hlsl")

    -- HLSL files that don't end with 'Extensions' will be ignored as they will be
    -- used as includes
    filter("files:**.hlsl")
        shaderobjectfileoutput("./content/cooked/".."%{file.basename}"..".cso")

    filter("files:**PS.hlsl")
        shadertype("Pixel")

    filter("files:**VS.hlsl")
        shadertype("Vertex")
        
    ]]--

project "Oven"
    kind "ConsoleApp"
    vectorextensions "AVX2"
    floatingpoint "Fast"
    language "C++"
    staticruntime "Off"
    stringpooling "on"
	cppdialect "C++20"
    location "."
    warnings "Extra"
    objdir "./bin-int"
    targetdir ("./bin")

    postbuildcommands {
        "copy thirdparty\\assimp\\bin\\Release\\assimp-vc142-mt.dll bin",
        "copy thirdparty\\directstorage\\native\\bin\\x64\\dstorage.dll bin",
        "copy thirdparty\\directstorage\\native\\bin\\x64\\dstoragecore.dll bin",
    }

	prebuildcommands { "GenerateProjects.bat" }

    libdirs {
        -- thirdparty
        "./thirdparty/assimp",
        "./thirdparty/irrXML",
        "./thirdparty/EASTL",
        "./thirdparty/dxc/lib/x64",
        "./thirdparty/winpixeventruntime/bin/x64",
        "./thirdparty/directstorage/native/lib/x64",
    }

    files {
        "./thirdparty/tracy/public/TracyClient.cpp",
        "./thirdparty/minilzo/minilzo.c",
        "./thirdparty/EASTL/EASTL.natvis",
        "./Oven/**.h", "./Oven/**.cpp",
        "./src/Util/**.h", "./src/Util/**.cpp",
        "./src/Containers/**.h", "./src/Containers/**.cpp",
        "./src/Assets/**.h", "./src/Assets/**.cpp",
        "./src/Threading/**.h", "./src/Threading/**.cpp",
        "./src/external/Implementations.cpp",
     }

    links {
        -- d3d
        "dxcompiler",
        "dstorage",
        -- windows
        "Winmm",
        "onecore",
        -- thirdparty
        "irrxml",
        "CityHash",
        "meshoptimizer",
    }

    includedirs {   
        "./src/",
        "./thirdparty/glfw/include",
        "./thirdparty/minilzo",
        "./thirdparty/tracy/public/tracy",
        "./thirdparty/tracy/public/client",
        "./thirdparty/assimp/include",
        "./thirdparty/irrXML/include",
        "./thirdparty/EASTL/include",
        "./thirdparty/cityhash/src",
        "./thirdparty/cityhashconfig",
        "./thirdparty/directstorage/native/include",
        "./thirdparty/meshoptimizer/src",
    }

    defines {
        "EASTL_CUSTOM_FLOAT_CONSTANTS_REQUIRED",
        "_CRT_SECURE_NO_WARNINGS",
        "WIN32_LEAN_AND_MEAN",
        "NOMINMAX",
        "WIN32",
        "_WINDOWS",
        "OVEN",
    }

    filter "Debug"
        defines {
            "EA_DEBUG",
        }
        links {
            "EASTLd",
            "assimp-vc142-mt",
        }

    filter "Profile"
        runtime "Release"
        defines {
            "TRACY_ENABLE",
        }
        links {
            "EASTL",
            "assimp-vc142-mt",
        }
    
    filter "Development"
        runtime "Release"
        links {
            "EASTL",
            "assimp-vc142-mt",
        }

    filter "Release"
        runtime "Release"
        links {
            "EASTL",
            "assimp-vc142-mt",
        }
