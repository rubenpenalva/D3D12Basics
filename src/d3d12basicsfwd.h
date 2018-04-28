#pragma once

// c++ includes
#include <memory>
#include <vector>

// d3d12 fwd decls
#include "d3d12fwd.h"

namespace D3D12Render
{
    using D3D12ResourceID           = size_t;
    using D3D12DynamicResourceID    = size_t;

    struct D3D12GpuRenderTask;
    struct D3D12ResourceExt;
    struct D3D12BufferAllocation;

    class D3D12Gpu;
    class D3D12Material;
    class D3D12CBV_SV_UAVDescriptorHeap;
    class D3D12RTVDescriptorHeap;
    class D3D12DSVDescriptorHeap;
    class D3D12SwapChain;
    class D3D12CommittedBufferLoader;
    class D3D12GpuSynchronizer;
    class D3D12CPUDescriptorBuffer;
    class D3D12GPUDescriptorRingBuffer;
    class D3D12BufferAllocator;

    using IDXGIAdapters                     = std::vector<IDXGIAdapterPtr>;
    using D3D12MaterialPtr                  = std::unique_ptr<D3D12Material>;
    using D3D12CBV_SV_UAVDescriptorHeapPtr  = std::unique_ptr<D3D12CBV_SV_UAVDescriptorHeap>;
    using D3D12RTVDescriptorHeapPtr         = std::unique_ptr<D3D12RTVDescriptorHeap>;
    using D3D12DSVDescriptorHeapPtr         = std::unique_ptr<D3D12DSVDescriptorHeap>;
    using D3D12SwapChainPtr                 = std::unique_ptr<D3D12SwapChain>;
    using D3D12CommittedBufferLoaderPtr     = std::unique_ptr<D3D12CommittedBufferLoader>;
    using D3D12GpuRenderTaskPtr             = std::unique_ptr<D3D12GpuRenderTask>;
    using D3D12GpuSynchronizerPtr           = std::unique_ptr<D3D12GpuSynchronizer>;
    using D3D12CPUDescriptorBufferPtr       = std::unique_ptr<D3D12CPUDescriptorBuffer>;
    using D3D12GPUDescriptorRingBufferPtr   = std::unique_ptr<D3D12GPUDescriptorRingBuffer>;
    using D3D12BufferAllocatorPtr           = std::unique_ptr<D3D12BufferAllocator>;
}