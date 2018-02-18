#include "d3d12gpu.h"

// project includes
#include "utils.h"
#include "d3d12descriptorheap.h"
#include "d3d12swapchain.h"
#include "d3d12committedbuffer.h"

using namespace D3D12Basics;
using namespace D3D12Render;

namespace
{
    const auto g_swapChainFormat = DXGI_FORMAT::DXGI_FORMAT_R8G8B8A8_UNORM;

    unsigned int CalculateConstantBufferRequiredSize(unsigned int requestedSizeInBytes)
    {
        const unsigned int requiredAlignmentForCB = 256;
        return (requestedSizeInBytes + (requiredAlignmentForCB - 1)) & ~(requiredAlignmentForCB - 1);
    }
}

D3D12GpuLockWait::D3D12GpuLockWait(ID3D12DevicePtr device, ID3D12CommandQueuePtr cmdQueue) : m_cmdQueue(cmdQueue)
{
    assert(device);
    assert(m_cmdQueue);

    AssertIfFailed(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
    assert(m_fence);
    m_nextFenceValue = 1;

    // Create an event handle to use for frame synchronization.
    m_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    assert(m_event);
    if (!m_event)
        AssertIfFailed(HRESULT_FROM_WIN32(GetLastError()));
}

D3D12GpuLockWait::~D3D12GpuLockWait()
{
    CloseHandle(m_event);
}

void D3D12GpuLockWait::Wait()
{
    const UINT64 fenceValue = m_nextFenceValue;
    AssertIfFailed(m_cmdQueue->Signal(m_fence.Get(), fenceValue));
    m_nextFenceValue++;

    // NOTE: this is an optimization. Just in case the fence has already been 
    // signaled by the gpu. Theres no need to call the eventoncompletion 
    // (which most likely is expensive to run, ie spawning a thread to 
    // check the fence value in the background) neither the wait call
    if (m_fence->GetCompletedValue() < fenceValue)
    {
        AssertIfFailed(m_fence->SetEventOnCompletion(fenceValue, m_event));
        WaitForSingleObject(m_event, INFINITE);
    }
}

D3D12Gpu::D3D12Gpu()
{
    auto adapter = CreateDXGIInfrastructure();
    assert(adapter);

    CreateDevice(adapter);

    CreateCommandInfrastructure();

    CreateDescriptorHeaps();

    m_committedBufferLoader = std::make_shared<D3D12CommittedBufferLoader>(m_device, m_cmdAllocator, m_graphicsCmdQueue, m_cmdList);
    assert(m_committedBufferLoader);

    // TODO move this outside ot d3d12gpu
    m_simpleMaterial = std::make_shared<D3D12SimpleMaterial>(m_device);
    assert(m_simpleMaterial);

    CreateDynamicConstantBuffersInfrastructure();

    m_gpuLockWait = std::make_shared<D3D12GpuLockWait>(m_device, m_graphicsCmdQueue);
}

D3D12Gpu::~D3D12Gpu()
{
    m_gpuLockWait->Wait();
}

void D3D12Gpu::SetOutputWindow(HWND hwnd)
{
    // NOTE only one output supported
    m_swapChain = std::make_shared<D3D12SwapChain>(hwnd, g_swapChainFormat, m_safestResolution, 
                                                   m_factory, m_device, m_graphicsCmdQueue,
                                                   m_rtvDescriptorHeap);
    assert(m_swapChain);

    CreateDepthBuffer();
}

const Resolution& D3D12Gpu::GetCurrentResolution() const 
{ 
    return m_swapChain->GetCurrentResolution(); 
}

D3D12ResourceID D3D12Gpu::CreateCommittedBuffer(const void* data, size_t  dataSizeBytes, const std::wstring& debugName)
{
    ID3D12ResourcePtr resource = m_committedBufferLoader->Upload(data, dataSizeBytes, debugName);
    assert(resource);

    m_resources.push_back({ 0, resource });

    return m_resources.size() - 1;
}

D3D12ResourceID D3D12Gpu::CreateTexture(const void* data, size_t dataSizeBytes, unsigned int width, unsigned int height,
                                        DXGI_FORMAT format, const std::wstring& debugName)
{
    ID3D12ResourcePtr resource = m_committedBufferLoader->Upload(data, dataSizeBytes, width, height, format,debugName);
    assert(resource);

    D3D12_SHADER_RESOURCE_VIEW_DESC viewDesc;
    viewDesc.Format                     = format;
    viewDesc.ViewDimension              = D3D12_SRV_DIMENSION_TEXTURE2D;
    viewDesc.Shader4ComponentMapping    = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    viewDesc.Texture2D.MostDetailedMip  = 0;
    viewDesc.Texture2D.MipLevels        = 1;
    viewDesc.Texture2D.PlaneSlice       = 0;
    viewDesc.Texture2D.ResourceMinLODClamp = 0.0f;

    const D3D12DescriptorID srvID = m_srvDescHeap->CreateSRV(resource.Get(), viewDesc);

    m_resources.push_back({ srvID, resource });

    return m_resources.size() - 1;
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

void D3D12Gpu::ExecuteRenderTasks(const std::vector<D3D12GpuRenderTask>& renderTasks)
{
    assert(renderTasks.size() > 0);

    AssertIfFailed(m_cmdAllocator->Reset());
    AssertIfFailed(m_cmdList->Reset(m_cmdAllocator.Get(), nullptr));

    const unsigned int backbufferIndex = m_swapChain->GetCurrentBackBufferIndex();
    m_cmdList->ResourceBarrier(1, &m_swapChain->Transition(backbufferIndex, D3D12SwapChain::Present_To_RenderTarget));

    // Execute the graphics commands
    // All render tasks have the same render target, depth buffer and clear value
    auto backbufferRT = m_swapChain->RTV(backbufferIndex);
    D3D12_CPU_DESCRIPTOR_HANDLE& depthStencilDescriptor = m_dsvDescHeap->GetDescriptorHandles(m_depthBufferDescID).m_cpuHandle;
    m_cmdList->OMSetRenderTargets(1, &backbufferRT, FALSE, &depthStencilDescriptor);
    const auto& firstRenderTask = renderTasks[0];
    m_cmdList->ClearRenderTargetView(backbufferRT, firstRenderTask.m_clearColor, 0, nullptr);
    auto& dsvDescriptorHandle = m_dsvDescHeap->GetDescriptorHandles(m_depthBufferDescID).m_cpuHandle;
    m_cmdList->ClearDepthStencilView(dsvDescriptorHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0x0, 0, nullptr);

    // All render tasks have the same pso and root signature
    m_cmdList->SetPipelineState(m_simpleMaterial->GetPSO().Get());
    m_cmdList->SetGraphicsRootSignature(m_simpleMaterial->GetRootSignature().Get());
        
    ID3D12DescriptorHeap* ppHeaps[] = { m_srvDescHeap->GetDescriptorHeap().Get() };
    m_cmdList->SetDescriptorHeaps(1, ppHeaps);
        
    for (const auto& renderTask : renderTasks)
    {
        BindSimpleMaterialResources(renderTask.m_simpleMaterialResources);

        m_cmdList->RSSetViewports(1, &renderTask.m_viewport);
        m_cmdList->RSSetScissorRects(1, &renderTask.m_scissorRect);
        m_cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            
        // NOTE creating the vb and ib views on the fly shouldn't be a performance issue
        D3D12_VERTEX_BUFFER_VIEW vertexBufferView
        {
            m_resources[renderTask.m_vertexBufferResourceID].m_resource->GetGPUVirtualAddress(),
            static_cast<UINT>(renderTask.m_vertexSize * renderTask.m_vertexCount),
            static_cast<UINT>(renderTask.m_vertexSize)
        };
        m_cmdList->IASetVertexBuffers(0, 1, &vertexBufferView);
            
        D3D12_INDEX_BUFFER_VIEW indexBufferView
        {
            m_resources[renderTask.m_indexBufferResourceID].m_resource->GetGPUVirtualAddress(),
            static_cast<UINT>(renderTask.m_indexCount * sizeof(uint16_t)), DXGI_FORMAT_R16_UINT
        };
        m_cmdList->IASetIndexBuffer(&indexBufferView);
            
        m_cmdList->DrawIndexedInstanced(static_cast<UINT>(renderTask.m_indexCount), 1, 0, 0, 0);
    }

    m_cmdList->ResourceBarrier(1, &m_swapChain->Transition(backbufferIndex, D3D12SwapChain::RenderTarget_To_Present));
    AssertIfFailed(m_cmdList->Close());

    // Execute the command list.
    ID3D12CommandList* ppCommandLists[] = { m_cmdList.Get() };
    m_graphicsCmdQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);
}

void D3D12Gpu::FinishFrame()
{
    m_swapChain->Present();

    m_gpuLockWait->Wait();
}

// 
void D3D12Gpu::OnToggleFullScreen()
{
    m_swapChain->ToggleFullScreen();
}

void D3D12Gpu::OnResize(const Resolution& resolution)
{
    const auto& swapChainDisplayMode = FindClosestDisplayModeMatch(g_swapChainFormat, resolution);
    m_swapChain->Resize(swapChainDisplayMode);

    CreateDepthBuffer();
}

DisplayModes D3D12Gpu::EnumerateDisplayModes(DXGI_FORMAT format)
{
    unsigned int displayModesCount = 0;
    AssertIfFailed(m_output1->GetDisplayModeList1(format, 0, &displayModesCount, nullptr));

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

    AssertIfFailed(m_output1->FindClosestMatchingMode1(&modeToMatch, &closestMatch, m_device.Get()));

    return closestMatch;
}

void D3D12Gpu::CreateCommandInfrastructure()
{
    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    AssertIfFailed(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_graphicsCmdQueue)));
    assert(m_graphicsCmdQueue);

    AssertIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_cmdAllocator)));
    assert(m_cmdAllocator);
    m_cmdAllocator->SetName(L"Command Allocator");

    AssertIfFailed(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_cmdAllocator.Get(), nullptr, IID_PPV_ARGS(&m_cmdList)));
    assert(m_cmdList);
    AssertIfFailed(m_cmdList->Close());
    m_cmdList->SetName(L"Command List");
}

