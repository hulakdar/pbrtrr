#include "Window.h"

void System::Callbacks::WindowSize(GLFWwindow* window, int width, int height)
{
	Window *user_this = (Window*)glfwGetWindowUserPointer(window);

	if ((int)user_this->mSize.x != width || (int)user_this->mSize.y != height)
	{
		user_this->mSize = Vector2(width, height);
		user_this->mViewport = CD3DX12_VIEWPORT(0.f, 0.f, user_this->mSize.x, user_this->mSize.y);

		user_this->mWindowStateDirty = true;
	}
}

void System::Callbacks::Scroll(GLFWwindow* window, double xoffset, double yoffset)
{
	Window *user_this = (Window*)glfwGetWindowUserPointer(window);

	user_this->mScrollOffset = Vector2(xoffset, yoffset);
}

void System::Callbacks::Key(GLFWwindow* window, int key, int scancode, int action, int mods)
{
	Window *user_this = (Window*)glfwGetWindowUserPointer(window);

	user_this->mKeyboard[key] = true;
}

void System::Callbacks::Drop(GLFWwindow* window, int path_count, const char* paths[])
{
	Window *user_this = (Window*)glfwGetWindowUserPointer(window);

	for (int i = 0; i < path_count; ++i)
	{
		user_this->mDroppedPaths.emplace_back(paths[i]);
	}
}

void System::Callbacks::MouseButton(GLFWwindow* window, int button, int action, int mods)
{
	Window *user_this = (Window*)glfwGetWindowUserPointer(window);

	user_this->mMouseButtons[button] = action;
}
