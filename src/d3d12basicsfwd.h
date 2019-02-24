#pragma once

// c++ includes
#include <memory>
#include <vector>

// d3d12 fwd decls
#include "d3d12fwd.h"

namespace enki
{
    class TaskSet;
}

namespace D3D12Basics
{
    struct D3D12GpuRenderTask;
    struct D3D12DynamicBufferAllocation;
    
    class CustomWindow;

    class D3D12Gpu;
    class D3D12Material;
    class D3D12DSVDescriptorPool;
    class D3D12SwapChain;
    class D3D12GpuSynchronizer;
    class D3D12CBV_SRV_UAVDescriptorBuffer;
    class D3D12RTVDescriptorBuffer;
    class D3D12GPUDescriptorRingBuffer;
    class D3D12DynamicBufferAllocator;
    class D3D12CommittedResourceAllocator;
    class D3D12ImGui;
    class D3D12SceneRender;

    using CustomWindowPtr                       = std::unique_ptr<CustomWindow>;
    using IDXGIAdapters                         = std::vector<IDXGIAdapterPtr>;
    using D3D12MaterialPtr                      = std::unique_ptr<D3D12Material>;
    using D3D12DSVDescriptorPoolPtr             = std::unique_ptr<D3D12DSVDescriptorPool>;
    using D3D12SwapChainPtr                     = std::unique_ptr<D3D12SwapChain>;
    using D3D12GpuRenderTaskPtr                 = std::unique_ptr<D3D12GpuRenderTask>;
    using D3D12GpuSynchronizerPtr               = std::unique_ptr<D3D12GpuSynchronizer>;
    using D3D12CBV_SRV_UAVDescriptorBufferPtr   = std::unique_ptr<D3D12CBV_SRV_UAVDescriptorBuffer>;
    using D3D12RTVDescriptorBufferPtr           = std::unique_ptr<D3D12RTVDescriptorBuffer>;
    using D3D12GPUDescriptorRingBufferPtr       = std::unique_ptr<D3D12GPUDescriptorRingBuffer>;
    using D3D12DynamicBufferAllocatorPtr        = std::unique_ptr<D3D12DynamicBufferAllocator>;
    using D3D12CommittedResourceAllocatorPtr    = std::unique_ptr<D3D12CommittedResourceAllocator>;
    using D3D12ImGuiPtr                         = std::unique_ptr<D3D12ImGui>;
    using D3D12SceneRenderPtr                   = std::unique_ptr<D3D12SceneRender>;
    using TaskSetPtr                            = std::unique_ptr<enki::TaskSet>;
}