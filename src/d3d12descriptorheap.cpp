#include "d3d12descriptorheap.h"


// project includes
#include "utils.h"

using namespace D3D12Basics;

namespace
{
    ID3D12DescriptorHeapPtr CreateDescriptorHeap(ID3D12DevicePtr d3d12Device, D3D12_DESCRIPTOR_HEAP_TYPE type, 
                                                 bool isShaderVisible, unsigned int maxDescriptors)
    {
        assert(d3d12Device);
        assert(maxDescriptors > 0);
        assert(((type == D3D12_DESCRIPTOR_HEAP_TYPE_RTV || type == D3D12_DESCRIPTOR_HEAP_TYPE_DSV) && !isShaderVisible) ||
                ((type != D3D12_DESCRIPTOR_HEAP_TYPE_RTV && type != D3D12_DESCRIPTOR_HEAP_TYPE_DSV)));

        D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
        heapDesc.NumDescriptors = maxDescriptors;
        heapDesc.Type = type;
        heapDesc.Flags = isShaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

        ID3D12DescriptorHeapPtr descriptorHeap;
        AssertIfFailed(d3d12Device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&descriptorHeap)));
        
        return descriptorHeap;
    }
}

D3D12DescriptorStackAllocator::D3D12DescriptorStackAllocator(unsigned int descriptorHandleIncrementSize,
                                                             ID3D12DescriptorHeap* descriptorHeap,
                                                             unsigned int maxDescriptors,
                                                             unsigned int descriptorHeapOffset) : m_stackTop(0)
{
    assert(descriptorHandleIncrementSize > 0);
    assert(descriptorHeap);
    assert(maxDescriptors > 0);

    m_startCPUHandle = descriptorHeap->GetCPUDescriptorHandleForHeapStart();
    assert(m_startCPUHandle.ptr);
    m_startCPUHandle.ptr += descriptorHandleIncrementSize * descriptorHeapOffset;

    m_startGPUHandle = descriptorHeap->GetGPUDescriptorHandleForHeapStart();
    assert(m_startGPUHandle.ptr);
    m_startGPUHandle.ptr += descriptorHandleIncrementSize * descriptorHeapOffset;

    auto cpuHandle = m_startCPUHandle;
    auto gpuHandle = m_startGPUHandle;

    m_allocations.resize(maxDescriptors);
    for (size_t i = 0; i < maxDescriptors; ++i)
    {
        m_allocations[i].m_cpuHandle = cpuHandle;
        m_allocations[i].m_gpuHandle = gpuHandle;

        cpuHandle.ptr += descriptorHandleIncrementSize;
        gpuHandle.ptr += descriptorHandleIncrementSize;
    }
}

D3D12DescriptorHeapAllocation* D3D12DescriptorStackAllocator::Allocate()
{
    assert(m_stackTop <= m_allocations.size());

    if (m_stackTop == m_allocations.size())
        return nullptr;

    return &m_allocations[m_stackTop++];
}

void D3D12DescriptorStackAllocator::Clear()
{
    m_stackTop = 0;
}

