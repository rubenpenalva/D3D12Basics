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

namespace D3D12Basics
{
    // Stack allocator
    // Note: fixed size
    class D3D12DescriptorStackAllocator
    {
    public:
        // Note maxDescriptors is the max number of descriptors in the heap
        D3D12DescriptorStackAllocator(unsigned int descriptorHandleIncrementSize, ID3D12DescriptorHeap* descriptorHeap,
            unsigned int maxDescriptors, unsigned int descriptorHeapOffset = 0);

        D3D12DescriptorAllocation* Allocate();

        void Clear();

    private:
        D3D12_CPU_DESCRIPTOR_HANDLE m_startCPUHandle;
        D3D12_GPU_DESCRIPTOR_HANDLE m_startGPUHandle;

        size_t m_stackTop;
        std::vector<D3D12DescriptorAllocation> m_allocations;
    };

    // Pool allocator
    // Note: fixed size
    // TODO why is the pool needed anyway? destroying descriptors doesnt seem to be important
    // therefore the stack implementation would in theory be enough.
    class D3D12DescriptorPoolAllocator
    {
    public:
        // Note maxDescriptors is the max number of descriptors in the heap
        D3D12DescriptorPoolAllocator(unsigned int descriptorHandleIncrementSize, 
                                     ID3D12DescriptorHeap* descriptorHeap,
                                     unsigned int maxDescriptors, unsigned int heapStartOffset = 0);

        D3D12DescriptorAllocation* Allocate();

        void Free(D3D12DescriptorAllocation* allocation);

    protected:
        std::list<D3D12DescriptorAllocation*>   m_freeAllocations;
        std::vector<D3D12DescriptorAllocation>  m_allocations;
    };
}

D3D12DescriptorStackAllocator::D3D12DescriptorStackAllocator(unsigned int descriptorHandleIncrementSize,
                                                             ID3D12DescriptorHeap* descriptorHeap,
                                                             unsigned int maxDescriptors,
                                                             unsigned int descriptorHeapOffset) :   m_stackTop(0), 
                                                                                                    m_allocations(maxDescriptors)
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

    for (size_t i = 0; i < maxDescriptors; ++i)
    {
        m_allocations[i].m_cpuHandle = cpuHandle;
        m_allocations[i].m_gpuHandle = gpuHandle;

        cpuHandle.ptr += descriptorHandleIncrementSize;
        gpuHandle.ptr += descriptorHandleIncrementSize;
    }
}

D3D12DescriptorAllocation* D3D12DescriptorStackAllocator::Allocate()
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

