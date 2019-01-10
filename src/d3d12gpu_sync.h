#pragma once

// project includes
#include "d3d12basicsfwd.h"
#include "utils.h"

// windows includes
#include <windows.h>

// directx includes
#include <d3d12.h>

namespace D3D12Basics
{
    // TODO move this class to d3d12gpu.h/cpp files
    class D3D12GpuSynchronizer
    {
    public:
        D3D12GpuSynchronizer(ID3D12DevicePtr device, ID3D12CommandQueuePtr cmdQueue, unsigned int maxFramesInFlight,
                             StopClock& waitClock);
        ~D3D12GpuSynchronizer();

        bool Wait();

        void WaitAll();

        uint64_t GetLastRetiredFrameId() const { return m_lastRetiredFrameId; }

        uint64_t GetNextFrameId() const { return m_nextFenceValue; }

    private:
        ID3D12CommandQueuePtr m_cmdQueue;

        const uint64_t      m_maxFramesInFlight;
        uint64_t            m_framesInFlight;
        uint64_t            m_completedFramesCount;
        uint64_t            m_lastRetiredFrameId;

        HANDLE          m_event;
        ID3D12FencePtr  m_fence;
        UINT64          m_currentFenceValue;
        UINT64          m_nextFenceValue;

        StopClock&    m_waitClock;

        void SignalWork();

        void WaitForFence(UINT64 fenceValue);
    };
}