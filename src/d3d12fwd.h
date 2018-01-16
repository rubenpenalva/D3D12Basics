#pragma once

// windows includes
#include <wrl.h>

struct IDXGIFactory4;
struct IDXGISwapChain3;
struct IDXGIAdapter1;
struct ID3D12Device;
struct ID3D12Debug;
struct ID3D12CommandQueue;
struct ID3D12GraphicsCommandList;
struct ID3D12Fence;
struct ID3D12PipelineState;
struct ID3D12CommandAllocator;
struct ID3D12RootSignature;
struct ID3D12Resource;
struct ID3D12DescriptorHeap;

// TODO is there a better way to forward declare a typedef?
struct ID3D10Blob;
typedef ID3D10Blob ID3DBlob;

struct D3D12_CPU_DESCRIPTOR_HANDLE;
struct D3D12_FEATURE_DATA_ROOT_SIGNATURE;

namespace D3D12Render
{
    using IDXGIFactory4Ptr              = Microsoft::WRL::ComPtr<IDXGIFactory4>;
    using IDXGISwapChain3Ptr            = Microsoft::WRL::ComPtr<IDXGISwapChain3>;
    using IDXGIAdapterPtr               = Microsoft::WRL::ComPtr<IDXGIAdapter1>;

    using ID3D12DevicePtr               = Microsoft::WRL::ComPtr<ID3D12Device>;
    using ID3D12DebugPtr                = Microsoft::WRL::ComPtr<ID3D12Debug>;
    using ID3D12CommandQueuePtr         = Microsoft::WRL::ComPtr<ID3D12CommandQueue>;
    using ID3D12GraphicsCommandListPtr  = Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>;
    using ID3D12FencePtr                = Microsoft::WRL::ComPtr<ID3D12Fence>;
    using ID3D12PipelineStatePtr        = Microsoft::WRL::ComPtr<ID3D12PipelineState>;
    using ID3D12CommandAllocatorPtr     = Microsoft::WRL::ComPtr<ID3D12CommandAllocator>;
    using ID3D12RootSignaturePtr        = Microsoft::WRL::ComPtr<ID3D12RootSignature>;
    using ID3D12ResourcePtr             = Microsoft::WRL::ComPtr<ID3D12Resource>;
    using ID3D12DescriptorHeapPtr       = Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>;
    using ID3DBlobPtr                   = Microsoft::WRL::ComPtr<ID3DBlob>;
}