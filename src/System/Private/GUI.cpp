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

void InitGUI(const System::Window& WindowHandle)
{
	ImGui::CreateContext();
	ImNodes::CreateContext();
	ImNodes::GetIO().AltMouseButton = ImGuiMouseButton_Right;

	ImGui_ImplGlfw_InitForOther(WindowHandle.mHandle, false);

	ImGuiIO& io = ImGui::GetIO();
	io.DisplaySize = ImVec2((float)WindowHandle.mSize.x, (float)WindowHandle.mSize.y);

	EnqueueRenderThreadWork([]() {
		ImGuiIO& io = ImGui::GetIO();
		io.Fonts->AddFontFromFileTTF("content/fonts/Roboto.ttf", 20);
		uint8_t* Data = nullptr;
		io.Fonts->GetTexDataAsAlpha8(&Data, &gFontTexData.Width, &gFontTexData.Height);
		gFontTexData.Format = DXGI_FORMAT_R8_UNORM;
		CreateTexture(gFontTexData, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
		UploadTextureData(gFontTexData, Data);
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
