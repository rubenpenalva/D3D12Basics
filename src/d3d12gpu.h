#pragma once

// project includes
#include "utils.h"
#include "d3d12basicsfwd.h"
#include "d3d12descriptorheap.h"
#include "d3d12committedresources.h"

// c++ includes
#include <vector>
#include <list>
#include <array>

// windows includes
#include "d3d12fwd.h"
#include <dxgi1_4.h>
#include <windows.h>

// directx includes
#include <d3d12.h>

namespace D3D12Basics
{
    using DisplayModes = std::vector<DXGI_MODE_DESC1>;

    enum TransitionType
    {
        Present_To_RenderTarget,
        RenderTarget_To_Present,
        TransitionType_COUNT
    };

    struct FrameStats
    {
        StopClock m_presentTime;
        StopClock m_waitForPresentTime;
        StopClock m_waitForFenceTime;
        StopClock m_frameTime;
        std::vector<std::pair<std::wstring, StopClock::SplitTimeBuffer>> m_cmdListTimes;
    };

    struct D3D12GpuHandle
    {
        using HandleType = size_t;
        static const HandleType m_invalidId = std::numeric_limits<HandleType>::max();
        static const HandleType m_nullId = std::numeric_limits<HandleType>::max() - 1;
        static bool IsValid(HandleType id) { return id != m_invalidId; }
        static bool IsNull(HandleType id) { return id == m_nullId; }

        HandleType m_id = m_invalidId;
        bool IsValid() const { return m_id != m_invalidId; }
        bool IsNull() const { return m_id == m_nullId; }
        void Reset() { m_id = m_invalidId; }
    };

    struct D3D12GpuMemoryHandle : public D3D12GpuHandle {};
    struct D3D12GpuViewHandle : public D3D12GpuHandle {};

    struct GpuTexture
    {
        D3D12GpuMemoryHandle    m_memHandle;

        D3D12GpuViewHandle      m_srv;
        D3D12GpuViewHandle      m_dsv;
    };

    struct Buffer
    {
        D3D12GpuMemoryHandle    m_memHandle;

        D3D12GpuViewHandle      m_cbv;
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
        size_t                              m_bindingSlot;
        std::vector<D3D12GpuViewHandle>     m_views;
    };
    struct D3D12Bindings
    {
        std::vector<D3D1232BitConstants>        m_32BitConstants;
        std::vector<D3D12ConstantBufferView>    m_constantBufferViews;
        std::vector<D3D12DescriptorTable>       m_descriptorTables;
    };

    struct D3D12GpuShareableState;
    using D3D12GpuShareableStatePtr = std::unique_ptr<D3D12GpuShareableState>;

    struct D3D12GpuConfig
    {
        // TODO check c++ core guidelines about static class data members naming
        // for better ideas on it
        static const unsigned int   m_framesInFlight    = 2;
        static const unsigned int   m_backBuffersCount  = 2;
        static const bool           m_vsync             = true;
    };

    class D3D12CmdListTimeStamp;
    using D3D12CmdListTimeStampPtr = std::unique_ptr<D3D12CmdListTimeStamp>;

    class D3D12GraphicsCmdList
    {
    public:
        D3D12GraphicsCmdList(D3D12GpuShareableState* gpuState, D3D12CommittedResourceAllocator* committedAllocator,
                             UINT64 cmdQueueTimestampFrequency, StopClock::SplitTimeBuffer& splitTimes,
                             const std::wstring& debugName);

        // Note Forcing the compiler to use a definition of the destructor in order to
        // not trigger a default inline destructor usage. In that case the compiler will
        // need the complete definition of D3D12CmdListTimeStamp.
        ~D3D12GraphicsCmdList();

        void Open();

        void Close();

        ID3D12GraphicsCommandListPtr GetCmdList() const { return m_cmdList; }

    private:
        D3D12GpuShareableState*     m_gpuState;
        D3D12CmdListTimeStampPtr    m_timeStamp;

        ID3D12GraphicsCommandListPtr    m_cmdList;
        ID3D12CommandAllocatorPtr       m_cmdAllocators[D3D12GpuConfig::m_framesInFlight];
    };
    using D3D12GraphicsCmdListPtr   = std::unique_ptr<D3D12GraphicsCmdList>;
    using D3D12CmdLists             = std::vector<ID3D12CommandList*>;

    // Creates a d3d12 device bound to the main adapter and the main output
    // with debug capabilities enabled, feature level 11.0 set, 
    // a 3d command queue, a command list, a swap chain, double buffering, etc...
    // Right now is a bag where you have a mix of high level and low level
    // data, ie depth buffer or simplematerial
    class D3D12Gpu
    {
    public:
        D3D12Gpu(bool isWaitableForPresentEnabled);

