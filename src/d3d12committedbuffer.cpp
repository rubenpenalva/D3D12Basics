#include "d3d12committedbuffer.h"

using namespace D3D12Render;

// project libs
#include "utils.h"

// c++ libs
#include <cstdint>

// directx
#include <d3d12.h>

using namespace D3D12Basics;

namespace
{
    enum class ResourceHeapType
    {
        DefaultHeap,
        UploadHeap
    };

    D3D12_RESOURCE_DESC CreateBufferDesc(uint64_t sizeBytes)
    {
        D3D12_RESOURCE_DESC resourceDesc;
        resourceDesc.Dimension           = D3D12_RESOURCE_DIMENSION_BUFFER;
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
   
    D3D12_RESOURCE_DESC CreateTextureDesc(unsigned int width, unsigned int height, DXGI_FORMAT format, bool isDepthStencil = false)
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
        resourceDesc.Flags = isDepthStencil? D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL : D3D12_RESOURCE_FLAG_NONE;

        return resourceDesc;
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

    void MapUnmap(const void* data, size_t dataSizeBytes, ID3D12ResourcePtr uploadHeap)
    {
        // NOTE: Check code from d3dx12.h MemcpySubresource to see 
        // how the copy of a resource (subresources, type and dimensions)
        // is done properly
        // MemcpySubresource(&DestData, &pSrcData[i], (SIZE_T)pRowSizesInBytes[i], pNumRows[i], pLayouts[i].Footprint.Depth);

        BYTE* pData;
        AssertIfFailed(uploadHeap->Map(0, NULL, reinterpret_cast<void**>(&pData)));

        BYTE* pDestSlice = reinterpret_cast<BYTE*>(pData);
        const BYTE* pSrcSlice = reinterpret_cast<const BYTE*>(data);
        memcpy(pDestSlice, pSrcSlice, dataSizeBytes);

        uploadHeap->Unmap(0, NULL);
    }
}

ID3D12ResourcePtr D3D12Render::D3D12CreateCommittedDepthStencil(ID3D12DevicePtr device, unsigned int width, unsigned int height, 
                                                                DXGI_FORMAT format, const D3D12_CLEAR_VALUE* clearValue, 
                                                                const std::wstring& debugName)
{
    D3D12_RESOURCE_DESC resourceDesc = CreateTextureDesc(width, height, format, true);

    ID3D12ResourcePtr resource = CreateResourceHeap(device, resourceDesc, ResourceHeapType::DefaultHeap, 
                                                    D3D12_RESOURCE_STATE_DEPTH_WRITE, clearValue);
    assert(resource);
    resource->SetName(debugName.c_str());
    
    return resource;
}

ID3D12ResourcePtr D3D12Render::D3D12CreateDynamicCommittedBuffer(ID3D12DevicePtr device, size_t dataSizeBytes)
{
    D3D12_RESOURCE_DESC resourceDesc = CreateBufferDesc(dataSizeBytes);
    ID3D12ResourcePtr resource = CreateResourceHeap(device, resourceDesc, ResourceHeapType::UploadHeap, 
                                                    D3D12_RESOURCE_STATE_GENERIC_READ);
    assert(resource);

    return resource;
}


D3D12CommittedBufferLoader::D3D12CommittedBufferLoader(ID3D12DevicePtr device, ID3D12CommandAllocatorPtr cmdAllocator,
                                                       ID3D12CommandQueuePtr cmdQueue, 
                                                       ID3D12GraphicsCommandListPtr cmdList)    :   m_device(device),
                                                                                                    m_cmdAllocator(cmdAllocator),
                                                                                                    m_cmdQueue(cmdQueue),
                                                                                                    m_cmdList(cmdList),
                                                                                                    m_gpuLockWait(device, cmdQueue)
{
    assert(m_device);
    assert(m_cmdList);
    assert(m_cmdAllocator);
    assert(m_cmdQueue);
}

D3D12CommittedBufferLoader::~D3D12CommittedBufferLoader()
{
}

// TODO: batch this
ID3D12ResourcePtr D3D12CommittedBufferLoader::Upload(const void* data, size_t dataSizeBytes, const std::wstring& resourceName)
{
    D3D12_RESOURCE_DESC resourceDesc = CreateBufferDesc(dataSizeBytes);
    
    return UploadInternal(data, dataSizeBytes, resourceDesc, resourceName, CopyType::Buffer);
}

ID3D12ResourcePtr D3D12CommittedBufferLoader::Upload(const void* data, size_t dataSizeBytes, unsigned int width, unsigned int height,
                                                    DXGI_FORMAT format, const std::wstring& resourceName)
{
    D3D12_RESOURCE_DESC resourceDesc = CreateTextureDesc(width, height, format);

    return UploadInternal(data, dataSizeBytes, resourceDesc, resourceName, CopyType::Texture);
}

ID3D12ResourcePtr D3D12CommittedBufferLoader::UploadInternal(const void* data, size_t dataSizeBytes, const D3D12_RESOURCE_DESC& resourceDesc,
                                                             const std::wstring& resourceName, CopyType copyType)
{
    // Create upload heap
    const auto uploadBufferDesc = CreateBufferDesc(dataSizeBytes);
    ID3D12ResourcePtr uploadHeap = CreateResourceHeap(m_device, uploadBufferDesc, ResourceHeapType::UploadHeap,
                                                      D3D12_RESOURCE_STATE_GENERIC_READ);
    assert(uploadHeap);

    // Copy data to upload heap
    MapUnmap(data, dataSizeBytes, uploadHeap);

    // Create default heap
    ID3D12ResourcePtr defaultHeap = CreateResourceHeap(m_device, resourceDesc, ResourceHeapType::DefaultHeap, 
                                                       D3D12_RESOURCE_STATE_COPY_DEST);
    assert(defaultHeap);
    defaultHeap->SetName(resourceName.c_str());

    AssertIfFailed(m_cmdAllocator->Reset());
    AssertIfFailed(m_cmdList->Reset(m_cmdAllocator.Get(), nullptr));

    if (copyType == CopyType::Buffer)
        EnqueueBufferCopyCmd(uploadHeap, defaultHeap, dataSizeBytes);
    else if (copyType == CopyType::Texture)
        EnqueueTextureCopyCmd(uploadHeap, defaultHeap, resourceDesc);
    else
        assert(true);

    // Add barrier
    // TODO: this is ugly af. Is there a better way of exposing this functionality? Check d3d12x.h for ideas.
    // Maybe some c++ template magic?
    D3D12_RESOURCE_BARRIER copyDestToReadDest;
    copyDestToReadDest.Type                     = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    copyDestToReadDest.Flags                    = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    copyDestToReadDest.Transition.pResource     = defaultHeap.Get();
    copyDestToReadDest.Transition.Subresource   = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    copyDestToReadDest.Transition.StateBefore   = D3D12_RESOURCE_STATE_COPY_DEST;
    copyDestToReadDest.Transition.StateAfter    = copyType == CopyType::Buffer? D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER : 
                                                                                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

    m_cmdList->ResourceBarrier(1, &copyDestToReadDest);

    // Execute command list
    AssertIfFailed(m_cmdList->Close());
    ID3D12CommandList* ppCommandLists[] = { m_cmdList.Get() };
    m_cmdQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

    // Wait for the command list to finish executing on the gpu
    m_gpuLockWait.Wait();

    // Release upload heap

    return defaultHeap;
}

void D3D12CommittedBufferLoader::EnqueueBufferCopyCmd(ID3D12ResourcePtr uploadHeap, ID3D12ResourcePtr defaultHeap, size_t dataSizeBytes)
{
    const UINT64 dstOffset = 0;
    const UINT64 srcOffset = 0;
    const UINT64 numBytes = dataSizeBytes;
    m_cmdList->CopyBufferRegion(defaultHeap.Get(), dstOffset, uploadHeap.Get(), srcOffset, numBytes);
}

void D3D12CommittedBufferLoader::EnqueueTextureCopyCmd(ID3D12ResourcePtr uploadHeap, ID3D12ResourcePtr defaultHeap,
                                                      const D3D12_RESOURCE_DESC& resourceDesc)
{
    const unsigned int firstSubresource = 0;
    const unsigned int numSubresources = 1;
    const unsigned int intermediateOffset = 0;
    UINT64 requiredSize = 0;
    const size_t maxSubresources = 1;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layouts[maxSubresources];
    unsigned int numRows[maxSubresources];
    UINT64 rowSizesInBytes[maxSubresources];
    m_device->GetCopyableFootprints(&resourceDesc, firstSubresource, numSubresources, intermediateOffset, layouts, 
                                    numRows, rowSizesInBytes, &requiredSize);

    D3D12_TEXTURE_COPY_LOCATION dest;
    dest.pResource = defaultHeap.Get();
    dest.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dest.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION src;
    src.pResource = uploadHeap.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint = layouts[0];

    m_cmdList->CopyTextureRegion(&dest, 0, 0, 0, &src, nullptr);
}