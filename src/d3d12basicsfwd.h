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

    class D3D12Gpu;
    class ID3D12GpuJob;
    class D3D12SimpleMaterial;
    class D3D12DescriptorHeap;
    
    using IDXGIAdapters             = std::vector<IDXGIAdapterPtr>;
    using D3D12GpuPtr               = std::shared_ptr<D3D12Gpu>;
    using ID3D12GpuJobPtr           = std::shared_ptr<ID3D12GpuJob>;
    using D3D12SimpleMaterialPtr    = std::shared_ptr<D3D12SimpleMaterial>;
    using D3D12DescriptorHeapPtr    = std::shared_ptr<D3D12DescriptorHeap>;
}