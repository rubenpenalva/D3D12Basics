#pragma once

// project includes
#include "utils.h"
#include "d3d12basicsfwd.h"

// c++ includes
#include <vector>

// windows includes
#include "d3d12fwd.h"
#include <dxgi1_4.h>
#include <windows.h>

namespace D3D12Render
{
    using DisplayModes = std::vector<DXGI_MODE_DESC1>;

    class D3D12GpuSynchronizer
    {
    public:
        D3D12GpuSynchronizer(ID3D12DevicePtr device, ID3D12CommandQueuePtr cmdQueue, unsigned int maxFramesInFlight);
        ~D3D12GpuSynchronizer();

        void Wait();

        void WaitAll();

    private:
        ID3D12CommandQueuePtr m_cmdQueue;

        const unsigned int  m_maxFramesInFlight;
        unsigned int        m_framesInFlight;

        HANDLE          m_event;
        ID3D12FencePtr  m_fence;
        UINT64          m_currentFenceValue;
        UINT64          m_nextFenceValue;

        float m_waitTime;

        void SignalWork();

        void WaitForFence(UINT64 fenceValue);
    };

    // TODO add root constants and root descriptors
    // Samplers are always static for now
    enum class D3D12ResourceType
    {
        DynamicConstantBuffer,
        StaticResource,
    };
    struct D3D12Binding
    {
        D3D12ResourceType   m_resourceType;
        D3D12ResourceID     m_resourceID;
    };
    using D3D12Bindings = std::vector<D3D12Binding>;

    struct D3D12GpuRenderTask
    {
        ID3D12PipelineStatePtr  m_pipelineState;
        ID3D12RootSignaturePtr  m_rootSignature;
        D3D12Bindings           m_bindings;
        D3D12_VIEWPORT          m_viewport;
        RECT                    m_scissorRect;
        size_t                  m_vertexBufferResourceID;
        size_t                  m_indexBufferResourceID;
        size_t                  m_vertexCount;
        size_t                  m_vertexSizeBytes;
        size_t                  m_indexCount;
        float                   m_clearColor[4];
    };

    // Creates a d3d12 device bound to the main adapter and the main output
    // with debug capabilities enabled, feature level 11.0 set, 
    // a 3d command queue, a command list, a swap chain, double buffering, etc...
    // Right now is a bag where you have a mix of high level and low level
    // data, ie depth buffer or simplematerial
    class D3D12Gpu
    {
    public:

        // TODO check c++ core guidelines about static class data members naming
        // for better ideas on it
        static const unsigned int   m_framesInFlight        = 2;
        static const unsigned int   m_backBuffersCount      = 2;
        static const bool           m_vsync                 = true;

        D3D12Gpu(bool isWaitableForPresentEnabled);
        
        ~D3D12Gpu();

        // Features support
        unsigned int GetFormatPlaneCount(DXGI_FORMAT format);

        // GPU and output display support
        const D3D12Basics::Resolution& GetSafestResolutionSupported() const { return m_safestResolution; }

        void SetOutputWindow(HWND hwnd);

        const D3D12Basics::Resolution& GetCurrentResolution() const;

        // GPU Resources
        D3D12ResourceID CreateCommittedBuffer(const void* data, size_t  dataSizeBytes, const std::wstring& debugName);

        D3D12ResourceID CreateStaticConstantBuffer(const void* data, size_t dataSizeBytes, const std::wstring& resourceName);

        D3D12ResourceID CreateTexture(const std::vector<D3D12_SUBRESOURCE_DATA>& subresources, const D3D12_RESOURCE_DESC& desc,
                                      const std::wstring& debugName);

        D3D12DynamicResourceID CreateDynamicConstantBuffer(unsigned int sizeInBytes);
        
        void UpdateDynamicConstantBuffer(D3D12DynamicResourceID id, const void* data);

        // GPU Execution
        void ExecuteRenderTasks(const std::vector<D3D12GpuRenderTask>& renderTasks);
        void BeginFrame();
        void FinishFrame();

        // GPU Others
        // TODO think about how to expose this unifying the pipeline state
        ID3D12RootSignaturePtr CreateRootSignature(ID3DBlobPtr signature, const std::wstring& name);
        ID3D12PipelineStatePtr CreatePSO(const D3D12_GRAPHICS_PIPELINE_STATE_DESC& psoDesc, const std::wstring& name);

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
            void*               m_memPtr[m_framesInFlight];
            D3D12DescriptorID   m_cbvID[m_framesInFlight];
        };

        // dxgi data
        IDXGIFactory4Ptr        m_factory;
        IDXGIOutput1Ptr         m_output1;
        DXGI_MODE_DESC1         m_safestDisplayMode;
        D3D12Basics::Resolution m_safestResolution;

        // d3d12 data
        ID3D12DevicePtr m_device;

        ID3D12CommandQueuePtr                       m_graphicsCmdQueue;
        ID3D12GraphicsCommandListPtr                m_cmdLists[m_framesInFlight];
        ID3D12CommandAllocatorPtr                   m_cmdAllocators[m_framesInFlight];
        D3D12GpuSynchronizerPtr                     m_gpuSync;
        unsigned int                                m_currentBackbufferIndex;
        unsigned int                                m_currentFrameIndex;
        D3D12Basics::Timer                          m_frameTime;
        D3D12Basics::Timer                          m_frameWaitTime;

        bool                        m_isWaitableForPresentEnabled;
        D3D12SwapChainPtr           m_swapChain;
        D3D12RTVDescriptorHeapPtr   m_rtvDescriptorHeap;

        D3D12DSVDescriptorHeapPtr   m_dsvDescHeap;
        ID3D12ResourcePtr           m_depthBufferResource;
        D3D12DescriptorID           m_depthBufferDescID;

        // Resources
        D3D12CommittedBufferLoaderPtr   m_committedBufferLoader;
        std::vector<D3D12ResourceExt>   m_resources;
        D3D12CBVSRVUAVDescHeapPtr       m_srvDescHeap;

        // TODO move all these to a ring buffer class
        // Dynamic constant buffer
        ID3D12ResourcePtr                   m_dynamicConstantBuffersHeap;
        const size_t                        m_dynamicConstantBuffersMaxSize;
        size_t                              m_dynamicConstantBuffersCurrentSize;
        D3D12_GPU_VIRTUAL_ADDRESS           m_dynamicConstantBufferHeapCurrentPtr;
        void*                               m_dynamicConstantBuffersMemPtr;         // TODO is caching the memorys start ptr useful?
        void*                               m_dynamicConstantBuffersCurrentMemPtr;
        std::vector<DynamicConstantBuffer>  m_dynamicConstantBuffers;

        DisplayModes EnumerateDisplayModes(DXGI_FORMAT format);

        DXGI_MODE_DESC1 FindClosestDisplayModeMatch(DXGI_FORMAT format, const D3D12Basics::Resolution& resolution);

        void CreateCommandInfrastructure();

        void CreateDevice(IDXGIAdapterPtr adapter);

        IDXGIAdapterPtr CreateDXGIInfrastructure();

        void CreateDescriptorHeaps();

        void CreateDepthBuffer();

        void CreateDynamicConstantBuffersInfrastructure();

        void CheckFeatureSupport();
    };
}