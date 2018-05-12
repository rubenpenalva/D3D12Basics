#include "d3d12gpu.h"

// project includes
#include "utils.h"
#include "d3d12descriptorheap.h"
#include "d3d12swapchain.h"
#include "d3d12committedbuffer.h"
#include "d3d12gpu_sync.h"

// c++ includes
#include <sstream>
#include <algorithm>

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

    const auto g_swapChainFormat = DXGI_FORMAT::DXGI_FORMAT_R8G8B8A8_UNORM;

    D3D12GpuMemoryHandle EncodeGpuMemoryHandle(uint64_t handleId, bool isDynamic)
    {
        return handleId | (static_cast<uint64_t>(isDynamic) << 63);
    }

    uint64_t DecodeGpuMemoryHandle_ID(D3D12GpuMemoryHandle memHandle)
    {
        return memHandle & 0x7fffffffffffffff;
    }

    bool DecodeGpuMemoryHandle_IsDynamic(D3D12GpuMemoryHandle memHandle)
    {
        return memHandle >> 63;
    }
}

D3D12Gpu::D3D12Gpu(bool isWaitableForPresentEnabled)    :   m_currentBackbufferIndex(0), m_currentFrameIndex(0), 
                                                            m_isWaitableForPresentEnabled(isWaitableForPresentEnabled),
                                                            m_depthBufferDescHandle(nullptr), m_nextMemoryHandle(0),
                                                            m_currentFrame(0)
{
    auto adapter = CreateDXGIInfrastructure();
    assert(adapter);

    CreateDevice(adapter);
    CheckFeatureSupport();

    CreateCommandInfrastructure();

    CreateDescriptorHeaps();

    m_dynamicMemoryAllocator = std::make_unique<D3D12BufferAllocator>(m_device, g_64kb);
    assert(m_dynamicMemoryAllocator);

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
    m_swapChain = std::make_unique<D3D12SwapChain>(hwnd, g_swapChainFormat, m_safestResolution,
                                                   m_factory, m_device, m_graphicsCmdQueue,
                                                   m_isWaitableForPresentEnabled);
    assert(m_swapChain);

    CreateDepthBuffer();
}

const Resolution& D3D12Gpu::GetCurrentResolution() const 
{ 
    return m_swapChain->GetCurrentResolution(); 
}

// TODO think about how to use the debugname of a dynamic resource
D3D12GpuMemoryHandle D3D12Gpu::AllocateDynamicMemory(size_t sizeBytes, const std::wstring& /*debugName*/)
{
    // TODO what happens when sizeInBytes is bigger than 64kb? fix this
    assert(sizeBytes < g_64kb);

    DynamicMemoryAlloc allocation;

    for (unsigned int i = 0; i < m_framesInFlight; ++i)
    {
        // TODO does forcing the alignment to the one for constant buffers makes sense?
        allocation.m_allocation[i] = m_dynamicMemoryAllocator->Allocate(sizeBytes, g_constantBufferReadAlignment);
        assert(D3D12Basics::IsAlignedToPowerof2(allocation.m_allocation[i].m_gpuPtr, g_constantBufferReadAlignment));
        allocation.m_frameId[i] = m_currentFrame;
    }

    auto encodedHandle = EncodeGpuMemoryHandle(m_nextMemoryHandle++, true);
    m_dynamicMemoryAllocations[encodedHandle] = std::move(allocation);

    return encodedHandle;
}

D3D12GpuMemoryHandle D3D12Gpu::AllocateStaticMemory(const void* data, size_t  sizeBytes, const std::wstring& debugName)
{
    D3D12GpuUploadContext uploadContext{ m_device, m_cmdAllocators[0], m_graphicsCmdQueue, m_cmdLists[0] };

    ID3D12ResourcePtr resource = D3D12Render::D3D12CreateCommittedBuffer(uploadContext, data, sizeBytes, debugName);
    assert(resource);

    auto encodedHandle = EncodeGpuMemoryHandle(m_nextMemoryHandle++, false);
    m_staticMemoryAllocations[encodedHandle] = StaticMemoryAlloc{ m_currentFrame, resource, sizeBytes };

    return encodedHandle;
}

