#pragma once

// project includes
#include "d3d12fwd.h"
#include "d3d12gpu_sync.h"

// c++ includes
#include <string>
#include <list>
#include <cassert>

// directx includes
#include <d3d12.h>

namespace D3D12Basics
{
    struct D3D12CommittedBuffer
    {
        ID3D12ResourcePtr   m_resource;
        size_t              m_alignedSize;
    };

    class D3D12CommittedResourceAllocator
    {
    public:
        struct Context
        {
            ID3D12CommandAllocatorPtr       m_cmdAllocator;
            ID3D12GraphicsCommandListPtr    m_cmdList;
        };

        D3D12CommittedResourceAllocator(ID3D12DevicePtr device, ID3D12CommandQueuePtr cmdQueue);

        D3D12CommittedBuffer AllocateReadBackBuffer(size_t sizeBytes, size_t alignment, const std::wstring& debugName);

        D3D12CommittedBuffer AllocateBuffer(const void* data, size_t sizeBytes,
                                            size_t alignment, const std::wstring& debugName);

        ID3D12ResourcePtr AllocateTexture(const std::vector<D3D12_SUBRESOURCE_DATA>& subresources,
                                          const D3D12_RESOURCE_DESC& desc, const std::wstring& debugName);
    private:
        ID3D12DevicePtr         m_device;
        ID3D12CommandQueuePtr   m_cmdQueue;

        Context m_uploadingContext;
    };

    // TODO what to do with this function?
    enum class ResourceHeapType
    {
        DefaultHeap,
        UploadHeap
    };
    ID3D12ResourcePtr CreateResourceHeap(ID3D12DevicePtr device, const D3D12_RESOURCE_DESC& resourceDesc, 
                                         ResourceHeapType heapType, D3D12_RESOURCE_STATES initialState, 
                                         const D3D12_CLEAR_VALUE* clearValue = nullptr);

    // TODO move these to D3D12DynamicBufferAllocator?
    // NOTE m_size is aligned to the alignment requirements. It might be same or bigger size than
    // the requested size.
    struct D3D12DynamicBufferAllocationBlock;
    using D3D12DynamicBufferAllocationBlockPtr = std::unique_ptr<D3D12DynamicBufferAllocationBlock>;

    struct D3D12DynamicBufferAllocation
    {
        D3D12DynamicBufferAllocation();
        D3D12DynamicBufferAllocation(D3D12DynamicBufferAllocation&&);
        D3D12DynamicBufferAllocation& operator=(D3D12DynamicBufferAllocation&&);
        ~D3D12DynamicBufferAllocation();

        uint8_t*                    m_cpuPtr;
        D3D12_GPU_VIRTUAL_ADDRESS   m_gpuPtr;
        size_t                      m_size;

        D3D12DynamicBufferAllocationBlockPtr m_allocationBlock;
    };

    // Straightforward free list allocator with first fit strategy. No coalescing.
    // Grows a page when cant find a suitable space.
    // Page is aligned to the smallest 64kb multiple of pageSizeInBytes
    // TODO implement buddy allocator
    // NOTE: D3D12_RESOURCE_DIMENSION is D3D12_RESOURCE_DIMENSION_BUFFER on the D3D12_RESOURCE_DESC
    // used when calling to CreateCommittedResource
    class D3D12DynamicBufferAllocator
    {
    public:
        // NOTE pageSizeInBytes doesnt have to be aligned as it will be aligned
        // to 64kb internally by default
        D3D12DynamicBufferAllocator(ID3D12DevicePtr device, size_t pageSizeInBytes);

        ~D3D12DynamicBufferAllocator();

        D3D12DynamicBufferAllocator(const D3D12DynamicBufferAllocator&) = delete;

        D3D12DynamicBufferAllocation Allocate(size_t sizeInBytes, size_t alignment);

        void Deallocate(D3D12DynamicBufferAllocation& allocation);

    private:

        struct Page
        {
            Page() = default;
            
            // NOTE: default move constructor is needed in order to have this
            // class in a container. Otherwise it will fail as the copy constructor
            // will get compiled and D3D12DynamicBufferAllocationBlockPtr doesn't have one
            Page(Page&&) = default;
            Page& operator=(Page&&) = default;

            ID3D12ResourcePtr           m_resource;
            uint8_t*                    m_cpuPtr;
            D3D12_GPU_VIRTUAL_ADDRESS   m_gpuPtr;

            std::list<D3D12DynamicBufferAllocationBlockPtr>        m_freeBlocks;
        };

        ID3D12DevicePtr m_device;

        const size_t m_pageSizeInBytes;

        std::vector<Page> m_pages;

        void AllocatePage();
    };
}