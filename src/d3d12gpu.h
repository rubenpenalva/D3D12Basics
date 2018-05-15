#pragma once

// project includes
#include "utils.h"
#include "d3d12basicsfwd.h"
#include "d3d12descriptorheap.h"
#include "d3d12committedbuffer.h"

// c++ includes
#include <vector>
#include <list>

// windows includes
#include "d3d12fwd.h"
#include <dxgi1_4.h>
#include <windows.h>

// directx includes
#include <d3d12.h>

namespace D3D12Basics
{
    using DisplayModes = std::vector<DXGI_MODE_DESC1>;

    // TODO store pointers instead of values. Less map accesses>!!!!
    struct D3D12GpuMemoryView
    {
        D3D12GpuMemoryHandle                        m_memHandle;
        std::vector<D3D12DescriptorHeapHandlePtr>   m_descriptors;
    };

    struct D3D1232BitConstants
    {
        size_t                  m_bindingSlot;
        std::vector<uint32_t>   m_data;
    };
    struct D3D12ConstantBufferView
    {
        size_t                m_bindingSlot;
        D3D12GpuMemoryHandle  m_memoryHandle;
    };
    struct D3D12DescriptorTable
    {
        size_t                          m_bindingSlot;
        std::vector<D3D12GpuMemoryView> m_views;
    };
    struct D3D12Bindings
    {
        std::vector<D3D1232BitConstants>        m_32BitConstants;
        std::vector<D3D12ConstantBufferView>    m_constantBufferViews;
        std::vector<D3D12DescriptorTable>       m_descriptorTables;
    };

    struct D3D12GpuRenderTask
    {
        ID3D12PipelineStatePtr  m_pipelineState;
        ID3D12RootSignaturePtr  m_rootSignature;
        D3D12Bindings           m_bindings;
        D3D12_VIEWPORT          m_viewport;
        RECT                    m_scissorRect;
        D3D12GpuMemoryHandle    m_vertexBufferId;
        D3D12GpuMemoryHandle    m_indexBufferId;
        size_t                  m_vertexBufferSizeBytes;
        size_t                  m_vertexSizeBytes;
        size_t                  m_vertexOffset;
        size_t                  m_indexBufferSizeBytes;
        size_t                  m_indexCountPerInstance;
        size_t                  m_indexOffset;
        bool                    m_clear;
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

        // GPU memory handling
        D3D12GpuMemoryHandle AllocateDynamicMemory(size_t  sizeBytes, const std::wstring& debugName);

        D3D12GpuMemoryHandle AllocateStaticMemory(const void* data, size_t  sizeBytes, const std::wstring& debugName);

        D3D12GpuMemoryHandle AllocateStaticMemory(const std::vector<D3D12_SUBRESOURCE_DATA>& subresources, const D3D12_RESOURCE_DESC& desc,
                                                  const std::wstring& debugName);

        // TODO can be done better than void*? maybe uint64_t or one of the pointer types?
        void UpdateMemory(D3D12GpuMemoryHandle memHandle, const void* data, size_t sizeBytes, size_t offsetBytes = 0);

        void FreeMemory(D3D12GpuMemoryHandle memHandle);

        // Views handling
        D3D12GpuMemoryView CreateConstantBufferView(D3D12GpuMemoryHandle memHandle);

        D3D12GpuMemoryView CreateTextureView(D3D12GpuMemoryHandle memHandle, const D3D12_RESOURCE_DESC& desc);

        // GPU Execution
        void ExecuteRenderTasks(const std::vector<D3D12GpuRenderTask>& renderTasks);
        void BeginFrame();
        void FinishFrame();
        void WaitAll();

        // GPU Others
        // TODO think about how to expose this unifying the pipeline state
        ID3D12RootSignaturePtr CreateRootSignature(ID3DBlobPtr signature, const std::wstring& name);
        ID3D12PipelineStatePtr CreatePSO(const D3D12_GRAPHICS_PIPELINE_STATE_DESC& psoDesc, const std::wstring& name);

        // Callbacks
        void OnToggleFullScreen();

        void OnResize(const D3D12Basics::Resolution& resolution);

    private:
        struct StaticMemoryAlloc
        {
            uint64_t                                    m_frameId;
            ID3D12ResourcePtr                           m_resource;
            size_t                                      m_size;
        };
        struct DynamicMemoryAlloc
        {
            uint64_t                                    m_frameId[m_framesInFlight] = { 0,0 };
            D3D12BufferAllocation                       m_allocation[m_framesInFlight];
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
        uint64_t                                    m_currentFrame;
        D3D12Basics::Timer                          m_frameTime;
        D3D12Basics::Timer                          m_frameWaitTime;

        bool                        m_isWaitableForPresentEnabled;
        D3D12SwapChainPtr           m_swapChain;

        // Depth stencil management
        D3D12DSVDescriptorHeapPtr       m_dsvDescHeap;
        ID3D12ResourcePtr               m_depthBufferResource;
        D3D12DescriptorHeapHandlePtr    m_depthBufferDescHandle;

        // Descriptors management (from system memory to local vid mem)
        D3D12CPUDescriptorBufferPtr         m_cpuSRV_CBVDescHeap;       //system memory
        D3D12GPUDescriptorRingBufferPtr     m_gpuDescriptorRingBuffer;  // vidmem

        // Gpu memory management
        // TODO unordered_map is suboptimal. Use a multiindirection vector based as in a packedarray/slot map
        // http://bitsquid.blogspot.ca/2011/09/managing-decoupling-part-4-id-lookup.html
        // http://seanmiddleditch.com/data-structures-for-game-developers-the-slot-map/
        uint64_t                                                        m_nextMemoryHandle;
        std::unordered_map<D3D12GpuMemoryHandle, StaticMemoryAlloc>     m_staticMemoryAllocations;
        std::unordered_map<D3D12GpuMemoryHandle, DynamicMemoryAlloc>    m_dynamicMemoryAllocations;
        std::vector<D3D12GpuMemoryHandle>                               m_retiredAllocations;
        D3D12BufferAllocatorPtr                                         m_dynamicMemoryAllocator;

        DisplayModes EnumerateDisplayModes(DXGI_FORMAT format);

        DXGI_MODE_DESC1 FindClosestDisplayModeMatch(DXGI_FORMAT format, const D3D12Basics::Resolution& resolution);

        void CreateCommandInfrastructure();

        void CreateDevice(IDXGIAdapterPtr adapter);

        IDXGIAdapterPtr CreateDXGIInfrastructure();

        void CreateDescriptorHeaps();

        void CreateDepthBuffer();

        void CheckFeatureSupport();

        D3D12_GPU_VIRTUAL_ADDRESS GetBufferVA(D3D12GpuMemoryHandle memHandle);

        void SetVertexBuffer(D3D12GpuMemoryHandle memHandle, size_t vertexBufferSizeBytes, size_t vertexSizeBytes,
                             ID3D12GraphicsCommandListPtr cmdList);

        void SetIndexBuffer(D3D12GpuMemoryHandle memHandle, size_t indexBufferSizeBytes,
                            ID3D12GraphicsCommandListPtr cmdList);

        void DestroyRetiredAllocations();
    };
}