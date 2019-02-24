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
    struct D3D12DescriptorAllocation
    {
        D3D12_CPU_DESCRIPTOR_HANDLE m_cpuHandle;
        D3D12_GPU_DESCRIPTOR_HANDLE m_gpuHandle;
    };

    class D3D12DescriptorStackAllocator;
    class D3D12DescriptorPoolAllocator;

    using D3D12DescriptorPoolAllocatorPtr = std::unique_ptr<D3D12DescriptorPoolAllocator>;
    using D3D12DescriptorStackAllocatorPtr = std::unique_ptr<D3D12DescriptorStackAllocator>;

    class D3D12DescriptorPool
    {
    public:
        D3D12DescriptorPool(ID3D12DevicePtr d3d12Device, D3D12_DESCRIPTOR_HEAP_TYPE type, 
                            bool isShaderVisible, unsigned int maxDescriptors);

        ~D3D12DescriptorPool();

        // d3d12 objects access
        ID3D12DescriptorHeapPtr GetDescriptorHeap() const { return m_descriptorHeap; }

        // TODO think about moving this to a D3D12DescriptorAllocation smart pointer
        void Destroy(D3D12DescriptorAllocation* handle);

    protected:
        D3D12DescriptorPoolAllocatorPtr m_allocator;

    private:
        ID3D12DescriptorHeapPtr m_descriptorHeap;
    };

    class D3D12CBV_SRV_UAVDescriptorPool : public D3D12DescriptorPool
    {
    public:
        D3D12CBV_SRV_UAVDescriptorPool(ID3D12DevicePtr d3d12Device, unsigned int maxDescriptors, bool isShaderVisible = false);

        D3D12DescriptorAllocation* CreateCBV(const D3D12_CONSTANT_BUFFER_VIEW_DESC& desc);

        D3D12DescriptorAllocation* CreateSRV(ID3D12ResourcePtr resource, const D3D12_SHADER_RESOURCE_VIEW_DESC& desc);

    private:
        ID3D12DevicePtr m_d3d12Device;
    };

    class D3D12RTVDescriptorPool : public D3D12DescriptorPool
    {
    public:
        D3D12RTVDescriptorPool(ID3D12DevicePtr d3d12Device, unsigned int maxDescriptors);

        D3D12DescriptorAllocation* CreateRTV(ID3D12ResourcePtr resource, D3D12DescriptorAllocation* handle = nullptr);

    private:
        ID3D12DevicePtr m_d3d12Device;
    };

    class D3D12DSVDescriptorPool : public D3D12DescriptorPool
    {
    public:
        D3D12DSVDescriptorPool(ID3D12DevicePtr d3d12Device,  unsigned int maxDescriptors);

        D3D12DescriptorAllocation* CreateDSV(ID3D12ResourcePtr resource, const D3D12_DEPTH_STENCIL_VIEW_DESC& desc, D3D12DescriptorAllocation* handle);

    private:
        ID3D12DevicePtr m_d3d12Device;
    };

    // This is a ring buffer of descriptor stacks sets in a single gpu descriptor heap (CBV_SRV_UAV)
    // Every stacks set will be used in a different frame to guarantee no concurrency issues.
    // When using a stacks set, every stack will be used by a different thread to guarantee no concurrency issues
    // Set of stacks -> cpu/gpu concurrency -> one per frame
    // Stacks in a set -> cpu/cpu concurrency -> one per thread
    // maxDescriptorsPerHeap is the number of descriptors that the stacks set will hold.
    class D3D12GPUDescriptorRingBuffer
    {
    public:
        D3D12GPUDescriptorRingBuffer(ID3D12DevicePtr d3d12Device, unsigned int maxHeaps, 
                                     unsigned int maxDescriptorsPerHeap);

        ~D3D12GPUDescriptorRingBuffer();

        D3D12_GPU_DESCRIPTOR_HANDLE CurrentDescriptor(unsigned int stackIndex = 0) const;

        // Moves to the next descriptor in the current ringbuffer stacks set in the stackIndex stack of the set 
        void NextDescriptor(size_t stackIndex = 0);

        // Moves to the next ringbuffer stacks set
        void NextStacksSet();

        // Copy numDescriptors starting from srcDescriptorRangeStart to a stack of index stackIndex
        // of the current ringbuffer stack set starting from the current descriptor
        void CopyToDescriptor(unsigned int numDescriptors, D3D12_CPU_DESCRIPTOR_HANDLE srcDescriptorRangeStart,
                              unsigned int stackIndex = 0);

        void UpdateStacksSetSize(unsigned int stacksSetSize);

        // This clears the current stacks set. Its not synced with the gpu. This has to be called
        // when the stack is no longer in flight.
        void ClearStacksSet();

        // d3d12 objects access
        ID3D12DescriptorHeapPtr GetDescriptorHeap() const { return m_descriptorHeap; }

    private:
        using DescriptorStackAllocators     = std::vector<D3D12DescriptorStackAllocatorPtr>;
        using DescriptorStackAllocatorsSets = std::vector<DescriptorStackAllocators>;
        
        ID3D12DevicePtr m_d3d12Device;

        unsigned int m_maxDescriptorsPerHeap;
        unsigned int m_descriptorHandleIncrementSize;

        size_t m_ringBufferSize;

        // TODO change name to m_stackAllocatorsSetSize
        size_t m_stacksSetSize;

        std::vector<unsigned int> m_descriptorHeapOffsets;

        ID3D12DescriptorHeapPtr         m_descriptorHeap;
        DescriptorStackAllocatorsSets   m_stackAllocatorsSets;

        size_t m_currentStackAllocatorSet;
        
        // Note: one current descriptor per stack in the set
        std::vector<D3D12DescriptorAllocation*> m_currentStackDescriptorAllocations;

        void FillStackAllocatorSet(DescriptorStackAllocators& stackAllocatorsSet, unsigned int descriptorHeapOffset);

        void NextDescriptor(size_t stackAllocatorsSetIndex, size_t stackIndex);
    };

    // This is a growing array of cpu descriptors
    // Note: still not convinced a pool is needed here since it seems the descriptors
    // are going to be released in a batch and not one at a time
    template<class DescriptorPool>
    class D3D12DescriptorBuffer
    {
    public:
        // initialSize is the size of the first heap and the following newly allocated
        // heaps when the array needs to grow.
        D3D12DescriptorBuffer(ID3D12DevicePtr d3d12Device, unsigned int initialSize);

        void Destroy(D3D12DescriptorAllocation* handle);

    protected:
        using DescriptorPoolPtr = std::unique_ptr<DescriptorPool>;

        ID3D12DevicePtr m_d3d12Device;
    
        unsigned int m_heapSize;

        std::vector<DescriptorPoolPtr> m_descriptorPools;

        std::unordered_map<D3D12DescriptorAllocation*, size_t> m_handlesAllocators;

        void AddPool();
    };

    template<class DescriptorPool>
    D3D12DescriptorBuffer<DescriptorPool>::D3D12DescriptorBuffer(ID3D12DevicePtr d3d12Device,
                                                                    unsigned int initialSize) : m_d3d12Device(d3d12Device),
                                                                                                m_heapSize(initialSize)
    {
        assert(m_d3d12Device);
        assert(m_heapSize > 0);

        AddPool();
    }

    template<class DescriptorPool>
    void D3D12DescriptorBuffer<DescriptorPool>::Destroy(D3D12DescriptorAllocation* handle)
    {
        assert(handle);
        assert(m_handlesAllocators.count(handle));

        auto index = m_handlesAllocators[handle];
        assert(index < m_descriptorPools.size());

        m_descriptorPools[index]->Destroy(handle);
    }

    template<class DescriptorPool>
    void D3D12DescriptorBuffer<DescriptorPool>::AddPool()
    {
        m_descriptorPools.push_back(std::make_unique<DescriptorPool>(m_d3d12Device, m_heapSize));
    }

    class D3D12CBV_SRV_UAVDescriptorBuffer : public D3D12DescriptorBuffer<D3D12CBV_SRV_UAVDescriptorPool>
    {
    public:
        D3D12CBV_SRV_UAVDescriptorBuffer(ID3D12DevicePtr d3d12Device, unsigned int initialSize);

        D3D12DescriptorAllocation* CreateCBV(const D3D12_CONSTANT_BUFFER_VIEW_DESC& desc);

        D3D12DescriptorAllocation* CreateSRV(ID3D12ResourcePtr resource, const D3D12_SHADER_RESOURCE_VIEW_DESC& desc);
    };

    class D3D12RTVDescriptorBuffer : public D3D12DescriptorBuffer<D3D12RTVDescriptorPool>
    {
    public:
        D3D12RTVDescriptorBuffer(ID3D12DevicePtr d3d12Device, unsigned int initialSize);
        
        D3D12DescriptorAllocation* CreateRTV(ID3D12ResourcePtr resource);
    };
}