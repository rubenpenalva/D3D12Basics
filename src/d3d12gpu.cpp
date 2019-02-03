#include "d3d12gpu.h"

// project includes
#include "utils.h"
#include "d3d12utils.h"
#include "d3d12descriptorheap.h"
#include "d3d12swapchain.h"
#include "d3d12committedresources.h"
#include "d3d12gpu_sync.h"

// c++ includes
#include <sstream>
#include <algorithm>

#define ENABLE_D3D12_DEBUG_LAYER            (1)

// NOTE enabling gpu validation with an intel igpu will trigger a device removed.
// maybe the TDR is actually timing out? For now just ignoring the igpu and using
// the nvidia dgpu.
#define ENABLE_D3D12_DEBUG_GPU_VALIDATION   (1)

using namespace D3D12Basics;

namespace
{
    const wchar_t* g_prePresentUUID = L"a9744ea3-cccc-4f2f-be6a-42aad08a9c6f";
    const wchar_t* g_postPresentUUID = L"a9744ea3-dddd-4f2f-be6a-42aad08a9c6f";
    const wchar_t* g_preWaitUUID = L"a9744ea3-eeee-4f2f-be6a-42aad08a9c6f";
    const wchar_t* g_postWaitUUID = L"a9744ea3-ffff-4f2f-be6a-42aad08a9c6f";

    const wchar_t* g_prePresentName = L"PRE PRESENT";
    const wchar_t* g_postPresentName = L"POST PRESENT";
    const wchar_t* g_preWaitName = L"PRE WAIT";
    const wchar_t* g_postWaitName = L"POST WAIT";

    D3D12Basics::GpuViewMarker g_gpuViewMarkerPrePresentFrame(g_prePresentName, g_prePresentUUID);
    D3D12Basics::GpuViewMarker g_gpuViewMarkerPostPresentFrame(g_postPresentName, g_postPresentUUID);
    D3D12Basics::GpuViewMarker g_gpuViewMarkerPreWaitFrame(g_preWaitName, g_preWaitUUID);
    D3D12Basics::GpuViewMarker g_gpuViewMarkerPostWaitFrame(g_postWaitName, g_postWaitUUID);

    const auto g_swapChainFormat = DXGI_FORMAT::DXGI_FORMAT_R8G8B8A8_UNORM;

    enum ResourceType
    {
        Texture = 0,
        Buffer
    };

    D3D12GpuMemoryHandle EncodeGpuMemoryHandle(D3D12GpuHandle::HandleType handle, bool isDynamic, ResourceType resourceType)
    {
        assert(!D3D12GpuHandle::IsNull(handle) && D3D12GpuHandle::IsValid(handle));

        // This checks that the isDynamic and isResourceType bits are not used, which it also means the handle
        // is not using those two last bits
        D3D12GpuHandle::HandleType mask = static_cast<D3D12GpuHandle::HandleType>(3) << 
                                          (std::numeric_limits<D3D12GpuHandle::HandleType>::digits - 2);
        assert((handle & mask) == 0);

        const auto dynamicBitEnabled = static_cast<D3D12GpuHandle::HandleType>(isDynamic);
        const auto resourceTypeBitEnabled = static_cast<D3D12GpuHandle::HandleType>(resourceType);
        return { handle |
                (dynamicBitEnabled << (std::numeric_limits<D3D12GpuHandle::HandleType>::digits - 1)) |
                (resourceTypeBitEnabled << (std::numeric_limits<D3D12GpuHandle::HandleType>::digits - 2)) };
    }

    D3D12GpuHandle::HandleType DecodeGpuMemoryHandle_ID(D3D12GpuMemoryHandle memHandle)
    {
        assert(memHandle.IsValid() && !memHandle.IsNull());

        constexpr auto bitsToShift = static_cast<uint64_t>(std::numeric_limits<D3D12GpuHandle::HandleType>::digits - 2);

        const D3D12GpuHandle::HandleType msbEnabled = ~(static_cast<D3D12GpuHandle::HandleType>(3) << bitsToShift);
        return memHandle.m_id & msbEnabled;
    }

    bool DecodeGpuMemoryHandle_IsDynamic(D3D12GpuMemoryHandle memHandle)
    {
        assert(memHandle.IsValid() && !memHandle.IsNull());

        return memHandle.m_id >> (std::numeric_limits<D3D12GpuHandle::HandleType>::digits - 1);
    }

    ResourceType DecodeGpuMemoryHandle_ResourceType(D3D12GpuMemoryHandle memHandle)
    {
        assert(memHandle.IsValid() && !memHandle.IsNull());

        D3D12GpuHandle::HandleType rawResourceType = (memHandle.m_id >> (std::numeric_limits<D3D12GpuHandle::HandleType>::digits - 2)) &
                                                     ~(static_cast<D3D12GpuHandle::HandleType>(1) << (std::numeric_limits<D3D12GpuHandle::HandleType>::digits - 1));
        return static_cast<ResourceType>(rawResourceType);
    }

    DXGI_MODE_DESC1 CreateDefaultDisplayMode()
    {
        DXGI_MODE_DESC1 displayMode{ 0 };

        displayMode.Format  = DXGI_FORMAT_R8G8B8A8_UNORM;
        displayMode.Height  = 480;
        displayMode.Width   = 640;

        return displayMode;
    }
}

namespace D3D12Basics
{
    struct D3D12GpuShareableState
    {
        ID3D12DevicePtr m_device{};

        ID3D12DescriptorHeap* m_descriptorHeap{};

        unsigned int m_currentFrameIndex{};
    };

    class D3D12CmdListTimeStamp
    {
    public:
        D3D12CmdListTimeStamp(ID3D12GraphicsCommandListPtr cmdList, 
                              D3D12GpuShareableState* gpuState,
                              D3D12CommittedResourceAllocator* committedAllocator,
                              UINT64 cmdQueueTimestampFrequency, 
                              StopClock::SplitTimeBuffer& splitTimes);

        void Begin();

        void End();

