#include "d3d12gpus.h"

// C includes
#include <cassert>

// C++ includes
#include <string>
#include <iostream>
#include <sstream>

// windows includes
#include <dxgi1_4.h>

// directx includes
#include <d3d12.h>
#include <d3dcompiler.h>

// Project includes
#include "utils.h"
#include "d3d12simplematerial.h"
#include "d3d12descriptorheap.h"

using namespace D3D12Render;
using namespace Utils;

namespace
{
    IDXGIFactory4Ptr CreateFactory()
    {
        // NOTE: Enabling the debug layer after device creation will invalidate the active device.
        // Enable the D3D12 debug layer.
        Microsoft::WRL::ComPtr<ID3D12Debug> debugController;
        AssertIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)));
        debugController->EnableDebugLayer();
        const UINT dxgiFactoryFlags = DXGI_CREATE_FACTORY_DEBUG;

        IDXGIFactory4Ptr factory;
        AssertIfFailed(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)));

        return factory;
    }

    D3D12_RESOURCE_DESC CreateBufferDesc(uint64_t sizeInBytes)
    {
        D3D12_RESOURCE_DESC resourceDesc;
        resourceDesc.Dimension           = D3D12_RESOURCE_DIMENSION_BUFFER;
        resourceDesc.Alignment           = 0;
        resourceDesc.Width               = sizeInBytes;
        resourceDesc.Height              = 1;
        resourceDesc.DepthOrArraySize    = 1;
        resourceDesc.MipLevels           = 1;
        resourceDesc.Format              = DXGI_FORMAT_UNKNOWN;
        resourceDesc.SampleDesc          = { 1, 0 };
        resourceDesc.Layout              = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        resourceDesc.Flags               = D3D12_RESOURCE_FLAG_NONE;

        return resourceDesc;
    }

    unsigned int CalculateConstantBufferRequiredSize(unsigned int requestedSizeInBytes)
    {
        const unsigned int requiredAlignmentForCB = 256;
        return (requestedSizeInBytes + (requiredAlignmentForCB - 1)) & ~(requiredAlignmentForCB - 1);
    }
}

template<unsigned int BackBuffersCount>
class D3D12Gpu::D3D12BackBuffers
{
public:
    enum TransitionType
    {
        Present_To_RenderTarget,
        RenderTarget_To_Present,
        TransitionType_COUNT
    };

    D3D12BackBuffers(ID3D12DevicePtr device, IDXGISwapChain3Ptr swapChain)
    {
        // TODO: does this requires C++17?
        //static_assert(BackBuffersCount > 0);

        assert(device);
        assert(swapChain);

        // Describe and create a render target view (RTV) descriptor heap.
        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{ D3D12_DESCRIPTOR_HEAP_TYPE_RTV, BackBuffersCount, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0 };
        AssertIfFailed(device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_heap)));

        m_descriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle{ m_heap->GetCPUDescriptorHandleForHeapStart() };
        for (unsigned int i = 0; i < BackBuffersCount; ++i)
        {
            // Create a RTV for each frame.
            AssertIfFailed(swapChain->GetBuffer(i, IID_PPV_ARGS(&m_renderTargets[i])));
            device->CreateRenderTargetView(m_renderTargets[i].Get(), nullptr, rtvHandle);
            rtvHandle.ptr += m_descriptorSize;
            std::wstringstream converter;
            converter << L"Back buffer " << i;
            m_renderTargets[i]->SetName(converter.str().c_str());

            // Create the transitions
            m_transitions[TransitionType::Present_To_RenderTarget][i].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            m_transitions[TransitionType::Present_To_RenderTarget][i].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
            m_transitions[TransitionType::Present_To_RenderTarget][i].Transition.pResource = m_renderTargets[i].Get();
            m_transitions[TransitionType::Present_To_RenderTarget][i].Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
            m_transitions[TransitionType::Present_To_RenderTarget][i].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
            m_transitions[TransitionType::Present_To_RenderTarget][i].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

            m_transitions[TransitionType::RenderTarget_To_Present][i] = m_transitions[TransitionType::Present_To_RenderTarget][i];
            m_transitions[TransitionType::RenderTarget_To_Present][i].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
            m_transitions[TransitionType::RenderTarget_To_Present][i].Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;

        }
    }

    D3D12_RESOURCE_BARRIER& Transition(unsigned int backBufferIndex, TransitionType transitionType)
    {
        assert(backBufferIndex < BackBuffersCount);

        return m_transitions[transitionType][backBufferIndex];
    }

    D3D12_CPU_DESCRIPTOR_HANDLE GetRenderTarget(unsigned int backBufferIndex)
    {
        return D3D12_CPU_DESCRIPTOR_HANDLE { m_heap->GetCPUDescriptorHandleForHeapStart().ptr + m_descriptorSize * backBufferIndex };
    }

