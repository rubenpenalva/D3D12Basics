#include "d3d12gpu.h"

// project includes
#include "utils.h"
#include "d3d12descriptorheap.h"
#include "d3d12swapchain.h"
#include "d3d12committedbuffer.h"

// c++ includes
#include <sstream>

using namespace D3D12Basics;
using namespace D3D12Render;

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

    const uint64_t g_page_size_64kb_in_bytes    = 1024 * 64; // 64 << 10
    const uint64_t g_page_size_128kb_in_bytes   = 1024 * 128;
    const uint64_t g_page_size_256kb_in_bytes   = 1024 * 256;
    const uint64_t g_page_size_512kb_in_bytes   = 1024 * 512;
    const uint64_t g_page_size_1mb_in_bytes     = 1024 * 1024;

    const auto g_swapChainFormat = DXGI_FORMAT::DXGI_FORMAT_R8G8B8A8_UNORM;

    size_t CalculateConstantBufferRequiredSize(size_t requestedSizeInBytes)
    {
        const size_t requiredAlignmentForCB = 256;
        return (requestedSizeInBytes + (requiredAlignmentForCB - 1)) & ~(requiredAlignmentForCB - 1);
    }
}

D3D12GpuSynchronizer::D3D12GpuSynchronizer(ID3D12DevicePtr device, ID3D12CommandQueuePtr cmdQueue,
                                           unsigned int maxFramesInFlight)  :   m_cmdQueue(cmdQueue), m_maxFramesInFlight(maxFramesInFlight),
                                                                                m_framesInFlight(0), m_currentFenceValue(0)
{
    assert(device);
    assert(m_cmdQueue);

    AssertIfFailed(device->CreateFence(m_currentFenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
    assert(m_fence);
    m_nextFenceValue = m_currentFenceValue + 1;

    // Create an event handle to use for frame synchronization.
    m_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    assert(m_event);
    if (!m_event)
        AssertIfFailed(HRESULT_FROM_WIN32(GetLastError()));
}

D3D12GpuSynchronizer::~D3D12GpuSynchronizer()
{
    CloseHandle(m_event);
}

void D3D12GpuSynchronizer::Wait()
{
    SignalWork();

    if (m_framesInFlight == m_maxFramesInFlight)
    {
        auto completedFenceValue = m_fence->GetCompletedValue();

        assert(m_currentFenceValue <= m_currentFenceValue);
        if (completedFenceValue < m_currentFenceValue)
        {
            WaitForFence(completedFenceValue + 1);
            completedFenceValue++;
        }

        auto completedFramesCount = static_cast<unsigned int>(completedFenceValue - m_currentFenceValue + m_framesInFlight);
        assert(m_framesInFlight >= completedFramesCount);
        m_framesInFlight -= completedFramesCount;
    }
}

void D3D12GpuSynchronizer::WaitAll()
{
    SignalWork();

    WaitForFence(m_currentFenceValue);

    m_framesInFlight = 0;
}

void D3D12GpuSynchronizer::SignalWork()
{
    AssertIfFailed(m_cmdQueue->Signal(m_fence.Get(), m_nextFenceValue));
    m_currentFenceValue = m_nextFenceValue;
    m_nextFenceValue++;

    m_framesInFlight++;
    assert(m_framesInFlight <= m_maxFramesInFlight);
}

void D3D12GpuSynchronizer::WaitForFence(UINT64 fenceValue)
{
    Timer timer;
    AssertIfFailed(m_fence->SetEventOnCompletion(fenceValue, m_event));
    WaitForSingleObject(m_event, INFINITE);
    timer.Mark();
    m_waitTime = timer.ElapsedTime();
}

D3D12Gpu::D3D12Gpu(bool isWaitableForPresentEnabled)    :   m_dynamicConstantBuffersMaxSize(4 * g_page_size_1mb_in_bytes),
                                                            m_dynamicConstantBuffersCurrentSize(0),
                                                            m_currentBackbufferIndex(0), m_currentFrameIndex(0), 
                                                            m_isWaitableForPresentEnabled(isWaitableForPresentEnabled)
{
    auto adapter = CreateDXGIInfrastructure();
    assert(adapter);

    CreateDevice(adapter);

    CreateCommandInfrastructure();

    CreateDescriptorHeaps();

    m_committedBufferLoader = std::make_shared<D3D12CommittedBufferLoader>(m_device, m_cmdAllocators[0], m_graphicsCmdQueue, m_cmdLists[0]);
    assert(m_committedBufferLoader);

    // TODO move this outside
    m_simpleMaterial = std::make_shared<D3D12Material>(m_device);
    assert(m_simpleMaterial);

    CreateDynamicConstantBuffersInfrastructure();

    m_gpuSync = std::make_unique<D3D12GpuSynchronizer>(m_device, m_graphicsCmdQueue, m_framesInFlight);
}

D3D12Gpu::~D3D12Gpu()
{
    m_gpuSync->WaitAll();
}

unsigned int D3D12Gpu::GetFormatPlaneCount(DXGI_FORMAT format)
{
    D3D12_FEATURE_DATA_FORMAT_INFO formatInfo = { format };
    return FAILED(m_device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_INFO, &formatInfo, sizeof(formatInfo))) ? 0 : formatInfo.PlaneCount;
}

void D3D12Gpu::SetOutputWindow(HWND hwnd)
{
    // NOTE only one output supported
    m_swapChain = std::make_shared<D3D12SwapChain>(hwnd, g_swapChainFormat, m_safestResolution, 
                                                   m_factory, m_device, m_graphicsCmdQueue,
                                                   m_rtvDescriptorHeap, m_isWaitableForPresentEnabled);
    assert(m_swapChain);

    CreateDepthBuffer();
}

const Resolution& D3D12Gpu::GetCurrentResolution() const 
{ 
    return m_swapChain->GetCurrentResolution(); 
}

D3D12ResourceID D3D12Gpu::CreateCommittedBuffer(const void* data, size_t  dataSizeBytes, const std::wstring& debugName)
{
    ID3D12ResourcePtr resource = m_committedBufferLoader->Upload(data, dataSizeBytes, dataSizeBytes, debugName);
    assert(resource);

    m_resources.push_back({ 0, resource });

    return m_resources.size() - 1;
}

D3D12ResourceID D3D12Gpu::CreateStaticConstantBuffer(const void* data, size_t dataSizeBytes, const std::wstring& resourceName)
{
    size_t requiredDataSizeBytes = CalculateConstantBufferRequiredSize(dataSizeBytes);
    ID3D12ResourcePtr resource = m_committedBufferLoader->Upload(data, dataSizeBytes, requiredDataSizeBytes, resourceName);
    assert(resource);

    const D3D12DescriptorID cbvID = m_srvDescHeap->CreateCBV(m_dynamicConstantBufferHeapCurrentPtr, requiredDataSizeBytes);

    m_resources.push_back({ cbvID, resource });

    return m_resources.size() - 1;
}

D3D12ResourceID D3D12Gpu::CreateTexture(const std::vector<D3D12_SUBRESOURCE_DATA>& subresources, const D3D12_RESOURCE_DESC& desc,
                                        const std::wstring& debugName)
{
    ID3D12ResourcePtr resource = m_committedBufferLoader->Upload(subresources, desc, debugName);
    assert(resource);

    D3D12_SHADER_RESOURCE_VIEW_DESC viewDesc;
    viewDesc.Format                         = desc.Format;
    viewDesc.ViewDimension                  = D3D12_SRV_DIMENSION_TEXTURE2D;
    viewDesc.Shader4ComponentMapping        = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    viewDesc.Texture2D.MostDetailedMip      = 0;
    viewDesc.Texture2D.MipLevels            = static_cast<UINT>(subresources.size());
    viewDesc.Texture2D.PlaneSlice           = 0;
    viewDesc.Texture2D.ResourceMinLODClamp  = 0.0f;

    const D3D12DescriptorID srvID = m_srvDescHeap->CreateSRV(resource.Get(), viewDesc);

    m_resources.push_back({ srvID, resource });

    return m_resources.size() - 1;
}

D3D12DynamicResourceID D3D12Gpu::CreateDynamicConstantBuffer(unsigned int sizeInBytes)
{
    const size_t actualSize = CalculateConstantBufferRequiredSize(sizeInBytes);
    assert(m_dynamicConstantBuffersCurrentSize + m_framesInFlight * actualSize <= m_dynamicConstantBuffersMaxSize);

    const size_t cbID = m_dynamicConstantBuffers.size();
    
    DynamicConstantBuffer dynamicConstantBuffer;
    dynamicConstantBuffer.m_sizeInBytes = sizeInBytes;
    dynamicConstantBuffer.m_requiredSizeInBytes = actualSize;

    for (unsigned int i = 0; i < m_framesInFlight; ++i)
    {
        const D3D12DescriptorID cbvID = m_srvDescHeap->CreateCBV(m_dynamicConstantBufferHeapCurrentPtr, actualSize);

        dynamicConstantBuffer.m_memPtr[i] = m_dynamicConstantBuffersCurrentMemPtr;
        dynamicConstantBuffer.m_cbvID[i] = cbvID;

        m_dynamicConstantBufferHeapCurrentPtr += actualSize;
        m_dynamicConstantBuffersCurrentSize += actualSize;
        m_dynamicConstantBuffersCurrentMemPtr = static_cast<char*>(m_dynamicConstantBuffersCurrentMemPtr) + actualSize;
    }

    m_dynamicConstantBuffers.push_back(dynamicConstantBuffer);

    return cbID;
}

void D3D12Gpu::UpdateDynamicConstantBuffer(D3D12DynamicResourceID id, const void* data)
{
    assert(id < m_dynamicConstantBuffers.size());

    auto& dynamicConstantBuffer = m_dynamicConstantBuffers[id];

    memcpy(dynamicConstantBuffer.m_memPtr[m_currentFrameIndex], data, dynamicConstantBuffer.m_sizeInBytes);
}

void D3D12Gpu::ExecuteRenderTasks(const std::vector<D3D12GpuRenderTask>& renderTasks)
{
    assert(renderTasks.size() > 0);

    assert(m_swapChain->GetCurrentBackBufferIndex() == m_currentBackbufferIndex);

    auto cmdList = m_cmdLists[m_currentFrameIndex];
    auto cmdAllocator = m_cmdAllocators[m_currentFrameIndex];

    AssertIfFailed(cmdAllocator->Reset());
    AssertIfFailed(cmdList->Reset(cmdAllocator.Get(), nullptr));

    cmdList->ResourceBarrier(1, &m_swapChain->Transition(m_currentBackbufferIndex, D3D12SwapChain::Present_To_RenderTarget));

    // Execute the graphics commands
    // All render tasks have the same render target, depth buffer and clear value
    auto backbufferRT = m_swapChain->RTV(m_currentBackbufferIndex);
    D3D12_CPU_DESCRIPTOR_HANDLE& depthStencilDescriptor = m_dsvDescHeap->GetDescriptorHandles(m_depthBufferDescID).m_cpuHandle;
    cmdList->OMSetRenderTargets(1, &backbufferRT, FALSE, &depthStencilDescriptor);
    const auto& firstRenderTask = renderTasks[0];
    cmdList->ClearRenderTargetView(backbufferRT, firstRenderTask.m_clearColor, 0, nullptr);
    auto& dsvDescriptorHandle = m_dsvDescHeap->GetDescriptorHandles(m_depthBufferDescID).m_cpuHandle;
    cmdList->ClearDepthStencilView(dsvDescriptorHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0x0, 0, nullptr);

    // All render tasks have the same pso and root signature
    cmdList->SetPipelineState(m_simpleMaterial->GetPSO().Get());
    cmdList->SetGraphicsRootSignature(m_simpleMaterial->GetRootSignature().Get());
        
    ID3D12DescriptorHeap* ppHeaps[] = { m_srvDescHeap->GetDescriptorHeap().Get() };
    cmdList->SetDescriptorHeaps(1, ppHeaps);

    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    D3D12MaterialResources boundSimpleMaterialResources{ 0xffffffff, 0xffffffff };
    D3D12_VIEWPORT  boundViewport {};
    RECT            boundScissorRect {};

    for (const auto& renderTask : renderTasks)
    {
        if (boundSimpleMaterialResources[0] != renderTask.m_materialResources[0] ||
            boundSimpleMaterialResources[1] != renderTask.m_materialResources[1])
        {
            boundSimpleMaterialResources = renderTask.m_materialResources;
            BindSimpleMaterialResources(renderTask.m_materialResources, cmdList);
        }
        
        if (boundViewport.Height != renderTask.m_viewport.Height || boundViewport.MaxDepth != renderTask.m_viewport.MaxDepth ||
            boundViewport.MinDepth != renderTask.m_viewport.MinDepth || boundViewport.TopLeftX != renderTask.m_viewport.TopLeftX || 
            boundViewport.TopLeftY != renderTask.m_viewport.TopLeftY || boundViewport.Width != renderTask.m_viewport.Width)
        {
            cmdList->RSSetViewports(1, &renderTask.m_viewport);
            boundViewport = renderTask.m_viewport;
        }

        if (boundScissorRect.bottom != renderTask.m_scissorRect.bottom || boundScissorRect.left != renderTask.m_scissorRect.left ||
            boundScissorRect.right != renderTask.m_scissorRect.right || boundScissorRect.top != renderTask.m_scissorRect.top)
        {
            cmdList->RSSetScissorRects(1, &renderTask.m_scissorRect);
            boundScissorRect = renderTask.m_scissorRect;
        }

        // NOTE creating the vb and ib views on the fly shouldn't be a performance issue
        D3D12_VERTEX_BUFFER_VIEW vertexBufferView
        {
            m_resources[renderTask.m_vertexBufferResourceID].m_resource->GetGPUVirtualAddress(),
            static_cast<UINT>(renderTask.m_vertexSizeBytes * renderTask.m_vertexCount),
            static_cast<UINT>(renderTask.m_vertexSizeBytes)
        };
        cmdList->IASetVertexBuffers(0, 1, &vertexBufferView);
            
        D3D12_INDEX_BUFFER_VIEW indexBufferView
        {
            m_resources[renderTask.m_indexBufferResourceID].m_resource->GetGPUVirtualAddress(),
            static_cast<UINT>(renderTask.m_indexCount * sizeof(uint16_t)), DXGI_FORMAT_R16_UINT
        };
        cmdList->IASetIndexBuffer(&indexBufferView);

        cmdList->DrawIndexedInstanced(static_cast<UINT>(renderTask.m_indexCount), 1, 0, 0, 0);
    }

    cmdList->ResourceBarrier(1, &m_swapChain->Transition(m_currentBackbufferIndex, D3D12SwapChain::RenderTarget_To_Present));
    AssertIfFailed(cmdList->Close());

    // Execute the command list.
    ID3D12CommandList* ppCommandLists[] = { cmdList.Get() };
    m_graphicsCmdQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);
}

