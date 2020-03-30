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
	cppdialect "C++latest"
    location "."
    warnings "Extra"
    objdir "./bin-int"
    targetdir ("./bin")

if _ACTION == "vs2017" then
    libdirs {"./thirdparty/glfw/lib-vc2017"}
else
    libdirs {"./thirdparty/glfw/lib-vc2019"}
end

    files { "./src/**.h",   "./src/**.cpp",
            "./src/**.hpp", "./src/**.tpp" }

    links { "d3dcompiler", "dxguid", "d3d12", "dxgi", "glfw3" }
    includedirs { "./thirdparty/glfw/include" }
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