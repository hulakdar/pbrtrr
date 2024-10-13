#pragma once
#include "Common.h"
#include <thread>

using i64_atomic = std::atomic<i64>;
struct Semaphore
{
    i64_atomic Count;
    u64 Aquire()
    {
        do
        {
            i64 Old = Count.load(std::memory_order_relaxed);
            if (Old)
            {
                Old = Count.fetch_sub(1, std::memory_order_acquire);
                if (Old > 0)
                {
                    return Old - 1;
                }
                else
                {
                    Count.fetch_add(1, std::memory_order_release);
                }
            }
            Sleep(0);
            std::this_thread::yield();
        }
        while (true);
    }

    void Release()
    {
        Count.fetch_add(1, std::memory_order_release);
    }
};