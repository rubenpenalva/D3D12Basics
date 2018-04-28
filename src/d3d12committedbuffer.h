#pragma once

// project includes
#include "d3d12fwd.h"
#include "d3d12gpu_sync.h"

// c++ includes
#include <string>
#include <list>

// directx includes
#include <d3d12.h>

namespace D3D12Render
{
    const uint16_t g_constantBufferReadAlignment = 256;

    // TODO remove committed from names

    ID3D12ResourcePtr D3D12CreateCommittedDepthStencil(ID3D12DevicePtr device, unsigned int width, unsigned int height, DXGI_FORMAT format,
                                                       const D3D12_CLEAR_VALUE* clearValue, const std::wstring& debugName);

    // TODO why is this a class if it doest have state? refactor it to a free function
    class D3D12CommittedBufferLoader
    {
    public:
        D3D12CommittedBufferLoader(ID3D12DevicePtr device, ID3D12CommandAllocatorPtr cmdAllocator,
                                   ID3D12CommandQueuePtr cmdQueue, ID3D12GraphicsCommandListPtr cmdList);

        ~D3D12CommittedBufferLoader();

        ID3D12ResourcePtr Upload(const void* data, size_t dataSizeBytes, size_t requiredDataSize, const std::wstring& resourceName);

        ID3D12ResourcePtr Upload(const std::vector<D3D12_SUBRESOURCE_DATA>& subresources, const D3D12_RESOURCE_DESC& desc, 
                                 const std::wstring& resourceName);
    
    private:
        enum class CopyType
        {
            Buffer,
            Texture
        };

        ID3D12DevicePtr m_device;

        ID3D12GraphicsCommandListPtr    m_cmdList;
        ID3D12CommandAllocatorPtr       m_cmdAllocator;
        ID3D12CommandQueuePtr           m_cmdQueue;

        D3D12GpuSynchronizer            m_gpuSync;

        ID3D12ResourcePtr UploadInternal(const std::vector<D3D12_SUBRESOURCE_DATA>& subresources,
                                         const D3D12_RESOURCE_DESC& resourceDesc, 
                                         const std::wstring& resourceName, CopyType copyType);

        void EnqueueBufferCopyCmd(ID3D12ResourcePtr uploadHeap, ID3D12ResourcePtr defaultHeap, size_t dataSizeBytes);

        void EnqueueTextureCopyCmd(ID3D12ResourcePtr uploadHeap, ID3D12ResourcePtr defaultHeap,
                                   const std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT>& layouts);
    };

    struct D3D12BufferAllocation
    {
        uint8_t*                    m_cpuPtr;
        D3D12_GPU_VIRTUAL_ADDRESS   m_gpuPtr;

        // NOTE m_size is aligned to the alignment requirements. It might be same or bigger size than
        // the requested size.
        size_t                      m_size;
        size_t                      m_pageIndex;
    };

    // Straightforward free list allocator with first fit strategy. No coalescing.
    // Grows a page when cant find a suitable space.
    // Page is aligned to the smallest 64kb multiple of pageSizeInBytes
    // TODO implement buddy allocator
    class D3D12BufferAllocator
    {
    public:
        // NOTE pageSizeInBytes doesnt have to be aligned as it will be aligned
        // to 64kb internally
        D3D12BufferAllocator(ID3D12DevicePtr device, size_t pageSizeInBytes);

        ~D3D12BufferAllocator();

        D3D12BufferAllocation Allocate(size_t sizeInBytes, size_t alignment);

        void Deallocate(const D3D12BufferAllocation& allocation);

    private:
        struct Page
        {
            ID3D12ResourcePtr                   m_resource;
            std::list<D3D12BufferAllocation>    m_freeBlocks;
        };

        ID3D12DevicePtr m_device;

        const size_t m_pageSizeInBytes;

        std::vector<Page> m_pages;

        void AllocatePage();
    };
}