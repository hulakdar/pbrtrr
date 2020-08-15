#pragma once
#include <imgui.h>

#include "System/Window.h"

namespace System
{

class GUI
{
public:
	void Init(const Window& WindowHandle, Render::Context& RenderContext)
	{
		ImGui::CreateContext();

		ImGuiIO& io = ImGui::GetIO();
		io.DisplaySize = ImVec2(WindowHandle.mSize.x, WindowHandle.mSize.y);

		io.Fonts->AddFontFromFileTTF("content/fonts/Roboto.ttf", 20);
		unsigned char *Data = nullptr;
		io.Fonts->GetTexDataAsAlpha8(&Data, &FontTexData.Size.x, &FontTexData.Size.y);
		FontTexData.Data = StringView((char *)Data, FontTexData.Size.x * FontTexData.Size.y);
		FontTexData.Format = DXGI_FORMAT_R8_UNORM;
		FontTexData.Texture = RenderContext.CreateTexture(&FontTexData, 1);
		FontTexData.SRVHeapIndex = RenderContext.CreateSRV(FontTexData);

		io.IniFilename = NULL;
		io.LogFilename = NULL;
		io.WantSaveIniSettings = false;
		io.WantTextInput = false;
	}

	Render::TextureData FontTexData;
};

}