    private:
        D3D12GpuShareableState*         m_gpuState;
        UINT64                          m_cmdQueueTimestampFrequency;
        StopClock::SplitTimeBuffer&     m_splitTimes;
        ID3D12GraphicsCommandListPtr    m_cmdList;

        Microsoft::WRL::ComPtr<ID3D12QueryHeap> m_timestampQueryHeap;
        ID3D12ResourcePtr                       m_timestampBuffer;
    };
}

D3D12CmdListTimeStamp::D3D12CmdListTimeStamp(ID3D12GraphicsCommandListPtr cmdList,
                                             D3D12GpuShareableState* gpuState,
                                             D3D12CommittedResourceAllocator* committedAllocator,
                                             UINT64 cmdQueueTimestampFrequency,
                                             StopClock::SplitTimeBuffer& splitTimes) :  m_cmdList(cmdList),
                                                                                        m_gpuState(gpuState),
                                                                                        m_cmdQueueTimestampFrequency(cmdQueueTimestampFrequency),
                                                                                        m_splitTimes(splitTimes)
{
    assert(m_cmdList);
    assert(m_gpuState);
    assert(committedAllocator);

    D3D12_QUERY_HEAP_DESC queryHeapDesc;
    queryHeapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    // Note one at the beginning of the cmd list and another one at the end per frame.
    queryHeapDesc.Count = 2 * D3D12GpuConfig::m_framesInFlight; 
    queryHeapDesc.NodeMask = 0;

    AssertIfFailed(m_gpuState->m_device->CreateQueryHeap(&queryHeapDesc, IID_PPV_ARGS(&m_timestampQueryHeap)));

    // NOTE: ResolveQueryData requires an alignment of 8 when using the offset
    // https://docs.microsoft.com/en-us/windows/desktop/api/d3d12/nf-d3d12-id3d12graphicscommandlist-resolvequerydata
    const size_t alignment = 8;
    const std::wstring debugName = L"Time stamp buffer - Query";
    m_timestampBuffer = committedAllocator->AllocateReadBackBuffer(queryHeapDesc.Count * sizeof(uint64_t), alignment, debugName).m_resource;
    assert(m_timestampBuffer);
}

void D3D12CmdListTimeStamp::Begin()
{
    const UINT timestampQueryHeapIndex = 2 * m_gpuState->m_currentFrameIndex;
    m_cmdList->EndQuery(m_timestampQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, timestampQueryHeapIndex);
}

void D3D12CmdListTimeStamp::End()
{
    const UINT timestampQueryHeapIndex = 2 * m_gpuState->m_currentFrameIndex;

    m_cmdList->EndQuery(m_timestampQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, timestampQueryHeapIndex + 1);
    m_cmdList->ResolveQueryData(m_timestampQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, timestampQueryHeapIndex,
                                2, m_timestampBuffer.Get(), timestampQueryHeapIndex * sizeof(uint64_t));

    D3D12_RANGE readRange = {};
    readRange.Begin = 2 * m_gpuState->m_currentFrameIndex * sizeof(uint64_t);
    readRange.End = readRange.Begin + 2 * sizeof(uint64_t);

    void* pData = nullptr;
    AssertIfFailed(m_timestampBuffer->Map(0, &readRange, &pData));

    const uint64_t* pTimestamps = reinterpret_cast<uint64_t*>(static_cast<uint8_t*>(pData) + readRange.Begin);
    const uint64_t timeStampDelta = pTimestamps[1] - pTimestamps[0];

    // Unmap with an empty range (written range).
    D3D12_RANGE emptyRange = {};
    emptyRange.Begin = emptyRange.End = 0;
    m_timestampBuffer->Unmap(0, &emptyRange);

    // Calculate the GPU execution time in milliseconds.
    const float gpuTimeS = (timeStampDelta / static_cast<float>(m_cmdQueueTimestampFrequency));
    m_splitTimes.SetValue(gpuTimeS);
    m_splitTimes.Next();
}

D3D12GraphicsCmdList::D3D12GraphicsCmdList(D3D12GpuShareableState* gpuState,
                                           D3D12CommittedResourceAllocator* committedAllocator,
                                           UINT64 cmdQueueTimestampFrequency,
                                           StopClock::SplitTimeBuffer& splitTimes,
                                           const std::wstring& debugName) :     m_gpuState(gpuState)
{
    assert(m_gpuState);
    assert(m_gpuState->m_device);
    assert(committedAllocator);

    for (unsigned int i = 0; i < D3D12GpuConfig::m_framesInFlight; ++i)
    {
        AssertIfFailed(m_gpuState->m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                                    IID_PPV_ARGS(&m_cmdAllocators[i])));
        assert(m_cmdAllocators[i]);
        {
            std::wstringstream converter;
            converter << L"Command Allocator " << i << " for cmdlist " << debugName;
            m_cmdAllocators[i]->SetName(converter.str().c_str());
        }
    }
    AssertIfFailed(m_gpuState->m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                                m_cmdAllocators[0].Get(),
                                                                nullptr, IID_PPV_ARGS(&m_cmdList)));
    assert(m_cmdList);
    AssertIfFailed(m_cmdList->Close());

    m_cmdList->SetName(debugName.c_str());

    m_timeStamp = std::make_unique<D3D12CmdListTimeStamp>(m_cmdList, m_gpuState, committedAllocator, 
                                                          cmdQueueTimestampFrequency, splitTimes);
    assert(m_timeStamp);

    std::wstringstream ss;
    ss << debugName << L" ID3D12GraphicsCommandList 0x" 
                    << std::ios::uppercase << std::ios::hex << m_cmdList.Get();
    m_debugName = ss.str();
}

D3D12GraphicsCmdList::~D3D12GraphicsCmdList() = default;

void D3D12GraphicsCmdList::Open()
{
    auto cmdAllocator = m_cmdAllocators[m_gpuState->m_currentFrameIndex];

    AssertIfFailed(cmdAllocator->Reset());
    AssertIfFailed(m_cmdList->Reset(cmdAllocator.Get(), nullptr));

    m_timeStamp->Begin();

    ID3D12DescriptorHeap* ppHeaps[] = { m_gpuState->m_descriptorHeap };
    m_cmdList->SetDescriptorHeaps(1, ppHeaps);
}

