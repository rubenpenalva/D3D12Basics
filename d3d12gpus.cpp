#include "d3d12gpus.h"

#include "utils.h"

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

using ID3D12DescriptorHeapPtr   = Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>;
using ID3D12ResourcePtr         = Microsoft::WRL::ComPtr<ID3D12Resource>;
using ID3D12CommandListPtr      = Microsoft::WRL::ComPtr<ID3D12CommandList>;
using ID3D12CommandAllocatorPtr = Microsoft::WRL::ComPtr<ID3D12CommandAllocator>;

using ID3DBlobPtr               = Microsoft::WRL::ComPtr<ID3DBlob>;

using namespace D3D12Render;
using namespace Utils;

namespace
{
    const size_t g_vertexElemsCount = 3;
    const size_t g_verticesCount = 3;
    float g_vertices[g_verticesCount * g_vertexElemsCount]
    {
        0.0f, 0.5f, 0.0f,
        0.5f, -0.5f, 0.0f,
        -0.5f, -0.5f, 0.0f,
    };
    const size_t g_vertexSize = g_vertexElemsCount * sizeof(float);
    const size_t g_vertexBufferSize = g_verticesCount * g_vertexSize;

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

    D3D12_RASTERIZER_DESC CreateDefaultRasterizerState()
    {
        return  D3D12_RASTERIZER_DESC 
        {
                /*FillMode*/                D3D12_FILL_MODE_SOLID,
                /*CullMode*/                D3D12_CULL_MODE_BACK,
                /*FrontCounterClockwise*/   FALSE,
                /*DepthBias*/               D3D12_DEFAULT_DEPTH_BIAS,
                /*DepthBiasClamp*/          D3D12_DEFAULT_DEPTH_BIAS_CLAMP,
                /*SlopeScaledDepthBias*/    D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS,
                /*DepthClipEnable*/         TRUE,
                /*MultisampleEnable*/       FALSE,
                /*AntialiasedLineEnable*/   FALSE,
                /*ForcedSampleCount*/       0,
                /*ConservativeRaster*/      D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF 
        };
    }

    // TODO: is there a more elegant way of constructing a D3D12_BLEND_DESC object?
    D3D12_BLEND_DESC CreateDefaultBlendState()
    {
        const D3D12_RENDER_TARGET_BLEND_DESC defaultRenderTargetBlendDesc =
        {
            FALSE,FALSE,
            D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
            D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
            D3D12_LOGIC_OP_NOOP,
            D3D12_COLOR_WRITE_ENABLE_ALL,
        };
        
        D3D12_BLEND_DESC blendDesc
        {
            /*AlphaToCoverageEnable*/   FALSE,
            /*IndependentBlendEnable*/  FALSE,
            /*RenderTarget*/ {}
        };

        for (UINT i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
            blendDesc.RenderTarget[i] = defaultRenderTargetBlendDesc;

        return blendDesc;
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

ID3D12GpuJob::ID3D12GpuJob(ID3D12GraphicsCommandListPtr commandList) : m_commandList(commandList)
{
    assert(m_commandList);
}

D3D12Gpu::D3D12Gpu(IDXGIFactory4Ptr factory, IDXGIAdapterPtr adapter, HWND hwnd) : m_backbufferIndex(0)
{
    assert(factory);
    assert(hwnd);

    AssertIfFailed(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device)));

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

    CreateDefaultPipelineState();

    CreateCommandList();

    CreateResources();
}

D3D12Gpu::~D3D12Gpu()
{
    WaitForGPU();

    // TODO: cant it be done with a comptr? else impl share_ptr with custom destructor
    CloseHandle(m_fenceEvent);
}

ID3D12GraphicsCommandListPtr D3D12Gpu::GetCommandList()
{
    assert(m_commandList);
    
    return m_commandList;
}

void D3D12Gpu::SetJob(ID3D12GpuJobPtr job)
{
    assert(job);

    m_job = job;
}

void D3D12Gpu::Execute()
{
    AssertIfFailed(m_commandAllocator->Reset());
    AssertIfFailed(m_commandList->Reset(m_commandAllocator.Get(), m_defaultPSO.Get()));

    m_commandList->ResourceBarrier(1, &m_backbuffers->Transition(m_backbufferIndex, D3D12Gpu::D3D12BackBuffers<BackBuffersCount>::TransitionType::Present_To_RenderTarget));

    auto backbufferRT = m_backbuffers->GetRenderTarget(m_backbufferIndex);

    m_job->Record(backbufferRT);
    
    // TODO move this to the job
    {
        m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());

        D3D12_VIEWPORT viewport = { 0.0f, 0.0f, static_cast<float>(CustomWindow::GetResolution().m_width), static_cast<float>(CustomWindow::GetResolution().m_height), D3D12_MIN_DEPTH, D3D12_MAX_DEPTH };
        RECT scissorRect = { 0L, 0L, static_cast<long>(CustomWindow::GetResolution().m_width), static_cast<long>(CustomWindow::GetResolution().m_height) };
        m_commandList->RSSetViewports(1, &viewport);
        m_commandList->RSSetScissorRects(1, &scissorRect);
        m_commandList->OMSetRenderTargets(1, &backbufferRT, FALSE, nullptr);
        m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        D3D12_VERTEX_BUFFER_VIEW vertexBufferView{ m_vertexBuffer->GetGPUVirtualAddress(), g_vertexBufferSize, g_vertexSize };
        m_commandList->IASetVertexBuffers(0, 1, &vertexBufferView);
        m_commandList->DrawInstanced(3, 1, 0, 0);
    }

