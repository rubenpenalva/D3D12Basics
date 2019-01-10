#include "d3d12committedresources.h"

using namespace D3D12Basics;

// project libs
#include "utils.h"
#include "d3d12utils.h"

// c++ libs
#include <cstdint>
#include <algorithm>

// directx
#include <d3d12.h>

namespace D3D12Basics
{
    struct D3D12DynamicBufferAllocationBlock
    {
        size_t m_offset;
        size_t m_size;
        size_t m_pageIndex;
    };
}

namespace
{
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

    SubresourcesFootPrint CreateSubresourceFootPrint(ID3D12DevicePtr device, size_t subresourcesCount, D3D12_RESOURCE_DESC desc)
    {
        SubresourcesFootPrint subresourcesFootPrint(subresourcesCount);

        // TODO implement this function, cache the results and use the table.
        device->GetCopyableFootprints(&desc, 0, static_cast<UINT>(subresourcesCount), 0, &subresourcesFootPrint.m_layouts[0],
                                      &subresourcesFootPrint.m_rowsCounts[0], &subresourcesFootPrint.m_rowSizesInBytes[0],
                                      &subresourcesFootPrint.m_requiredSize);

        return subresourcesFootPrint;
    }

    ID3D12ResourcePtr D3D12CreateDynamicCommittedBuffer(ID3D12DevicePtr device, size_t dataSizeBytes)
    {
        D3D12_RESOURCE_DESC resourceDesc = CreateBufferDesc(dataSizeBytes);
        ID3D12ResourcePtr resource = CreateResourceHeap(device, resourceDesc, ResourceHeapType::UploadHeap,
                                                        D3D12_RESOURCE_STATE_GENERIC_READ);
        assert(resource);

        return resource;
    }

    size_t TotalFreeBlockAlignedSize(size_t offset, size_t alignedSize, size_t alignment)
    {
        const auto alignedOffset = AlignToPowerof2(offset, alignment);
        assert(alignedOffset >= offset);
        return (alignedOffset - offset) + alignedSize;
    }

    void AssertContextIsValid(const D3D12CommittedResourceAllocator::Context& context)
    {
#if NDEBUG
        assert(context.m_cmdAllocator);
        assert(context.m_cmdList);
#else
        context;
#endif // !NDEBUG
    }

    // NOTE probably for this project using CopyResource would be enough as theres no subresource copying planned
    // TODO change to CopyResource whenever is convenient
    class UploaderHelper
    {
    public:
        UploaderHelper(const D3D12CommittedResourceAllocator::Context& context, ID3D12DevicePtr device,
                       ID3D12CommandQueuePtr cmdQueue, const D3D12_RESOURCE_DESC& resourceDesc,
                       size_t alignedSize, D3D12_RESOURCE_STATES stateAfter,
                       const std::wstring& debugName) : m_context(context), m_device(device), 
                                                        m_cmdQueue(cmdQueue),
                                                        m_stateAfter(stateAfter)
        {
            assert(m_device);
            assert(m_cmdQueue);

            m_gpuSync = std::make_unique<D3D12GpuSynchronizer>(device, cmdQueue, 1, m_gpuSyncWaitClock);
            assert(m_gpuSync);

            const auto bufferDesc = CreateBufferDesc(alignedSize);
            m_uploadHeap = CreateResourceHeap(m_device, bufferDesc, ResourceHeapType::UploadHeap,
                                              D3D12_RESOURCE_STATE_GENERIC_READ);
            assert(m_uploadHeap);
            m_uploadHeap->SetName((L"Upload heap - Texture - " + debugName).c_str());

            // Create default heap
            m_defaultHeap = CreateResourceHeap(m_device, resourceDesc, ResourceHeapType::DefaultHeap,
                                               D3D12_RESOURCE_STATE_COPY_DEST);
            assert(m_defaultHeap);
            m_defaultHeap->SetName(debugName.c_str());

            AssertIfFailed(context.m_cmdAllocator->Reset());
            AssertIfFailed(context.m_cmdList->Reset(context.m_cmdAllocator.Get(), nullptr));
        }
        
