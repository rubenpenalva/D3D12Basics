#pragma once

// d3d12 fwd decl
#include "d3d12fwd.h"
#include <d3d12.h>
#include <windows.h>

// project includes
#include "d3d12basicsfwd.h"
#include "utils.h"
// TODO needed because D3D12GpuConfig::m_backBuffersCount
//      find a better way to expose the static parameters
//      for frames in flight and backbuffers count
//      without having to add dependencies.
#include "d3d12gpu.h" 

namespace D3D12Basics
{
    class D3D12SwapChain
    {
    public:
        D3D12SwapChain(HWND hwnd, DXGI_FORMAT format, const D3D12Basics::Resolution& resolution, 
                       IDXGIFactoryPtr factory, ID3D12DevicePtr device, 
                       ID3D12CommandQueuePtr commandQueue, 
                       StopClock& presentClock, StopClock& waitForPresentClock,
                       bool waitForPresentEnabled = false);

        ~D3D12SwapChain();

        // TODO use Present1?
        HRESULT Present(bool vsync);

        void WaitForPresent();

        void ToggleFullScreen();

        D3D12_RESOURCE_BARRIER& Transition(TransitionType transitionType);

        const D3D12_CPU_DESCRIPTOR_HANDLE& RTV() const;

        void Resize(const DXGI_MODE_DESC1& mode);

        const D3D12Basics::Resolution& GetCurrentResolution() const { return m_resolution; }

    private:
        ID3D12DevicePtr m_device;
        
        D3D12RTVDescriptorPool m_descriptorPool;

        D3D12Basics::Resolution m_resolution;

        IDXGISwapChainPtr m_swapChain;
        
        D3D12DescriptorAllocation* m_backbuffersRTVHandles[D3D12GpuConfig::m_backBuffersCount];

        ID3D12ResourcePtr m_backbufferResources[D3D12GpuConfig::m_backBuffersCount];

        D3D12_RESOURCE_BARRIER m_transitions[TransitionType_COUNT][D3D12GpuConfig::m_backBuffersCount];

        D3D12Basics::StopClock& m_presentClock;
        D3D12Basics::StopClock& m_waitForPresentClock;

        bool    m_waitForPresentEnabled;
        HANDLE  m_frameLatencyWaitableObject;

        void CreateBackBuffers();

        void UpdateBackBuffers();
    };

}