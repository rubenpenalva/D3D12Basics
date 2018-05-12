#include "d3d12committedbuffer.h"

using namespace D3D12Render;

// project libs
#include "utils.h"

// c++ libs
#include <cstdint>
#include <algorithm>

// directx
#include <d3d12.h>

using namespace D3D12Basics;

namespace
{
    enum class CopyType
    {
        Buffer,
        Texture
    };

    enum class ResourceHeapType
    {
        DefaultHeap,
        UploadHeap
    };
    
    struct SubresourcesFootPrint
    {
        SubresourcesFootPrint(size_t subresourcesCount) :   m_layouts(subresourcesCount),
                                                            m_rowSizesInBytes(subresourcesCount),
                                                            m_rowsCounts(subresourcesCount)
        {
        }
        std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> m_layouts;
        std::vector<UINT64>                             m_rowSizesInBytes;
        std::vector<UINT>                               m_rowsCounts;
        
        UINT64 m_requiredSize;
    };

    D3D12_RESOURCE_DESC CreateBufferDesc(uint64_t sizeBytes)
    {
        D3D12_RESOURCE_DESC resourceDesc;
        resourceDesc.Dimension           = D3D12_RESOURCE_DIMENSION_BUFFER;
        // https://msdn.microsoft.com/en-us/library/windows/desktop/dn903813(v=vs.85).aspx
        // Alignment must be 64KB (D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT) or 0, which is effectively 64KB.
        resourceDesc.Alignment           = 0;
        resourceDesc.Width               = sizeBytes;
        resourceDesc.Height              = 1;
        resourceDesc.DepthOrArraySize    = 1;
        resourceDesc.MipLevels           = 1;
        resourceDesc.Format              = DXGI_FORMAT_UNKNOWN;
        resourceDesc.SampleDesc          = { 1, 0 };
        resourceDesc.Layout              = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        resourceDesc.Flags               = D3D12_RESOURCE_FLAG_NONE;
    
        return resourceDesc;
    }
   
    D3D12_RESOURCE_DESC CreateDepthStencilDesc(unsigned int width, unsigned int height, DXGI_FORMAT format)
    {
        D3D12_RESOURCE_DESC resourceDesc;
        resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        resourceDesc.Alignment = 0;
        resourceDesc.Width = width;
        resourceDesc.Height = height;
        resourceDesc.DepthOrArraySize = 1;
        resourceDesc.MipLevels = 1;
        resourceDesc.Format = format;
        resourceDesc.SampleDesc = { 1, 0 };
        resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

        return resourceDesc;
    }

    SubresourcesFootPrint CreateSubresourceFootPrint(ID3D12DevicePtr device, size_t subresourcesCount, D3D12_RESOURCE_DESC desc)
    {
        SubresourcesFootPrint subresourcesFootPrint(subresourcesCount);

        // TODO implement this function, cache the results and use the table.
        device->GetCopyableFootprints(&desc, 0, static_cast<UINT>(subresourcesCount), 0, &subresourcesFootPrint.m_layouts[0],
                                      &subresourcesFootPrint.m_rowsCounts[0], &subresourcesFootPrint.m_rowSizesInBytes[0],
                                      &subresourcesFootPrint.m_requiredSize);

        return subresourcesFootPrint;
    }

    ID3D12ResourcePtr CreateResourceHeap(ID3D12DevicePtr device, const D3D12_RESOURCE_DESC& resourceDesc, ResourceHeapType heapType, 
                                         D3D12_RESOURCE_STATES initialState, const D3D12_CLEAR_VALUE* clearValue = nullptr)
    {
        D3D12_HEAP_PROPERTIES resourceHeap;
        resourceHeap.Type                   = heapType == ResourceHeapType::DefaultHeap ? D3D12_HEAP_TYPE_DEFAULT : 
                                                                                          D3D12_HEAP_TYPE_UPLOAD;
        resourceHeap.CPUPageProperty        = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        resourceHeap.MemoryPoolPreference   = D3D12_MEMORY_POOL_UNKNOWN;
        resourceHeap.CreationNodeMask       = 1;
        resourceHeap.VisibleNodeMask        = 1;

        ID3D12ResourcePtr resource;
        AssertIfFailed(device->CreateCommittedResource(&resourceHeap, D3D12_HEAP_FLAG_NONE, &resourceDesc, initialState,
                                                        clearValue, IID_PPV_ARGS(&resource)));
        return resource;
    }

