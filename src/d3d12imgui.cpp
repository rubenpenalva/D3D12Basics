#include "d3d12imgui.h"

// project includes
#include "d3d12utils.h"

// directx
#include <d3d12.h>

// thirdparty libraries include
#include "imgui/imgui.h"

using namespace D3D12Basics;

namespace
{
    D3D12_DEPTH_STENCIL_DESC CreateDepthStencilDesc()
    {
        D3D12_DEPTH_STENCIL_DESC depthStencilDesc = {};

        depthStencilDesc.DepthEnable = FALSE;
        depthStencilDesc.StencilEnable = FALSE;

        return depthStencilDesc;
    }

    const D3D12PipelineStateDesc g_imguiPipelineStateDesc =
    {
        {
            { "POSITION",   0, DXGI_FORMAT_R32G32_FLOAT,    0, 0,   D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD",   0, DXGI_FORMAT_R32G32_FLOAT,    0, 8,   D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "COLOR",      0, DXGI_FORMAT_R8G8B8A8_UNORM,  0, 16,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
        },
        L"./data/shaders/imgui.hlsl",
        L"./data/shaders/imgui.hlsl",
        std::move(CreateDefaultRasterizerState()),
        std::move(CreateAlphaBlendState()),
        std::move(CreateDepthStencilDesc()),
        D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
        D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST,
        { DXGI_FORMAT_R8G8B8A8_UNORM },
        DXGI_FORMAT_D24_UNORM_S8_UINT,
        { 1, 0 }
    };
}

D3D12ImGui::D3D12ImGui(D3D12Basics::D3D12Gpu& gpu, FileMonitor& fileMonitor)  : m_gpu(gpu), 
                                                                                m_vertexBufferSizeBytes(0), 
                                                                                m_indexBufferSizeBytes(0),
                                                                                m_pipelineState(gpu, fileMonitor, 
                                                                                                g_imguiPipelineStateDesc,
                                                                                                L"D3D12 ImGui")
{
    // Application init
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize.x = 1920.0f;
    io.DisplaySize.y = 1280.0f;
    // TODO: Fill others settings of the io structure later.

    //CreatePSO();
    CreateFontTexture();

    m_defaultViewport =
    {
        0.0f, 0.0f,
        static_cast<float>(ImGui::GetIO().DisplaySize.x),
        static_cast<float>(ImGui::GetIO().DisplaySize.y),
        D3D12_MIN_DEPTH, D3D12_MAX_DEPTH
    };

    m_transformation = m_gpu.AllocateDynamicMemory(sizeof(float) * 16, L"Dynamic CB - DearImgui Transformation");
}

D3D12ImGui::~D3D12ImGui()
{
    ImGui::DestroyContext();
}

void D3D12ImGui::BeginFrame()
{
    ImGui::NewFrame();
}

void D3D12ImGui::EndFrame(ID3D12GraphicsCommandListPtr cmdList)
{
    ImGui::Render();
    ImDrawData* drawData = ImGui::GetDrawData();
    assert(drawData);

    if (drawData->CmdListsCount == 0)
        return;

    CreateVertexBuffer(drawData);
    CreateIndexBuffer(drawData);

    UpdateBuffers(drawData);

    D3D12Bindings bindings;
    {
        float L = 0.0f;
        float R = ImGui::GetIO().DisplaySize.x;
        float B = ImGui::GetIO().DisplaySize.y;
        float T = 0.0f;
        float mvp[16] =
        {
            2.0f / (R - L),     0.0f,               0.0f,       0.0f ,
            0.0f,               2.0f / (T - B),     0.0f,       0.0f,
            0.0f,               0.0f,               0.5f,       0.0f ,
            (R + L) / (L - R),  (T + B) / (B - T),  0.5f,       1.0f ,
        };
        m_gpu.UpdateMemory(m_transformation, &mvp[0], sizeof(float) * 16);

        D3D12ConstantBufferView constants{ 0, m_transformation };
        bindings.m_constantBufferViews.emplace_back(std::move(constants));
    }

    D3D12DescriptorTable descriptorTable{ 1, {m_textureView} };
    bindings.m_descriptorTables.push_back(descriptorTable);

    if (!m_pipelineState.ApplyState(cmdList))
        return;

    cmdList->RSSetViewports(1, &m_defaultViewport);
    m_gpu.SetBindings(cmdList, bindings);
    m_gpu.SetVertexBuffer(cmdList, m_vertexBuffer, m_vertexBufferSizeBytes, sizeof(ImDrawVert));
    m_gpu.SetIndexBuffer(cmdList, m_indexBuffer, m_indexBufferSizeBytes);

    int vertexOffset = 0;
    int indexOffset = 0;
    for (int n = 0; n < drawData->CmdListsCount; n++)
    {
        const ImDrawList* cmd_list = drawData->CmdLists[n];
        for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++)
        {
            const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];
            const D3D12_RECT scissorRect = { (LONG)pcmd->ClipRect.x, (LONG)pcmd->ClipRect.y,
                                             (LONG)pcmd->ClipRect.z, (LONG)pcmd->ClipRect.w };
            cmdList->RSSetScissorRects(1, &scissorRect);
            
            cmdList->DrawIndexedInstanced(static_cast<UINT>(pcmd->ElemCount), 1,
                                            static_cast<UINT>(indexOffset),
                                            static_cast<UINT>(vertexOffset), 0);

            indexOffset += pcmd->ElemCount;
        }
        vertexOffset += cmd_list->VtxBuffer.Size;
    }
}

