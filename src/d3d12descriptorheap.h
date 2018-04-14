#pragma once

// c++ includes
#include <cassert>
#include <list>

// d3d12 fwd decl
#include <d3d12.h>

// project includes
#include "d3d12basicsfwd.h"

namespace D3D12Render
{
    struct D3D12DescriptorHeapAllocation
    {
        D3D12_CPU_DESCRIPTOR_HANDLE m_cpuHandle;
        D3D12_GPU_DESCRIPTOR_HANDLE m_gpuHandle;
    };

    class D3D12DescriptorHeapAllocator
    {
    public:
        // Note maxDescriptors is the max number of descriptors in the heap
        D3D12DescriptorHeapAllocator(unsigned int descriptorHandleIncrementSize, ID3D12DescriptorHeap* descriptorHeap, 
                                     unsigned int maxDescriptors);

        D3D12DescriptorHeapAllocation* Allocate();

        void Free(D3D12DescriptorHeapAllocation* allocation);

    protected:
        std::list<D3D12DescriptorHeapAllocation*>   m_freeAllocations;
        std::vector<D3D12DescriptorHeapAllocation>  m_allocations;
    };

    using D3D12DescriptorHeapHandlePtr = D3D12DescriptorHeapAllocation*;

    class D3D12DescriptorHeap
    {
    public:
        D3D12DescriptorHeap(ID3D12DevicePtr d3d12Device, D3D12_DESCRIPTOR_HEAP_TYPE type, bool isShaderVisible, unsigned int maxDescriptors);

        // d3d12 objects access
        ID3D12DescriptorHeapPtr GetDescriptorHeap() const { return m_descriptorHeap; }

        // TODO think about moving this to a D3D12DescriptorHeapAllocation smart pointer
        void Destroy(D3D12DescriptorHeapHandlePtr handle);

    protected:
        using D3D12DescriptorHeapAllocatorPtr = std::unique_ptr<D3D12DescriptorHeapAllocator>;
        D3D12DescriptorHeapAllocatorPtr m_allocator;

    private:
        ID3D12DescriptorHeapPtr m_descriptorHeap;
    };

    class D3D12CBV_SV_UAVDescriptorHeap : public D3D12DescriptorHeap
    {
    public:
        D3D12CBV_SV_UAVDescriptorHeap(ID3D12DevicePtr d3d12Device, bool isShaderVisible, unsigned int maxDescriptors);

        D3D12DescriptorHeapHandlePtr CreateCBV(const D3D12_CONSTANT_BUFFER_VIEW_DESC& desc);

        D3D12DescriptorHeapHandlePtr CreateSRV(ID3D12ResourcePtr resource, const D3D12_SHADER_RESOURCE_VIEW_DESC& desc);

    private:
        ID3D12DevicePtr m_d3d12Device;
    };

    class D3D12RTVDescriptorHeap : public D3D12DescriptorHeap
    {
    public:
        D3D12RTVDescriptorHeap(ID3D12DevicePtr d3d12Device, unsigned int maxDescriptors);

        D3D12DescriptorHeapHandlePtr CreateRTV(ID3D12ResourcePtr resource, D3D12DescriptorHeapHandlePtr handle);

    private:
        ID3D12DevicePtr m_d3d12Device;
    };

    class D3D12DSVDescriptorHeap : public D3D12DescriptorHeap
    {
    public:
        D3D12DSVDescriptorHeap(ID3D12DevicePtr d3d12Device,  unsigned int maxDescriptors);

        D3D12DescriptorHeapHandlePtr CreateDSV(ID3D12ResourcePtr resource, const D3D12_DEPTH_STENCIL_VIEW_DESC& desc);

    private:
        ID3D12DevicePtr m_d3d12Device;
    };
}