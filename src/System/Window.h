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

namespace Callbacks
{
	void Error(int error,const char* description);
	void WindowSize(GLFWwindow* window, int width, int height);
	void Scroll(GLFWwindow* window, double xoffset, double yoffset);
	void Key(GLFWwindow* window, int key, int scancode, int action, int mods);
	void Char(GLFWwindow* window,unsigned int character);
	void Drop(GLFWwindow* window, int path_count, const char* paths[]);
	void MouseButton(GLFWwindow* window, int button, int action, int mods);
}

class Window
{
public:
	void Init()
	{
		ZoneScoped;

		glfwSetErrorCallback(&Callbacks::Error);

		glfwInit();
		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		mHandle = glfwCreateWindow(mSize.x, mSize.y, "pbrtrr", nullptr, nullptr);
		CHECK(mHandle != nullptr, "GLFW failed to create a window.");

		mHwnd = glfwGetWin32Window(mHandle);
		CHECK(mHwnd != nullptr, "Couldn't get HWND from GLFW window.");

		glfwSetWindowUserPointer(mHandle, this);
		glfwSetWindowSizeCallback(mHandle, &Callbacks::WindowSize);
		glfwSetScrollCallback(mHandle, &Callbacks::Scroll);
		glfwSetKeyCallback(mHandle, &Callbacks::Key);
		glfwSetCharCallback(mHandle, &Callbacks::Char);
		glfwSetDropCallback(mHandle, &Callbacks::Drop);
		glfwSetMouseButtonCallback(mHandle, &Callbacks::MouseButton);
		glfwSetWindowFocusCallback(mHandle, &ImGui_ImplGlfw_WindowFocusCallback);
		glfwSetCursorPosCallback(mHandle, &ImGui_ImplGlfw_CursorPosCallback);
		glfwSetCursorEnterCallback(mHandle, &ImGui_ImplGlfw_CursorEnterCallback);
	}

	void Deinit()
	{
		ZoneScoped;
		ImGui_ImplGlfw_Shutdown();
		glfwDestroyWindow(mHandle);
		mHandle = NULL;
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