private:
    ID3D12DescriptorHeapPtr m_heap;
    unsigned int m_descriptorSize;
    ID3D12ResourcePtr m_renderTargets[BackBuffersCount];
    D3D12_RESOURCE_BARRIER m_transitions[TransitionType::TransitionType_COUNT][BackBuffersCount];
};

D3D12Gpus::D3D12Gpus() : m_factory(CreateFactory())
{
    assert(m_factory);

    DiscoverAdapters();
}

D3D12Gpus::~D3D12Gpus()
{
}

size_t D3D12Gpus::Count() const
{
    return m_adapters.size();
}

D3D12GpuPtr D3D12Gpus::CreateGpu(GpuID id, HWND hwnd)
{
    assert(id < m_adapters.size());
    assert(hwnd);

    return std::make_shared<D3D12Gpu>(m_factory, m_adapters[id], hwnd);
}

// NOTE: Check the procedure to create a device
// https://blogs.msdn.microsoft.com/chuckw/2016/08/16/anatomy-of-direct3d-12-create-device/
void D3D12Gpus::DiscoverAdapters()
{
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;

    for (UINT adapterIndex = 0; DXGI_ERROR_NOT_FOUND != m_factory->EnumAdapters1(adapterIndex, &adapter); ++adapterIndex)
    {
        DXGI_ADAPTER_DESC1 desc;
        if (FAILED(adapter->GetDesc1(&desc)))
            continue;

        // Don't select the Basic Render Driver adapter.
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
            continue;

        // Check to see if the adapter supports Direct3D 12,
        // but don't create the actual device yet.
        if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr)))
            m_adapters.push_back(adapter);
    }
}

