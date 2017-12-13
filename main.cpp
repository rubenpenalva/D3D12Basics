/// Copyright (c) 2016 Ruben Penalva Ambrona. All rights reserved.
/// Redistribution and use in source and binary forms, with or without modification, 
/// are permitted provided that the following conditions are met:
///    1. Redistributions of source code must retain the above copyright notice, this 
///    list of conditions and the following disclaimer.
///    2. Redistributions in binary form must reproduce the above copyright notice,
///    this list of conditions and the following disclaimer in the documentation and/or
///    other materials provided with the distribution.
///    3. The name of the author may not be used to endorse or promote products derived
///    from this software without specific prior written permission.
///
/// THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
/// INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR 
/// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, 
/// INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED 
/// TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
/// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT 
/// LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF 
/// THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// app includes
#include "utils.h"
#include "d3d12gpus.h"

//c includes
#include <cassert>

// c++ includes
//#include <string>
//#include <array>
//
//// direct3d includes

//#include "d3dx12.h"
//

// windows includes
#include <windows.h>
#include <d3d12.h>

namespace
{
    class D3D12GpuJob : public D3D12Render::ID3D12GpuJob
    {
    public:
        D3D12GpuJob(D3D12Render::ID3D12GraphicsCommandListPtr commandList) : ID3D12GpuJob(commandList)
        {

        }

        void D3D12GpuJob::Record(D3D12_CPU_DESCRIPTOR_HANDLE backbufferRT)
        {
            const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
            m_commandList->ClearRenderTargetView(backbufferRT, clearColor, 0, nullptr);
        }
    };
// 


//    struct FrameResources
//    {
//        ID3D12ResourcePtr m_renderTarget;
//        ID3D12CommandAllocatorPtr m_commandAllocator;
//    };
//

//    const uint8_t g_backBuffersCount = 2;
//
//    typedef std::array<FrameResources, g_backBuffersCount> FrameResouresArray;
//





//    ID3D12DescriptorHeapPtr CreateRTVDescriptorHeap(ID3D12Device* device)
//    {
//        // Describe and create a render target view (RTV) descriptor heap.
//        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
//        rtvHeapDesc.NumDescriptors = g_backBuffersCount;
//        rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
//        rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
//        
//        ID3D12DescriptorHeapPtr rtvDescriptorHeap;
//        AssertIfFailed(device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&rtvDescriptorHeap)));
//
//        return rtvDescriptorHeap;
//    }
//
//    void CreateFrameResources(FrameResouresArray& frameResourcesArray, unsigned int rtvDescriptorSize, ID3D12DescriptorHeap* rtvDescriptorHeap, IDXGISwapChain3* swapChain, ID3D12Device* device)
//    {
//        assert(rtvDescriptorHeap && swapChain && device);
//
//        CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(rtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
//
//        FrameResources frameResources;
//
//        // Create a RTV and a command allocator for each frame.
//        for (uint8_t i = 0; i < g_backBuffersCount; ++i)
//        {
//            FrameResources& frameResources = frameResourcesArray[i];
//
//            AssertIfFailed(swapChain->GetBuffer(i, IID_PPV_ARGS(&frameResources.m_renderTarget)));
//            device->CreateRenderTargetView(frameResources.m_renderTarget.Get(), nullptr, rtvHandle);
//            rtvHandle.Offset(1, rtvDescriptorSize);
//
//            AssertIfFailed(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&frameResources.m_commandAllocator)));
//        }
//    }

}

int WINAPI WinMain(HINSTANCE /*hInstance*/, HINSTANCE /*hPrevInstance*/, LPSTR /*szCmdLine*/, int /*iCmdShow*/)
{
    //float elapsedTime = 0.0f;

    Utils::CustomWindow customWindow;
    
    D3D12Render::D3D12Gpus gpus;
    assert(gpus.Count());

    const D3D12Render::D3D12Gpus::GpuID mainGpuID = 0;
    D3D12Render::D3D12GpuPtr gpu = gpus.CreateGpu(mainGpuID, customWindow.GetHWND());
    D3D12Render::ID3D12GpuJobPtr gpuJob = std::make_shared<D3D12GpuJob>(gpu->GetCommandList());
    gpu->SetJob(gpuJob);

    // Main loop
    while (1)
    {
        MSG msg = {};
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
            {
                break;
            }
            else
            {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }
        else
        {
            // Record the command list
            {

            }

            // Execute command list
            {
                gpu->Execute();
            }

            // Present
            {
                gpu->Flush();
            }

            // Next frame
            {

            }
        }
    }

    

    return 0;
}