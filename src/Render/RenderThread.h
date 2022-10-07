#pragma once

#include <Threading/DedicatedThread.h>

void StartRenderThread();
void StopRenderThread();

Thread::id GetRenderThreadID();

u64 GetCurrentFrameID();
void NotifyCompletedFrameFence(u64 FrameID);
void StartRenderThreadFrame(u64 FrameID);
void EndRenderThreadFrame();

void EnqueueToRenderThread(TFunction<void(void)>&&);
TicketCPU EnqueueToRenderThreadWithTicket(TFunction<void(void)>&& RenderThreadWork);

struct TicketGPU;

void EnqueueDelayedWork(TFunction<void(void)>&& Work, const TicketGPU& Ticket);
void RunDelayedWork();