D3D12Gpu::D3D12Gpu(IDXGIFactory4Ptr factory, IDXGIAdapterPtr adapter, HWND hwnd)    :   m_backbufferIndex(0), m_srvDescHeap()
{
    assert(factory);
    assert(hwnd);

    Utils::AssertIfFailed(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device)));
    assert(m_device);

    // Describe and create the command queue.
    {
        D3D12_COMMAND_QUEUE_DESC queueDesc { };
        AssertIfFailed(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue)));
    }

    // Creating the swap chain
    {
        // NOTE: not sRGB format available directly for Swap Chain back buffer
        //       Create with UNORM format and use a sRGB Render Target View.
        DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
        swapChainDesc.Width                 = CustomWindow::GetResolution().m_width;
        swapChainDesc.Height                = CustomWindow::GetResolution().m_height;
        swapChainDesc.Format                = DXGI_FORMAT_R8G8B8A8_UNORM;
        swapChainDesc.Stereo                = FALSE;
        swapChainDesc.SampleDesc.Count      = 1;
        swapChainDesc.SampleDesc.Quality    = 0;
        swapChainDesc.BufferUsage           = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapChainDesc.BufferCount           = BackBuffersCount;
        swapChainDesc.Scaling               = DXGI_SCALING_STRETCH;
        swapChainDesc.SwapEffect            = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        swapChainDesc.AlphaMode             = DXGI_ALPHA_MODE_UNSPECIFIED;
        swapChainDesc.Flags                 = 0;
        

        // Swap chain needs the queue so that it can force a flush on it.
        Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain1;
        AssertIfFailed(factory->CreateSwapChainForHwnd(m_commandQueue.Get(), hwnd, &swapChainDesc, nullptr, nullptr, &swapChain1));
        AssertIfFailed(swapChain1.As(&m_swapChain));
    }

    // NOTE: this is based on the d3d12 samples. beginnings are humble! ^^U
    //  https://github.com/Microsoft/DirectX-Graphics-Samples
    // Create the gpu jobs done synchronization
    {
        AssertIfFailed(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
        m_fenceValue = 1;

        // Create an event handle to use for frame synchronization.
        m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (!m_fenceEvent)
            AssertIfFailed(HRESULT_FROM_WIN32(GetLastError()));
    }
    
    m_backbuffers = std::make_shared<D3D12BackBuffers<BackBuffersCount>>(m_device, m_swapChain);
    assert(m_backbuffers);

    CreateCommandList();

    // Create descriptor heap
    m_srvDescHeap = std::make_shared<D3D12DescriptorHeap>(m_device);

    // Create dynamic constant buffers heap
    {
        D3D12_HEAP_PROPERTIES heapProperties;
        //D3D12_HEAP_TYPE Type;
        heapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
        heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heapProperties.CreationNodeMask = 1;
        heapProperties.VisibleNodeMask = 1;

        const uint64_t page_size_64kb_in_bytes = 1024 * 64;
        D3D12_RESOURCE_DESC uploadHeapDesc = CreateBufferDesc(page_size_64kb_in_bytes);
        D3D12_RESOURCE_STATES initialResourceState = D3D12_RESOURCE_STATE_GENERIC_READ;
        Utils::AssertIfFailed(m_device->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &uploadHeapDesc, 
                                                                 initialResourceState, nullptr, IID_PPV_ARGS(&m_dynamicConstantBuffersHeap)));

        m_dynamicConstantBufferHeapCurrentPtr = m_dynamicConstantBuffersHeap->GetGPUVirtualAddress();
        
        // NOTE: Leaving the constant buffer memory mapped is used to avoid map/unmap pattern
        D3D12_RANGE readRange{ 0, 0 };
        Utils::AssertIfFailed(m_dynamicConstantBuffersHeap->Map(0, &readRange, &m_dynamicConstantBuffersMemPtr));
    }
}

D3D12Gpu::~D3D12Gpu()
{
    WaitForGPU();

    // TODO: cant it be done with a comptr? else impl share_ptr with custom destructor
    CloseHandle(m_fenceEvent);
}

D3D12ResourceID D3D12Gpu::AddUploadTexture2DTask(const D3D12GpuUploadTexture2DTask& uploadTask)
{
    const size_t resourceID = m_resources.size();
    m_resources.push_back({ 0, nullptr });

    m_uploadTexture2DTasks.push_back({ resourceID, uploadTask });

    return resourceID;
}

D3D12ResourceID D3D12Gpu::AddUploadBufferTask(const D3D12GpuUploadBufferTask& uploadTask)
{
    const size_t resourceID = m_resources.size();
    m_resources.push_back({0, nullptr });
    
    m_uploadBufferTasks.push_back({ resourceID, uploadTask });

    return resourceID;
}

void D3D12Gpu::AddRenderTask(const D3D12GpuRenderTask& renderTask)
{
    m_renderTasks.push_back(renderTask);
}

