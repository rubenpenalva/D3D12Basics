#pragma once

// c++ includes
#include <vector>
#include <memory>
#include <functional>

// windows includes
#include <windows.h>
#include <d3d12.h>

// d3d12 fwd decl
#include "d3d12fwd.h"

// project includes
#include "d3d12basicsfwd.h"

namespace D3D12Render
{
    static const unsigned int BackBuffersCount = 2;

    class D3D12Gpus
    {
    public:
        using GpuID = size_t;

        D3D12Gpus();
        ~D3D12Gpus();

        size_t Count() const;

        D3D12GpuPtr CreateGpu(GpuID id, HWND hwnd);

    private:
        IDXGIFactory4Ptr m_factory;
        IDXGIAdapters m_adapters;
        
        void DiscoverAdapters();
    };

    struct D3D12GpuUploadTexture2DTask
    {
        void*                               m_data;
        unsigned int                        m_dataSize;
        unsigned int                        m_width;
        unsigned int                        m_height;
        D3D12_SHADER_RESOURCE_VIEW_DESC     m_desc;
        std::wstring                        m_debugName;
    };

    struct D3D12GpuUploadBufferTask
    {
        void*           m_bufferData;
        unsigned int    m_bufferDataSize;
        std::wstring    m_bufferName;
    };

    struct D3D12GpuRenderTask
    {
        D3D12SimpleMaterialPtr      m_simpleMaterial;
        D3D12_VIEWPORT              m_viewport;
        RECT                        m_scissorRect;
        size_t                      m_vertexBufferResourceID;
        size_t                      m_indexBufferResourceID;
        unsigned int                m_vertexCount;
        unsigned int                m_vertexSize;
        unsigned int                m_indexCount;
        float                       m_clearColor[4];
    };

    class D3D12Gpu
    {
    public:
        // NOTE: hide this using an abstract interface? ID3D12Gpu?
        //       D3D12Gpus gpus; ID3D12Gpu gpu = gpus.CreateGpu(GpuID(0));
        //       It could be a cool performance test on using high frequency
        //       pure virtual functions
        // NOTE: Hidden those details shouldn't be done through runtime polymorphism
        //       but compile time.
        D3D12Gpu(IDXGIFactory4Ptr factory, IDXGIAdapterPtr adapter, HWND hwnd);
        
        ~D3D12Gpu();

        ID3D12DevicePtr GetDevice() const { return m_device; }

        ID3D12DescriptorHeapPtr GetSRVDescriptorHeap() const { return m_cbvsrvuavDescriptorHeap; };

        void AddUploadTexture2DTask(const D3D12GpuUploadTexture2DTask& uploadTask);
        
        size_t AddUploadBufferTask(const D3D12GpuUploadBufferTask& uploadTask);

        void AddRenderTask(const D3D12GpuRenderTask& renderTask);

        size_t CreateDynamicConstantBuffer(unsigned int sizeInBytes);

        void UpdateDynamicConstantBuffer(size_t id, void* data);

        void Execute();

        void Flush();

    private:
        using D3D12GpuUploadBufferTaskExt = std::pair<size_t, D3D12GpuUploadBufferTask>;
        using D3D12GpuUploadTexture2DTaskExt = std::pair<size_t, D3D12GpuUploadTexture2DTask>;

        template<unsigned int BackBuffersCount>
        class D3D12BackBuffers;
        using D3D12BackBuffersPtr       = std::shared_ptr<D3D12BackBuffers<BackBuffersCount>>;

        ID3D12DevicePtr         m_device;
        ID3D12CommandQueuePtr   m_commandQueue;
        IDXGISwapChain3Ptr      m_swapChain;

        // Synchronization objects.
        //UINT m_frameIndex;
        HANDLE          m_fenceEvent;
        ID3D12FencePtr  m_fence;
        UINT64          m_fenceValue;

        // Back buffer rts
        D3D12BackBuffersPtr m_backbuffers;

        // NOTE: Only one command list with a command allocator at the moment.
        // TODO: Use threads to populate several cmds with several cmdsAllocs
        ID3D12GraphicsCommandListPtr    m_commandList;
        ID3D12CommandAllocatorPtr       m_commandAllocator;
        
        // NOTE: Only one job at the moment
        ID3D12GpuJobPtr m_job;

        unsigned int m_backbufferIndex;
                
        ID3D12DescriptorHeapPtr m_cbvsrvuavDescriptorHeap;
        unsigned int m_cbvsrvuavDescriptorSize;
        size_t m_cbvsrvuavDescriptorOffset;

        std::vector<D3D12GpuUploadTexture2DTaskExt>    m_uploadTexture2DTasks;
        std::vector<D3D12GpuUploadBufferTaskExt>       m_uploadBufferTasks;

        std::vector<D3D12GpuRenderTask> m_renderTasks;

        std::vector<ID3D12ResourcePtr> m_resources;

        ID3D12ResourcePtr m_dynamicConstantBuffersHeap;
        D3D12_GPU_VIRTUAL_ADDRESS m_dynamicConstantBufferHeapCurrentPtr;
        void* m_dynamicConstantBuffersMemPtr;
        struct DynamicConstantBuffer
        {
            uint64_t m_sizeInBytes;
            uint64_t m_requiredSizeInBytes;
            void* m_memPtr;
        };
        std::vector<DynamicConstantBuffer> m_dynamicConstantBuffers;

        void WaitForGPU();

        void CreateCommandList();

        // TODO: does visual studio 2017 support string views already? Check it and investigate when its appropriate
        // to use it
        ID3D12ResourcePtr CreateCommitedBuffer(ID3D12ResourcePtr* buffer, void* bufferData, unsigned int bufferDataSize, const std::wstring& bufferName);

        ID3D12ResourcePtr CreateCommitedTexture2D(ID3D12ResourcePtr* resource, void* data, unsigned int dataSize,
                                                  unsigned int width, unsigned int height, const std::wstring& debugName);

        void RecordRenderTask(const D3D12GpuRenderTask& renderTask, D3D12_CPU_DESCRIPTOR_HANDLE backbufferRT);
    };


}