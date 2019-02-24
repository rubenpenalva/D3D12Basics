#include "d3d12gpu_sync.h"

// project includes
#include "utils.h"

// c++ includes
#include <cassert>
#include <sstream>

using namespace D3D12Basics;

D3D12GpuSynchronizer::D3D12GpuSynchronizer(ID3D12DevicePtr device, ID3D12CommandQueuePtr cmdQueue,
                                           unsigned int maxFramesInFlight,
                                           StopClock& waitClock)  : m_cmdQueue(cmdQueue), 
                                                                    m_maxFramesInFlight(maxFramesInFlight),
                                                                    m_currentFrameIndex(0),
                                                                    m_currentFenceValue(0),
                                                                    m_waitedFenceValue(0),
                                                                    m_nextFenceValue(0),
                                                                    m_waitClock(waitClock)
{
    assert(device);
    assert(m_cmdQueue);

    m_fences.resize(m_maxFramesInFlight);
    m_events.resize(m_maxFramesInFlight);
    for (unsigned int i = 0; i < m_maxFramesInFlight; ++i)
    {
        AssertIfFailed(device->CreateFence(m_currentFenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fences[i])));
        assert(m_fences[i]);

        m_events[i] = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        assert(m_events[i]);
        if (!m_events[i])
            AssertIfFailed(HRESULT_FROM_WIN32(GetLastError()));
    }

    m_nextFenceValue = m_currentFenceValue + 1;
}

D3D12GpuSynchronizer::~D3D12GpuSynchronizer()
{
    for (auto& event : m_events)
        CloseHandle(event);
}

bool D3D12GpuSynchronizer::Wait()
{
#if ENABLE_GPU_SYNC_DEBUG_OUTPUT
    m_debugReportStream << "D3D12GpuSynchronizer 0x" << std::ios::hex << this << L"\nD3D12GpuSynchronizer::Wait()\n";
#endif

    SignalWork();

    bool hasWaitedForFence = false;

#if ENABLE_GPU_SYNC_DEBUG_OUTPUT
    m_debugReportStream << " m_maxFramesInFlight " << m_maxFramesInFlight << "\n"
                        << " PRE m_waitedFenceValue " << m_waitedFenceValue << "\n";
#endif

    m_waitedFenceValue = m_fences[m_currentFrameIndex]->GetCompletedValue();
    assert(m_currentFenceValue >= m_waitedFenceValue);
    const auto framesInFlight = m_currentFenceValue - m_waitedFenceValue;

#if ENABLE_GPU_SYNC_DEBUG_OUTPUT
    m_debugReportStream << "POST m_waitedFenceValue " << m_waitedFenceValue << "\n" 
                        << " framesInFlight " << framesInFlight << "\n";
#endif

    if (framesInFlight == m_maxFramesInFlight)
    {
#if ENABLE_GPU_SYNC_DEBUG_OUTPUT
        m_debugReportStream << "+[COND] m_framesSignaled == m_maxFramesInFlight\n";
#endif

        WaitForFence(m_waitedFenceValue + 1);
        hasWaitedForFence = true;
    }

#if ENABLE_GPU_SYNC_DEBUG_OUTPUT
    m_debugReportStream << L"-------------\n-------------\n";
    OutputDebugString(m_debugReportStream.str().c_str());
    m_debugReportStream = std::wstringstream();
#endif

    NextFrame();

    return hasWaitedForFence;
}

void D3D12GpuSynchronizer::WaitAll()
{
#if ENABLE_GPU_SYNC_DEBUG_OUTPUT
    m_debugReportStream << L"D3D12GpuSynchronizer 0x " << std::ios::hex << this
                        << L" D3D12GpuSynchronizer::WaitAll()\n m_maxFramesInFlight "
                        << m_maxFramesInFlight << "\n";
#endif

    SignalWork();

    WaitForFence(m_currentFenceValue);

#if ENABLE_GPU_SYNC_DEBUG_OUTPUT
    m_debugReportStream << L"-------------\n-------------\n";
    OutputDebugString(m_debugReportStream.str().c_str());
    m_debugReportStream = std::wstringstream();
#endif

    NextFrame();
}

void D3D12GpuSynchronizer::SignalWork()
{
#if ENABLE_GPU_SYNC_DEBUG_OUTPUT
    m_debugReportStream << "D3D12GpuSynchronizer::SignalWork() with fence " << m_nextFenceValue << "\n"
                        << " PRE m_nextFenceValue " << m_nextFenceValue
                        << " PRE m_currentFenceValue " << m_currentFenceValue << "\n";
#endif

    AssertIfFailed(m_cmdQueue->Signal(m_fences[m_currentFrameIndex].Get(), m_nextFenceValue));
    m_currentFenceValue = m_nextFenceValue++;

#if ENABLE_GPU_SYNC_DEBUG_OUTPUT
    m_debugReportStream << " POST m_nextFenceValue " << m_nextFenceValue
                        << " POST m_currentFenceValue " << m_currentFenceValue << "\n";
#endif
}

void D3D12GpuSynchronizer::WaitForFence(UINT64 fenceValue)
{
#if ENABLE_GPU_SYNC_DEBUG_OUTPUT
    m_debugReportStream << "D3D12GpuSynchronizer::WaitForFence() fence " << fenceValue
                        << " PRE m_waitedFenceValue " << m_waitedFenceValue << "\n";
#endif

    m_waitedFenceValue = fenceValue;

#if ENABLE_GPU_SYNC_DEBUG_OUTPUT
    m_debugReportStream << " POST m_waitedFenceValue " << m_waitedFenceValue << "\n";
#endif

    m_waitClock.ResetMark();
    m_waitClock.Mark();

#if ENABLE_GPU_SYNC_DEBUG_OUTPUT
    OutputDebugString(m_debugReportStream.str().c_str());
    m_debugReportStream = std::wstringstream();
#endif

    AssertIfFailed(m_fences[m_currentFrameIndex]->SetEventOnCompletion(fenceValue, m_events[m_currentFrameIndex]));
    AssertIfFailed(WaitForSingleObject(m_events[m_currentFrameIndex], INFINITE), WAIT_FAILED);
    m_waitClock.Mark();

#if ENABLE_GPU_SYNC_DEBUG_OUTPUT
    m_debugReportStream << " waited time " << m_waitClock.AverageSplitTime() << "\n";
#endif
}

void D3D12GpuSynchronizer::NextFrame()
{
    m_currentFrameIndex = (m_currentFrameIndex + 1) % m_maxFramesInFlight;
}
