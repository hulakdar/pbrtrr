#include "Render/RenderDebug.h"

#include "Common.cpp"

#include "Render/Private/CommandAllocatorPool.cpp"
#include "Render/Private/CommandListPool.cpp"
#include "System/Private/Window.cpp"
#include "Render/Private/RenderDX12.cpp"
#include "Render/Private/DXGIHelpers.cpp"
#include "Render/Private/UploadDX12.cpp"
#include "System/Private/Gui.cpp"
#include "Render/Private/RenderThread.cpp"
#include "Render/Private/Texture.cpp"
#include "Render/Private/TransientResourcesPool.cpp"

#include "backends/imgui_impl_glfw.cpp"

#include "../pbrtrr/main.cpp"