    void MapUnmap(const SubresourcesFootPrint& subresourcesFootPrint, const std::vector<D3D12_SUBRESOURCE_DATA>& subresources, 
                  ID3D12ResourcePtr uploadHeap)
    {
        BYTE* uploadHeapData;
        AssertIfFailed(uploadHeap->Map(0, NULL, reinterpret_cast<void**>(&uploadHeapData)));

        for (UINT i = 0; i < subresources.size(); ++i)
        {
            assert(subresourcesFootPrint.m_rowSizesInBytes[i] <= (SIZE_T)-1);
            const auto& layout = subresourcesFootPrint.m_layouts[i];
            const auto& rowSizeBytes = subresourcesFootPrint.m_rowSizesInBytes[i];
            const auto& rowsCount = subresourcesFootPrint.m_rowsCounts[i];

            const BYTE* srcData = reinterpret_cast<const BYTE*>(subresources[i].pData);
            SIZE_T srcRowPitch = subresources[i].RowPitch;
            SIZE_T srcSlicePitch = subresources[i].SlicePitch;

            BYTE* dstData = uploadHeapData + layout.Offset;
            SIZE_T dstRowPitch = layout.Footprint.RowPitch;
            SIZE_T dstSlicePitch = layout.Footprint.RowPitch * rowsCount;
            for (UINT z = 0; z < layout.Footprint.Depth; ++z)
            {
                BYTE* pDestSlice = dstData + dstSlicePitch * z;
                const BYTE* pSrcSlice = srcData + srcSlicePitch * z;
                for (UINT y = 0; y < rowsCount; ++y)
                {
                    memcpy(pDestSlice + dstRowPitch * y,
                           pSrcSlice + srcRowPitch * y,
                           rowSizeBytes);
                }
            }
        }

        uploadHeap->Unmap(0, NULL);
    }

    ID3D12ResourcePtr D3D12CreateDynamicCommittedBuffer(ID3D12DevicePtr device, size_t dataSizeBytes)
    {
        D3D12_RESOURCE_DESC resourceDesc = CreateBufferDesc(dataSizeBytes);
        ID3D12ResourcePtr resource = CreateResourceHeap(device, resourceDesc, ResourceHeapType::UploadHeap,
                                                        D3D12_RESOURCE_STATE_GENERIC_READ);
        assert(resource);

        return resource;
    }

    void EnqueueBufferCopyCmd(ID3D12GraphicsCommandListPtr cmdList,
                              ID3D12ResourcePtr uploadHeap, ID3D12ResourcePtr defaultHeap,
                              size_t dataSizeBytes)
    {
        const UINT64 dstOffset = 0;
        const UINT64 srcOffset = 0;
        const UINT64 numBytes = dataSizeBytes;
        cmdList->CopyBufferRegion(defaultHeap.Get(), dstOffset, uploadHeap.Get(), srcOffset, numBytes);
    }

    void EnqueueTextureCopyCmd(ID3D12GraphicsCommandListPtr cmdList, ID3D12ResourcePtr uploadHeap,
                               ID3D12ResourcePtr defaultHeap,
                               const std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT>& layouts)
    {
        D3D12_TEXTURE_COPY_LOCATION dest;
        dest.pResource = defaultHeap.Get();
        dest.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;

        D3D12_TEXTURE_COPY_LOCATION src;
        src.pResource = uploadHeap.Get();
        src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;

        for (UINT i = 0; i < layouts.size(); ++i)
        {
            src.PlacedFootprint = layouts[i];
            dest.SubresourceIndex = i;
            cmdList->CopyTextureRegion(&dest, 0, 0, 0, &src, nullptr);
        }
    }