    m_commandList->ResourceBarrier(1, &m_backbuffers->Transition(m_backbufferIndex, D3D12Gpu::D3D12BackBuffers<BackBuffersCount>::TransitionType::RenderTarget_To_Present));
    AssertIfFailed(m_commandList->Close());

    // Execute the command list.
    ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);
}

void D3D12Gpu::Flush()
{
    // Present the frame.
    AssertIfFailed(m_swapChain->Present(1, 0));

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

void D3D12Gpu::CreateDefaultPipelineState()
{
    assert(m_device);

    D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc{ 0, nullptr, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT };
    ID3DBlobPtr signature;
    AssertIfFailed(D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, nullptr));
    AssertIfFailed(m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)));
    m_rootSignature->SetName(L"Root Signature");

    // Define the vertex input layout.
    const size_t inputElementDescsCount = 1;
    D3D12_INPUT_ELEMENT_DESC inputElementDescs[inputElementDescsCount] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };
    
    const UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
    ID3DBlobPtr vertexShader;
    {
        const char* vertexShaderSrc = R"(float4 VertexShaderMain(float4 position : POSITION) : SV_POSITION
                                         {
                                            return position;
                                        })";
        AssertIfFailed(D3DCompile(vertexShaderSrc, strlen(vertexShaderSrc), nullptr, nullptr, nullptr, "VertexShaderMain", "vs_5_0", compileFlags, 0, &vertexShader, nullptr));
    }

    ID3DBlobPtr pixelShader;
    {
        const char* pixelShaderSrc = R"(static const float4 FixedColor = float4(1.0f, 0.0f, 0.0f, 1.0f);
                                     float4 PixelShaderMain(float4 position : SV_POSITION) : SV_TARGET
                                     {
                                        return FixedColor;
                                     })";
        AssertIfFailed(D3DCompile(pixelShaderSrc, strlen(pixelShaderSrc), nullptr, nullptr, nullptr, "PixelShaderMain", "ps_5_0", compileFlags, 0, &pixelShader, nullptr));
    }
    

    // Describe and create the graphics pipeline state object (PSO).
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = { inputElementDescs , inputElementDescsCount };
    psoDesc.pRootSignature = m_rootSignature.Get();
    psoDesc.VS = { vertexShader->GetBufferPointer(), vertexShader->GetBufferSize() };
    psoDesc.PS = { pixelShader->GetBufferPointer(), pixelShader->GetBufferSize() };
    psoDesc.RasterizerState = CreateDefaultRasterizerState();
    psoDesc.BlendState = CreateDefaultBlendState();
    psoDesc.DepthStencilState.DepthEnable = FALSE;
    psoDesc.DepthStencilState.StencilEnable = FALSE;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.SampleDesc.Count = 1;
    AssertIfFailed(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_defaultPSO)));
    m_defaultPSO->SetName(L"Default PSO");
}

void D3D12Gpu::CreateCommandList()
{
    assert(m_device);
    assert(m_defaultPSO);

    AssertIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocator)));
    m_commandAllocator->SetName(L"Command Allocator");

    AssertIfFailed(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocator.Get(), m_defaultPSO.Get(), IID_PPV_ARGS(&m_commandList)));
    m_commandList->SetName(L"Command List");
}

void D3D12Gpu::CreateResources()
{
    ID3D12ResourcePtr uploadHeap = CreateCommitedBuffer(&m_vertexBuffer, g_vertices, g_vertexBufferSize, L"Triangle vb");

    // Execute the resources copying commands
    {
        AssertIfFailed(m_commandList->Close());
        ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
        m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

        WaitForGPU();
    }
}

ID3D12ResourcePtr D3D12Gpu::CreateCommitedBuffer(ID3D12ResourcePtr* buffer, void* bufferData, unsigned int bufferDataSize, const std::wstring& bufferName)
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
        /*D3D12_RESOURCE_DIMENSION Dimension*/ D3D12_RESOURCE_DIMENSION_BUFFER,
        /*UINT64 Alignment*/ 0,
        /*UINT64 Width*/ bufferDataSize,
        /*UINT Height*/ 1,
        /*UINT16 DepthOrArraySize*/ 1,
        /*UINT16 MipLevels*/ 1,
        /*DXGI_FORMAT Format*/ DXGI_FORMAT_UNKNOWN,
        /*DXGI_SAMPLE_DESC SampleDesc*/ { 1, 0 },
        /*D3D12_TEXTURE_LAYOUT Layout*/ D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
        /*D3D12_RESOURCE_FLAGS Flags*/ D3D12_RESOURCE_FLAG_NONE
    };

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