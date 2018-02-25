#include "d3d12descriptorheap.h"

// c includes
#include <cassert>

// directx includes
#include <d3d12.h>

// project includes
#include "utils.h"
#include "d3d12gpu.h"

using namespace D3D12Basics;
using namespace D3D12Render;

D3D12DescriptorHeap::D3D12DescriptorHeap(ID3D12DevicePtr d3d12Device, 
                                         D3D12_DESCRIPTOR_HEAP_TYPE type, 
                                         unsigned int heapSize) : m_d3d12Device(d3d12Device), m_heapSize(heapSize)
{
    assert(m_d3d12Device);
    assert(heapSize > 0);

    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.NumDescriptors = heapSize;
    heapDesc.Type           = type;
    heapDesc.Flags          = type == D3D12_DESCRIPTOR_HEAP_TYPE_RTV || type == D3D12_DESCRIPTOR_HEAP_TYPE_DSV ?    D3D12_DESCRIPTOR_HEAP_FLAG_NONE : 
                                                                                                                    D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    AssertIfFailed(m_d3d12Device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_descriptorHeap)));
    m_descriptorSize = m_d3d12Device->GetDescriptorHandleIncrementSize(type);

    m_gpuStackHandle = m_descriptorHeap->GetGPUDescriptorHandleForHeapStart();
    m_cpuStackHandle = m_descriptorHeap->GetCPUDescriptorHandleForHeapStart();
}

D3D12DescriptorHandles& D3D12DescriptorHeap::GetDescriptorHandles(D3D12DescriptorID id)
{
    assert(id < m_descriptorHandles.size());
    return m_descriptorHandles[id];
}

D3D12DescriptorID D3D12DescriptorHeap::AddDescriptor()
{
    assert(m_descriptorHandles.size() < m_heapSize);

    D3D12DescriptorID resourceViewID = m_descriptorHandles.size();
    m_descriptorHandles.push_back({ m_cpuStackHandle, m_gpuStackHandle });
    
    m_cpuStackHandle.ptr += m_descriptorSize;
    m_gpuStackHandle.ptr += m_descriptorSize;

    return resourceViewID;
}

D3D12CBVSRVUAVDescHeap::D3D12CBVSRVUAVDescHeap(ID3D12DevicePtr d3d12Device, 
                                               unsigned int heapSize)  :   D3D12DescriptorHeap(d3d12Device, 
                                                                                               D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 
                                                                                               heapSize)
{}

D3D12DescriptorID D3D12CBVSRVUAVDescHeap::CreateCBV(D3D12_GPU_VIRTUAL_ADDRESS bufferPtr, int bufferSize)
{
    // TODO assert bufferSize complies with constant buffer memory alignment requirements
    D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
    cbvDesc.BufferLocation  = bufferPtr;
    cbvDesc.SizeInBytes     = bufferSize;

    m_d3d12Device->CreateConstantBufferView(&cbvDesc, m_cpuStackHandle);

    return AddDescriptor();
}

D3D12DescriptorID D3D12CBVSRVUAVDescHeap::CreateSRV(ID3D12ResourcePtr resource, const D3D12_SHADER_RESOURCE_VIEW_DESC& desc)
{
    assert(resource);

    m_d3d12Device->CreateShaderResourceView(resource.Get(), &desc, m_cpuStackHandle);

    return AddDescriptor();
}

D3D12DSVDescriptorHeap::D3D12DSVDescriptorHeap(ID3D12DevicePtr d3d12Device, unsigned int heapSize) : D3D12DescriptorHeap(d3d12Device,
                                                                                                                         D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
                                                                                                                         heapSize)
{}

D3D12DescriptorID D3D12DSVDescriptorHeap::CreateDSV(ID3D12ResourcePtr resource, const D3D12_DEPTH_STENCIL_VIEW_DESC& desc)
{
    assert(resource);

    m_d3d12Device->CreateDepthStencilView(resource.Get(), &desc, m_cpuStackHandle);

    return AddDescriptor();
}

D3D12RTVDescriptorHeap::D3D12RTVDescriptorHeap(ID3D12DevicePtr d3d12Device, unsigned int heapSize) : D3D12DescriptorHeap(d3d12Device,
                                                                                                                         D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
                                                                                                                         heapSize)
{
}

D3D12DescriptorID D3D12RTVDescriptorHeap::CreateRTV(ID3D12ResourcePtr resource, const D3D12_RENDER_TARGET_VIEW_DESC* desc)
{
    m_d3d12Device->CreateRenderTargetView(resource.Get(), desc, m_cpuStackHandle);

    return AddDescriptor();
}