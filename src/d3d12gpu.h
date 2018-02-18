#pragma once

// project includes
#include "utils.h"
#include "d3d12basicsfwd.h"
#include "d3d12simplematerial.h"

// c++ includes
#include <vector>

// windows includes
#include "d3d12fwd.h"
#include <dxgi1_4.h>
#include <windows.h>

namespace D3D12Render
{
    using DisplayModes = std::vector<DXGI_MODE_DESC1>;

    class D3D12GpuLockWait
    {
    public:
        D3D12GpuLockWait(ID3D12DevicePtr device, ID3D12CommandQueuePtr cmdQueue);
        ~D3D12GpuLockWait();

        void Wait();

    private:
        ID3D12CommandQueuePtr m_cmdQueue;

        HANDLE          m_event;
        ID3D12FencePtr  m_fence;
        UINT64          m_nextFenceValue;
    };

    struct D3D12GpuRenderTask
    {
        D3D12SimpleMaterialResources    m_simpleMaterialResources;
        D3D12_VIEWPORT                  m_viewport;
        RECT                            m_scissorRect;
        size_t                          m_vertexBufferResourceID;
        size_t                          m_indexBufferResourceID;
        size_t                          m_vertexCount;
        size_t                          m_vertexSize;
        size_t                          m_indexCount;
        float                           m_clearColor[4];
    };

    // Creates a d3d12 device bound to the main adapter and the main output
    // with debug capabilities enabled, feature level 11.0 set, 
    // a 3d command queue, a command list, a swap chain, etc...
    // Right now is a bag where you have a mix of high level and low level
    // data, ie depth buffer or simplematerial
    class D3D12Gpu
    {
    public:
        D3D12Gpu();
        
        ~D3D12Gpu();

        // GPU and output display support
        const D3D12Basics::Resolution& GetSafestResolutionSupported() const { return m_safestResolution; }

        void SetOutputWindow(HWND hwnd);

        const D3D12Basics::Resolution& GetCurrentResolution() const;

        // GPU Resources
        D3D12ResourceID CreateCommittedBuffer(const void* data, size_t  dataSizeBytes, const std::wstring& debugName);

        D3D12ResourceID CreateTexture(const void* data, size_t dataSizeBytes, unsigned int width, unsigned int height, 
                                      DXGI_FORMAT format, const std::wstring& debugName);

        D3D12DynamicResourceID CreateDynamicConstantBuffer(unsigned int sizeInBytes);
        
        void UpdateDynamicConstantBuffer(D3D12DynamicResourceID id, const void* data);

        // GPU Execution
        void ExecuteRenderTasks(const std::vector<D3D12GpuRenderTask>& renderTasks);
        void FinishFrame();

        // Callbacks
        void OnToggleFullScreen();

        void OnResize(const D3D12Basics::Resolution& resolution);

    private:
        struct D3D12ResourceExt
        {
            D3D12DescriptorID m_resourceViewID;
            ID3D12ResourcePtr m_resource;
        };
        struct DynamicConstantBuffer
        {
            uint64_t            m_sizeInBytes;
            uint64_t            m_requiredSizeInBytes;
            void*               m_memPtr;
            D3D12DescriptorID   m_cbvID;
        };

        // dxgi data
        IDXGIFactory4Ptr        m_factory;
        IDXGIOutput1Ptr         m_output1;
        DXGI_MODE_DESC1         m_safestDisplayMode;
        D3D12Basics::Resolution m_safestResolution;

        // d3d12 data
        ID3D12DevicePtr m_device;
        
        ID3D12CommandQueuePtr           m_graphicsCmdQueue;
        ID3D12GraphicsCommandListPtr    m_cmdList;
        ID3D12CommandAllocatorPtr       m_cmdAllocator;
        D3D12GpuLockWaitPtr             m_gpuLockWait;

        D3D12SwapChainPtr           m_swapChain;
        D3D12RTVDescriptorHeapPtr   m_rtvDescriptorHeap;

        D3D12DSVDescriptorHeapPtr   m_dsvDescHeap;
        ID3D12ResourcePtr           m_depthBufferResource;
        D3D12DescriptorID           m_depthBufferDescID;

        D3D12SimpleMaterialPtr m_simpleMaterial;

        // Resources
        D3D12CommittedBufferLoaderPtr   m_committedBufferLoader;
        std::vector<D3D12ResourceExt>   m_resources;
        D3D12CBVSRVUAVDescHeapPtr       m_srvDescHeap;

        // TODO move this to a ring buffer class
        // Dynamic constant buffer
        ID3D12ResourcePtr                   m_dynamicConstantBuffersHeap;
        D3D12_GPU_VIRTUAL_ADDRESS           m_dynamicConstantBufferHeapCurrentPtr;
        void*                               m_dynamicConstantBuffersMemPtr;
        std::vector<DynamicConstantBuffer>  m_dynamicConstantBuffers;

        DisplayModes EnumerateDisplayModes(DXGI_FORMAT format);

        DXGI_MODE_DESC1 FindClosestDisplayModeMatch(DXGI_FORMAT format, const D3D12Basics::Resolution& resolution);

        void CreateCommandInfrastructure();

        void CreateDevice(IDXGIAdapterPtr adapter);

        IDXGIAdapterPtr CreateDXGIInfrastructure();

        void CreateDescriptorHeaps();

        void CreateDepthBuffer();

        void CreateDynamicConstantBuffersInfrastructure();

        void BindSimpleMaterialResources(const D3D12SimpleMaterialResources& simpleMaterialResources);
    };
}