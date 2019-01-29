#include "d3d12swapchain.h"

// Project includes
#include "utils.h"
#include "d3d12descriptorheap.h"

// directx includes
#include <dxgi1_4.h>
#include <d3d12.h>

// c++ includes
#include <sstream>

using namespace D3D12Basics;

D3D12SwapChain::D3D12SwapChain(HWND hwnd, DXGI_FORMAT format, 
                               const D3D12Basics::Resolution& resolution,
                               IDXGIFactoryPtr factory, ID3D12DevicePtr device,
                               ID3D12CommandQueuePtr commandQueue,
                               StopClock& presentClock, StopClock& waitForPresentClock,
                               bool waitForPresentEnabled)  :   m_device(device),
                                                                m_descriptorPool(device, D3D12GpuConfig::m_backBuffersCount),
                                                                m_resolution(resolution),
                                                                m_presentClock(presentClock), 
                                                                m_waitForPresentClock(waitForPresentClock),
                                                                m_waitForPresentEnabled(waitForPresentEnabled)
{
    assert(factory);
    assert(device);
    assert(commandQueue);

    // NOTE: not sRGB format available directly for Swap Chain back buffer
    //       Create with UNORM format and use a sRGB Render Target View.
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.Width                 = resolution.m_width;
    swapChainDesc.Height                = resolution.m_height;
    swapChainDesc.Format                = format;
    swapChainDesc.Stereo                = FALSE;
    swapChainDesc.SampleDesc.Count      = 1;
    swapChainDesc.SampleDesc.Quality    = 0;
    swapChainDesc.BufferUsage           = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.BufferCount           = D3D12GpuConfig::m_backBuffersCount;
    swapChainDesc.Scaling               = DXGI_SCALING_STRETCH;
    swapChainDesc.SwapEffect            = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.AlphaMode             = DXGI_ALPHA_MODE_UNSPECIFIED;
    swapChainDesc.Flags                 = m_waitForPresentEnabled? DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT : 0;
        
    // Swap chain needs the queue so that it can force a flush on it.
    Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain1;
    AssertIfFailed(factory->CreateSwapChainForHwnd(commandQueue.Get(), hwnd, &swapChainDesc, nullptr, nullptr, &swapChain1));
    AssertIfFailed(swapChain1.As(&m_swapChain));
    assert(m_swapChain);

    if (m_waitForPresentEnabled)
    {
        AssertIfFailed(m_swapChain->SetMaximumFrameLatency(D3D12GpuConfig::m_framesInFlight));

        m_frameLatencyWaitableObject = m_swapChain->GetFrameLatencyWaitableObject();
        assert(m_frameLatencyWaitableObject);
    }
    
    for (size_t i = 0; i < 2; ++i)
        m_backbuffersRTVHandles[i] = nullptr;

    CreateBackBuffers();
}

D3D12SwapChain::~D3D12SwapChain()
{
    AssertIfFailed(m_swapChain->SetFullscreenState(FALSE, nullptr));
}

// TODO switch to Present1
HRESULT D3D12SwapChain::Present(bool vsync)
{ 
    m_presentClock.Mark();
    HRESULT result = m_swapChain->Present(vsync ? 1 : 0, 0);

    return result;
}

void D3D12SwapChain::WaitForPresent()
{
    m_waitForPresentClock.Mark();
    AssertIfFailed(WaitForSingleObject(m_frameLatencyWaitableObject, INFINITE), WAIT_FAILED);
}

void D3D12SwapChain::ToggleFullScreen()
{
    BOOL fullscreenState;
    AssertIfFailed(m_swapChain->GetFullscreenState(&fullscreenState, nullptr));

    AssertIfFailed(m_swapChain->SetFullscreenState(!fullscreenState, nullptr));

    // Update resolution information
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc;
    AssertIfFailed(m_swapChain->GetDesc1(&swapChainDesc));
    m_resolution = { swapChainDesc.Width, swapChainDesc.Height };
}

D3D12_RESOURCE_BARRIER& D3D12SwapChain::Transition(TransitionType transitionType)
{
    const auto currentBackBufferIndex = m_swapChain->GetCurrentBackBufferIndex();
    return m_transitions[transitionType][currentBackBufferIndex];
}

const D3D12_CPU_DESCRIPTOR_HANDLE& D3D12SwapChain::RTV() const
{
    const auto currentBackBufferIndex = m_swapChain->GetCurrentBackBufferIndex();
    return m_backbuffersRTVHandles[currentBackBufferIndex]->m_cpuHandle;
}

void D3D12SwapChain::Resize(const DXGI_MODE_DESC1& mode)
{
    if (mode.Width == m_resolution.m_width &&
        mode.Height == m_resolution.m_height)
        return;

    for (unsigned int i = 0; i < D3D12GpuConfig::m_backBuffersCount; ++i)
        m_backbufferResources[i] = nullptr;
    
    AssertIfFailed(m_swapChain->ResizeTarget(reinterpret_cast<const DXGI_MODE_DESC*>(&mode)));

    AssertIfFailed(m_swapChain->ResizeBuffers(D3D12GpuConfig::m_backBuffersCount, mode.Width, mode.Height, mode.Format,
                                              m_waitForPresentEnabled? DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT : 0));

    UpdateBackBuffers();

    m_resolution = { mode.Width, mode.Height };
}

void D3D12SwapChain::CreateBackBuffers()
{
    for (unsigned int i = 0; i < D3D12GpuConfig::m_backBuffersCount; ++i)
    {
        AssertIfFailed(m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_backbufferResources[i])));
        std::wstringstream converter;
        converter << L"Back buffer " << i;
        m_backbufferResources[i]->SetName(converter.str().c_str());

        m_backbuffersRTVHandles[i] = m_descriptorPool.CreateRTV(m_backbufferResources[i].Get(), m_backbuffersRTVHandles[i]);
        assert(m_backbuffersRTVHandles[i]);

        // Create the transitions
        m_transitions[Present_To_RenderTarget][i].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        m_transitions[Present_To_RenderTarget][i].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        m_transitions[Present_To_RenderTarget][i].Transition.pResource = m_backbufferResources[i].Get();
        m_transitions[Present_To_RenderTarget][i].Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        m_transitions[Present_To_RenderTarget][i].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        m_transitions[Present_To_RenderTarget][i].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

        m_transitions[RenderTarget_To_Present][i] = m_transitions[Present_To_RenderTarget][i];
        m_transitions[RenderTarget_To_Present][i].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        m_transitions[RenderTarget_To_Present][i].Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    }
}

void D3D12SwapChain::UpdateBackBuffers()
{
    for (unsigned int i = 0; i < D3D12GpuConfig::m_backBuffersCount; ++i)
    {
        AssertIfFailed(m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_backbufferResources[i])));
        std::wstringstream converter;
        converter << L"Back buffer " << i;
        m_backbufferResources[i]->SetName(converter.str().c_str());

        m_backbuffersRTVHandles[i] = m_descriptorPool.CreateRTV(m_backbufferResources[i].Get(), m_backbuffersRTVHandles[i]);
        assert(m_backbuffersRTVHandles[i]);

        // Update backbuffer resources
        m_transitions[Present_To_RenderTarget][i].Transition.pResource = m_backbufferResources[i].Get();
        m_transitions[RenderTarget_To_Present][i].Transition.pResource = m_backbufferResources[i].Get();
    }
}