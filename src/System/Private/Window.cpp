#define GLFW_EXPOSE_NATIVE_WIN32
#include "System/Window.h"

#include <backends/imgui_impl_glfw.h>

namespace System {

	namespace {

		void Error(int error, const char* description)
		{
			Debug::Print(error, ": ", description);
			//DEBUG_BREAK();
		}

		void WindowSize(GLFWwindow* window, int width, int height)
		{
			Window *WindowPtr = (Window*)glfwGetWindowUserPointer(window);

			if (WindowPtr->mSize.x != width || WindowPtr->mSize.y != height)
			{
				WindowPtr->mSize = IVec2{ width, height };
				WindowPtr->mWindowStateDirty = true;
			}
		}

		void WindowClosed(GLFWwindow* window)
		{
			Window *WindowPtr = (Window*)glfwGetWindowUserPointer(window);
			WindowPtr->mWindowStateDirty = true;
		}

		void WindowRefresh(GLFWwindow* window)
		{

		}

		void WindowMaximized(GLFWwindow* window, int maximized)
		{
			int width, height;
			glfwGetWindowSize(window, &width, &height);
			WindowSize(window, width, height);
		}

		//void WindowMaximized(GLFWwindow* window, int maximized)

		void Scroll(GLFWwindow* window, double xoffset, double yoffset)
		{
			Window *WindowPtr = (Window*)glfwGetWindowUserPointer(window);

			WindowPtr->mScrollOffset = Vec2{ (float)xoffset, (float)yoffset };
		}

		void Key(GLFWwindow* window, int key, int scancode, int action, int mods)
		{
			Window *WindowPtr = (Window*)glfwGetWindowUserPointer(window);

			WindowPtr->mKeyboard[key] = true;
			ImGui_ImplGlfw_KeyCallback(window, key, scancode, action, mods);
		}

		void Char(GLFWwindow* window, unsigned int character)
		{
			ImGui_ImplGlfw_CharCallback(window, character);
		}

		void Drop(GLFWwindow* window, int path_count, const char* paths[])
		{
			Window *WindowPtr = (Window*)glfwGetWindowUserPointer(window);

			for (int i = 0; i < path_count; ++i)
			{
				//WindowPtr->mDroppedPaths.emplace_back(paths[i]);
			}
		}

		void MouseButton(GLFWwindow* window, int button, int action, int mods)
		{
			Window *WindowPtr = (Window*)glfwGetWindowUserPointer(window);

			WindowPtr->mMouseButtons[button] = action;
			ImGui_ImplGlfw_MouseButtonCallback(window, button, action, mods);
		}

	}
}

void System::Window::Init()
{
	ZoneScoped;

	glfwSetErrorCallback(&Error);

	glfwInit();
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

	GLFWmonitor* monitor = glfwGetPrimaryMonitor();
	const GLFWvidmode* mode = glfwGetVideoMode(monitor);
 
	glfwWindowHint(GLFW_RED_BITS, mode->redBits);
	glfwWindowHint(GLFW_GREEN_BITS, mode->greenBits);
	glfwWindowHint(GLFW_BLUE_BITS, mode->blueBits);
	glfwWindowHint(GLFW_REFRESH_RATE, mode->refreshRate);
	 
	mSize.x = mode->width;
	mSize.y = mode->height;

	GLFWwindow* window = glfwCreateWindow(mode->width, mode->height, "My Title", monitor, NULL);
	mHandle = window;
	//mHandle = glfwCreateWindow(mSize.x, mSize.y, "pbrtrr", nullptr, nullptr);
	CHECK(mHandle != nullptr, "GLFW failed to create a window.");

	mHwnd = glfwGetWin32Window(mHandle);
	CHECK(mHwnd != nullptr, "Couldn't get HWND from GLFW window.");

	glfwSetWindowUserPointer(mHandle, this);
	glfwSetWindowSizeCallback(mHandle, &WindowSize);
	glfwSetWindowCloseCallback(mHandle, &WindowClosed);
	glfwSetWindowMaximizeCallback(mHandle, &WindowMaximized);
	glfwSetFramebufferSizeCallback(mHandle, &WindowSize);
	//glfwSetWindowRefreshCallback(mHandle, &WindowRefresh);
	glfwSetScrollCallback(mHandle, &Scroll);
	glfwSetKeyCallback(mHandle, &Key);
	glfwSetCharCallback(mHandle, &Char);
	glfwSetDropCallback(mHandle, &Drop);
	glfwSetMouseButtonCallback(mHandle, &MouseButton);
	glfwSetWindowFocusCallback(mHandle, &ImGui_ImplGlfw_WindowFocusCallback);
	glfwSetCursorPosCallback(mHandle, &ImGui_ImplGlfw_CursorPosCallback);
	glfwSetCursorEnterCallback(mHandle, &ImGui_ImplGlfw_CursorEnterCallback);
}
