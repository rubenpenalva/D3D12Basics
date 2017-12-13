#pragma once

// c++ includes
#include <vector>
#include <memory>

// windows includes
#include <windows.h>
#include <wrl.h>

struct IDXGIFactory4;
struct ID3D12Device;
struct ID3D12CommandQueue;
struct IDXGISwapChain3;
struct IDXGIAdapter1;
struct ID3D12Debug;
struct ID3D12Fence;
struct ID3D12PipelineState;
struct ID3D12CommandList;
struct ID3D12CommandAllocator;
struct ID3D12GraphicsCommandList;
struct D3D12_CPU_DESCRIPTOR_HANDLE;
struct ID3D12RootSignature;
struct ID3D12Resource;

namespace D3D12Render
{
    static const unsigned int BackBuffersCount = 2;

    using IDXGIFactory4Ptr      = Microsoft::WRL::ComPtr<IDXGIFactory4>;
    using IDXGISwapChain3Ptr    = Microsoft::WRL::ComPtr<IDXGISwapChain3>;
    using IDXGIAdapterPtr       = Microsoft::WRL::ComPtr<IDXGIAdapter1>;
    using IDXGIAdapters         = std::vector<IDXGIAdapterPtr>;

    using ID3D12DevicePtr               = Microsoft::WRL::ComPtr<ID3D12Device>;
    using ID3D12DebugPtr                = Microsoft::WRL::ComPtr<ID3D12Debug>;
    using ID3D12CommandQueuePtr         = Microsoft::WRL::ComPtr<ID3D12CommandQueue>;
    using ID3D12GraphicsCommandListPtr  = Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>;
    using ID3D12FencePtr                = Microsoft::WRL::ComPtr<ID3D12Fence>;

    class D3D12Gpu;
    using D3D12GpuPtr = std::shared_ptr<D3D12Gpu>;

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

    class ID3D12GpuJob
    {
    public:
        virtual void Record(D3D12_CPU_DESCRIPTOR_HANDLE backbufferRT) = 0;

    protected:
        ID3D12GraphicsCommandListPtr m_commandList;

        ID3D12GpuJob(ID3D12GraphicsCommandListPtr commandList);
    };
    using ID3D12GpuJobPtr = std::shared_ptr<ID3D12GpuJob>;

    class D3D12Gpu
    {
    public:
        // NOTE: hide this using an abstract interface? ID3D12Gpu?
        //       D3D12Gpus gpus; ID3D12Gpu gpu = gpus.CreateGpu(GpuID(0));
        //       It could be a cool performance test on using high frequency
        //       pure virtual functions
        D3D12Gpu(IDXGIFactory4Ptr factory, IDXGIAdapterPtr adapter, HWND hwnd);
        
        ~D3D12Gpu();

        ID3D12GraphicsCommandListPtr GetCommandList();

        void SetJob(ID3D12GpuJobPtr job);

        void Execute();

        void Flush();

    private:
        template<unsigned int BackBuffersCount>
        class D3D12BackBuffers;
        using D3D12BackBuffersPtr       = std::shared_ptr<D3D12BackBuffers<BackBuffersCount>>;

        using ID3D12PipelineStatePtr    = Microsoft::WRL::ComPtr<ID3D12PipelineState>;
        using ID3D12CommandAllocatorPtr = Microsoft::WRL::ComPtr<ID3D12CommandAllocator>;
        using ID3D12RootSignaturePtr    = Microsoft::WRL::ComPtr<ID3D12RootSignature>;
        using ID3D12ResourcePtr         = Microsoft::WRL::ComPtr<ID3D12Resource>;

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

        // 
        ID3D12PipelineStatePtr  m_defaultPSO;
        ID3D12RootSignaturePtr  m_rootSignature;

        // NOTE: Only one command list with a command allocator at the moment.
        // TODO: Use threads to populate several cmds with several cmdsAllocs
        ID3D12GraphicsCommandListPtr    m_commandList;
        ID3D12CommandAllocatorPtr       m_commandAllocator;
        
        // NOTE: Only one job at the moment
        ID3D12GpuJobPtr m_job;

        unsigned int m_backbufferIndex;

        void WaitForGPU();

        void CreateDefaultPipelineState();

        void CreateCommandList();

        // TODO: does visual studio 2017 support string views already? Check it and investigate when its appropriate
        // to use it
        void CreateCommitedBuffer(ID3D12ResourcePtr** buffer, unsigned int bufferDataSize, const std::wstring& bufferName);
    };
}