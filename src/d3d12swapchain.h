#pragma once

// d3d12 fwd decl
#include "d3d12fwd.h"
#include <d3d12.h>
#include <windows.h>

// project includes
#include "d3d12basicsfwd.h"
#include "utils.h"

namespace D3D12Render
{
    class D3D12SwapChain
    {
    public:
        enum TransitionType
        {
            Present_To_RenderTarget,
            RenderTarget_To_Present,
            TransitionType_COUNT
        };

        static const unsigned int BackBuffersCount = 2;

        D3D12SwapChain(HWND hwnd, DXGI_FORMAT format, const D3D12Basics::Resolution& resolution, 
                       IDXGIFactory4Ptr factory, ID3D12DevicePtr device, 
                       ID3D12CommandQueuePtr commandQueue, D3D12RTVDescriptorHeapPtr descriptorHeap);

        ~D3D12SwapChain();

        // TODO use Present1?
        HRESULT Present();

        void ToggleFullScreen();

        unsigned int GetCurrentBackBufferIndex() const;

        D3D12_RESOURCE_BARRIER& Transition(unsigned int backBufferIndex, TransitionType transitionType);

        D3D12_CPU_DESCRIPTOR_HANDLE RTV(unsigned int backBufferIndex);

        void Resize(const DXGI_MODE_DESC1& mode);

        const D3D12Basics::Resolution& GetCurrentResolution() const { return m_resolution; }
    private:
        ID3D12DevicePtr m_device;

        D3D12Basics::Resolution m_resolution;

        IDXGISwapChain3Ptr m_swapChain;

        D3D12RTVDescriptorHeapPtr m_descriptorHeap;
        
        D3D12DescriptorID m_backbuffersDescIDs[BackBuffersCount];

        ID3D12ResourcePtr m_bacbufferResources[BackBuffersCount];

        D3D12_RESOURCE_BARRIER m_transitions[TransitionType_COUNT][BackBuffersCount];

        void CreateBackBuffers();

        void UpdateBackBuffers();
    };

}