void D3D12Gpu::CreateDevice(IDXGIAdapterPtr adapter)
{
    // Note this needs to be called before creating the d3d12 device
    Microsoft::WRL::ComPtr<ID3D12Debug> debugController;
    AssertIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)));
    debugController->EnableDebugLayer();

    AssertIfFailed(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device)));
    assert(m_device);
}

IDXGIAdapterPtr D3D12Gpu::CreateDXGIInfrastructure()
{
    AssertIfFailed(CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, IID_PPV_ARGS(&m_factory)));
    assert(m_factory);

    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
    unsigned int primaryAdapterIndex = 0;
    AssertIfFailed(m_factory->EnumAdapters1(primaryAdapterIndex, &adapter));

    Microsoft::WRL::ComPtr<IDXGIOutput> output;
    AssertIfFailed(adapter->EnumOutputs(0, &output));
    AssertIfFailed(output->QueryInterface(__uuidof(IDXGIOutput1), (void **)&m_output1));
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
    m_rtvDescriptorHeap = std::make_shared<D3D12RTVDescriptorHeap>(m_device);
    assert(m_rtvDescriptorHeap);

    m_srvDescHeap = std::make_shared<D3D12CBVSRVUAVDescHeap>(m_device);
    assert(m_srvDescHeap);

    m_dsvDescHeap = std::make_shared<D3D12DSVDescriptorHeap>(m_device);
    assert(m_dsvDescHeap);
}