D3D12DynamicResourceID D3D12Gpu::CreateDynamicConstantBuffer(unsigned int sizeInBytes)
{
    const unsigned int actualSize = CalculateConstantBufferRequiredSize(sizeInBytes);

    const D3D12DescriptorID cbvID = m_srvDescHeap->CreateCBV(m_dynamicConstantBufferHeapCurrentPtr, actualSize);
    
    m_dynamicConstantBufferHeapCurrentPtr += actualSize;
    
    const size_t cbID = m_dynamicConstantBuffers.size();
    
    void* currentBuffersMemPtr = nullptr;
    if (cbID == 0)
        currentBuffersMemPtr = m_dynamicConstantBuffersMemPtr;
    else
    {
        DynamicConstantBuffer& prevDynamicConstantBuffer = m_dynamicConstantBuffers.back();
        currentBuffersMemPtr = static_cast<char*>(prevDynamicConstantBuffer.m_memPtr) + prevDynamicConstantBuffer.m_requiredSizeInBytes;
    }

    DynamicConstantBuffer dynamicConstantBuffer;
    dynamicConstantBuffer.m_sizeInBytes = sizeInBytes;
    dynamicConstantBuffer.m_requiredSizeInBytes = actualSize;
    dynamicConstantBuffer.m_memPtr = currentBuffersMemPtr;
    dynamicConstantBuffer.m_cbvID = cbvID;

    m_dynamicConstantBuffers.push_back(dynamicConstantBuffer);

    return cbID;
}

void D3D12Gpu::UpdateDynamicConstantBuffer(D3D12DynamicResourceID id, const void* data)
{
    assert(id < m_dynamicConstantBuffers.size());

    auto& dynamicConstantBuffer = m_dynamicConstantBuffers[id];

    memcpy(dynamicConstantBuffer.m_memPtr, data, dynamicConstantBuffer.m_sizeInBytes);
}

void D3D12Gpu::ExecuteGraphicsCommands()
{
    // Execute the graphics commands
    if (!m_renderTasks.size())
        return;

    AssertIfFailed(m_commandAllocator->Reset());
    AssertIfFailed(m_commandList->Reset(m_commandAllocator.Get(), nullptr));

    m_commandList->ResourceBarrier(1, &m_backbuffers->Transition(m_backbufferIndex, D3D12Gpu::D3D12BackBuffers<BackBuffersCount>::TransitionType::Present_To_RenderTarget));

    auto backbufferRT = m_backbuffers->GetRenderTarget(m_backbufferIndex);
    m_commandList->OMSetRenderTargets(1, &backbufferRT, FALSE, nullptr);

    // TODO rework this
    if (!m_renderTasks[0].m_simpleMaterial)
        m_commandList->ClearRenderTargetView(backbufferRT, m_renderTasks[0].m_clearColor, 0, nullptr);

    if (m_renderTasks.size() > 1)
    {
        m_commandList->SetPipelineState(m_renderTasks[1].m_simpleMaterial->GetPSO().Get());

        // All render tasks have the same root signature
        m_commandList->SetGraphicsRootSignature(m_renderTasks[1].m_simpleMaterial->GetRootSignature().Get());

        ID3D12DescriptorHeap* ppHeaps[] = { m_srvDescHeap->GetDescriptorHeap().Get() };
        m_commandList->SetDescriptorHeaps(1, ppHeaps);

        for (const auto& renderTask : m_renderTasks)
            RecordRenderTask(renderTask, backbufferRT);
    }

    m_renderTasks.clear();

    m_commandList->ResourceBarrier(1, &m_backbuffers->Transition(m_backbufferIndex, D3D12Gpu::D3D12BackBuffers<BackBuffersCount>::TransitionType::RenderTarget_To_Present));
    AssertIfFailed(m_commandList->Close());

    // Execute the command list.
    ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);
}

