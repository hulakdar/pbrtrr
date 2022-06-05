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
	cppdialect "C++17"
    location "."
    warnings "Extra"
    objdir "./bin-int"
    targetdir ("./bin")

if _ACTION == "vs2017" then
    libdirs {"./thirdparty/glfw/lib-vc2017"}
else
    libdirs {"./thirdparty/glfw/lib-vc2019"}
end

    libdirs {
        -- thirdparty
        "./thirdparty/assimp",
        "./thirdparty/irrXML",
        "./thirdparty/zlib",
        "./thirdparty/EASTL",
        "./thirdparty/winpixeventruntime/bin/x64",
    }

    files {
        "./thirdparty/imgui/backends/imgui_impl_glfw.cpp",
        "./thirdparty/imgui/backends/imgui_impl_glfw.h",
        "./thirdparty/tracy/TracyClient.cpp",
        "./thirdparty/EASTL/EASTL.natvis",
        "./src/**.h",   "./src/**.cpp",
        "./src/**.hpp", "./src/**.tpp" }

    links {
        -- d3d
        "d3dcompiler",
        "dxguid",
        "d3d12",
        "dxgi",
        -- thirdparty
        "imgui",
        "irrxml",
        "glfw3",
        "WinPixEventRuntime",
    }

    includedirs {   
        "./src/",
        "./thirdparty/glfw/include",
        "./thirdparty/imgui",
        "./thirdparty/imnodes",
        "./thirdparty/tracy",
        "./thirdparty/assimp/include",
        "./thirdparty/irrXML/include",
        "./thirdparty/EASTL/include",
        "./thirdparty/winpixeventruntime/include",
    }

    defines {
        "_CRT_SECURE_NO_WARNINGS",
        "WIN32_LEAN_AND_MEAN",
        "NOMINMAX",
        "WIN32",
        "_WINDOWS",
        "GLFW_EXPOSE_NATIVE_WIN32"
    }

    filter "Debug"
        staticruntime "Off"
        defines {
            "GLFW_INCLUDE_NONE",
            "EA_DEBUG",
            "USE_PIX",
            "BUILD_DEBUG"
        }
        links {
            "EASTLd",
            "assimp-vc142-mtd",
            "libz-staticmtd"
        }

    filter "Profile"
        staticruntime "Off"
        defines {
            "TRACY_ENABLE",
            "USE_PIX",
            "BUILD_PROFILE",
        }
        links {
            "EASTL",
            "assimp-vc142-mt",
            "libz-staticmt"
        }
    
    filter "Development"
        staticruntime "Off"
        defines {
            "BUILD_DEVELOPMENT",
            "USE_PIX",
        }
        links {
            "EASTL",
            "assimp-vc142-mt",
            "libz-staticmt"
        }


    filter "Release"
        staticruntime "Off"
        defines { "BUILD_RELEASE" }
        links {
            "EASTL",
            "assimp-vc142-mt",
            "libz-staticmt"
        }


    --[[ This stuff is just fore reference, it doesn't really work that well

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