D3D12GpuMemoryHandle D3D12Gpu::AllocateStaticMemory(const std::vector<D3D12_SUBRESOURCE_DATA>& subresources, const D3D12_RESOURCE_DESC& desc,
                                                    const std::wstring& debugName)
{
    D3D12GpuUploadContext uploadContext{ m_device, m_cmdAllocators[0], m_graphicsCmdQueue, m_cmdLists[0] };

    ID3D12ResourcePtr resource = D3D12Render::D3D12CreateCommittedBuffer(uploadContext, subresources, desc, debugName);
    assert(resource);

    // TODO this only works for 1d and 2d textures
    size_t sizeBytes = 0;
    for (auto& subResourceData : subresources)
        sizeBytes += subResourceData.SlicePitch;

    auto encodedHandle = EncodeGpuMemoryHandle(m_nextMemoryHandle++, false);
    m_staticMemoryAllocations[encodedHandle] = StaticMemoryAlloc{ m_currentFrame, resource, sizeBytes };

    return encodedHandle;
}

void D3D12Gpu::UpdateMemory(D3D12GpuMemoryHandle memHandle, const void* data, size_t sizeBytes, size_t offsetBytes)
{
    assert(DecodeGpuMemoryHandle_IsDynamic(memHandle));
    assert(data);
    assert(sizeBytes > 0);
    assert(m_dynamicMemoryAllocations.count(memHandle) == 1);

    auto& memoryAlloc = m_dynamicMemoryAllocations[memHandle];
    assert(memoryAlloc.m_allocation[m_currentFrameIndex].m_cpuPtr);

    memcpy(memoryAlloc.m_allocation[m_currentFrameIndex].m_cpuPtr + offsetBytes, data, sizeBytes);

    memoryAlloc.m_frameId[m_currentFrameIndex] = m_currentFrame;
}

void D3D12Gpu::FreeMemory(D3D12GpuMemoryHandle memHandle)
{
    m_retiredAllocations.push_back(memHandle);
}

D3D12GpuMemoryView D3D12Gpu::CreateConstantBufferView(D3D12GpuMemoryHandle memHandle)
{
    D3D12GpuMemoryView view;
    view.m_memHandle = memHandle;

    if (DecodeGpuMemoryHandle_IsDynamic(memHandle))
    {
        view.m_descriptors.resize(m_framesInFlight);
        assert(m_dynamicMemoryAllocations.count(memHandle) == 1);
        auto& memoryAlloc = m_dynamicMemoryAllocations[memHandle];

        for (unsigned int i = 0; i < m_framesInFlight; ++i)
        {
            D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
            cbvDesc.BufferLocation = memoryAlloc.m_allocation[i].m_gpuPtr;
            cbvDesc.SizeInBytes = static_cast<UINT>(memoryAlloc.m_allocation[i].m_size);
            D3D12DescriptorHeapHandlePtr cbvHandle = m_cpuSRV_CBVDescHeap->CreateCBV(cbvDesc);

            view.m_descriptors[i] = cbvHandle;
        }
    }
    else 
    {
        assert(m_staticMemoryAllocations.count(memHandle) == 1);
        auto& memoryAlloc = m_staticMemoryAllocations[memHandle];

        D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
        cbvDesc.BufferLocation = memoryAlloc.m_resource->GetGPUVirtualAddress();
        cbvDesc.SizeInBytes = static_cast<UINT>(memoryAlloc.m_size);
        D3D12DescriptorHeapHandlePtr cbvHandle = m_cpuSRV_CBVDescHeap->CreateCBV(cbvDesc);

        view.m_descriptors.push_back(cbvHandle);
    }

    return view;
}

