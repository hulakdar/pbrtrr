#pragma once

struct TextureData;
namespace System {
	class Window;
};

void InitGUI(const System::Window& WindowHandle);
void UpdateGUI(const System::Window& WindowHandle);
TextureData& GetGUIFont();

