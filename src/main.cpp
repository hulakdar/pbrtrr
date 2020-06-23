#define WIN32_LEAN_AND_MEAN
#include <d3dcompiler.h>

#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <d3dcompiler.h>
#include <directxmath.h>
#include "external/d3dx12.h"
#include "Util/Util.h"
#include "System/Window.h"

int main(void)
{
	EnableDebug();

	CHECK(DirectX::XMVerifyCPUSupport());

	System::CWindow Window;

    //Main message loop
    while(!glfwWindowShouldClose(Window.Handle))
	{
    }

	glfwTerminate();
}

