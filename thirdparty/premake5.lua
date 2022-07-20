project "ImGui"
    kind "StaticLib"
    language "C++"
    staticruntime "Off"
    systemversion "latest"
    cppdialect "C++17"
    runtime "Release"

	targetdir ("bin/" .. outputdir .. "/%{prj.name}")
    objdir ("bin-int/" .. outputdir .. "/%{prj.name}")

	files
	{
        "./imgui/imconfig.h",
        "./imgui/imgui.h",
        "./imgui/imgui.cpp",
        "./imgui/imgui_draw.cpp",
        "./imgui/imgui_internal.h",
        "./imgui/imgui_tables.cpp",
        "./imgui/imgui_widgets.cpp",
        "./imgui/imstb_rectpack.h",
        "./imgui/imstb_textedit.h",
        "./imgui/imstb_truetype.h",
         -- "./imgui/imgui_demo.cpp"
        "./imnodes/imnodes.cpp"
    }

    includedirs {
        "./imgui"
    }

    defines {
        "IMGUI_DISABLE_OBSOLETE_FUNCTIONS",
        "IMGUI_DISABLE_OBSOLETE_KEYIO"
    }