        ID3D12ResourcePtr GetUploadedResource() const { return m_defaultHeap; }

        ~UploaderHelper()
        {
            D3D12_RESOURCE_BARRIER copyDestToReadDest;
            copyDestToReadDest.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            copyDestToReadDest.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
            copyDestToReadDest.Transition.pResource = m_defaultHeap.Get();
            copyDestToReadDest.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            copyDestToReadDest.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
            copyDestToReadDest.Transition.StateAfter = m_stateAfter;

            m_context.m_cmdList->ResourceBarrier(1, &copyDestToReadDest);

            // Execute command list
            AssertIfFailed(m_context.m_cmdList->Close());
            ID3D12CommandList* ppCommandLists[] = { m_context.m_cmdList.Get() };
            m_cmdQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

            // Wait for the command list to finish executing on the gpu
            m_gpuSync->Wait();
        }

    protected:
        const D3D12CommittedResourceAllocator::Context& m_context;

        ID3D12DevicePtr         m_device;
        ID3D12CommandQueuePtr   m_cmdQueue;
        D3D12GpuSynchronizerPtr m_gpuSync;

        D3D12_RESOURCE_STATES m_stateAfter;

        ID3D12ResourcePtr   m_uploadHeap;
        ID3D12ResourcePtr   m_defaultHeap;

        StopClock m_gpuSyncWaitClock;
    };

    class UploadHelperBuffer : public UploaderHelper
    {
    public:
        UploadHelperBuffer(const D3D12CommittedResourceAllocator::Context& context, 
                           ID3D12DevicePtr device, ID3D12CommandQueuePtr cmdQueue,
                           const void* src, size_t sizeBytes, size_t alignedSize, 
                           const std::wstring& debugName) : UploaderHelper(context, device,
                                                                           cmdQueue, 
                                                                           CreateBufferDesc(alignedSize),
                                                                           alignedSize,
                                                                           D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, 
                                                                           debugName),
                                                            m_alignedSize(alignedSize)
        {
            MemCpyBuffer(src, sizeBytes);
            EnqueueBufferCopyCmd();
        }

    private:
        size_t m_alignedSize;

        void MemCpyBuffer(const void* src, size_t sizeBytes)
        {
            BYTE* destUploadHeapPtr = nullptr;
            AssertIfFailed(m_uploadHeap->Map(0, NULL, reinterpret_cast<void**>(&destUploadHeapPtr)));
            assert(destUploadHeapPtr);

            memcpy(destUploadHeapPtr, src, sizeBytes);

            m_uploadHeap->Unmap(0, NULL);
        }

        void EnqueueBufferCopyCmd()
        {
            const UINT64 dstOffset = 0;
            const UINT64 srcOffset = 0;
            const UINT64 numBytes = m_alignedSize;
            m_context.m_cmdList->CopyBufferRegion(m_defaultHeap.Get(), dstOffset, m_uploadHeap.Get(), srcOffset, numBytes);
        }
    };

    class UploadHelperTexture : public UploaderHelper
    {
    public:
        UploadHelperTexture(const D3D12CommittedResourceAllocator::Context& context,
                            ID3D12DevicePtr device, ID3D12CommandQueuePtr cmdQueue,
                            const D3D12_RESOURCE_DESC& resourceDesc,
                            const SubresourcesFootPrint& subresourcesFootPrint,
                            const std::vector<D3D12_SUBRESOURCE_DATA>& subresources,
                            const std::wstring& debugName) : UploaderHelper(context, device,
                                                                            cmdQueue, resourceDesc, 
                                                                            subresourcesFootPrint.m_requiredSize,
                                                                            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                                                                            debugName)
        {
            MemCpySubresources(subresourcesFootPrint, subresources);
            EnqueueTextureCopyCmd(subresourcesFootPrint.m_layouts);
        }

