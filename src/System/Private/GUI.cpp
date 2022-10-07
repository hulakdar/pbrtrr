#include "System/Window.h"
#include "System/GUI.h"
#include "Render/Texture.h"
#include "Render/RenderThread.h"
#include "Render/Context.h"

#include <imgui.h>
#include <imnodes.h>
#include <backends/imgui_impl_glfw.h>

template<typename T>
inline ImVec2 ToImVec2(T Vec)
{
	return ImVec2((float)Vec.x, (float)Vec.y);
}

TextureData gFontTexData;

TextureData& GetGUIFont()
{
	return gFontTexData;
}

void* Allocate(size_t size, void*)
{
	return new u8[size];
}

void Deallocate(void* ptr, void*)
{
	delete[] ptr;
}

void InitGUI(const System::Window& WindowHandle)
{
	ImGui::CreateContext();
	ImNodes::CreateContext();
	ImNodes::GetIO().AltMouseButton = ImGuiMouseButton_Right;

	ImGui::SetAllocatorFunctions(&Allocate, Deallocate);

	ImGui_ImplGlfw_InitForOther(WindowHandle.mHandle, false);

	ImGuiIO& io = ImGui::GetIO();
	io.DisplaySize = ImVec2((float)WindowHandle.mSize.x, (float)WindowHandle.mSize.y);

	EnqueueToRenderThread([]() {
		ImGuiIO& io = ImGui::GetIO();
		io.Fonts->AddFontFromFileTTF("content/fonts/Roboto.ttf", 20);
		uint8_t* Data = nullptr;
		int w = 0, h = 0;
		io.Fonts->GetTexDataAsAlpha8(&Data, &w, &h);
		gFontTexData.Width = (u16)w;
		gFontTexData.Height = (u16)h;
		gFontTexData.Format = DXGI_FORMAT_R8_UNORM;
		CreateResourceForTexture(gFontTexData, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COMMON);
		UploadTextureData(gFontTexData, Data, w*h*4);
		CreateSRV(gFontTexData);
	});

	io.IniFilename = NULL;
	io.LogFilename = NULL;
	io.WantSaveIniSettings = false;
	io.WantTextInput = false;
}

void UpdateGUI(const System::Window& WindowHandle)
{
	ZoneScoped;
	ImGuiIO& io = ImGui::GetIO();
	io.DisplaySize = ToImVec2(WindowHandle.mSize);
	io.MousePos = ToImVec2(WindowHandle.mMousePosition);

	for (int i = 0; i < 5; ++i)
	{
		io.MouseDown[i] = WindowHandle.mMouseButtons[i];
	}

	io.MouseWheel = WindowHandle.mScrollOffset.y;
	io.MouseWheelH = WindowHandle.mScrollOffset.x;

	//io.AddKeyEvent(ImGui)

	ImGui_ImplGlfw_NewFrame();
}