void D3D12Gpu::ExecuteCopyCommands()
{
    // Execute the resources copying commands
    const size_t uploadBufferTasksCount = m_uploadBufferTasks.size();
    const size_t uploadTextures2DTasksCount = m_uploadTexture2DTasks.size();
    if (uploadBufferTasksCount != 0 || uploadTextures2DTasksCount != 0)
    {
        std::vector<ID3D12ResourcePtr> uploadHeaps;
        AssertIfFailed(m_commandAllocator->Reset());
        AssertIfFailed(m_commandList->Reset(m_commandAllocator.Get(), nullptr));

        if (uploadBufferTasksCount)
        {
            for (const auto& uploadBufferTask : m_uploadBufferTasks)
            {
                ID3D12ResourcePtr uploadHeapPtr = CreateCommitedBuffer(&m_resources[uploadBufferTask.m_resourceID].m_resource, 
                                                                        uploadBufferTask.m_task.m_bufferData,
                                                                        uploadBufferTask.m_task.m_bufferDataSize,
                                                                        uploadBufferTask.m_task.m_bufferName);
                uploadHeaps.push_back(uploadHeapPtr);
            }
            m_uploadBufferTasks.clear();
        }
        if (uploadTextures2DTasksCount)
        {
            for (const auto& uploadTexture2DTask : m_uploadTexture2DTasks)
            {
                auto& resource = m_resources[uploadTexture2DTask.m_resourceID];
                ID3D12ResourcePtr uploadHeapPtr = CreateCommitedTexture2D(&resource.m_resource,
                                                                            uploadTexture2DTask.m_task.m_data,
                                                                            uploadTexture2DTask.m_task.m_dataSize,
                                                                            uploadTexture2DTask.m_task.m_width,
                                                                            uploadTexture2DTask.m_task.m_height,
                                                                            uploadTexture2DTask.m_task.m_debugName);

                uploadHeaps.push_back(uploadHeapPtr);

                const D3D12DescriptorID srvID = m_srvDescHeap->CreateSRV(resource.m_resource.Get(),
                                                                         uploadTexture2DTask.m_task.m_desc);

                resource.m_resourceViewID = srvID;
            }
            m_uploadTexture2DTasks.clear();
        }

        AssertIfFailed(m_commandList->Close());
        ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
        m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

        WaitForGPU();
    }
}

void D3D12Gpu::Flush()
{
    // Present the frame.
    AssertIfFailed(m_swapChain->Present(0, 0));

    WaitForGPU();

    // NOTE: leaving this for quick reference
    //std::wstringstream outputStream;
    //outputStream << L"[DEBUG] BackBuffer Index " << m_swapChain->GetCurrentBackBufferIndex() << "\n";
    //OutputDebugString(outputStream.str().c_str());

    m_backbufferIndex = m_swapChain->GetCurrentBackBufferIndex();
}

// NOTE: simple wait for the gpu to be done with its current work.
//  As noted in the directx graphics samples this is not a strategy
//  to be used on production. 
void D3D12Gpu::WaitForGPU()
{
    const UINT64 fenceValue = m_fenceValue;
    AssertIfFailed(m_commandQueue->Signal(m_fence.Get(), fenceValue));
    m_fenceValue++;

    // NOTE: this is an optimization. Just in case the fence has already been 
    // signaled by the gpu. Theres no need to call the eventoncompletion 
    // (which most likely is expensive to run, ie spawning a thread to 
    // check the fence value in the background) neither the wait call
    if (m_fence->GetCompletedValue() < fenceValue)
    {
        AssertIfFailed(m_fence->SetEventOnCompletion(fenceValue, m_fenceEvent));
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }
}

void D3D12Gpu::CreateCommandList()
{
    assert(m_device);

    AssertIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocator)));
    m_commandAllocator->SetName(L"Command Allocator");

    AssertIfFailed(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocator.Get(), nullptr, IID_PPV_ARGS(&m_commandList)));
    m_commandList->SetName(L"Command List");
    AssertIfFailed(m_commandList->Close());
}