    ID3D12ResourcePtr UploadInternal(const D3D12GpuUploadContext& uploadContext,
                                     const std::vector<D3D12_SUBRESOURCE_DATA>& subresources,
                                     const D3D12_RESOURCE_DESC& resourceDesc,
                                     const std::wstring& resourceName, CopyType copyType)
    {
        D3D12GpuSynchronizer gpuSync(uploadContext.m_device, uploadContext.m_cmdQueue, 1);

        // Create upload heap
        auto subresourcesFootPrint = CreateSubresourceFootPrint(uploadContext.m_device, subresources.size(), resourceDesc);

        const auto uploadBufferDesc = CreateBufferDesc(subresourcesFootPrint.m_requiredSize);
        ID3D12ResourcePtr uploadHeap = CreateResourceHeap(uploadContext.m_device, uploadBufferDesc, ResourceHeapType::UploadHeap,
                                                          D3D12_RESOURCE_STATE_GENERIC_READ);
        assert(uploadHeap);

        // Copy data to upload heap
        MapUnmap(subresourcesFootPrint, subresources, uploadHeap);

        // Create default heap
        ID3D12ResourcePtr defaultHeap = CreateResourceHeap(uploadContext.m_device, resourceDesc, ResourceHeapType::DefaultHeap,
            D3D12_RESOURCE_STATE_COPY_DEST);
        assert(defaultHeap);
        defaultHeap->SetName(resourceName.c_str());

        AssertIfFailed(uploadContext.m_cmdAllocator->Reset());
        AssertIfFailed(uploadContext.m_cmdList->Reset(uploadContext.m_cmdAllocator.Get(), nullptr));

        if (copyType == CopyType::Buffer)
            EnqueueBufferCopyCmd(uploadContext.m_cmdList, uploadHeap, defaultHeap, subresourcesFootPrint.m_layouts[0].Footprint.Width);
        else if (copyType == CopyType::Texture)
            EnqueueTextureCopyCmd(uploadContext.m_cmdList, uploadHeap, defaultHeap, subresourcesFootPrint.m_layouts);
        else
            assert(true);

        // Add barrier
        D3D12_RESOURCE_BARRIER copyDestToReadDest;
        copyDestToReadDest.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        copyDestToReadDest.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        copyDestToReadDest.Transition.pResource = defaultHeap.Get();
        copyDestToReadDest.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        copyDestToReadDest.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        copyDestToReadDest.Transition.StateAfter = copyType == CopyType::Buffer ?   D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER :
                                                                                    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

        uploadContext.m_cmdList->ResourceBarrier(1, &copyDestToReadDest);

        // Execute command list
        AssertIfFailed(uploadContext.m_cmdList->Close());
        ID3D12CommandList* ppCommandLists[] = { uploadContext.m_cmdList.Get() };
        uploadContext.m_cmdQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

        // Wait for the command list to finish executing on the gpu
        gpuSync.Wait();

        // Release upload heap

        return defaultHeap;
    }
}

ID3D12ResourcePtr D3D12Render::D3D12CreateCommittedDepthStencil(ID3D12DevicePtr device, unsigned int width, unsigned int height, 
                                                                DXGI_FORMAT format, const D3D12_CLEAR_VALUE* clearValue, 
                                                                const std::wstring& debugName)
{
    D3D12_RESOURCE_DESC resourceDesc = CreateDepthStencilDesc(width, height, format);

    ID3D12ResourcePtr resource = CreateResourceHeap(device, resourceDesc, ResourceHeapType::DefaultHeap, 
                                                    D3D12_RESOURCE_STATE_DEPTH_WRITE, clearValue);
    assert(resource);
    resource->SetName(debugName.c_str());
    
    return resource;
}

ID3D12ResourcePtr D3D12Render::D3D12CreateCommittedBuffer(const D3D12GpuUploadContext& uploadContext,
                                                         const void* data, size_t dataSizeBytes,
                                                         const std::wstring& resourceName)
{

    D3D12_RESOURCE_DESC resourceDesc = CreateBufferDesc(dataSizeBytes);
    std::vector<D3D12_SUBRESOURCE_DATA> subresources;
    subresources.push_back({ data, static_cast<LONG_PTR>(dataSizeBytes), static_cast<LONG_PTR>(dataSizeBytes) });

    return UploadInternal(uploadContext, subresources, resourceDesc, resourceName, CopyType::Buffer);
}

