#include "d3d12gpu_sync.h"

// project includes
#include "utils.h"

// c++ includes
#include <cassert>

using namespace D3D12Basics;

D3D12GpuSynchronizer::D3D12GpuSynchronizer(ID3D12DevicePtr device, ID3D12CommandQueuePtr cmdQueue,
                                           unsigned int maxFramesInFlight)  :   m_cmdQueue(cmdQueue), m_maxFramesInFlight(maxFramesInFlight),
                                                                                m_framesInFlight(0), m_currentFenceValue(0), m_completedFramesCount(0),
                                                                                m_lastRetiredFrameId(0)
{
    assert(device);
    assert(m_cmdQueue);

    AssertIfFailed(device->CreateFence(m_currentFenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
    assert(m_fence);
    m_nextFenceValue = m_currentFenceValue + 1;

    // Create an event handle to use for frame synchronization.
    m_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    assert(m_event);
    if (!m_event)
        AssertIfFailed(HRESULT_FROM_WIN32(GetLastError()));
}

D3D12GpuSynchronizer::~D3D12GpuSynchronizer()
{
    CloseHandle(m_event);
}

bool D3D12GpuSynchronizer::Wait()
{
    bool hasWaitedForFence = false;
    m_completedFramesCount = 0;
    m_waitTime = 0;

    SignalWork();

    m_lastRetiredFrameId = m_fence->GetCompletedValue();

    if (m_framesInFlight == m_maxFramesInFlight)
    {
        assert(m_lastRetiredFrameId <= m_currentFenceValue);
        if (m_lastRetiredFrameId < m_currentFenceValue)
        {
            WaitForFence(m_lastRetiredFrameId + 1);
            m_lastRetiredFrameId++;
            hasWaitedForFence = true;
        }

        m_completedFramesCount = m_lastRetiredFrameId - m_currentFenceValue + m_maxFramesInFlight;
        assert(m_maxFramesInFlight >= m_completedFramesCount);
        m_framesInFlight -= m_completedFramesCount;
    }

    return hasWaitedForFence;
}

void D3D12GpuSynchronizer::WaitAll()
{
    SignalWork();

    WaitForFence(m_currentFenceValue);

    m_framesInFlight = 0;
}

void D3D12GpuSynchronizer::SignalWork()
{
    AssertIfFailed(m_cmdQueue->Signal(m_fence.Get(), m_nextFenceValue));
    m_currentFenceValue = m_nextFenceValue;
    m_nextFenceValue++;

    m_framesInFlight++;
    assert(m_framesInFlight <= m_maxFramesInFlight);
}

void D3D12GpuSynchronizer::WaitForFence(UINT64 fenceValue)
{
    Timer timer;
    AssertIfFailed(m_fence->SetEventOnCompletion(fenceValue, m_event));
    WaitForSingleObject(m_event, INFINITE);
    timer.Mark();
    m_waitTime = timer.ElapsedTime();
}