// TODO check alignment requirements https://www.braynzarsoft.net/viewtutorial/q16390-directx-12-constant-buffers-root-descriptor-tables
ID3D12ResourcePtr D3D12Gpu::CreateCommitedBuffer(ID3D12ResourcePtr* buffer, const void* bufferData, size_t bufferDataSize, const std::wstring& bufferName)
{
    D3D12_HEAP_PROPERTIES defaultHeapProps 
    {
        /*D3D12_HEAP_TYPE Type*/ D3D12_HEAP_TYPE_DEFAULT,
        /*D3D12_CPU_PAGE_PROPERTY CPUPageProperty*/ D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
        /*D3D12_MEMORY_POOL MemoryPoolPreference*/ D3D12_MEMORY_POOL_UNKNOWN,
        /*UINT CreationNodeMask*/ 1,
        /*UINT VisibleNodeMask*/ 1
    };

    D3D12_RESOURCE_DESC heapResourceDesc = CreateBufferDesc(bufferDataSize);

    AssertIfFailed(m_device->CreateCommittedResource(&defaultHeapProps, D3D12_HEAP_FLAG_NONE, &heapResourceDesc,
                                                     D3D12_RESOURCE_STATE_COPY_DEST, nullptr, 
                                                     IID_PPV_ARGS(&*buffer)));
    (*buffer)->SetName(bufferName.c_str());

    D3D12_HEAP_PROPERTIES uploadHeapProps = defaultHeapProps;
    uploadHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
    ID3D12ResourcePtr bufferUploadHeap;

    AssertIfFailed(m_device->CreateCommittedResource(&uploadHeapProps, D3D12_HEAP_FLAG_NONE, &heapResourceDesc,
                                                     D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, 
                                                     IID_PPV_ARGS(&bufferUploadHeap)));
    bufferUploadHeap->SetName((bufferName + L" upload heap").c_str());

    // Copy data to the intermediate upload heap and then schedule a copy 
    // from the upload heap to the vertex buffer default heap.
    D3D12_SUBRESOURCE_DATA data = {};
    data.pData = bufferData;
    data.RowPitch = bufferDataSize;
    data.SlicePitch = data.RowPitch;

    // NOTE: Check UpdateSubresources in d3dx12.h to learn about handling it in a proper way
    // Q: Whats the need of GetCopyableFootprints? is it because you might not now the props in
    // advance? is it because depending on some hardware requirements it might be different
    // from the ones you have?
    //UpdateSubresources<1>(m_commandList.Get(), m_vertexBuffer.Get(), vertexBufferUploadHeap.Get(), 0, 0, 1, &vertexData);
    D3D12_RESOURCE_DESC desc = (*buffer)->GetDesc();
    const unsigned int firstSubresource = 0;
    const unsigned int numSubresources = 1;
    const unsigned int intermediateOffset = 0;
    UINT64 requiredSize = 0;
    const size_t maxSubresources = 1;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layouts[maxSubresources];
    unsigned int numRows[maxSubresources];
    UINT64 rowSizesInBytes[maxSubresources];
    m_device->GetCopyableFootprints(&desc, firstSubresource, numSubresources, intermediateOffset, layouts, numRows, rowSizesInBytes, &requiredSize);

    // Copy cpu data to staging buffer aka upload heap
    {
        BYTE* pData;
        AssertIfFailed(bufferUploadHeap->Map(0, NULL, reinterpret_cast<void**>(&pData)));

        // NOTE: Check code from d3dx12.h MemcpySubresource to see 
        // how the copy of a resource (subresources, type and dimensions)
        // is done properly
        // MemcpySubresource(&DestData, &pSrcData[i], (SIZE_T)pRowSizesInBytes[i], pNumRows[i], pLayouts[i].Footprint.Depth);
        BYTE* pDestSlice = reinterpret_cast<BYTE*>(pData);
        const BYTE* pSrcSlice = reinterpret_cast<const BYTE*>(data.pData);
        memcpy(pDestSlice, pSrcSlice, bufferDataSize);

        bufferUploadHeap->Unmap(0, NULL);
    }

    const UINT64 dstOffset  = 0;
    const UINT64 srcOffset  = 0;
    const UINT64 numBytes   = bufferDataSize;
    m_commandList->CopyBufferRegion(buffer->Get(), dstOffset, bufferUploadHeap.Get(), srcOffset, numBytes);

    // TODO: this is ugly af. Is there a better way of exposing this functionality? Check d3d12x.h for ideas.
    D3D12_RESOURCE_BARRIER copyDestToReadDest
    { 
        D3D12_RESOURCE_BARRIER_TYPE_TRANSITION, D3D12_RESOURCE_BARRIER_FLAG_NONE, 
        {
            buffer->Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,
        } 
    };
    m_commandList->ResourceBarrier(1, &copyDestToReadDest);

    return bufferUploadHeap;
}