void D3D12Gpu::CreateDepthBuffer()
{
     const auto& resolution = m_swapChain->GetCurrentResolution();

    D3D12_CLEAR_VALUE optimizedClearValue;
    optimizedClearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    optimizedClearValue.DepthStencil = { 1.0f, 0x0 };

    m_depthBufferResource = D3D12CreateCommittedDepthStencil(m_device, resolution.m_width, resolution.m_height,
                                                            optimizedClearValue.Format, &optimizedClearValue, 
                                                            L"Depth buffer");

    D3D12_DEPTH_STENCIL_VIEW_DESC desc;
    desc.Format             = optimizedClearValue.Format;
    desc.ViewDimension      = D3D12_DSV_DIMENSION_TEXTURE2D;
    desc.Flags              = D3D12_DSV_FLAG_NONE;
    desc.Texture2D.MipSlice = 0;
    m_depthBufferDescID     = m_dsvDescHeap->CreateDSV(m_depthBufferResource, desc);
}

void D3D12Gpu::CreateDynamicConstantBuffersInfrastructure()
{
    const uint64_t page_size_64kb_in_bytes = 1024 * 64; // 64 << 10
    m_dynamicConstantBuffersHeap = D3D12CreateDynamicCommittedBuffer(m_device, page_size_64kb_in_bytes);
    m_dynamicConstantBufferHeapCurrentPtr = m_dynamicConstantBuffersHeap->GetGPUVirtualAddress();
            
    // NOTE: Leaving the constant buffer memory mapped is used to avoid map/unmap pattern
    D3D12_RANGE readRange{ 0, 0 };
    D3D12Basics::AssertIfFailed(m_dynamicConstantBuffersHeap->Map(0, &readRange, &m_dynamicConstantBuffersMemPtr));
}

void D3D12Gpu::BindSimpleMaterialResources(const D3D12SimpleMaterialResources& simpleMaterialResources)
{
    const auto& cbDescID = m_dynamicConstantBuffers[simpleMaterialResources.m_cbID].m_cbvID;
    const auto& cbGpuHandle = m_srvDescHeap->GetDescriptorHandles(cbDescID).m_gpuHandle;
    const auto& srvDescID = m_resources[simpleMaterialResources.m_textureID].m_resourceViewID;
    const auto& srvGpuHandle = m_srvDescHeap->GetDescriptorHandles(srvDescID).m_gpuHandle;

    m_simpleMaterial->Apply(m_cmdList, cbGpuHandle, srvGpuHandle);
}