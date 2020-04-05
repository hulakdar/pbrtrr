#pragma once

#include "Util/Math.hpp"

#include "external/d3dx12.h"
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <assert.h>

namespace System
{

class CWindow
{
public:
	void Init()
	{
		assert(glfwInit());
		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

		Handle = glfwCreateWindow(Size.x, Size.y, "pbrtrr", nullptr, nullptr);
	}

	void Update()
	{
		glfwPollEvents();
		if (glfwGetKey(Handle, GLFW_KEY_ESCAPE) == GLFW_PRESS)
		{
			glfwSetWindowShouldClose(Handle, GLFW_TRUE);
		}
	}

	IVector2	Size{ 1280, 720 };
	 
	D3D12_VIEWPORT	Viewport{ 0.f, 0.f, (float)Size.x, (float)Size.y, 0.f, 1.f };
	D3D12_RECT		ScissorRect{ 0, 0, LONG_MAX, LONG_MAX };
	GLFWwindow *Handle = nullptr;
	HWND GetHWND() { return glfwGetWin32Window(Handle); }
};

}