ID3D12ResourcePtr D3D12Gpu::CreateCommitedTexture2D(ID3D12ResourcePtr* resource, const void* data, size_t dataSize, 
                                                    unsigned int width, unsigned int height, const std::wstring& debugName)
{
    D3D12_HEAP_PROPERTIES defaultHeapProps
    {
        /*D3D12_HEAP_TYPE Type*/ D3D12_HEAP_TYPE_DEFAULT,
        /*D3D12_CPU_PAGE_PROPERTY CPUPageProperty*/ D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
        /*D3D12_MEMORY_POOL MemoryPoolPreference*/ D3D12_MEMORY_POOL_UNKNOWN,
        /*UINT CreationNodeMask*/ 1,
        /*UINT VisibleNodeMask*/ 1
    };

    D3D12_RESOURCE_DESC heapResourceDesc
    {
        /*D3D12_RESOURCE_DIMENSION Dimension*/ D3D12_RESOURCE_DIMENSION_TEXTURE2D,
        /*UINT64 Alignment*/ 0,
        /*UINT64 Width*/ width,
        /*UINT Height*/ height,
        /*UINT16 DepthOrArraySize*/ 1,
        /*UINT16 MipLevels*/ 1,
        /*DXGI_FORMAT Format*/ DXGI_FORMAT_R8G8B8A8_UNORM,
        /*DXGI_SAMPLE_DESC SampleDesc*/{ 1, 0 },
        /*D3D12_TEXTURE_LAYOUT Layout*/ D3D12_TEXTURE_LAYOUT_UNKNOWN,
        /*D3D12_RESOURCE_FLAGS Flags*/ D3D12_RESOURCE_FLAG_NONE
    };

    AssertIfFailed(m_device->CreateCommittedResource(&defaultHeapProps, D3D12_HEAP_FLAG_NONE, &heapResourceDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&*resource)));
    (*resource)->SetName(debugName.c_str());

    D3D12_HEAP_PROPERTIES uploadHeapProps
    {
        /*D3D12_HEAP_TYPE Type*/ D3D12_HEAP_TYPE_UPLOAD,
        /*D3D12_CPU_PAGE_PROPERTY CPUPageProperty*/ D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
        /*D3D12_MEMORY_POOL MemoryPoolPreference*/ D3D12_MEMORY_POOL_UNKNOWN,
        /*UINT CreationNodeMask*/ 1,
        /*UINT VisibleNodeMask*/ 1
    };
    ID3D12ResourcePtr uploadHeap;

    const unsigned int firstSubresource = 0;
    const unsigned int numSubresources = 1;
    const unsigned int intermediateOffset = 0;
    UINT64 requiredSize = 0;
    const size_t maxSubresources = 1;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layouts[maxSubresources];
    unsigned int numRows[maxSubresources];
    UINT64 rowSizesInBytes[maxSubresources];
    m_device->GetCopyableFootprints(&heapResourceDesc, firstSubresource, numSubresources, intermediateOffset, layouts, numRows, rowSizesInBytes, &requiredSize);

    D3D12_RESOURCE_DESC uploadHeapResourceDesc = CreateBufferDesc(dataSize);

    AssertIfFailed(m_device->CreateCommittedResource(&uploadHeapProps, D3D12_HEAP_FLAG_NONE, &uploadHeapResourceDesc,
                                                     D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadHeap)));
    uploadHeap->SetName((debugName + L" upload heap").c_str());

    D3D12_SUBRESOURCE_DATA subResourceData = {};
    subResourceData.pData = data;
    subResourceData.RowPitch = width * 4;
    subResourceData.SlicePitch = subResourceData.RowPitch * height;

    // Copy cpu data to staging buffer aka upload heap
    {
        BYTE* pData;
        AssertIfFailed(uploadHeap->Map(0, NULL, reinterpret_cast<void**>(&pData)));

        // NOTE: Check code from d3dx12.h MemcpySubresource to see 
        // how the copy of a resource (subresources, type and dimensions)
        // is done properly
        // MemcpySubresource(&DestData, &pSrcData[i], (SIZE_T)pRowSizesInBytes[i], pNumRows[i], pLayouts[i].Footprint.Depth);
        BYTE* pDestSlice = reinterpret_cast<BYTE*>(pData);
        const BYTE* pSrcSlice = reinterpret_cast<const BYTE*>(subResourceData.pData);
        memcpy(pDestSlice, pSrcSlice, dataSize);

        uploadHeap->Unmap(0, NULL);
    }

    D3D12_TEXTURE_COPY_LOCATION dest
    {
        /*ID3D12Resource *pResource*/ resource->Get(),
        /*D3D12_TEXTURE_COPY_TYPE Type*/ D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,
        /*UINT SubresourceIndex*/ 0
    }; 
    D3D12_TEXTURE_COPY_LOCATION src
    {
        /*ID3D12Resource *pResource*/ uploadHeap.Get(),
        /*D3D12_TEXTURE_COPY_TYPE Type*/ D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,
        /*UINT SubresourceIndex*/ layouts[0]
    };
    m_commandList->CopyTextureRegion(&dest, 0, 0, 0, &src, nullptr);

    // TODO: this is ugly af. Is there a better way of exposing this functionality? Check d3d12x.h for ideas.
    D3D12_RESOURCE_BARRIER copyDestToReadDest
    {
        D3D12_RESOURCE_BARRIER_TYPE_TRANSITION, D3D12_RESOURCE_BARRIER_FLAG_NONE,
    {
        resource->Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
    }
    };
    m_commandList->ResourceBarrier(1, &copyDestToReadDest);

    return uploadHeap;
}

