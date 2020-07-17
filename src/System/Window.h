#pragma once

#include "Util/Math.hpp"

#include "external/d3dx12.h"
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <Util/Debug.h>

#define VALIDATE_GLFW_CALL(x) if (!(x)) { const char* Error; glfwGetError(&Error); Debug::Print("Error during:", #x, Error); }

namespace System
{

class Window
{
public:
	void Init()
	{
		VALIDATE_GLFW_CALL(glfwInit());

		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

		VALIDATE_GLFW_CALL(mHandle = glfwCreateWindow(mSize.x, mSize.y, "pbrtrr", nullptr, nullptr));
		CHECK(mHandle != nullptr, "GLFW failed to create a window.");

		VALIDATE_GLFW_CALL(mHwnd = glfwGetWin32Window(mHandle));
		CHECK(mHwnd != nullptr, "Couldn't get HWND from GLFW window.");
	}

	void Update()
	{
		glfwPollEvents();
		if (glfwGetKey(mHandle, GLFW_KEY_ESCAPE) == GLFW_PRESS)
		{
			glfwSetWindowShouldClose(mHandle, GLFW_TRUE);
		}
	}

	IVector2			mSize{ 720, 720 };
	 
	CD3DX12_VIEWPORT	mViewport = CD3DX12_VIEWPORT(0.f, 0.f, (float)mSize.x, (float)mSize.y);
	CD3DX12_RECT		mScissorRect = CD3DX12_RECT(0, 0, LONG_MAX, LONG_MAX);
	HWND				mHwnd = nullptr;
	GLFWwindow			*mHandle = nullptr;
};

}
