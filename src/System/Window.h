#pragma once

#include "Util/Math.hpp"

#include "external/d3dx12.h"
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <Util\Util.h>

#define VALIDATE_GLFW_CALL(x) if (!(x)) { const char* Error; glfwGetError(&Error); Print("Error during:", #x, Error); }

namespace System
{

class CWindow
{
public:
	void Init()
	{
		VALIDATE_GLFW_CALL(glfwInit());

		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

		VALIDATE_GLFW_CALL(Handle = glfwCreateWindow(Size.x, Size.y, "pbrtrr", nullptr, nullptr));
		CHECK(Handle != nullptr, "GLFW failed to create a window.");

		VALIDATE_GLFW_CALL(Hwnd = glfwGetWin32Window(Handle));
		CHECK(Hwnd != nullptr, "Couldn't get HWND from GLFW window.");
	}

	void Update()
	{
		glfwPollEvents();
		if (glfwGetKey(Handle, GLFW_KEY_ESCAPE) == GLFW_PRESS)
		{
			glfwSetWindowShouldClose(Handle, GLFW_TRUE);
		}
	}

	IVector2	Size{ 720, 720 };
	 
	CD3DX12_VIEWPORT	Viewport = CD3DX12_VIEWPORT(0.f, 0.f, (float)Size.x, (float)Size.y);
	CD3DX12_RECT		ScissorRect = CD3DX12_RECT(0, 0, LONG_MAX, LONG_MAX);
	HWND				Hwnd = nullptr;
	GLFWwindow			*Handle = nullptr;
};

}