void D3D12ImGui::CreateFontTexture()
{
    // Build texture atlas
    ImGuiIO& io = ImGui::GetIO();
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

    std::vector<D3D12_SUBRESOURCE_DATA> subresources;
    subresources.emplace_back(D3D12_SUBRESOURCE_DATA{ pixels, width * sizeof(uint32_t), width * height * sizeof(uint32_t) });
    D3D12_RESOURCE_DESC desc;
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Alignment = 0;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc = { 1, 0 };
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_NONE;

    D3D12GpuMemoryHandle textureMemHandle = m_gpu.AllocateStaticMemory(subresources, desc, L"ImGui Font Texture");
    m_textureView = m_gpu.CreateTextureView(textureMemHandle, desc);
}

void D3D12ImGui::CreateVertexBuffer(ImDrawData* drawData)
{
    const auto verticesCount = drawData->TotalVtxCount;
    assert(verticesCount);
    const size_t vertexSize = sizeof(ImDrawVert);
    const size_t vertexBufferSize = verticesCount * vertexSize;
    if (m_vertexBufferSizeBytes < vertexBufferSize)
    {
        if (m_vertexBufferSizeBytes != 0)
            m_gpu.FreeMemory(m_vertexBuffer);

        m_vertexBuffer = m_gpu.AllocateDynamicMemory(vertexBufferSize, L"Vertex Buffer - DearImgui");

        m_vertexBufferSizeBytes = vertexBufferSize;
    }
}


void D3D12ImGui::CreateIndexBuffer(ImDrawData* drawData)
{
    const auto indicesCount = drawData->TotalIdxCount;
    assert(indicesCount);
    const auto indexSize = sizeof(ImDrawIdx);
    const auto indexBufferSize = indicesCount * indexSize;
    if (m_indexBufferSizeBytes < indexBufferSize)
    {
        if (m_indexBufferSizeBytes != 0)
            m_gpu.FreeMemory(m_indexBuffer);

        m_indexBuffer = m_gpu.AllocateDynamicMemory(indexBufferSize, L"Index Buffer - DearImgui");

        m_indexBufferSizeBytes = indexBufferSize;
    }
}

void D3D12ImGui::UpdateBuffers(ImDrawData* drawData)
{
    int vertexBufferOffsetBytes = 0;
    int indexBufferOffsetBytes = 0;
    for (int i = 0; i < drawData->CmdListsCount; ++i )
    {
        const ImDrawList* cmdList = drawData->CmdLists[i];
        m_gpu.UpdateMemory(m_vertexBuffer, cmdList->VtxBuffer.Data, cmdList->VtxBuffer.Size * sizeof(ImDrawVert), vertexBufferOffsetBytes);
        m_gpu.UpdateMemory(m_indexBuffer, cmdList->IdxBuffer.Data, cmdList->IdxBuffer.Size * sizeof(ImDrawIdx), indexBufferOffsetBytes);

        vertexBufferOffsetBytes += cmdList->VtxBuffer.Size * sizeof(ImDrawVert);
        indexBufferOffsetBytes += cmdList->IdxBuffer.Size * sizeof(ImDrawIdx);
    }
}