void D3D12GraphicsCmdList::Close()
{
    m_timeStamp->End();

    AssertIfFailed(m_cmdList->Close());
}

D3D12Gpu::D3D12Gpu(bool isWaitableForPresentEnabled)    :    m_isWaitableForPresentEnabled(isWaitableForPresentEnabled),
                                                             m_nextHandleId(0), m_currentFrame(0)
{
    m_state = std::make_unique<D3D12GpuShareableState>();
    assert(m_state);

    auto adapter = CreateDXGIInfrastructure();
    assert(adapter);

    CreateDevice(adapter);
    CheckFeatureSupport();

    m_dynamicMemoryAllocator = std::make_unique<D3D12DynamicBufferAllocator>(m_state->m_device, g_64kb, g_128kb);
    assert(m_dynamicMemoryAllocator);

    CreateCommandInfrastructure();

    m_committedResourceAllocator = std::make_unique<D3D12CommittedResourceAllocator>(m_state->m_device, m_graphicsCmdQueue);
    assert(m_committedResourceAllocator);

    CreateDescriptorHeaps();

    m_gpuSync = std::make_unique<D3D12GpuSynchronizer>(m_state->m_device, m_graphicsCmdQueue, D3D12GpuConfig::m_framesInFlight,
                                                       m_frameStats.m_waitForFenceTime);
    assert(m_gpuSync);
    m_currentFrame = m_gpuSync->GetNextFrameId();
}

D3D12Gpu::~D3D12Gpu()
{
    m_gpuSync->WaitAll();
}

unsigned int D3D12Gpu::GetFormatPlaneCount(DXGI_FORMAT format) const
{
    D3D12_FEATURE_DATA_FORMAT_INFO formatInfo = { format };
    return FAILED(m_state->m_device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_INFO, 
                                                         &formatInfo, sizeof(formatInfo))) ? 0 : formatInfo.PlaneCount;
}

void D3D12Gpu::SetOutputWindow(HWND hwnd)
{
    // NOTE only one output supported
    m_swapChain = std::make_unique<D3D12SwapChain>(hwnd, g_swapChainFormat, m_safestResolution,
                                                   m_factory, m_state->m_device, m_graphicsCmdQueue,
                                                   m_frameStats.m_presentTime,
                                                   m_frameStats.m_waitForPresentTime,
                                                   m_isWaitableForPresentEnabled);
    assert(m_swapChain);
}

const Resolution& D3D12Gpu::GetCurrentResolution() const 
{ 
    return m_swapChain->GetCurrentResolution(); 
}

bool D3D12Gpu::IsFrameFinished(uint64_t frameId)
{
    return m_gpuSync->GetLastRetiredFrameId() >= frameId;
}