void D3D12Gpu::RecordRenderTask(const D3D12GpuRenderTask& renderTask, D3D12_CPU_DESCRIPTOR_HANDLE backbufferRT)
{
    if (!renderTask.m_simpleMaterial)
    {
        m_commandList->ClearRenderTargetView(backbufferRT, m_renderTasks[0].m_clearColor, 0, nullptr);
        return;
    }

    renderTask.m_simpleMaterial->Apply(m_commandList, m_srvDescHeap, m_resources);

    m_commandList->RSSetViewports(1, &renderTask.m_viewport);
    m_commandList->RSSetScissorRects(1, &renderTask.m_scissorRect);
    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView 
    {
        m_resources[renderTask.m_vertexBufferResourceID].m_resource->GetGPUVirtualAddress(), 
        static_cast<UINT>(renderTask.m_vertexSize * renderTask.m_vertexCount), 
        static_cast<UINT>(renderTask.m_vertexSize)
    };
    m_commandList->IASetVertexBuffers(0, 1, &vertexBufferView);

    D3D12_INDEX_BUFFER_VIEW indexBufferView
    {
        m_resources[renderTask.m_indexBufferResourceID].m_resource->GetGPUVirtualAddress(), 
        static_cast<UINT>(renderTask.m_indexCount * sizeof(uint16_t)), DXGI_FORMAT_R16_UINT
    };
    m_commandList->IASetIndexBuffer(&indexBufferView);

    m_commandList->DrawIndexedInstanced(static_cast<UINT>(renderTask.m_indexCount), 1, 0, 0, 0);
}