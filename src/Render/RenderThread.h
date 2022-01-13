#pragma once

#include "System/Window.h"
#include "Render/Context.h"

void StartRenderThread(System::Window& Window);
void StopRenderThread();

void EnqueueRenderThreadWork(eastl::function<void(RenderContext&)>);
RenderContext& GetRenderContext();
