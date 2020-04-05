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
if _OPTIONS["SHIPPING"] == "1" then
    kind "WindowedApp"
    defines { "SHIPPING" }
else
    kind "ConsoleApp"
end
    vectorextensions "AVX2"
    floatingpoint "Fast"
    language "C++"
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
        "./thirdparty/EASTL",
        "./thirdparty/DirectXTK12"
    }

    files { "./src/**.h",   "./src/**.cpp",
            "./src/**.hpp", "./src/**.tpp" }

    links {
        "d3dcompiler",
        "dxguid",
        "d3d12",
        "dxgi",
        -- thirdparty
        "DirectXTK12",
        "EASTL",
        "glfw3"
    }
    includedirs {   
        "./src/",
        "./thirdparty/glfw/include",
        "./thirdparty/DirectXTK12/include",
        "./thirdparty/EASTL/include"
    }
    defines { "_CRT_SECURE_NO_WARNINGS", "WIN32", "_WINDOWS", "GLFW_EXPOSE_NATIVE_WIN32" }
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