D3D12GpuMemoryView D3D12Gpu::CreateTextureView(D3D12GpuMemoryHandle memHandle, const D3D12_RESOURCE_DESC& desc)
{
    // TODO not supported yet
    assert(!DecodeGpuMemoryHandle_IsDynamic(memHandle));

    assert(m_staticMemoryAllocations.count(memHandle));

    // TODO only 2d textures with limited props supported
    D3D12_SHADER_RESOURCE_VIEW_DESC viewDesc;
    viewDesc.Format = desc.Format;
    viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    viewDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    viewDesc.Texture2D.MostDetailedMip = 0;
    viewDesc.Texture2D.MipLevels = desc.MipLevels;
    viewDesc.Texture2D.PlaneSlice = 0;
    viewDesc.Texture2D.ResourceMinLODClamp = 0.0f;

    auto& memoryAllocation = m_staticMemoryAllocations[memHandle];
    D3D12DescriptorHeapHandlePtr descriptorHandle = m_cpuSRV_CBVDescHeap->CreateSRV(memoryAllocation.m_resource.Get(), viewDesc);

    return D3D12GpuMemoryView{ memHandle, { descriptorHandle } };
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
    D3D12_CPU_DESCRIPTOR_HANDLE& depthStencilDescriptor = m_depthBufferDescHandle->m_cpuHandle;
    cmdList->OMSetRenderTargets(1, &backbufferRT, FALSE, &depthStencilDescriptor); 

    ID3D12DescriptorHeap* ppHeaps[] = { m_gpuDescriptorRingBuffer->GetDescriptorHeap().Get() };
    cmdList->SetDescriptorHeaps(1, ppHeaps);

    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    D3D12_VIEWPORT  boundViewport {};
    RECT            boundScissorRect {};

    ID3D12PipelineState* currentPipelineState = nullptr;
    ID3D12RootSignature* currentRootSignature = nullptr;
    for (const auto& renderTask : renderTasks)
    {
        if (renderTask.m_clear)
        {
            cmdList->ClearRenderTargetView(backbufferRT, renderTask.m_clearColor, 0, nullptr);
            cmdList->ClearDepthStencilView(m_depthBufferDescHandle->m_cpuHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0x0, 0, nullptr);
            continue;
        }

        auto pipelineState = renderTask.m_pipelineState.Get();
        if (currentPipelineState != pipelineState)
        {
            cmdList->SetPipelineState(pipelineState);
            currentPipelineState = pipelineState;
        }

        auto rootSignature = renderTask.m_rootSignature.Get();
        if (currentRootSignature != rootSignature)
        {
            cmdList->SetGraphicsRootSignature(rootSignature);
            currentRootSignature = rootSignature;
        }

        // Set bindings
        for (auto& constants : renderTask.m_bindings.m_32BitConstants)
        {
            cmdList->SetGraphicsRoot32BitConstants(static_cast<UINT>(constants.m_bindingSlot),
                                                   static_cast<UINT>(constants.m_data.size()),
                                                   &constants.m_data[0], 0);
        }

        for (auto& cbv : renderTask.m_bindings.m_constantBufferViews)
        {
            D3D12_GPU_VIRTUAL_ADDRESS memoryVA {};
            if (DecodeGpuMemoryHandle_IsDynamic(cbv.m_memoryHandle))
            {
                assert(m_dynamicMemoryAllocations.count(cbv.m_memoryHandle) == 1);
                memoryVA = m_dynamicMemoryAllocations[cbv.m_memoryHandle].m_allocation[m_currentFrameIndex].m_gpuPtr;
                m_dynamicMemoryAllocations[cbv.m_memoryHandle].m_frameId[m_currentFrameIndex] = m_currentFrame;
            }
            else
            {
                assert(m_staticMemoryAllocations.count(cbv.m_memoryHandle) == 1);
                memoryVA = m_staticMemoryAllocations[cbv.m_memoryHandle].m_resource->GetGPUVirtualAddress();
                m_staticMemoryAllocations[cbv.m_memoryHandle].m_frameId = m_currentFrame;
            }

            cmdList->SetGraphicsRootConstantBufferView(static_cast<UINT>(cbv.m_bindingSlot), memoryVA);
        }

        for (auto& cpuDescriptorTable : renderTask.m_bindings.m_descriptorTables)
        {
            D3D12_GPU_DESCRIPTOR_HANDLE descriptorTableHandle = m_gpuDescriptorRingBuffer->CurrentDescriptor();

            for (auto& view : cpuDescriptorTable.m_views)
            {
                D3D12_CPU_DESCRIPTOR_HANDLE descriptorHandle{};
                if (DecodeGpuMemoryHandle_IsDynamic(view.m_memHandle))
                {
                    assert(view.m_descriptors.size() == m_framesInFlight);
                    assert(m_dynamicMemoryAllocations.count(view.m_memHandle) == 1);
                    DynamicMemoryAlloc& dynamicAllocation = m_dynamicMemoryAllocations[view.m_memHandle];
                    dynamicAllocation.m_frameId[m_currentFrameIndex] = m_currentFrame;

                    descriptorHandle = view.m_descriptors[m_currentFrameIndex]->m_cpuHandle;
                }
                else
                {
                    assert(view.m_descriptors.size() == 1);
                    descriptorHandle = view.m_descriptors[0]->m_cpuHandle;
                    m_staticMemoryAllocations[view.m_memHandle].m_frameId = m_currentFrame;
                }

                m_gpuDescriptorRingBuffer->CopyToCurrentDescriptor(1, descriptorHandle);
                m_gpuDescriptorRingBuffer->NextCurrentStackDescriptor();
            }

            cmdList->SetGraphicsRootDescriptorTable(static_cast<UINT>(cpuDescriptorTable.m_bindingSlot), descriptorTableHandle);
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
        SetVertexBuffer(renderTask.m_vertexBufferId, renderTask.m_vertexBufferSizeBytes, renderTask.m_vertexSizeBytes, cmdList);

        SetIndexBuffer(renderTask.m_indexBufferId, renderTask.m_indexBufferSizeBytes, cmdList);

        cmdList->DrawIndexedInstanced(static_cast<UINT>(renderTask.m_indexCountPerInstance), 1, 
                                      static_cast<UINT>(renderTask.m_indexOffset), 
                                      static_cast<UINT>(renderTask.m_vertexOffset), 0);
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
    m_currentFrame = m_gpuSync->GetNextFrameId();

    m_gpuDescriptorRingBuffer->NextDescriptorStack();
    m_gpuDescriptorRingBuffer->ClearCurrentStack();

    auto lastRetiredFrameId = m_gpuSync->GetLastRetiredFrameId();
    // TODO destroy retired buffers depending on if the frames they were bound are
    // still in flight.
    for (auto it = m_retiredAllocations.begin(); it != m_retiredAllocations.end(); )
    {
        auto& memAllocation = *it;

        bool completelyRetired = true;
        if (DecodeGpuMemoryHandle_IsDynamic(memAllocation))
        {
            assert(m_dynamicMemoryAllocations.count(memAllocation) == 1);
            auto& dynamicMemoryAllocation = m_dynamicMemoryAllocations[memAllocation];
            for (auto i = 0; i < m_framesInFlight; ++i)
                completelyRetired &= dynamicMemoryAllocation.m_frameId[i] <= lastRetiredFrameId;

            if (completelyRetired)
            {
                for (auto i = 0; i < m_framesInFlight; ++i)
                    m_dynamicMemoryAllocator->Deallocate(dynamicMemoryAllocation.m_allocation[i]);
            }

            m_dynamicMemoryAllocations.erase(memAllocation);
        }
        else
        {
            assert(m_staticMemoryAllocations.count(memAllocation) == 1);
            completelyRetired &= m_staticMemoryAllocations[memAllocation].m_frameId <= lastRetiredFrameId;
            if (completelyRetired)
                m_staticMemoryAllocations.erase(memAllocation);
        }

        if (completelyRetired)
            it = m_retiredAllocations.erase(it);
        else
            ++it;
    }
}

void D3D12Gpu::WaitAll()
{
    m_gpuSync->WaitAll();
}

ID3D12RootSignaturePtr D3D12Gpu::CreateRootSignature(ID3DBlobPtr signature, const std::wstring& name)
{
    ID3D12RootSignaturePtr rootSignature;
    AssertIfFailed(m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&rootSignature)));
    rootSignature->SetName(name.c_str());

    return rootSignature;
}