// TODO think about how to use the debugname of a dynamic resource
D3D12GpuMemoryHandle D3D12Gpu::AllocateDynamicMemory(size_t sizeBytes, const std::wstring& /*debugName*/)
{
    if (sizeBytes > g_128kb)
    {
        // assert(sizeBytes < g_128kb);
        OutputDebugString(L"D3D12Gpu::AllocateDynamicMemory. sizeBytes is bigger than the big page size g_128kb\n");
        return {};
    }
 
    DynamicMemoryAlloc allocation;

    for (unsigned int i = 0; i < D3D12GpuConfig::m_framesInFlight; ++i)
    {
        allocation.m_allocation[i] = m_dynamicMemoryAllocator->Allocate(sizeBytes, 
                                                                        D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
        assert(D3D12Basics::IsAlignedToPowerof2(allocation.m_allocation[i].m_gpuPtr, 
                                                D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT));
        allocation.m_frameId[i] = m_currentFrame;
    }

    m_dynamicMemoryAllocations[m_nextHandleId] = std::move(allocation);

    return EncodeGpuMemoryHandle(m_nextHandleId++, true, ResourceType::Buffer);
}

D3D12GpuMemoryHandle D3D12Gpu::AllocateStaticMemory(const void* data, size_t sizeBytes, const std::wstring& debugName)
{
    auto committedBuffer = m_committedResourceAllocator->AllocateBuffer(data, sizeBytes, 
                                                                        D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT,
                                                                        debugName);

    m_staticBufferMemoryAllocations[m_nextHandleId] = StaticBufferAlloc{ m_currentFrame, committedBuffer };

    return EncodeGpuMemoryHandle(m_nextHandleId++, false, ResourceType::Buffer);
}

D3D12GpuMemoryHandle D3D12Gpu::AllocateStaticMemory(const std::vector<D3D12_SUBRESOURCE_DATA>& subresources, 
                                                    const D3D12_RESOURCE_DESC& desc,
                                                    const std::wstring& debugName)
{
    auto resource = m_committedResourceAllocator->AllocateTexture(subresources, desc, debugName);
    assert(resource);

    m_staticTextureMemoryAllocations[m_nextHandleId] = StaticTextureAlloc{ m_currentFrame, resource };

    return EncodeGpuMemoryHandle(m_nextHandleId++, false, ResourceType::Texture);
}

D3D12GpuMemoryHandle D3D12Gpu::AllocateStaticMemory(const D3D12_RESOURCE_DESC& desc, D3D12_RESOURCE_STATES initialState, 
                                                    const D3D12_CLEAR_VALUE* clearValue, const std::wstring& debugName)
{
    ID3D12ResourcePtr resource = CreateResourceHeap(m_state->m_device, desc, ResourceHeapType::DefaultHeap, initialState, clearValue);
    assert(resource);
    resource->SetName(debugName.c_str());

    m_staticTextureMemoryAllocations[m_nextHandleId] = StaticTextureAlloc{ m_currentFrame, resource };

    return EncodeGpuMemoryHandle(m_nextHandleId++, false, ResourceType::Texture);
}

void D3D12Gpu::UpdateMemory(D3D12GpuMemoryHandle memHandle, const void* data, size_t sizeBytes, size_t offsetBytes)
{
    assert(memHandle.IsValid());
    assert(data);
    assert(sizeBytes > 0);
    assert(DecodeGpuMemoryHandle_IsDynamic(memHandle));

    auto decodedHandle = DecodeGpuMemoryHandle_ID(memHandle);
    assert(m_dynamicMemoryAllocations.count(decodedHandle) == 1);

    auto& memoryAlloc = m_dynamicMemoryAllocations[decodedHandle];
    assert(memoryAlloc.m_allocation[m_state->m_currentFrameIndex].m_cpuPtr);

    memcpy(memoryAlloc.m_allocation[m_state->m_currentFrameIndex].m_cpuPtr + offsetBytes, data, sizeBytes);

    memoryAlloc.m_frameId[m_state->m_currentFrameIndex] = m_currentFrame;
}

void D3D12Gpu::FreeMemory(D3D12GpuMemoryHandle memHandle)
{
    assert(memHandle.IsValid());

    m_retiredAllocations.push_back(memHandle);
}

D3D12GpuViewHandle D3D12Gpu::CreateConstantBufferView(D3D12GpuMemoryHandle memHandle)
{
    assert(memHandle.IsValid());
    auto decodedHandle = DecodeGpuMemoryHandle_ID(memHandle);

    DescriptorHandlesPtrs descriptors;
    if (DecodeGpuMemoryHandle_IsDynamic(memHandle))
    {
        // Create a descriptor per frames in flight pointing each to the corresponding
        // memory of that buffer in that frame
        assert(m_dynamicMemoryAllocations.count(decodedHandle) == 1);
        auto& memoryAlloc = m_dynamicMemoryAllocations[decodedHandle];

        for (unsigned int i = 0; i < D3D12GpuConfig::m_framesInFlight; ++i)
        {
            D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
            cbvDesc.BufferLocation = memoryAlloc.m_allocation[i].m_gpuPtr;
            cbvDesc.SizeInBytes = static_cast<UINT>(memoryAlloc.m_allocation[i].m_size);
            D3D12DescriptorAllocation* cbvHandle = m_cpuSRV_CBVDescHeap->CreateCBV(cbvDesc);
            assert(cbvHandle);

            descriptors[i] = cbvHandle;
        }
    }
    else
    {
        assert(DecodeGpuMemoryHandle_ResourceType(memHandle) == ResourceType::Buffer);

        assert(m_staticBufferMemoryAllocations.count(decodedHandle) == 1);
        auto& memoryAlloc = m_staticBufferMemoryAllocations[decodedHandle];

        D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
        cbvDesc.BufferLocation = memoryAlloc.m_committedBuffer.m_resource->GetGPUVirtualAddress();
        cbvDesc.SizeInBytes = static_cast<UINT>(memoryAlloc.m_committedBuffer.m_alignedSize);
        descriptors[0] = m_cpuSRV_CBVDescHeap->CreateCBV(cbvDesc);
        assert(descriptors[0]);
    }

    return CreateView(memHandle, std::move(descriptors));
}

D3D12GpuViewHandle D3D12Gpu::CreateTextureView(D3D12GpuMemoryHandle memHandle, const D3D12_RESOURCE_DESC& desc)
{
    assert(memHandle.IsValid());

    // TODO not supported yet
    assert(!DecodeGpuMemoryHandle_IsDynamic(memHandle));
    assert(DecodeGpuMemoryHandle_ResourceType(memHandle) == ResourceType::Texture);

    auto decodedHandle = DecodeGpuMemoryHandle_ID(memHandle);
    assert(m_staticTextureMemoryAllocations.count(decodedHandle) == 1);

    // TODO only 2d textures with limited props supported
    D3D12_SHADER_RESOURCE_VIEW_DESC viewDesc;
    viewDesc.Format = desc.Format;
    viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    viewDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    viewDesc.Texture2D.MostDetailedMip = 0;
    viewDesc.Texture2D.MipLevels = desc.MipLevels;
    viewDesc.Texture2D.PlaneSlice = 0;
    viewDesc.Texture2D.ResourceMinLODClamp = 0.0f;

    DescriptorHandlesPtrs descriptors;
    auto& memoryAllocation = m_staticTextureMemoryAllocations[decodedHandle];
    descriptors[0] = m_cpuSRV_CBVDescHeap->CreateSRV(memoryAllocation.m_resource.Get(), viewDesc);
    assert(descriptors[0]);
    
    return CreateView(memHandle, std::move(descriptors));
}

D3D12GpuViewHandle D3D12Gpu::CreateRenderTargetView(D3D12GpuMemoryHandle memHandle, 
                                                    const D3D12_RESOURCE_DESC& desc)
{
    assert(memHandle.IsValid());

    // TODO not supported yet
    assert(!DecodeGpuMemoryHandle_IsDynamic(memHandle));
    assert(DecodeGpuMemoryHandle_ResourceType(memHandle) == ResourceType::Texture);

    auto decodedHandle = DecodeGpuMemoryHandle_ID(memHandle);
    assert(m_staticTextureMemoryAllocations.count(decodedHandle) == 1);

    // TODO only 2d textures with limited props supported
    D3D12_SHADER_RESOURCE_VIEW_DESC viewDesc;
    viewDesc.Format = desc.Format;
    viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    viewDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    viewDesc.Texture2D.MostDetailedMip = 0;
    viewDesc.Texture2D.MipLevels = desc.MipLevels;
    viewDesc.Texture2D.PlaneSlice = 0;
    viewDesc.Texture2D.ResourceMinLODClamp = 0.0f;

    DescriptorHandlesPtrs descriptors;
    auto& memoryAllocation = m_staticTextureMemoryAllocations[decodedHandle];
    descriptors[0] = m_cpuRTVDescHeap->CreateRTV(memoryAllocation.m_resource.Get());
    assert(descriptors[0]);

    return CreateView(memHandle, std::move(descriptors));
}

D3D12GpuViewHandle D3D12Gpu::CreateDepthStencilView(D3D12GpuMemoryHandle memHandle, DXGI_FORMAT format)
{
    assert(memHandle.IsValid());

    // TODO not supported yet
    assert(!DecodeGpuMemoryHandle_IsDynamic(memHandle));
    assert(DecodeGpuMemoryHandle_ResourceType(memHandle) == ResourceType::Texture);

    auto decodedHandle = DecodeGpuMemoryHandle_ID(memHandle);
    assert(m_staticTextureMemoryAllocations.count(decodedHandle) == 1);

    D3D12_DEPTH_STENCIL_VIEW_DESC desc;
    desc.Format             = format;
    desc.ViewDimension      = D3D12_DSV_DIMENSION_TEXTURE2D;
    desc.Flags              = D3D12_DSV_FLAG_NONE;
    desc.Texture2D.MipSlice = 0;

    DescriptorHandlesPtrs descriptors;
    auto& memoryAllocation = m_staticTextureMemoryAllocations[decodedHandle];
    descriptors[0] = m_dsvDescPool->CreateDSV(memoryAllocation.m_resource.Get(), desc, nullptr);
    assert(descriptors[0]);

    return CreateView(memHandle, std::move(descriptors));
}

D3D12GpuViewHandle D3D12Gpu::CreateNULLTextureView(const D3D12_RESOURCE_DESC& desc)
{
    // TODO only 2d textures with limited props supported
    D3D12_SHADER_RESOURCE_VIEW_DESC viewDesc;
    viewDesc.Format = desc.Format;
    viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    viewDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    viewDesc.Texture2D.MostDetailedMip = 0;
    viewDesc.Texture2D.MipLevels = desc.MipLevels;
    viewDesc.Texture2D.PlaneSlice = 0;
    viewDesc.Texture2D.ResourceMinLODClamp = 0.0f;

    DescriptorHandlesPtrs descriptors;
    descriptors[0] = m_cpuSRV_CBVDescHeap->CreateSRV(nullptr, viewDesc);
    assert(descriptors[0]);

    return CreateView(D3D12GpuMemoryHandle{ D3D12GpuHandle::m_nullId }, std::move(descriptors));
}

D3D12_RESOURCE_BARRIER& D3D12Gpu::SwapChainTransition(TransitionType transitionType)
{
    return m_swapChain->Transition(transitionType);
}

const D3D12_CPU_DESCRIPTOR_HANDLE& D3D12Gpu::SwapChainBackBufferViewHandle() const
{
    return m_swapChain->RTV();
}

D3D12GraphicsCmdListPtr D3D12Gpu::CreateCmdList(const std::wstring& debugName)
{
    FrameStats::NamedCmdListTime cmdListTime{ debugName, {} };
    m_frameStats.m_cmdListTimes.push_back(std::make_unique<FrameStats::NamedCmdListTime>(std::move(cmdListTime)));
    return std::make_unique<D3D12GraphicsCmdList>(m_state.get(), m_committedResourceAllocator.get(),
                                                  m_cmdQueueTimestampFrequency, 
                                                  m_frameStats.m_cmdListTimes.back()->second, 
                                                  debugName);
}

void D3D12Gpu::ExecuteCmdLists(const D3D12CmdLists& cmdLists)
{
    m_graphicsCmdQueue->ExecuteCommandLists(static_cast<UINT>(cmdLists.size()), &cmdLists[0]);
}

void D3D12Gpu::PresentFrame()
{
    g_gpuViewMarkerPrePresentFrame.Mark();
    m_swapChain->Present(D3D12GpuConfig::m_vsync);
    g_gpuViewMarkerPostPresentFrame.Mark();

    g_gpuViewMarkerPreWaitFrame.Mark();

    const bool hasWaitedForFence = m_gpuSync->Wait();

    // TODO Fences for gpu/cpu sync after present are already being used.
    //      Why would be needed to use the waitable object to wait for
    //      the present if its already being counted for in the fence?
    //      Does the waitable object work signal in a different time than 
    //      the fence?
    if (m_isWaitableForPresentEnabled)
    {
        m_swapChain->WaitForPresent();
    }
    g_gpuViewMarkerPostWaitFrame.Mark();

    // Note: we can have x frames in flight and y backbuffers
    m_state->m_currentFrameIndex = (m_state->m_currentFrameIndex + 1) % D3D12GpuConfig::m_framesInFlight;
    m_currentFrame = m_gpuSync->GetNextFrameId();

    m_gpuDescriptorRingBuffer->NextStack();
    m_gpuDescriptorRingBuffer->ClearStack();

    // TODO destroy retired buffers depending on if the frames they were bound are
    // still in flight.
    DestroyRetiredAllocations();

    m_frameStats.m_frameTime.Mark();
}

void D3D12Gpu::WaitAll()
{
    m_gpuSync->WaitAll();
}

ID3D12RootSignaturePtr D3D12Gpu::CreateRootSignature(ID3DBlobPtr signature, const std::wstring& name)
{
    assert(signature);

    ID3D12RootSignaturePtr rootSignature;
    if (FAILED(m_state->m_device->CreateRootSignature(0, signature->GetBufferPointer(), 
                                                      signature->GetBufferSize(), 
                                                      IID_PPV_ARGS(&rootSignature))))
        return nullptr;

    rootSignature->SetName(name.c_str());

    return rootSignature;
}

ID3D12PipelineStatePtr D3D12Gpu::CreatePSO(const D3D12_GRAPHICS_PIPELINE_STATE_DESC& psoDesc, const std::wstring& name)
{
    ID3D12PipelineStatePtr pso;
    if (FAILED(m_state->m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pso))))
        return nullptr;

    pso->SetName(name.c_str());

    return pso;
}

