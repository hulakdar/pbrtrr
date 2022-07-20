#define GLFW_EXPOSE_NATIVE_WIN32
#include "System/Window.h"

#include <backends/imgui_impl_glfw.h>

void System::Callbacks::Error(int error, const char* description)
{
	Debug::Print(error, ": ", description);
	//DEBUG_BREAK();
}

void System::Callbacks::WindowSize(GLFWwindow* window, int width, int height)
{
	Window *WindowPtr = (Window*)glfwGetWindowUserPointer(window);

	if (WindowPtr->mSize.x != width || WindowPtr->mSize.y != height)
	{
		WindowPtr->mSize = IVec2{ width, height };
		WindowPtr->mWindowStateDirty = true;
	}
}

void System::Callbacks::Scroll(GLFWwindow* window, double xoffset, double yoffset)
{
	Window *WindowPtr = (Window*)glfwGetWindowUserPointer(window);

	WindowPtr->mScrollOffset = Vec2{ (float)xoffset, (float)yoffset };
}

void System::Callbacks::Key(GLFWwindow* window, int key, int scancode, int action, int mods)
{
	Window *WindowPtr = (Window*)glfwGetWindowUserPointer(window);

	WindowPtr->mKeyboard[key] = true;
	ImGui_ImplGlfw_KeyCallback(window, key, scancode, action, mods);
}

void System::Callbacks::Char(GLFWwindow* window, unsigned int character)
{
	ImGui_ImplGlfw_CharCallback(window, character);
}

void System::Callbacks::Drop(GLFWwindow* window, int path_count, const char* paths[])
{
	Window *WindowPtr = (Window*)glfwGetWindowUserPointer(window);

	for (int i = 0; i < path_count; ++i)
	{
		//WindowPtr->mDroppedPaths.emplace_back(paths[i]);
	}
}

void System::Callbacks::MouseButton(GLFWwindow* window, int button, int action, int mods)
{
	Window *WindowPtr = (Window*)glfwGetWindowUserPointer(window);

	WindowPtr->mMouseButtons[button] = action;
	ImGui_ImplGlfw_MouseButtonCallback(window, button, action, mods);
}

void System::Window::Init()
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
