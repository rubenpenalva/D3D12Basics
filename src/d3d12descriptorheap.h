#pragma once

// c++ includes
#include <cassert>
#include <list>
#include <vector>
#include <unordered_map>

// d3d12 fwd decl
#include <d3d12.h>

// project includes
#include "d3d12basicsfwd.h"

namespace D3D12Basics
{
    struct D3D12DescriptorHeapAllocation
    {
        D3D12_CPU_DESCRIPTOR_HANDLE m_cpuHandle;
        D3D12_GPU_DESCRIPTOR_HANDLE m_gpuHandle;
    };

    // Stack allocator
    class D3D12DescriptorStackAllocator
    {
    public:
        // Note maxDescriptors is the max number of descriptors in the heap
        D3D12DescriptorStackAllocator(unsigned int descriptorHandleIncrementSize, ID3D12DescriptorHeap* descriptorHeap,
                                      unsigned int maxDescriptors, unsigned int descriptorHeapOffset = 0);

        D3D12DescriptorHeapAllocation* Allocate();

        void Clear();

    private:
        D3D12_CPU_DESCRIPTOR_HANDLE m_startCPUHandle;
        D3D12_GPU_DESCRIPTOR_HANDLE m_startGPUHandle;

        size_t m_stackTop;
        std::vector<D3D12DescriptorHeapAllocation> m_allocations;
    };

    // Pool allocator
    class D3D12DescriptorHeapAllocator
    {
    public:
        // Note maxDescriptors is the max number of descriptors in the heap
        D3D12DescriptorHeapAllocator(unsigned int descriptorHandleIncrementSize, ID3D12DescriptorHeap* descriptorHeap, 
                                     unsigned int maxDescriptors, unsigned int heapStartOffset = 0);

        D3D12DescriptorHeapAllocation* Allocate();

        void Free(D3D12DescriptorHeapAllocation* allocation);

    protected:
        std::list<D3D12DescriptorHeapAllocation*>   m_freeAllocations;
        std::vector<D3D12DescriptorHeapAllocation>  m_allocations;
    };

    using D3D12DescriptorHeapHandlePtr = D3D12DescriptorHeapAllocation*;
    using D3D12DescriptorHeapAllocatorPtr = std::unique_ptr<D3D12DescriptorHeapAllocator>;

    // Pool based specialized heaps
    class D3D12DescriptorHeap
    {
    public:
        D3D12DescriptorHeap(ID3D12DevicePtr d3d12Device, D3D12_DESCRIPTOR_HEAP_TYPE type, 
                            bool isShaderVisible, unsigned int maxDescriptors);

        // d3d12 objects access
        ID3D12DescriptorHeapPtr GetDescriptorHeap() const { return m_descriptorHeap; }

        // TODO think about moving this to a D3D12DescriptorHeapAllocation smart pointer
        void Destroy(D3D12DescriptorHeapHandlePtr handle);

    protected:
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

        D3D12DescriptorHeapHandlePtr CreateDSV(ID3D12ResourcePtr resource, const D3D12_DEPTH_STENCIL_VIEW_DESC& desc, D3D12DescriptorHeapHandlePtr handle);

    private:
        ID3D12DevicePtr m_d3d12Device;
    };

    // This is a ring buffer of descriptor stacks in a single gpu descriptor heap
    class D3D12GPUDescriptorRingBuffer
    {
    public:
        D3D12GPUDescriptorRingBuffer(ID3D12DevicePtr d3d12Device, unsigned int maxHeaps, unsigned int maxDescriptorsPerHeap);

        D3D12_GPU_DESCRIPTOR_HANDLE CurrentDescriptor() const;

        // Moves to the next descriptor in the current ringbuffer stack 
        void NextCurrentStackDescriptor();

        // Moves to the next ringbuffer stack
        void NextDescriptorStack();

        // Copy numDescriptors starting from srcDescriptorRangeStart to the current ringbuffer stack
        // starting from the current descriptor
        void CopyToCurrentDescriptor(unsigned int numDescriptors, D3D12_CPU_DESCRIPTOR_HANDLE srcDescriptorRangeStart);

        // This clears the current stack. Its not synced with the gpu. This has to be called
        // when the stack is no longer in flight.
        void ClearCurrentStack();

        // d3d12 objects access
        ID3D12DescriptorHeapPtr GetDescriptorHeap() const { return m_descriptorHeap; }

    private:
        using D3D12DescriptorStackAllocatorPtr = std::unique_ptr<D3D12DescriptorStackAllocator>;

        ID3D12DevicePtr m_d3d12Device;

        size_t m_ringBufferSize;

        ID3D12DescriptorHeapPtr                         m_descriptorHeap;
        std::vector<D3D12DescriptorStackAllocatorPtr>   m_allocators;

        size_t                          m_currentHeap;
        D3D12DescriptorHeapAllocation*  m_currentAllocation;
    };

    // This is a growing array of cpu cb_srv_uav heaps
    class D3D12CPUDescriptorBuffer
    {
    public:
        // initialSize is the size of the first heap and the following newly allocated
        // heaps when the array needs to grow.
        D3D12CPUDescriptorBuffer(ID3D12DevicePtr d3d12Device, unsigned int initialSize);

        D3D12DescriptorHeapHandlePtr CreateCBV(const D3D12_CONSTANT_BUFFER_VIEW_DESC& desc);

        D3D12DescriptorHeapHandlePtr CreateSRV(ID3D12ResourcePtr resource, const D3D12_SHADER_RESOURCE_VIEW_DESC& desc);

        void Destroy(D3D12DescriptorHeapHandlePtr handle);

    private:
        ID3D12DevicePtr m_d3d12Device;
        
        unsigned int m_heapSize;

        std::vector<D3D12CBV_SV_UAVDescriptorHeapPtr> m_descriptorHeaps;

        std::unordered_map<D3D12DescriptorHeapHandlePtr, size_t> m_handlesAllocators;

        void AddHeap();
    };
}