// 
void D3D12Gpu::OnToggleFullScreen()
{
    // NOTE the actual resizing of the buffer doesnt happen here
    //      so its safe to keep the gpu working a little bit more
    //      until the actual resize happens.
    m_swapChain->ToggleFullScreen();
}

void D3D12Gpu::OnResize(const Resolution& resolution)
{
    m_gpuSync->WaitAll();

    const auto& swapChainDisplayMode = FindClosestDisplayModeMatch(g_swapChainFormat, resolution);
    m_swapChain->Resize(swapChainDisplayMode);
}

void D3D12Gpu::SetBindings(ID3D12GraphicsCommandListPtr cmdList, const D3D12Bindings& bindings)
{
    for (auto& constants : bindings.m_32BitConstants)
    {
        cmdList->SetGraphicsRoot32BitConstants(static_cast<UINT>(constants.m_bindingSlot),
                                               static_cast<UINT>(constants.m_data.size()),
                                               &constants.m_data[0], 0);
    }

    for (auto& cbv : bindings.m_constantBufferViews)
    {
        auto decodedHandle = DecodeGpuMemoryHandle_ID(cbv.m_memoryHandle);
        D3D12_GPU_VIRTUAL_ADDRESS memoryVA{};
        if (DecodeGpuMemoryHandle_IsDynamic(cbv.m_memoryHandle))
        {
            assert(m_dynamicMemoryAllocations.count(decodedHandle) == 1);
            memoryVA = m_dynamicMemoryAllocations[decodedHandle].m_allocation[m_state->m_currentFrameIndex].m_gpuPtr;
            m_dynamicMemoryAllocations[decodedHandle].m_frameId[m_state->m_currentFrameIndex] = m_currentFrame;
        }
        else
        {
            assert(DecodeGpuMemoryHandle_ResourceType(cbv.m_memoryHandle) == ResourceType::Texture);

            assert(m_staticBufferMemoryAllocations.count(decodedHandle) == 1);
            memoryVA = m_staticBufferMemoryAllocations[decodedHandle].m_committedBuffer.m_resource->GetGPUVirtualAddress();
            m_staticBufferMemoryAllocations[decodedHandle].m_frameId = m_currentFrame;
        }

        cmdList->SetGraphicsRootConstantBufferView(static_cast<UINT>(cbv.m_bindingSlot), memoryVA);
    }

    // TODO figure out how to copy the descriptors in batches (maybe having arrays of views?) if possible
    for (auto& cpuDescriptorTable : bindings.m_descriptorTables)
    {
        D3D12_GPU_DESCRIPTOR_HANDLE descriptorTableHandle = m_gpuDescriptorRingBuffer->CurrentDescriptor();

        for (auto viewHandle : cpuDescriptorTable.m_views)
        {
            assert(viewHandle.IsValid());
            assert(viewHandle.m_id < m_memoryViews.size());
            auto& view = m_memoryViews[viewHandle.m_id];
            assert(view);

            auto decodedHandle = DecodeGpuMemoryHandle_ID(view->m_memHandle);
            D3D12_CPU_DESCRIPTOR_HANDLE descriptorHandle{};
            if (DecodeGpuMemoryHandle_IsDynamic(view->m_memHandle))
            {
                assert(m_dynamicMemoryAllocations.count(decodedHandle) == 1);
                DynamicMemoryAlloc& dynamicAllocation = m_dynamicMemoryAllocations[decodedHandle];
                dynamicAllocation.m_frameId[m_state->m_currentFrameIndex] = m_currentFrame;

                descriptorHandle = view->m_frameDescriptors[m_state->m_currentFrameIndex]->m_cpuHandle;
            }
            else
            {
                descriptorHandle = view->m_frameDescriptors[0]->m_cpuHandle;

                if (!view->m_memHandle.IsNull())
                {
                    const auto resourceType = DecodeGpuMemoryHandle_ResourceType(view->m_memHandle);
                    if (resourceType == ResourceType::Texture)
                    {
                        assert(m_staticTextureMemoryAllocations.count(decodedHandle) == 1);
                        m_staticTextureMemoryAllocations[decodedHandle].m_frameId = m_currentFrame;
                    }
                    else
                    {
                        assert(m_staticBufferMemoryAllocations.count(decodedHandle) == 1);
                        m_staticBufferMemoryAllocations[decodedHandle].m_frameId = m_currentFrame;
                    }
                }
            }

            m_gpuDescriptorRingBuffer->CopyToDescriptor(1, descriptorHandle);
            m_gpuDescriptorRingBuffer->NextDescriptor();
        }

        cmdList->SetGraphicsRootDescriptorTable(static_cast<UINT>(cpuDescriptorTable.m_bindingSlot), 
                                                descriptorTableHandle);
    }
}

