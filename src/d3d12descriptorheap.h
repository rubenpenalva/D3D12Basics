#pragma once

// d3d12 fwd decl
#include <d3d12.h>

// project includes
#include "d3d12basicsfwd.h"

namespace D3D12Render
{
    using D3D12DescriptorID = size_t;

    class D3D12DescriptorHeap
    {
    public:
        D3D12DescriptorHeap(ID3D12DevicePtr d3d12Device, unsigned int heapSize = 1024);

        D3D12DescriptorID CreateCBV(D3D12_GPU_VIRTUAL_ADDRESS bufferPtr, int bufferSize);

        D3D12DescriptorID CreateSRV(ID3D12ResourcePtr resource, const D3D12_SHADER_RESOURCE_VIEW_DESC& srvView);

        D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescHandle(D3D12DescriptorID id);

        ID3D12DescriptorHeapPtr GetDescriptorHeap() const { return m_descriptorHeap; }

    private:
        ID3D12DevicePtr                 m_d3d12Device;
        ID3D12DescriptorHeapPtr         m_descriptorHeap;
        unsigned int                    m_descriptorSize;
        D3D12_CPU_DESCRIPTOR_HANDLE     m_cpuStackHandle;
        D3D12_GPU_DESCRIPTOR_HANDLE     m_gpuDescHandleHeapStart;

        std::vector<D3D12_GPU_DESCRIPTOR_HANDLE> m_gpuDescHandles;

        D3D12DescriptorID AddDescriptor();
    };
}