ID3D12PipelineStatePtr D3D12Gpu::CreatePSO(const D3D12_GRAPHICS_PIPELINE_STATE_DESC& psoDesc, const std::wstring& name)
{
    ID3D12PipelineStatePtr pso;
    AssertIfFailed(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pso)));
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
    m_dsvDescHeap = std::make_unique<D3D12DSVDescriptorHeap>(m_device, 1);
    assert(m_dsvDescHeap);

    const uint32_t maxDescriptors = 1024;
    m_cpuSRV_CBVDescHeap = std::make_unique<D3D12CPUDescriptorBuffer>(m_device, maxDescriptors);
    assert(m_cpuSRV_CBVDescHeap);

    m_gpuDescriptorRingBuffer = std::make_unique<D3D12GPUDescriptorRingBuffer>(m_device, std::max(m_framesInFlight, m_backBuffersCount), maxDescriptors);
    assert(m_gpuDescriptorRingBuffer);
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
    m_depthBufferDescHandle = m_dsvDescHeap->CreateDSV(m_depthBufferResource, desc, m_depthBufferDescHandle);
}

void D3D12Gpu::CheckFeatureSupport()
{
    D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};
    featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;

    AssertIfFailed(m_device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData)));
}

D3D12_GPU_VIRTUAL_ADDRESS D3D12Gpu::GetBufferVA(D3D12GpuMemoryHandle memHandle)
{
    if (DecodeGpuMemoryHandle_IsDynamic(memHandle))
    {
        assert(m_dynamicMemoryAllocations.count(memHandle));
        auto& memoryAllocation = m_dynamicMemoryAllocations[memHandle];
        assert(memoryAllocation.m_allocation[m_currentFrameIndex].m_gpuPtr);
        // NOTE: record the frame when the function got called
        // TODO: assuming GetBufferVA means binding to the pipeline isnt the best way
        // to handle this.
        memoryAllocation.m_frameId[m_currentFrameIndex] = m_gpuSync->GetNextFrameId();
        return memoryAllocation.m_allocation[m_currentFrameIndex].m_gpuPtr;
    }

    assert(m_staticMemoryAllocations.count(memHandle));
    return m_staticMemoryAllocations[memHandle].m_resource->GetGPUVirtualAddress();
}

void D3D12Gpu::SetVertexBuffer(D3D12GpuMemoryHandle memHandle, size_t vertexBufferSizeBytes, size_t vertexSizeBytes,
                               ID3D12GraphicsCommandListPtr cmdList)
{
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView
    {
        GetBufferVA(memHandle),
        static_cast<UINT>(vertexBufferSizeBytes),
        static_cast<UINT>(vertexSizeBytes)
    };
    cmdList->IASetVertexBuffers(0, 1, &vertexBufferView);
}

void D3D12Gpu::SetIndexBuffer(D3D12GpuMemoryHandle memHandle, size_t indexBufferSizeBytes, ID3D12GraphicsCommandListPtr cmdList)
{
    D3D12_INDEX_BUFFER_VIEW indexBufferView
    {
        GetBufferVA(memHandle),
        static_cast<UINT>(indexBufferSizeBytes), DXGI_FORMAT_R16_UINT
    };
    cmdList->IASetIndexBuffer(&indexBufferView);
}