    private:
        void MemCpySubresources(const SubresourcesFootPrint& subresourcesFootPrint,
                                const std::vector<D3D12_SUBRESOURCE_DATA>& subresources)
        {
            BYTE* uploadHeapData = nullptr;
            AssertIfFailed(m_uploadHeap->Map(0, NULL, reinterpret_cast<void**>(&uploadHeapData)));

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

            m_uploadHeap->Unmap(0, NULL);
        }

        void EnqueueTextureCopyCmd(const std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT>& layouts)
        {
            D3D12_TEXTURE_COPY_LOCATION dest;
            dest.pResource = m_defaultHeap.Get();
            dest.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;

            D3D12_TEXTURE_COPY_LOCATION src;
            src.pResource = m_uploadHeap.Get();
            src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;

            for (UINT i = 0; i < layouts.size(); ++i)
            {
                src.PlacedFootprint = layouts[i];
                dest.SubresourceIndex = i;
                m_context.m_cmdList->CopyTextureRegion(&dest, 0, 0, 0, &src, nullptr);
            }
        }
    };
}

D3D12CommittedResourceAllocator::D3D12CommittedResourceAllocator(ID3D12DevicePtr device,
                                                                 ID3D12CommandQueuePtr cmdQueue) :  m_device(device),
                                                                                                    m_cmdQueue(cmdQueue)
{
    assert(m_device);
    assert(m_cmdQueue);
}

D3D12CommittedBuffer D3D12CommittedResourceAllocator::AllocateReadBackBuffer(size_t sizeBytes, size_t alignment,
                                                                             const std::wstring& debugName)
{
    const auto alignedSize = AlignToPowerof2(sizeBytes, alignment);
    const D3D12_RESOURCE_DESC resourceDesc = CreateBufferDesc(alignedSize);

    D3D12_HEAP_PROPERTIES resourceHeapProps;
    resourceHeapProps.Type                  = D3D12_HEAP_TYPE_READBACK;
    resourceHeapProps.CPUPageProperty       = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    resourceHeapProps.MemoryPoolPreference  = D3D12_MEMORY_POOL_UNKNOWN;
    resourceHeapProps.CreationNodeMask      = 1;
    resourceHeapProps.VisibleNodeMask       = 1;

    ID3D12ResourcePtr resource;
    AssertIfFailed(m_device->CreateCommittedResource(&resourceHeapProps, D3D12_HEAP_FLAG_NONE, &resourceDesc,
                                                     D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                     IID_PPV_ARGS(&resource)));
    assert(resource);
    resource->SetName(debugName.c_str());
    assert(IsPowerOf2(alignedSize) && alignedSize != 0 && alignedSize >= sizeBytes);

    return {resource, alignedSize};
}

D3D12CommittedBuffer D3D12CommittedResourceAllocator::AllocateBuffer(const Context& context, const void* data, 
                                                                     size_t sizeBytes, size_t alignment, 
                                                                     const std::wstring& debugName)
{
    AssertContextIsValid(context);
    const auto alignedSize = AlignToPowerof2(sizeBytes, alignment);

    UploadHelperBuffer uploadHelper(context, m_device, m_cmdQueue, data, sizeBytes, alignedSize, debugName);
    auto resource = uploadHelper.GetUploadedResource();
    assert(resource);

    return { resource, alignedSize };
}

ID3D12ResourcePtr D3D12CommittedResourceAllocator::AllocateTexture(const Context& context, 
                                                                   const std::vector<D3D12_SUBRESOURCE_DATA>& subresources,
                                                                   const D3D12_RESOURCE_DESC& desc, 
                                                                   const std::wstring& debugName)
{
    AssertContextIsValid(context);

    auto subresourcesFootPrint = CreateSubresourceFootPrint(m_device, subresources.size(), desc);

    UploadHelperTexture uploadHelper(context, m_device, m_cmdQueue, desc, subresourcesFootPrint, subresources, debugName);

    return uploadHelper.GetUploadedResource();
}