D3D12DescriptorHeapAllocator::D3D12DescriptorHeapAllocator(unsigned int descriptorHandleIncrementSize,
                                                           ID3D12DescriptorHeap* descriptorHeap,
                                                           unsigned int maxDescriptors, 
                                                           unsigned int heapStartOffset) :   m_allocations(maxDescriptors)
{
    assert(descriptorHandleIncrementSize > 0);
    assert(descriptorHeap);
    assert(maxDescriptors > 0);

    // TODO is there an invalid handle definition given by d3d12 that can be checked against?
    auto gpuHandle = descriptorHeap->GetGPUDescriptorHandleForHeapStart();
    auto cpuHandle = descriptorHeap->GetCPUDescriptorHandleForHeapStart();

    gpuHandle.ptr += heapStartOffset;
    cpuHandle.ptr += heapStartOffset;

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
    m_descriptorHeap = CreateDescriptorHeap(d3d12Device, type, isShaderVisible, maxDescriptors);

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

D3D12DescriptorHeapHandlePtr D3D12DSVDescriptorHeap::CreateDSV(ID3D12ResourcePtr resource, const D3D12_DEPTH_STENCIL_VIEW_DESC& desc, 
                                                               D3D12DescriptorHeapHandlePtr handle)
{
    assert(resource);

    if (!handle)
    {
        handle = m_allocator->Allocate();
        assert(handle);
    }

    m_d3d12Device->CreateDepthStencilView(resource.Get(), &desc, handle->m_cpuHandle);

    return handle;
}

D3D12GPUDescriptorRingBuffer::D3D12GPUDescriptorRingBuffer(ID3D12DevicePtr d3d12Device, unsigned int maxHeaps,
                                                           unsigned int maxDescriptorsPerHeap)  :   m_d3d12Device(d3d12Device), m_ringBufferSize(maxHeaps),
                                                                                                    m_currentAllocation(nullptr), m_currentHeap(0)
{
    assert(d3d12Device);
    assert(m_ringBufferSize > 0);
    assert(maxDescriptorsPerHeap > 0);

    const auto type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    m_descriptorHeap = CreateDescriptorHeap(d3d12Device, type, true, maxDescriptorsPerHeap * static_cast<unsigned int>(m_ringBufferSize));
    auto descriptorHandleIncrementSize = d3d12Device->GetDescriptorHandleIncrementSize(type);

    for (unsigned int i = 0; i < m_ringBufferSize; ++i)
    {
        const auto descriptorHeapOffset = maxDescriptorsPerHeap * i;
        auto allocator = std::make_unique<D3D12DescriptorStackAllocator>(descriptorHandleIncrementSize, 
                                                                         m_descriptorHeap.Get(), 
                                                                         maxDescriptorsPerHeap, descriptorHeapOffset);
        assert(allocator);
        m_allocators.emplace_back(std::move(allocator));
    }

    NextCurrentStackDescriptor();
}

D3D12_GPU_DESCRIPTOR_HANDLE D3D12GPUDescriptorRingBuffer::CurrentDescriptor() const
{
    assert(m_currentAllocation);

    return m_currentAllocation->m_gpuHandle;
}

void D3D12GPUDescriptorRingBuffer::NextCurrentStackDescriptor()
{
    assert(m_currentHeap < m_allocators.size());
    m_currentAllocation = m_allocators[m_currentHeap]->Allocate();

    // TODO resize the heap
    assert(m_currentAllocation);
}

void D3D12GPUDescriptorRingBuffer::NextDescriptorStack()
{
    m_currentHeap = (m_currentHeap + 1) % m_ringBufferSize;
}

void D3D12GPUDescriptorRingBuffer::CopyToCurrentDescriptor(unsigned int numDescriptors, D3D12_CPU_DESCRIPTOR_HANDLE srcDescriptorRangeStart)
{
    assert(m_currentAllocation);
    D3D12_CPU_DESCRIPTOR_HANDLE destDescriptorRangeStart = m_currentAllocation->m_cpuHandle;
    D3D12_DESCRIPTOR_HEAP_TYPE  descriptorHeapsType = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;

    m_d3d12Device->CopyDescriptorsSimple(numDescriptors, destDescriptorRangeStart, srcDescriptorRangeStart, descriptorHeapsType);
}

void D3D12GPUDescriptorRingBuffer::ClearCurrentStack()
{
    m_allocators[m_currentHeap]->Clear();
    NextCurrentStackDescriptor();
}

D3D12CPUDescriptorBuffer::D3D12CPUDescriptorBuffer(ID3D12DevicePtr d3d12Device, unsigned int initialSize) : m_d3d12Device(d3d12Device), 
                                                                                                            m_heapSize(initialSize)
{
    assert(m_d3d12Device);
    assert(m_heapSize > 0);

    AddHeap();
}

D3D12DescriptorHeapHandlePtr D3D12CPUDescriptorBuffer::CreateCBV(const D3D12_CONSTANT_BUFFER_VIEW_DESC& desc)
{
    auto handle = m_descriptorHeaps.back()->CreateCBV(desc);
    if (!handle)
    {
        AddHeap();
        handle = m_descriptorHeaps.back()->CreateCBV(desc);
        assert(handle);
    }

    return handle;
}

D3D12DescriptorHeapHandlePtr D3D12CPUDescriptorBuffer::CreateSRV(ID3D12ResourcePtr resource, const D3D12_SHADER_RESOURCE_VIEW_DESC& desc)
{
    auto handle = m_descriptorHeaps.back()->CreateSRV(resource, desc);
    if (!handle)
    {
        AddHeap();
        handle = m_descriptorHeaps.back()->CreateSRV(resource, desc);
        assert(handle);
    }

    // TODO revisit this. maybe store the descriptor heap in the handle?
    m_handlesAllocators[handle] = m_descriptorHeaps.size() - 1;

    return handle;
}

void D3D12CPUDescriptorBuffer::Destroy(D3D12DescriptorHeapHandlePtr handle)
{
    assert(handle);
    assert(m_handlesAllocators.count(handle));

    auto index = m_handlesAllocators[handle];
    assert(index < m_descriptorHeaps.size());

    m_descriptorHeaps[index]->Destroy(handle);
}

void D3D12CPUDescriptorBuffer::AddHeap()
{
    m_descriptorHeaps.push_back(std::make_unique<D3D12CBV_SV_UAVDescriptorHeap>(m_d3d12Device, false, m_heapSize));
}