void D3D12Gpu::BeginFrame()
{
    // TODO gather frame stats
}

void D3D12Gpu::FinishFrame()
{
    // TODO gather frame stats

    g_gpuViewMarkerPrePresentFrame.Mark();
    m_swapChain->Present(m_vsync);
    g_gpuViewMarkerPostPresentFrame.Mark();

    // TODO frame statistics
    //const float presentTime = m_swapChain->GetPresentTime();

    g_gpuViewMarkerPreWaitFrame.Mark();

    m_gpuSync->Wait();
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
    m_frameTime.Mark();

    // Note: we can have x frames in flight and y backbuffers
    m_currentBackbufferIndex = m_swapChain->GetCurrentBackBufferIndex();
    m_currentFrameIndex = (m_currentFrameIndex + 1) % m_framesInFlight;
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

    m_currentBackbufferIndex = m_swapChain->GetCurrentBackBufferIndex();

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

    for (unsigned int i = 0; i < D3D12Gpu::m_framesInFlight; ++i)
    {
        AssertIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_cmdAllocators[i])));
        assert(m_cmdAllocators[i]);
        {
            std::wstringstream converter;
            converter << L"Command Allocator " << i;
            m_cmdAllocators[i]->SetName(converter.str().c_str());
        }

        AssertIfFailed(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_cmdAllocators[i].Get(), nullptr, IID_PPV_ARGS(&m_cmdLists[i])));
        assert(m_cmdLists[i]);
        AssertIfFailed(m_cmdLists[i]->Close());

        {
            std::wstringstream converter;
            converter << L"Command Allocator " << i;
            m_cmdAllocators[i]->SetName(converter.str().c_str());
        }
        m_cmdLists[i]->SetName(L"Command List");
    }
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
    m_dynamicConstantBuffersHeap = D3D12CreateDynamicCommittedBuffer(m_device, m_dynamicConstantBuffersMaxSize);
    m_dynamicConstantBufferHeapCurrentPtr = m_dynamicConstantBuffersHeap->GetGPUVirtualAddress();
            
    // NOTE: Leaving the constant buffer memory mapped is used to avoid map/unmap pattern
    D3D12_RANGE readRange{ 0, 0 };
    D3D12Basics::AssertIfFailed(m_dynamicConstantBuffersHeap->Map(0, &readRange, &m_dynamicConstantBuffersMemPtr));

    m_dynamicConstantBuffersCurrentMemPtr = m_dynamicConstantBuffersMemPtr;
}

void D3D12Gpu::BindSimpleMaterialResources(const D3D12MaterialResources& materialResources, ID3D12GraphicsCommandListPtr cmdList)
{
    const auto& cbDescID = m_dynamicConstantBuffers[materialResources[0]].m_cbvID[m_currentFrameIndex];
    const auto& cbGpuHandle = m_srvDescHeap->GetDescriptorHandles(cbDescID).m_gpuHandle;
    const auto& srvDescID = m_resources[materialResources[1]].m_resourceViewID;
    const auto& srvGpuHandle = m_srvDescHeap->GetDescriptorHandles(srvDescID).m_gpuHandle;

    m_simpleMaterial->Apply(cmdList, cbGpuHandle, srvGpuHandle);
}