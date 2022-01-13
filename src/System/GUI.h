#pragma once
#include <imgui.h>

#include "System/Window.h"
#include "Render/RenderThread.h"

template<typename T>
inline ImVec2 ToImVec2(T Vec)
{
	return ImVec2((float)Vec.x, (float)Vec.y);
}

namespace System
{

class GUI
{
public:
	void Init(const Window& WindowHandle)
	{
		ImGui::CreateContext();

		ImGuiIO& io = ImGui::GetIO();
		io.DisplaySize = ImVec2((float)WindowHandle.mSize.x, (float)WindowHandle.mSize.y);

		EnqueueRenderThreadWork([this](RenderContext& RenderContext) {
			ImGuiIO& io = ImGui::GetIO();
			io.Fonts->AddFontFromFileTTF("content/fonts/Roboto.ttf", 20);
			uint8_t *Data = nullptr;
			io.Fonts->GetTexDataAsAlpha8(&Data, &FontTexData.Size.x, &FontTexData.Size.y);
			FontTexData.Format = DXGI_FORMAT_R8_UNORM;
			RenderContext.CreateTexture(FontTexData);
			RenderContext.UploadTextureData(FontTexData, Data);
			RenderContext.CreateSRV(FontTexData);
		});

		io.IniFilename = NULL;
		io.LogFilename = NULL;
		io.WantSaveIniSettings = false;
		io.WantTextInput = false;
	}

	void Update(const Window& WindowHandle)
	{
		ImGuiIO& io = ImGui::GetIO();
		io.DisplaySize = ToImVec2(WindowHandle.mSize);
		io.MousePos = ToImVec2(WindowHandle.mMousePosition);

		for (int i = 0; i < WindowHandle.mMouseButtons.size(); ++i)
		{
			io.MouseDown[i] = WindowHandle.mMouseButtons[i];
		}

		io.MouseWheel = WindowHandle.mScrollOffset.y;
		io.MouseWheelH = WindowHandle.mScrollOffset.x;
	}

	TextureData FontTexData;
};

}