void D3D12Gpu::SetVertexBuffer(ID3D12GraphicsCommandListPtr cmdList, D3D12GpuMemoryHandle memHandle,
                               size_t vertexBufferSizeBytes, size_t vertexSizeBytes)
{
    assert(memHandle.IsValid());

    D3D12_VERTEX_BUFFER_VIEW vertexBufferView
    {
        GetBufferVA(memHandle),
        static_cast<UINT>(vertexBufferSizeBytes),
        static_cast<UINT>(vertexSizeBytes)
    };
    cmdList->IASetVertexBuffers(0, 1, &vertexBufferView);
}

void D3D12Gpu::SetIndexBuffer(ID3D12GraphicsCommandListPtr cmdList, D3D12GpuMemoryHandle memHandle,
                              size_t indexBufferSizeBytes)
{
    assert(memHandle.IsValid());

    D3D12_INDEX_BUFFER_VIEW indexBufferView
    {
        GetBufferVA(memHandle),
        static_cast<UINT>(indexBufferSizeBytes), DXGI_FORMAT_R16_UINT
    };
    cmdList->IASetIndexBuffer(&indexBufferView);
}

D3D12_CPU_DESCRIPTOR_HANDLE D3D12Gpu::GetViewCPUHandle(D3D12GpuViewHandle gpuViewHandle) const
{
    assert(gpuViewHandle.IsValid());
    assert(gpuViewHandle.m_id < m_memoryViews.size());

    auto& memoryView = m_memoryViews[gpuViewHandle.m_id];
    assert(memoryView);

    const bool isDynamic = DecodeGpuMemoryHandle_IsDynamic(memoryView->m_memHandle);

	return memoryView->m_frameDescriptors[isDynamic ? m_state->m_currentFrameIndex : 0]->m_cpuHandle;
}

