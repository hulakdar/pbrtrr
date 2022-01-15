#include "System/Window.h"

void System::Callbacks::Error(int error, const char* description)
{
	Debug::Print(error, ": ", description);
}

void System::Callbacks::WindowSize(GLFWwindow* window, int width, int height)
{
	Window *user_this = (Window*)glfwGetWindowUserPointer(window);

	if (user_this->mSize.x != width || user_this->mSize.y != height)
	{
		user_this->mSize = IVector2{ width, height };
		user_this->mWindowStateDirty = true;
	}
}

void System::Callbacks::Scroll(GLFWwindow* window, double xoffset, double yoffset)
{
	Window *user_this = (Window*)glfwGetWindowUserPointer(window);

	user_this->mScrollOffset = Vector2{ (float)xoffset, (float)yoffset };
}

void System::Callbacks::Key(GLFWwindow* window, int key, int /*scancode*/, int /*action*/, int /*mods*/)
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

void System::Callbacks::MouseButton(GLFWwindow* window, int button, int action, int /*mods*/)
{
	Window *user_this = (Window*)glfwGetWindowUserPointer(window);

	user_this->mMouseButtons[button] = action;
}
