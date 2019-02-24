#pragma once

// project includes
#include "d3d12basicsfwd.h"
#include "utils.h"

// windows includes
#include <windows.h>

// directx includes
#include <d3d12.h>

#define ENABLE_GPU_SYNC_DEBUG_OUTPUT (0)

#if ENABLE_GPU_SYNC_DEBUG_OUTPUT
#include <sstream>
#endif

namespace D3D12Basics
{
    class D3D12GpuSynchronizer
    {
    public:
        D3D12GpuSynchronizer(ID3D12DevicePtr device, ID3D12CommandQueuePtr cmdQueue, 
                             unsigned int maxFramesInFlight, StopClock& waitClock);
        ~D3D12GpuSynchronizer();

        bool Wait();

        void WaitAll();

        uint64_t GetLastRetiredFrameId() const { return m_waitedFenceValue; }

        uint64_t GetNextFrameId() const { return m_nextFenceValue; }

    private:
        ID3D12CommandQueuePtr m_cmdQueue;

        const uint64_t  m_maxFramesInFlight;

        std::vector <HANDLE>        m_events;
        std::vector<ID3D12FencePtr> m_fences;
        unsigned int                m_currentFrameIndex;
        UINT64          m_waitedFenceValue;
        UINT64          m_currentFenceValue;
        UINT64          m_nextFenceValue;

#if ENABLE_GPU_SYNC_DEBUG_OUTPUT
        std::wstringstream m_debugReportStream;
#endif
        StopClock&    m_waitClock;

        void SignalWork();

        void WaitForFence(UINT64 fenceValue);

        void NextFrame();
    };
}