ID3D12ResourcePtr D3D12Basics::CreateResourceHeap(ID3D12DevicePtr device, const D3D12_RESOURCE_DESC& resourceDesc, 
                                                  ResourceHeapType heapType, D3D12_RESOURCE_STATES initialState, 
                                                  const D3D12_CLEAR_VALUE* clearValue)
{
    D3D12_HEAP_PROPERTIES resourceHeap;
    resourceHeap.Type = heapType == ResourceHeapType::DefaultHeap ? D3D12_HEAP_TYPE_DEFAULT : D3D12_HEAP_TYPE_UPLOAD;
    resourceHeap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    resourceHeap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    resourceHeap.CreationNodeMask = 1;
    resourceHeap.VisibleNodeMask = 1;

    ID3D12ResourcePtr resource;
    AssertIfFailed(device->CreateCommittedResource(&resourceHeap, D3D12_HEAP_FLAG_NONE, &resourceDesc, initialState,
                                                    clearValue, IID_PPV_ARGS(&resource)));
    return resource;
}

// NOTE: make these visible to the compiler when required, ie, when instancing DynamicMemoryAlloc in d3d12gpu.cpp
D3D12DynamicBufferAllocation::D3D12DynamicBufferAllocation() = default;
D3D12DynamicBufferAllocation::D3D12DynamicBufferAllocation(D3D12DynamicBufferAllocation&&) = default;
D3D12DynamicBufferAllocation& D3D12DynamicBufferAllocation::operator=(D3D12DynamicBufferAllocation&&) = default;
D3D12DynamicBufferAllocation::~D3D12DynamicBufferAllocation() = default;

D3D12DynamicBufferAllocator::D3D12DynamicBufferAllocator(ID3D12DevicePtr device, 
                                                         size_t pageSizeInBytes) :  m_device(device), 
                                                                                    m_pageSizeInBytes(pageSizeInBytes)
{
    assert(m_device);
    assert(m_pageSizeInBytes > 0);

    AllocatePage();
}

D3D12DynamicBufferAllocator::~D3D12DynamicBufferAllocator()
{
    D3D12_RANGE readRange{ 0, 0 };
    for(auto& page : m_pages)
        page.m_resource->Unmap(0, &readRange);
}

