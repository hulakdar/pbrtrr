workspace "pbrtrr"
	architecture "x64"
    startproject "pbrtrr"

	configurations
	{
		"Debug",
		"Development",
		"Release"
	}

project "pbrtrr"
    kind "WindowedApp"
    language "C++"
	cppdialect "C++17"
    location "."
    warnings "Extra"
    objdir "./build/intermidiate"
    targetdir ("./bin")

    files { "./src/**.h", "./src/**.cpp" }
    links { "d3dcompiler", "dxguid", "d3d12", "dxgi" }
    defines { "_CRT_SECURE_NO_WARNINGS", "WIN32", "_WINDOWS" }
    systemversion "latest"

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