D3D12DescriptorPoolAllocator::D3D12DescriptorPoolAllocator(unsigned int descriptorHandleIncrementSize,
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

D3D12DescriptorAllocation* D3D12DescriptorPoolAllocator::Allocate()
{
    if (m_freeAllocations.empty())
        return nullptr;

    auto allocation = m_freeAllocations.back();
    m_freeAllocations.pop_back();

    return allocation;
}

void D3D12DescriptorPoolAllocator::Free(D3D12DescriptorAllocation* allocation)
{
    m_freeAllocations.push_back(allocation);
}

D3D12DescriptorPool::D3D12DescriptorPool(ID3D12DevicePtr d3d12Device, D3D12_DESCRIPTOR_HEAP_TYPE type,
                                         bool isShaderVisible, unsigned int maxDescriptors)
{
    m_descriptorHeap = CreateDescriptorHeap(d3d12Device, type, isShaderVisible, maxDescriptors);

    auto descriptorHandleIncrementSize = d3d12Device->GetDescriptorHandleIncrementSize(type);
    m_allocator = std::make_unique<D3D12DescriptorPoolAllocator>(descriptorHandleIncrementSize, m_descriptorHeap.Get(), maxDescriptors);
    assert(m_allocator);
}

D3D12DescriptorPool::~D3D12DescriptorPool()
{}


void D3D12DescriptorPool::Destroy(D3D12DescriptorAllocation* handle)
{
    m_allocator->Free(handle);
}

D3D12CBV_SRV_UAVDescriptorPool::D3D12CBV_SRV_UAVDescriptorPool(ID3D12DevicePtr d3d12Device, 
                                                             unsigned int maxDescriptors,
                                                             bool isShaderVisible) : m_d3d12Device(d3d12Device),
                                                                                     D3D12DescriptorPool(d3d12Device, 
                                                                                                         D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                                                                                                         isShaderVisible, maxDescriptors)
{
    assert(m_d3d12Device);
}

D3D12DescriptorAllocation* D3D12CBV_SRV_UAVDescriptorPool::CreateCBV(const D3D12_CONSTANT_BUFFER_VIEW_DESC& desc)
{
    D3D12DescriptorAllocation* allocation = m_allocator->Allocate();
    if (!allocation)
        return nullptr;

    m_d3d12Device->CreateConstantBufferView(&desc, allocation->m_cpuHandle);

    return allocation;
}

D3D12DescriptorAllocation* D3D12CBV_SRV_UAVDescriptorPool::CreateSRV(ID3D12ResourcePtr resource, const D3D12_SHADER_RESOURCE_VIEW_DESC& desc)
{
    D3D12DescriptorAllocation* allocation = m_allocator->Allocate();
    if (!allocation)
        return nullptr;

    m_d3d12Device->CreateShaderResourceView(resource.Get(), &desc, allocation->m_cpuHandle);

    return allocation;
}

D3D12RTVDescriptorPool::D3D12RTVDescriptorPool(ID3D12DevicePtr d3d12Device,
                                               unsigned int maxDescriptors) :   m_d3d12Device(d3d12Device),
                                                                                D3D12DescriptorPool(d3d12Device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
                                                                                                    false, maxDescriptors)
{
    assert(m_d3d12Device);
}

D3D12DescriptorAllocation* D3D12RTVDescriptorPool::CreateRTV(ID3D12ResourcePtr resource, D3D12DescriptorAllocation* handle)
{
    assert(resource);

    if (!handle)
    {
        handle = m_allocator->Allocate();
        if (!handle)
            return nullptr;
    }

    m_d3d12Device->CreateRenderTargetView(resource.Get(), nullptr, handle->m_cpuHandle);

    return handle;
}

D3D12DSVDescriptorPool::D3D12DSVDescriptorPool(ID3D12DevicePtr d3d12Device,
                                               unsigned int maxDescriptors) :   m_d3d12Device(d3d12Device),
                                                                                D3D12DescriptorPool(d3d12Device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
                                                                                                    false, maxDescriptors)
{
    assert(m_d3d12Device);
}

D3D12DescriptorAllocation* D3D12DSVDescriptorPool::CreateDSV(ID3D12ResourcePtr resource, const D3D12_DEPTH_STENCIL_VIEW_DESC& desc, 
                                                               D3D12DescriptorAllocation* handle)
{
    assert(resource);

    if (!handle)
    {
        handle = m_allocator->Allocate();
        if (!handle)
            return nullptr;
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

    NextDescriptor();
}

D3D12GPUDescriptorRingBuffer::~D3D12GPUDescriptorRingBuffer()
{
}

D3D12_GPU_DESCRIPTOR_HANDLE D3D12GPUDescriptorRingBuffer::CurrentDescriptor() const
{
    assert(m_currentAllocation);

    return m_currentAllocation->m_gpuHandle;
}

void D3D12GPUDescriptorRingBuffer::NextDescriptor()
{
    assert(m_currentHeap < m_allocators.size());
    m_currentAllocation = m_allocators[m_currentHeap]->Allocate();

    // TODO resize the heap
    assert(m_currentAllocation);
}

void D3D12GPUDescriptorRingBuffer::NextStack()
{
    m_currentHeap = (m_currentHeap + 1) % m_ringBufferSize;
}

void D3D12GPUDescriptorRingBuffer::CopyToDescriptor(unsigned int numDescriptors, D3D12_CPU_DESCRIPTOR_HANDLE srcDescriptorRangeStart)
{
    assert(m_currentAllocation);
    D3D12_CPU_DESCRIPTOR_HANDLE destDescriptorRangeStart = m_currentAllocation->m_cpuHandle;
    D3D12_DESCRIPTOR_HEAP_TYPE  descriptorHeapsType = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;

    m_d3d12Device->CopyDescriptorsSimple(numDescriptors, destDescriptorRangeStart, srcDescriptorRangeStart, descriptorHeapsType);
}

void D3D12GPUDescriptorRingBuffer::ClearStack()
{
    m_allocators[m_currentHeap]->Clear();
    NextDescriptor();
}

D3D12CBV_SRV_UAVDescriptorBuffer::D3D12CBV_SRV_UAVDescriptorBuffer(ID3D12DevicePtr d3d12Device, 
                                                                 unsigned int initialSize) : D3D12DescriptorBuffer(d3d12Device, initialSize)
{
}

D3D12DescriptorAllocation* D3D12CBV_SRV_UAVDescriptorBuffer::CreateCBV(const D3D12_CONSTANT_BUFFER_VIEW_DESC& desc)
{
    auto handle = m_descriptorPools.back()->CreateCBV(desc);
    if (!handle)
    {
        AddPool();
        handle = m_descriptorPools.back()->CreateCBV(desc);
        assert(handle);
    }

    return handle;
}

D3D12DescriptorAllocation* D3D12CBV_SRV_UAVDescriptorBuffer::CreateSRV(ID3D12ResourcePtr resource, const D3D12_SHADER_RESOURCE_VIEW_DESC& desc)
{
    auto handle = m_descriptorPools.back()->CreateSRV(resource, desc);
    if (!handle)
    {
        AddPool();
        handle = m_descriptorPools.back()->CreateSRV(resource, desc);
        assert(handle);
    }

    // TODO revisit this. maybe store the descriptor heap in the handle?
    m_handlesAllocators[handle] = m_descriptorPools.size() - 1;

    return handle;
}

D3D12RTVDescriptorBuffer::D3D12RTVDescriptorBuffer(ID3D12DevicePtr d3d12Device,
                                                   unsigned int initialSize) : D3D12DescriptorBuffer(d3d12Device, initialSize)
{
}

D3D12DescriptorAllocation* D3D12RTVDescriptorBuffer::CreateRTV(ID3D12ResourcePtr resource)
{
    auto handle = m_descriptorPools.back()->CreateRTV(resource);
    if (!handle)
    {
        AddPool();
        handle = m_descriptorPools.back()->CreateRTV(resource);
        assert(handle);
    }

    // TODO revisit this. maybe store the descriptor heap in the handle?
    m_handlesAllocators[handle] = m_descriptorPools.size() - 1;

    return handle;
}