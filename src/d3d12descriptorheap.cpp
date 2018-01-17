#include "d3d12descriptorheap.h"

// c includes
#include <cassert>

// directx includes
#include <d3d12.h>

// project includes
#include "utils.h"
#include "d3d12gpus.h"

using namespace D3D12Render;

D3D12DescriptorHeap::D3D12DescriptorHeap(ID3D12DevicePtr d3d12Device, unsigned int heapSize) : m_d3d12Device(d3d12Device)
{
    assert(m_d3d12Device);
    assert(heapSize > 0);

    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.NumDescriptors = heapSize;
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    Utils::AssertIfFailed(m_d3d12Device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_descriptorHeap)));
    m_descriptorSize = m_d3d12Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    m_gpuDescHandleHeapStart = m_descriptorHeap->GetGPUDescriptorHandleForHeapStart();
    m_cpuStackHandle = m_descriptorHeap->GetCPUDescriptorHandleForHeapStart();
}

D3D12DescriptorID D3D12DescriptorHeap::CreateCBV(D3D12_GPU_VIRTUAL_ADDRESS bufferPtr, int bufferSize)
{
    // TODO assert bufferSize complies with constant buffer memory alignment requirements
    D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
    cbvDesc.BufferLocation = bufferPtr;
    cbvDesc.SizeInBytes = bufferSize;

    m_d3d12Device->CreateConstantBufferView(&cbvDesc, m_cpuStackHandle);

    return AddDescriptor();
}

D3D12DescriptorID D3D12DescriptorHeap::CreateSRV(ID3D12ResourcePtr resource, const D3D12_SHADER_RESOURCE_VIEW_DESC& srvView)
{
    m_d3d12Device->CreateShaderResourceView(resource.Get(), &srvView, m_cpuStackHandle);

    return AddDescriptor();
}

D3D12_GPU_DESCRIPTOR_HANDLE D3D12DescriptorHeap::GetGPUDescHandle(D3D12DescriptorID id)
{
    assert(id < m_gpuDescHandles.size());
    return m_gpuDescHandles[id];
}

D3D12DescriptorID D3D12DescriptorHeap::AddDescriptor()
{
    m_cpuStackHandle.ptr += m_descriptorSize;

    D3D12DescriptorID resourceViewID = m_gpuDescHandles.size();

    D3D12_GPU_DESCRIPTOR_HANDLE resourceViewHandle = m_gpuDescHandleHeapStart;
    resourceViewHandle.ptr += m_descriptorSize * resourceViewID;

    m_gpuDescHandles.push_back(resourceViewHandle);

    return resourceViewID;
}