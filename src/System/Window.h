#pragma once

#include "Util/Math.h"
#include "Util/Debug.h"
#include "Util/Util.h"

#include "external/d3dx12.h"
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <bitset>
#include <Tracy.hpp>
#include <backends/imgui_impl_glfw.h>

namespace System
{

class Window
{
public:
	void Init();

	void Deinit()
	{
		ZoneScoped;
		ImGui_ImplGlfw_Shutdown();
		glfwDestroyWindow(mHandle);
		glfwTerminate();
		mHandle = NULL;
	}

	void Update()
	{
		ZoneScoped;
		mScrollOffset = Vec2{ 0, 0 };
		mKeyboard.reset();

		glfwPollEvents();
		if (mKeyboard[GLFW_KEY_ESCAPE] == GLFW_PRESS)
		{
			glfwSetWindowShouldClose(mHandle, GLFW_TRUE);
			return;
		}

		UpdateInput();
		ImGui_ImplGlfw_NewFrame();
	}

	IVec2			mSize { 1280, 720 };
	Vec2			mMousePosition { 0, 0 };
	Vec2			mScrollOffset { 0, 0 };

	bool			mWindowStateDirty = false;

	std::bitset<GLFW_MOUSE_BUTTON_LAST + 1>		mMouseButtons;
	std::bitset<GLFW_KEY_LAST + 1>				mKeyboard;
	 
	HWND				mHwnd = nullptr;
	GLFWwindow			*mHandle = nullptr;

private:
	void UpdateInput()
	{
		ZoneScoped;
		double MouseX, MouseY;
		glfwGetCursorPos(mHandle, &MouseX, &MouseY);
		mMousePosition = Vec2{(float)MouseX, (float)MouseY};
	}
};

}