        ~D3D12Gpu();

        // Features support
        unsigned int GetFormatPlaneCount(DXGI_FORMAT format) const;

        // GPU and output display support
        const D3D12Basics::Resolution& GetSafestResolutionSupported() const { return m_safestResolution; }

        void SetOutputWindow(HWND hwnd);

        const D3D12Basics::Resolution& GetCurrentResolution() const;

        uint64_t GetCurrentFrameId() const { return m_currentFrame; }

        bool IsFrameFinished(uint64_t frameId);

        // GPU memory handling
        D3D12GpuMemoryHandle AllocateDynamicMemory(size_t  sizeBytes, const std::wstring& debugName);

        D3D12GpuMemoryHandle AllocateStaticMemory(const void* data, size_t  sizeBytes, 
                                                  const std::wstring& debugName);

        D3D12GpuMemoryHandle AllocateStaticMemory(const std::vector<D3D12_SUBRESOURCE_DATA>& subresources,
                                                  const D3D12_RESOURCE_DESC& desc,
                                                  const std::wstring& debugName);

        D3D12GpuMemoryHandle AllocateStaticMemory(const D3D12_RESOURCE_DESC& desc, 
                                                  D3D12_RESOURCE_STATES initialState,
                                                  const D3D12_CLEAR_VALUE* clearValue, 
                                                  const std::wstring& debugName);

        // TODO can be done better than void*? maybe uint64_t or one of the pointer types?
        void UpdateMemory(D3D12GpuMemoryHandle memHandle, const void* data, size_t sizeBytes, 
                            size_t offsetBytes = 0);

        void FreeMemory(D3D12GpuMemoryHandle memHandle);

        // Views handling
        D3D12GpuViewHandle CreateConstantBufferView(D3D12GpuMemoryHandle memHandle);

        D3D12GpuViewHandle CreateTextureView(D3D12GpuMemoryHandle memHandle, const D3D12_RESOURCE_DESC& desc);

        D3D12GpuViewHandle CreateRenderTargetView(D3D12GpuMemoryHandle memHandle, const D3D12_RESOURCE_DESC& desc);

        D3D12GpuViewHandle CreateDepthStencilView(D3D12GpuMemoryHandle memHandle, DXGI_FORMAT format);

        D3D12GpuViewHandle CreateNULLTextureView(const D3D12_RESOURCE_DESC& desc);

        // TODO what about destroying the views?

        // Swapchain
        // TODO rename it to Barrier better?
        D3D12_RESOURCE_BARRIER& SwapChainTransition(TransitionType transitionType);
        const D3D12_CPU_DESCRIPTOR_HANDLE& SwapChainBackBufferViewHandle() const;

        // Execution
        D3D12GraphicsCmdListPtr CreateCmdList(const std::wstring& debugName);
        void ExecuteCmdLists(const D3D12CmdLists& cmdLists);
        void PresentFrame();
        void WaitAll();

        // Pipeline state
        // TODO think about how to expose this unifying the pipeline state
        ID3D12RootSignaturePtr CreateRootSignature(ID3DBlobPtr signature, const std::wstring& name);
        ID3D12PipelineStatePtr CreatePSO(const D3D12_GRAPHICS_PIPELINE_STATE_DESC& psoDesc, const std::wstring& name);

        // Callbacks
        void OnToggleFullScreen();
        void OnResize(const D3D12Basics::Resolution& resolution);

        // Utils
        const FrameStats& GetFrameStats() const { return m_frameStats; }

        // Others
        // NOTE not sure about these ones here. Exposing too much detail? 
        //      move them to other classes ie, an extended cmd list class?
        void SetBindings(ID3D12GraphicsCommandListPtr cmdList, const D3D12Bindings& bindings);
        void SetVertexBuffer(ID3D12GraphicsCommandListPtr cmdList, D3D12GpuMemoryHandle memHandle,
                             size_t vertexBufferSizeBytes, size_t vertexSizeBytes);
        void SetIndexBuffer(ID3D12GraphicsCommandListPtr cmdList, D3D12GpuMemoryHandle memHandle,
                            size_t indexBufferSizeBytes);
        D3D12_CPU_DESCRIPTOR_HANDLE GetViewCPUHandle(D3D12GpuViewHandle gpuViewHandle) const;
        ID3D12Resource* GetResource(D3D12GpuMemoryHandle memHandle);

    private:
        using DescriptorHandlesPtrs = std::array<D3D12DescriptorAllocation*, D3D12GpuConfig::m_framesInFlight>;

