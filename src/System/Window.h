#pragma once

#include "Util/Math.hpp"
#include "Util/Debug.h"
#include "Util/Util.h"

#include "external/d3dx12.h"
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <bitset>
#include <Tracy.hpp>

#define VALIDATE_GLFW_CALL(x) if (!(x)) { const char* Error; glfwGetError(&Error); Debug::Print("Error during:", #x, Error); }

namespace System
{

namespace Callbacks
{
	void Error(int error,const char* description);
	void WindowSize(GLFWwindow* window, int width, int height);
	void Scroll(GLFWwindow* window, double xoffset, double yoffset);
	void Key(GLFWwindow* window, int key, int scancode, int action, int mods);
	void Drop(GLFWwindow* window, int path_count, const char* paths[]);
	void MouseButton(GLFWwindow* window, int button, int action, int mods);
}

using namespace Math;


class Window
{
public:
	void Init()
	{
		ZoneScoped;
		VALIDATE_GLFW_CALL(glfwInit());

		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		
		VALIDATE_GLFW_CALL(mHandle = glfwCreateWindow(mSize.x, mSize.y, "pbrtrr", nullptr, nullptr));
		CHECK(mHandle != nullptr, "GLFW failed to create a window.");

		VALIDATE_GLFW_CALL(mHwnd = glfwGetWin32Window(mHandle));
		CHECK(mHwnd != nullptr, "Couldn't get HWND from GLFW window.");

		glfwSetErrorCallback(&Callbacks::Error);

		glfwSetWindowUserPointer(mHandle, this);
		glfwSetWindowSizeCallback(mHandle, &Callbacks::WindowSize);
		glfwSetScrollCallback(mHandle, &Callbacks::Scroll);
		glfwSetKeyCallback(mHandle, &Callbacks::Key);
		glfwSetDropCallback(mHandle, &Callbacks::Drop);
		glfwSetMouseButtonCallback(mHandle, &Callbacks::MouseButton);
	}

	void Deinit()
	{
		ZoneScoped;
		glfwDestroyWindow(mHandle);
		mHandle = NULL;
		glfwTerminate();
		mHandle = NULL;
	}

	void Update()
	{
		ZoneScoped;
		mScrollOffset = Vector2(0, 0);
		mKeyboard.reset();

		glfwPollEvents();
		if (mKeyboard[GLFW_KEY_ESCAPE] == GLFW_PRESS)
		{
			glfwSetWindowShouldClose(mHandle, GLFW_TRUE);
			return;
		}

		UpdateInput();
	}

	IVector2			mSize { 1280, 720 };
	Vector2				mMousePosition { 0, 0 };
	Vector2				mScrollOffset { 0, 0 };

	bool				mWindowStateDirty = false;

	std::bitset<GLFW_MOUSE_BUTTON_LAST + 1>		mMouseButtons;
	std::bitset<GLFW_KEY_LAST + 1>				mKeyboard;
	 
	HWND				mHwnd = nullptr;
	GLFWwindow			*mHandle = nullptr;

	TArray<String>		mDroppedPaths;

private:
	void UpdateInput()
	{
		ZoneScoped;
		double MouseX, MouseY;
		glfwGetCursorPos(mHandle, &MouseX, &MouseY);
		mMousePosition = Vector2((float)MouseX, (float)MouseY);
	}
};

}