ID3D12Resource* D3D12Gpu::GetResource(D3D12GpuMemoryHandle memHandle)
{
    assert(memHandle.IsValid());

    // NOTE not supported yet
    assert(!DecodeGpuMemoryHandle_IsDynamic(memHandle));
    auto decodedHandle = DecodeGpuMemoryHandle_ID(memHandle);
    ID3D12ResourcePtr resource = nullptr;
    const auto resourceType = DecodeGpuMemoryHandle_ResourceType(memHandle);
    if (resourceType == ResourceType::Texture)
    {
        assert(m_staticTextureMemoryAllocations.count(decodedHandle) == 1);
        resource = m_staticTextureMemoryAllocations[decodedHandle].m_resource;
        assert(resource);
    }
    else
    {
        assert(m_staticBufferMemoryAllocations.count(decodedHandle) == 1);
        resource = m_staticBufferMemoryAllocations[decodedHandle].m_committedBuffer.m_resource;
        assert(resource);
    }

    return resource.Get();
}

DisplayModes D3D12Gpu::EnumerateDisplayModes(DXGI_FORMAT format)
{
    unsigned int displayModesCount = 0;
    auto result = m_output1->GetDisplayModeList1(format, 0, &displayModesCount, nullptr);
    // GetDisplayModeList doesn't work with a remote desktop connection
    // Find the safest resolution possible
    if (result == DXGI_ERROR_NOT_CURRENTLY_AVAILABLE)
    {
        DisplayModes displayModes{ 1 };
        displayModes[0] = CreateDefaultDisplayMode();

        return displayModes;
    }
    AssertIfFailed(result);

    DisplayModes displayModes{ displayModesCount };
    AssertIfFailed(m_output1->GetDisplayModeList1(format, 0, &displayModesCount, &displayModes[0]));

    return displayModes;
}

DXGI_MODE_DESC1 D3D12Gpu::FindClosestDisplayModeMatch(DXGI_FORMAT format, const Resolution& resolution)
{
    DXGI_MODE_DESC1 modeToMatch;
    ZeroMemory(&modeToMatch, sizeof(DXGI_MODE_DESC1));
    modeToMatch.Format = format;
    modeToMatch.Width = resolution.m_width;
    modeToMatch.Height = resolution.m_height;
    DXGI_MODE_DESC1 closestMatch;

    auto result = m_output1->FindClosestMatchingMode1(&modeToMatch, &closestMatch, m_state->m_device.Get());
    // FindClosestMatchingMode1 doesn't work with a remote desktop connection
    // Find the safest resolution possible
    if (result == DXGI_ERROR_NOT_CURRENTLY_AVAILABLE)
    {
        closestMatch = CreateDefaultDisplayMode();
    }
    else AssertIfFailed(result);

    return closestMatch;
}

void D3D12Gpu::CreateCommandInfrastructure()
{
    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    AssertIfFailed(m_state->m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_graphicsCmdQueue)));
    assert(m_graphicsCmdQueue);

    AssertIfFailed(m_graphicsCmdQueue->GetTimestampFrequency(&m_cmdQueueTimestampFrequency));
}

void D3D12Gpu::CreateDevice(IDXGIAdapterPtr adapter)
{
#if ENABLE_D3D12_DEBUG_LAYER
    // Note this needs to be called before creating the d3d12 device
    Microsoft::WRL::ComPtr<ID3D12Debug> debugController0;
    AssertIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController0)));
    debugController0->EnableDebugLayer();
#if ENABLE_D3D12_DEBUG_GPU_VALIDATION
    Microsoft::WRL::ComPtr<ID3D12Debug1> debugController1;
    AssertIfFailed(debugController0->QueryInterface(IID_PPV_ARGS(&debugController1)));
    debugController1->SetEnableGPUBasedValidation(TRUE);
#endif
#endif
    AssertIfFailed(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(&m_state->m_device)));
    assert(m_state->m_device);
}

// Note depending on the preferred gpu in a system with a dgpu and a igpu, the enum outputs might failed.
// Thats why theres a check for the output enum to work when searching for a suitable adapter.
IDXGIAdapterPtr D3D12Gpu::CreateDXGIInfrastructure()
{
    AssertIfFailed(CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, IID_PPV_ARGS(&m_factory)));
    assert(m_factory);

    Microsoft::WRL::ComPtr<IDXGIAdapter4> adapter;
    Microsoft::WRL::ComPtr<IDXGIOutput> adapterOutput;
    for (unsigned int adapterIndex = 0; ; ++adapterIndex)
    {
        HRESULT result = m_factory->EnumAdapterByGpuPreference(adapterIndex, 
                                                               DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                                               IID_PPV_ARGS(adapter.ReleaseAndGetAddressOf()));
        if (result != DXGI_ERROR_NOT_FOUND)
        {
            DXGI_ADAPTER_DESC1 desc;
            AssertIfFailed(adapter->GetDesc1(&desc));
            if (!(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE))
            {
                if (SUCCEEDED(adapter->EnumOutputs(0, &adapterOutput)))
                {
                    OutputDebugString((std::wstring(L"[D3D12Gpu] Using adapter ") + desc.Description + L"\n").c_str());
                    break;
                }
            }
        }
    }
    assert(adapterOutput);
    assert(adapter);

    AssertIfFailed(adapterOutput->QueryInterface(__uuidof(IDXGIOutput1), (void **)&m_output1));
    assert(m_output1);

    const auto& displayModes = EnumerateDisplayModes(g_swapChainFormat);
    assert(displayModes.size() > 0);

    m_safestDisplayMode = displayModes[0];
    m_safestResolution = { m_safestDisplayMode.Width, m_safestDisplayMode.Height };
    assert(m_safestResolution.m_width > 0 && m_safestResolution.m_height > 0);

    return adapter;
}