        struct D3D12GpuMemoryView
        {
            D3D12GpuMemoryView() = default;

            D3D12GpuMemoryView(D3D12GpuMemoryHandle handle,
                               DescriptorHandlesPtrs&& descriptors) :   m_memHandle(handle),
                                                                        m_frameDescriptors(std::move(descriptors))
            {
            }

            D3D12GpuMemoryHandle    m_memHandle;
            DescriptorHandlesPtrs   m_frameDescriptors;
        };
        using D3D12GpuMemoryViewPtr = std::unique_ptr<D3D12GpuMemoryView>;

        struct StaticMemoryAlloc
        {
            uint64_t            m_frameId;
        };
        struct StaticBufferAlloc : StaticMemoryAlloc
        {
            D3D12CommittedBuffer m_committedBuffer;
        };
        struct StaticTextureAlloc : StaticMemoryAlloc
        {
            ID3D12ResourcePtr   m_resource;
        };
        // TODO allocate the memory on demand instead of pre allocating the maximum needed
        struct DynamicMemoryAlloc
        {
            uint64_t                        m_frameId[D3D12GpuConfig::m_framesInFlight] = { 0 };
            D3D12DynamicBufferAllocation    m_allocation[D3D12GpuConfig::m_framesInFlight];
        };

        // dxgi data
        IDXGIFactory4Ptr        m_factory;
        IDXGIOutput1Ptr         m_output1;
        DXGI_MODE_DESC1         m_safestDisplayMode;
        D3D12Basics::Resolution m_safestResolution;

        // d3d12 data
        // TODO #6. this is used by cmdlist and other classes as internal data not exposed to
        // the user of that class. This way D3D12Gpu doesnt have to expose the data thats
        // required by the other classes like cmdlist
        D3D12GpuShareableStatePtr m_state;

        ID3D12CommandQueuePtr                       m_graphicsCmdQueue;
        UINT64                                      m_cmdQueueTimestampFrequency;

        D3D12GpuSynchronizerPtr         m_gpuSync;
        uint64_t                        m_currentFrame;

        bool                        m_isWaitableForPresentEnabled;
        D3D12SwapChainPtr           m_swapChain;

        // Descriptor
        D3D12DSVDescriptorPoolPtr           m_dsvDescPool;              // system memory
        D3D12CBV_SRV_UAVDescriptorBufferPtr m_cpuSRV_CBVDescHeap;       // system memory
        D3D12RTVDescriptorBufferPtr         m_cpuRTVDescHeap;           // system memory
        D3D12GPUDescriptorRingBufferPtr     m_gpuDescriptorRingBuffer;  // vidmem

        // TODO wrap this into its own class?
        // Gpu memory management
        // TODO unordered_map is suboptimal. Use a multiindirection vector based as in a packedarray/slot map
        // http://bitsquid.blogspot.ca/2011/09/managing-decoupling-part-4-id-lookup.html
        // http://seanmiddleditch.com/data-structures-for-game-developers-the-slot-map/
        D3D12GpuHandle::HandleType                                          m_nextHandleId;
        std::unordered_map<D3D12GpuHandle::HandleType, StaticBufferAlloc>   m_staticBufferMemoryAllocations;
        std::unordered_map<D3D12GpuHandle::HandleType, StaticTextureAlloc>  m_staticTextureMemoryAllocations;
        std::unordered_map<D3D12GpuHandle::HandleType, DynamicMemoryAlloc>  m_dynamicMemoryAllocations;
        std::vector<D3D12GpuMemoryHandle>                                   m_retiredAllocations;
        D3D12DynamicBufferAllocatorPtr                                      m_dynamicMemoryAllocator;
        D3D12CommittedResourceAllocatorPtr                                  m_committedResourceAllocator;

        std::vector<D3D12GpuMemoryViewPtr>  m_memoryViews;

        FrameStats                              m_frameStats;

        DisplayModes EnumerateDisplayModes(DXGI_FORMAT format);

        DXGI_MODE_DESC1 FindClosestDisplayModeMatch(DXGI_FORMAT format, const D3D12Basics::Resolution& resolution);

        void CreateCommandInfrastructure();

        void CreateDevice(IDXGIAdapterPtr adapter);

        IDXGIAdapterPtr CreateDXGIInfrastructure();

        void CreateDescriptorHeaps();

        void CheckFeatureSupport();

        D3D12_GPU_VIRTUAL_ADDRESS GetBufferVA(D3D12GpuMemoryHandle memHandle);

        void DestroyRetiredAllocations();

        D3D12GpuViewHandle CreateView(D3D12GpuMemoryHandle memHandle, DescriptorHandlesPtrs&& descriptors);
    };
}