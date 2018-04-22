#pragma once

// project includes
#include "d3d12fwd.h"
#include "d3d12gpu_sync.h"

// c++ includes
#include <string>

// directx includes
#include <d3d12.h>

namespace D3D12Render
{
    ID3D12ResourcePtr D3D12CreateCommittedDepthStencil(ID3D12DevicePtr device, unsigned int width, unsigned int height, DXGI_FORMAT format,
                                                       const D3D12_CLEAR_VALUE* clearValue, const std::wstring& debugName);

    ID3D12ResourcePtr D3D12CreateDynamicCommittedBuffer(ID3D12DevicePtr device, size_t dataSizeBytes);

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
}