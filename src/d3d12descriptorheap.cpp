#include "d3d12descriptorheap.h"


// project includes
#include "utils.h"

using namespace D3D12Basics;
using namespace D3D12Render;

D3D12DescriptorHeapAllocator::D3D12DescriptorHeapAllocator(unsigned int descriptorHandleIncrementSize,
                                                           ID3D12DescriptorHeap* descriptorHeap,
                                                           unsigned int maxDescriptors) :   m_allocations(maxDescriptors)
{
    assert(descriptorHandleIncrementSize > 0);
    assert(descriptorHeap);
    assert(maxDescriptors > 0);

    // TODO is there an invalid handle definition given by d3d12 that can be checked against?
    auto gpuHandle = descriptorHeap->GetGPUDescriptorHandleForHeapStart();
    auto cpuHandle = descriptorHeap->GetCPUDescriptorHandleForHeapStart();

    for (size_t i = 0; i < maxDescriptors; ++i)
    {
        m_allocations[i].m_cpuHandle = cpuHandle;
        m_allocations[i].m_gpuHandle = gpuHandle;

        cpuHandle.ptr += descriptorHandleIncrementSize;
        gpuHandle.ptr += descriptorHandleIncrementSize;

        m_freeAllocations.push_back(&m_allocations[i]);
    }
}

D3D12DescriptorHeapAllocation* D3D12DescriptorHeapAllocator::Allocate()
{
    if (m_freeAllocations.empty())
        return nullptr;

    auto allocation = m_freeAllocations.back();
    m_freeAllocations.pop_back();

    return allocation;
}

void D3D12DescriptorHeapAllocator::Free(D3D12DescriptorHeapAllocation* allocation)
{
    m_freeAllocations.push_back(allocation);
}

D3D12DescriptorHeap::D3D12DescriptorHeap(ID3D12DevicePtr d3d12Device, D3D12_DESCRIPTOR_HEAP_TYPE type,
                                         bool isShaderVisible, unsigned int maxDescriptors)
{
    assert(d3d12Device);
    assert(maxDescriptors > 0);
    assert(((type == D3D12_DESCRIPTOR_HEAP_TYPE_RTV || type == D3D12_DESCRIPTOR_HEAP_TYPE_DSV) && !isShaderVisible) ||
           ((type != D3D12_DESCRIPTOR_HEAP_TYPE_RTV && type != D3D12_DESCRIPTOR_HEAP_TYPE_DSV) && isShaderVisible));

    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.NumDescriptors = maxDescriptors;
    heapDesc.Type = type;
    heapDesc.Flags = isShaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    AssertIfFailed(d3d12Device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_descriptorHeap)));

    auto descriptorHandleIncrementSize = d3d12Device->GetDescriptorHandleIncrementSize(type);
    m_allocator = std::make_unique<D3D12DescriptorHeapAllocator>(descriptorHandleIncrementSize, m_descriptorHeap.Get(), maxDescriptors);
    assert(m_allocator);
}

void D3D12DescriptorHeap::Destroy(D3D12DescriptorHeapHandlePtr handle)
{
    m_allocator->Free(handle);
}

D3D12CBV_SV_UAVDescriptorHeap::D3D12CBV_SV_UAVDescriptorHeap(ID3D12DevicePtr d3d12Device, bool isShaderVisible, 
                                                             unsigned int maxDescriptors) : m_d3d12Device(d3d12Device),
                                                                                            D3D12DescriptorHeap(d3d12Device, 
                                                                                                                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                                                                                                                isShaderVisible, maxDescriptors)
{
    assert(m_d3d12Device);
}

D3D12DescriptorHeapHandlePtr D3D12CBV_SV_UAVDescriptorHeap::CreateCBV(const D3D12_CONSTANT_BUFFER_VIEW_DESC& desc)
{
    D3D12DescriptorHeapAllocation* allocation = m_allocator->Allocate();
    assert(allocation);

    m_d3d12Device->CreateConstantBufferView(&desc, allocation->m_cpuHandle);

    return D3D12DescriptorHeapHandlePtr{ allocation };
}

D3D12DescriptorHeapHandlePtr D3D12CBV_SV_UAVDescriptorHeap::CreateSRV(ID3D12ResourcePtr resource, const D3D12_SHADER_RESOURCE_VIEW_DESC& desc)
{
    assert(resource);

    D3D12DescriptorHeapAllocation* allocation = m_allocator->Allocate();
    assert(allocation);

    m_d3d12Device->CreateShaderResourceView(resource.Get(), &desc, allocation->m_cpuHandle);

    return D3D12DescriptorHeapHandlePtr{ allocation };
}

D3D12RTVDescriptorHeap::D3D12RTVDescriptorHeap(ID3D12DevicePtr d3d12Device, 
                                               unsigned int maxDescriptors) :   m_d3d12Device(d3d12Device),
                                                                                D3D12DescriptorHeap(d3d12Device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
                                                                                                    false, maxDescriptors)
{
    assert(m_d3d12Device);
}

D3D12DescriptorHeapHandlePtr D3D12RTVDescriptorHeap::CreateRTV(ID3D12ResourcePtr resource, D3D12DescriptorHeapHandlePtr handle)
{
    assert(resource);

    if (!handle)
    {
        handle = m_allocator->Allocate();
        assert(handle);
    }

    m_d3d12Device->CreateRenderTargetView(resource.Get(), nullptr, handle->m_cpuHandle);

    return handle;
}

D3D12DSVDescriptorHeap::D3D12DSVDescriptorHeap(ID3D12DevicePtr d3d12Device, 
                                               unsigned int maxDescriptors) :   m_d3d12Device(d3d12Device),
                                                                                D3D12DescriptorHeap(d3d12Device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
                                                                                                    false, maxDescriptors)
{
    assert(m_d3d12Device);
}

D3D12DescriptorHeapHandlePtr D3D12DSVDescriptorHeap::CreateDSV(ID3D12ResourcePtr resource, const D3D12_DEPTH_STENCIL_VIEW_DESC& desc)
{
    assert(resource);

    D3D12DescriptorHeapAllocation* allocation = m_allocator->Allocate();
    assert(allocation);

    m_d3d12Device->CreateDepthStencilView(resource.Get(), &desc, allocation->m_cpuHandle);

    return D3D12DescriptorHeapHandlePtr{ allocation };
}