D3D12DynamicBufferAllocation D3D12DynamicBufferAllocator::Allocate(size_t sizeInBytes, size_t alignment)
{
    assert(alignment);
    assert(m_pages.size());
    assert(D3D12Basics::IsPowerOf2(alignment));

    const auto alignedSize = AlignToPowerof2(sizeInBytes, alignment);
    assert(alignedSize >= sizeInBytes);

    D3D12DynamicBufferAllocationBlockPtr freeBlock = nullptr;

    // Linearly search through all the pages for a free block that fits the requested alignedSize
    for (size_t i = 0; i < m_pages.size(); ++i)
    {
        auto& page = m_pages[i];

        auto foundFreeBlock = std::find_if(page.m_freeBlocks.begin(), page.m_freeBlocks.end(), 
                                           [alignedSize, alignment](const auto& freeBlock)
        {
            const auto requiredAlignedSize = TotalFreeBlockAlignedSize(freeBlock->m_offset, alignedSize, alignment);
            return freeBlock->m_size >= requiredAlignedSize;
        });

        if (foundFreeBlock != page.m_freeBlocks.end())
        {
            freeBlock = std::move(*foundFreeBlock);
            page.m_freeBlocks.erase(foundFreeBlock);
            break;
        }
    }

    // No memory no problem. Allocate another page!
    if (!freeBlock)
    {
        AllocatePage();
        auto& lastPage = m_pages.back();
        freeBlock = std::move(lastPage.m_freeBlocks.back());
        lastPage.m_freeBlocks.pop_back();
    }

    assert(freeBlock);
    assert(freeBlock->m_size >= alignedSize);

    const auto alignedOffset = AlignToPowerof2(freeBlock->m_offset, alignment);
    const auto freeBlockOffset = freeBlock->m_offset;
    const auto freeBlockPageIndex = freeBlock->m_pageIndex;

    // Updating the old freeblock with its new size that takes into account the aligned size and the aligned ptr
    const auto totalSize = TotalFreeBlockAlignedSize(freeBlock->m_offset, alignedSize, alignment);;
    freeBlock->m_offset += totalSize;
    freeBlock->m_size -= totalSize;
    assert(freeBlockPageIndex < m_pages.size());
    auto& page = m_pages[freeBlockPageIndex];
    page.m_freeBlocks.push_back(std::move(freeBlock));

    // Calculating the aligned memory ptrs
    D3D12DynamicBufferAllocation allocation;
    allocation.m_cpuPtr = page.m_cpuPtr + alignedOffset;
    allocation.m_gpuPtr = page.m_gpuPtr + alignedOffset;
    allocation.m_size = alignedSize;
    D3D12Basics::D3D12DynamicBufferAllocationBlock allocationBlock { freeBlockOffset, totalSize, freeBlockPageIndex };
    allocation.m_allocationBlock = std::make_unique<D3D12Basics::D3D12DynamicBufferAllocationBlock>(std::move(allocationBlock));

    return allocation;
}

void D3D12DynamicBufferAllocator::Deallocate(D3D12DynamicBufferAllocation& allocation)
{
    assert(allocation.m_allocationBlock);
    assert(allocation.m_allocationBlock->m_pageIndex < m_pages.size());

    auto pageIndex = allocation.m_allocationBlock->m_pageIndex;
    auto& page = m_pages[pageIndex];
    page.m_freeBlocks.push_back(std::move(allocation.m_allocationBlock));

    //// TODO erasing pages does not work. It introduces glitches and crashes. Fix it
    //// TODO does destroying the resource immediately makes sense? would it better to 
    //// have a strategy based on size, frequency of use and other pages size?
    //size_t freeSize = 0;
    //for (auto& freeBlock : page.m_freeBlocks)
    //    freeSize += freeBlock->m_size;

    //// TODO linear time! change to list for const time?
    //if (freeSize == m_pageSizeInBytes)
    //    m_pages.erase(m_pages.begin() + pageIndex);
}

void D3D12DynamicBufferAllocator::AllocatePage()
{
    Page page;
    page.m_resource = D3D12CreateDynamicCommittedBuffer(m_device, m_pageSizeInBytes);
    assert(page.m_resource);

    page.m_gpuPtr = page.m_resource->GetGPUVirtualAddress();
    assert(page.m_gpuPtr);
    // TODO first page allocation passes the assert but not the second one. Investigate!
    //assert(D3D12Basics::IsAlignedToPowerof2(gpuPtr, g_64kb));

    D3D12_RANGE readRange{ 0, 0 };
    D3D12Basics::AssertIfFailed(page.m_resource->Map(0, &readRange, reinterpret_cast<void**>(&page.m_cpuPtr)));
    assert(D3D12Basics::IsAlignedToPowerof2(reinterpret_cast<size_t>(page.m_cpuPtr), g_4kb));

    const size_t pageIndex = m_pages.size();
    D3D12DynamicBufferAllocationBlock allocationBlock{ 0, m_pageSizeInBytes, pageIndex };
    D3D12DynamicBufferAllocationBlockPtr freeBlock = std::make_unique<D3D12DynamicBufferAllocationBlock>(std::move(allocationBlock));
    page.m_freeBlocks.push_back(std::move(freeBlock));

    m_pages.push_back(std::move(page));
}
