#pragma once

// d3d12 fwd decl
#include <d3d12.h>

// project includes
#include "d3d12basicsfwd.h"

namespace D3D12Render
{
    struct D3D12DescriptorHandles
    {
        D3D12_CPU_DESCRIPTOR_HANDLE m_cpuHandle;
        D3D12_GPU_DESCRIPTOR_HANDLE m_gpuHandle;
    };

    // TODO add out of bounds checks
    class D3D12DescriptorHeap
    {
    public:
        D3D12DescriptorHeap(ID3D12DevicePtr d3d12Device, D3D12_DESCRIPTOR_HEAP_TYPE type, unsigned int heapSize);

        D3D12DescriptorHandles& GetDescriptorHandles(D3D12DescriptorID id);

        ID3D12DescriptorHeapPtr GetDescriptorHeap() const { return m_descriptorHeap; }

    protected:
        ID3D12DevicePtr                 m_d3d12Device;
        const unsigned int              m_heapSize;
        D3D12_CPU_DESCRIPTOR_HANDLE     m_cpuStackHandle;

        D3D12DescriptorID AddDescriptor();

    private:
        using DescriptorHandles = std::vector<D3D12DescriptorHandles>;

        ID3D12DescriptorHeapPtr         m_descriptorHeap;
        unsigned int                    m_descriptorSize;
        
        D3D12_GPU_DESCRIPTOR_HANDLE     m_gpuStackHandle;

        DescriptorHandles               m_descriptorHandles;
    };

    class D3D12CBVSRVUAVDescHeap : public D3D12DescriptorHeap
    {
    public:
        D3D12CBVSRVUAVDescHeap(ID3D12DevicePtr d3d12Device, unsigned int heapSize = 1024);

        D3D12DescriptorID CreateCBV(D3D12_GPU_VIRTUAL_ADDRESS bufferPtr, int bufferSize);

        D3D12DescriptorID CreateSRV(ID3D12ResourcePtr resource, const D3D12_SHADER_RESOURCE_VIEW_DESC& desc);
    };

    class D3D12DSVDescriptorHeap : public D3D12DescriptorHeap
    {
    public:
        D3D12DSVDescriptorHeap(ID3D12DevicePtr d3d12Device, unsigned int heapSize = 1024);

        D3D12DescriptorID CreateDSV(ID3D12ResourcePtr resource, const D3D12_DEPTH_STENCIL_VIEW_DESC& desc);
    };

    class D3D12RTVDescriptorHeap : public D3D12DescriptorHeap
    {
    public:
        D3D12RTVDescriptorHeap(ID3D12DevicePtr d3d12Device, unsigned int heapSize = 1024);

        D3D12DescriptorID CreateRTV(ID3D12ResourcePtr resource, const D3D12_RENDER_TARGET_VIEW_DESC* desc = nullptr);
    };
}