ID3D12ResourcePtr D3D12Render::D3D12CreateCommittedBuffer(const D3D12GpuUploadContext& uploadContext,
                                                          const std::vector<D3D12_SUBRESOURCE_DATA>& subresources, 
                                                          const D3D12_RESOURCE_DESC& desc,
                                                          const std::wstring& resourceName)
{
    return UploadInternal(uploadContext, subresources, desc, resourceName, CopyType::Texture);
}

D3D12BufferAllocator::D3D12BufferAllocator(ID3D12DevicePtr device, size_t pageSizeInBytes) : m_device(device), m_pageSizeInBytes(pageSizeInBytes)
{
    assert(m_device);
    assert(m_pageSizeInBytes > 0);

    AllocatePage();
}

D3D12BufferAllocator::~D3D12BufferAllocator()
{
    D3D12_RANGE readRange{ 0, 0 };
    for(auto& page : m_pages)
        page.m_resource->Unmap(0, &readRange);
}

// TODO memory alignment
D3D12BufferAllocation D3D12BufferAllocator::Allocate(size_t sizeInBytes, size_t alignment)
{
    assert(alignment);
    assert(m_pages.size());
    assert(D3D12Basics::IsPowerOf2(alignment));

    const auto alignedSize = AlignToPowerof2(sizeInBytes, alignment);
    assert(alignedSize >= sizeInBytes);
    
    D3D12BufferAllocation* freeBlock = nullptr;

    // Linearly search through all the pages for a free block that fits the requested alignedSize
    for (size_t i = 0; i < m_pages.size(); ++i)
    {
        auto& page = m_pages[i];
        
        auto foundFreeBlock = std::find_if(page.m_freeBlocks.begin(), page.m_freeBlocks.end(), [alignedSize](const auto& element)
        {
            return element.m_size >= alignedSize;
        });

        if (foundFreeBlock != page.m_freeBlocks.end())
        {
            freeBlock = &(*foundFreeBlock);
            break;
        }
    }

    // No memory no problem. Allocate another page!
    if (!freeBlock)
    {
        AllocatePage();
        freeBlock = &m_pages.back().m_freeBlocks.back();
    }

    assert(freeBlock);
    assert(freeBlock->m_size >= alignedSize);

    // Book keeping for allocation
    D3D12BufferAllocation allocation = *freeBlock;
    allocation.m_size = alignedSize;

    freeBlock->m_cpuPtr += alignedSize;
    freeBlock->m_gpuPtr += alignedSize;
    freeBlock->m_size -= alignedSize;

    return allocation;
}

void D3D12BufferAllocator::Deallocate(const D3D12BufferAllocation& allocation)
{
    assert(allocation.m_pageIndex < m_pages.size());

    auto& page = m_pages[allocation.m_pageIndex];
    page.m_freeBlocks.push_back(allocation);

    // TODO does destroying the resource immediately makes sense? would it better to 
    // have a strategy based on size and frequency of use?
    size_t freeSize = 0;
    for (auto& freeBlock : page.m_freeBlocks)
        freeSize += freeBlock.m_size;

    // TODO linear time! change to list for const time?
    if (freeSize == m_pageSizeInBytes)
        m_pages.erase(m_pages.begin() + allocation.m_pageIndex);
}

void D3D12BufferAllocator::AllocatePage()
{
    Page page;
    page.m_resource = D3D12CreateDynamicCommittedBuffer(m_device, m_pageSizeInBytes);
    assert(page.m_resource);

    auto gpuPtr = page.m_resource->GetGPUVirtualAddress();
    assert(gpuPtr);
    // TODO first page allocation passes the assert but not the second one. Investigate!
    //assert(D3D12Basics::IsAlignedToPowerof2(gpuPtr, g_64kb));

    D3D12_RANGE readRange{ 0, 0 };
    uint8_t* cpuPtr;
    D3D12Basics::AssertIfFailed(page.m_resource->Map(0, &readRange, reinterpret_cast<void**>(&cpuPtr)));
    assert(D3D12Basics::IsAlignedToPowerof2(reinterpret_cast<size_t>(cpuPtr), g_4kb));

    const size_t pageIndex = m_pages.size();
    page.m_freeBlocks.push_back(D3D12BufferAllocation{ cpuPtr, gpuPtr, m_pageSizeInBytes, pageIndex });

    m_pages.emplace_back(std::move(page));
}