void D3D12Gpu::CreateDescriptorHeaps()
{
	const uint32_t maxDescriptors = 65536;
    m_dsvDescPool = std::make_unique<D3D12DSVDescriptorPool>(m_state->m_device, maxDescriptors);
    assert(m_dsvDescPool);

    m_cpuSRV_CBVDescHeap = std::make_unique<D3D12CBV_SRV_UAVDescriptorBuffer>(m_state->m_device, maxDescriptors);
    assert(m_cpuSRV_CBVDescHeap);

    m_cpuRTVDescHeap = std::make_unique<D3D12RTVDescriptorBuffer>(m_state->m_device, maxDescriptors);
    assert(m_cpuRTVDescHeap);
    
    auto maxHeaps = std::max(D3D12GpuConfig::m_framesInFlight, D3D12GpuConfig::m_backBuffersCount);
    m_gpuDescriptorRingBuffer = std::make_unique<D3D12GPUDescriptorRingBuffer>(m_state->m_device, maxHeaps, maxDescriptors);
    assert(m_gpuDescriptorRingBuffer);

    m_state->m_descriptorHeap = m_gpuDescriptorRingBuffer->GetDescriptorHeap().Get();
    assert(m_state->m_descriptorHeap);
}

void D3D12Gpu::CheckFeatureSupport()
{
    D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};
    featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;

    AssertIfFailed(m_state->m_device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData)));
}

D3D12_GPU_VIRTUAL_ADDRESS D3D12Gpu::GetBufferVA(D3D12GpuMemoryHandle memHandle)
{
    assert(memHandle.IsValid());
    const auto decodedHandle = DecodeGpuMemoryHandle_ID(memHandle);

    if (DecodeGpuMemoryHandle_IsDynamic(memHandle))
    {
        assert(m_dynamicMemoryAllocations.count(decodedHandle) == 1);
        auto& memoryAllocation = m_dynamicMemoryAllocations[decodedHandle];
        assert(memoryAllocation.m_allocation[m_state->m_currentFrameIndex].m_gpuPtr);
        // NOTE: record the frame when the function got called
        // NOTE: assuming GetBufferVA means binding to the pipeline isnt the best way
        // to handle this.
        memoryAllocation.m_frameId[m_state->m_currentFrameIndex] = m_gpuSync->GetNextFrameId();
        return memoryAllocation.m_allocation[m_state->m_currentFrameIndex].m_gpuPtr;
    }

    const auto resourceType = DecodeGpuMemoryHandle_ResourceType(memHandle);
    ID3D12ResourcePtr resource = nullptr;
    if (resourceType == ResourceType::Texture)
    {
        assert(decodedHandle < m_staticTextureMemoryAllocations.size());
        assert(m_staticTextureMemoryAllocations.count(decodedHandle) == 1);
        resource = m_staticTextureMemoryAllocations[decodedHandle].m_resource;
        assert(resource);
    }
    else
    {
        assert(m_staticBufferMemoryAllocations.count(decodedHandle) == 1);
        resource = m_staticBufferMemoryAllocations[decodedHandle].m_committedBuffer.m_resource;
        assert(resource);
    }

    return resource->GetGPUVirtualAddress();
}

void D3D12Gpu::DestroyRetiredAllocations()
{
    auto lastRetiredFrameId = m_gpuSync->GetLastRetiredFrameId();
    for (auto it = m_retiredAllocations.begin(); it != m_retiredAllocations.end(); )
    {
        auto& memAllocation = *it;

        // TODO free descriptors as well?
        bool completelyRetired = true;
        auto decodedHandle = DecodeGpuMemoryHandle_ID(memAllocation);

        if (DecodeGpuMemoryHandle_IsDynamic(memAllocation))
        {
            assert(m_dynamicMemoryAllocations.count(decodedHandle) == 1);
            auto& dynamicMemoryAllocation = m_dynamicMemoryAllocations[decodedHandle];
            for (auto i = 0; i < D3D12GpuConfig::m_framesInFlight; ++i)
                completelyRetired &= dynamicMemoryAllocation.m_frameId[i] <= lastRetiredFrameId;

            if (completelyRetired)
            {
                for (auto i = 0; i < D3D12GpuConfig::m_framesInFlight; ++i)
                    m_dynamicMemoryAllocator->Deallocate(dynamicMemoryAllocation.m_allocation[i]);
            }

            // TODO is it worth it to remove it?
             m_dynamicMemoryAllocations.erase(memAllocation.m_id);
        }
        else
        {
            const auto resourceType = DecodeGpuMemoryHandle_ResourceType(memAllocation);
            ID3D12ResourcePtr resource = nullptr;
            if (resourceType == ResourceType::Texture)
            {
                assert(m_staticTextureMemoryAllocations.count(decodedHandle) == 1);
                const auto frameId = m_staticTextureMemoryAllocations[decodedHandle].m_frameId;
                completelyRetired = frameId <= lastRetiredFrameId;
                // TODO is it worth it to remove it?
                if (completelyRetired)
                    m_staticBufferMemoryAllocations.erase(memAllocation.m_id);
            }
            else 
            {
                assert(m_staticBufferMemoryAllocations.count(decodedHandle) == 1);
                const auto frameId = m_staticBufferMemoryAllocations[decodedHandle].m_frameId;
                completelyRetired = frameId <= lastRetiredFrameId;
                // TODO is it worth it to remove it?
                if (completelyRetired)
                    m_staticBufferMemoryAllocations.erase(memAllocation.m_id);
            }
        }

        if (completelyRetired)
            it = m_retiredAllocations.erase(it);
        else
            ++it;
    }
}

D3D12GpuViewHandle D3D12Gpu::CreateView(D3D12GpuMemoryHandle memHandle, DescriptorHandlesPtrs&& descriptors)
{
    D3D12GpuMemoryViewPtr view = std::make_unique<D3D12GpuMemoryView>(memHandle, std::move(descriptors));
    assert(view);

    D3D12GpuViewHandle viewHandle{ m_memoryViews.size() };
    m_memoryViews.push_back(std::move(view));

    return viewHandle;
}