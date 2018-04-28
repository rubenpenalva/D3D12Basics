#pragma once

// project includes
#include "utils.h"
#include "d3d12basicsfwd.h"
#include "d3d12descriptorheap.h"
#include "d3d12committedbuffer.h"

// c++ includes
#include <vector>

// windows includes
#include "d3d12fwd.h"
#include <dxgi1_4.h>
#include <windows.h>

// directx includes
#include <d3d12.h>

namespace D3D12Render
{
    using DisplayModes = std::vector<DXGI_MODE_DESC1>;

    // TODO add root constants and root descriptors
    // Samplers are always static for now
    enum class D3D12ResourceType
    {
        DynamicConstantBuffer,
        StaticResource,
    };
    
    struct D3D12ResourceDescriptor
    {
        D3D12ResourceType   m_resourceType;
        D3D12ResourceID     m_resourceID;
    };

    using D3D12ResourceDescriptorTable = std::vector<D3D12ResourceDescriptor>;

    using D3D12Bindings = std::vector<D3D12ResourceDescriptorTable>;

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

        D3D12ResourceID CreateTexture(const std::vector<D3D12_SUBRESOURCE_DATA>& subresources, const D3D12_RESOURCE_DESC& desc,
                                      const std::wstring& debugName);

        D3D12DynamicResourceID CreateDynamicConstantBuffer(unsigned int sizeInBytes);
        
        // TODO can be done better than void*? maybe uint64_t or one of the pointer types?
        void UpdateDynamicConstantBuffer(D3D12DynamicResourceID id, const void* data, size_t sizeInBytes);

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
            D3D12DescriptorHeapHandlePtr    m_resourceViewHandle;
            ID3D12ResourcePtr               m_resource;
        };
        struct DynamicConstantBuffer
        {
            D3D12BufferAllocation           m_allocation[m_framesInFlight];
            D3D12DescriptorHeapHandlePtr    m_cbvHandle[m_framesInFlight];
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

        D3D12DSVDescriptorHeapPtr       m_dsvDescHeap;
        ID3D12ResourcePtr               m_depthBufferResource;
        D3D12DescriptorHeapHandlePtr    m_depthBufferDescHandle;

        // Resources
        D3D12CommittedBufferLoaderPtr       m_committedBufferLoader;
        std::vector<D3D12ResourceExt>       m_resources;
        D3D12CPUDescriptorBufferPtr         m_cpuSRV_CBVDescHeap;
        D3D12GPUDescriptorRingBufferPtr     m_gpuDescriptorRingBuffer;

        D3D12BufferAllocatorPtr             m_dynamicCBHeap;
        std::vector<DynamicConstantBuffer>  m_dynamicConstantBuffers;

        DisplayModes EnumerateDisplayModes(DXGI_FORMAT format);

        DXGI_MODE_DESC1 FindClosestDisplayModeMatch(DXGI_FORMAT format, const D3D12Basics::Resolution& resolution);

        void CreateCommandInfrastructure();

        void CreateDevice(IDXGIAdapterPtr adapter);

        IDXGIAdapterPtr CreateDXGIInfrastructure();

        void CreateDescriptorHeaps();

        void CreateDepthBuffer();

        void CheckFeatureSupport();
    };
}