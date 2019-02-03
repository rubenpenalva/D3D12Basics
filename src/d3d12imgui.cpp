#include "d3d12imgui.h"

// project includes
#include "d3d12utils.h"

// directx
#include <d3d12.h>

// thirdparty libraries include
#include "imgui/imgui.h"

// windows
#include <windows.h>

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

D3D12ImGui::D3D12ImGui(HWND hwnd, D3D12Basics::D3D12Gpu& gpu, 
                       FileMonitor& fileMonitor)  : m_hwnd(hwnd),
                                                    m_gpu(gpu), 
                                                    m_vertexBufferSizeBytes(0), 
                                                    m_indexBufferSizeBytes(0),
                                                    m_pipelineState(gpu, fileMonitor, 
                                                                    g_imguiPipelineStateDesc,
                                                                    L"D3D12 ImGui"),
                                                    m_vertexBuffer{}, m_indexBuffer{}
{
    // Application init
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    //io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;  // Enable Keyboard Controls

    // Keyboard mapping. ImGui will use those indices to peek into the io.KeysDown[] array that we will update during the application lifetime.
    io.KeyMap[ImGuiKey_Tab] = VK_TAB;
    io.KeyMap[ImGuiKey_LeftArrow] = VK_LEFT;
    io.KeyMap[ImGuiKey_RightArrow] = VK_RIGHT;
    io.KeyMap[ImGuiKey_UpArrow] = VK_UP;
    io.KeyMap[ImGuiKey_DownArrow] = VK_DOWN;
    io.KeyMap[ImGuiKey_PageUp] = VK_PRIOR;
    io.KeyMap[ImGuiKey_PageDown] = VK_NEXT;
    io.KeyMap[ImGuiKey_Home] = VK_HOME;
    io.KeyMap[ImGuiKey_End] = VK_END;
    io.KeyMap[ImGuiKey_Insert] = VK_INSERT;
    io.KeyMap[ImGuiKey_Delete] = VK_DELETE;
    io.KeyMap[ImGuiKey_Backspace] = VK_BACK;
    io.KeyMap[ImGuiKey_Space] = VK_SPACE;
    io.KeyMap[ImGuiKey_Enter] = VK_RETURN;
    io.KeyMap[ImGuiKey_Escape] = VK_ESCAPE;
    io.KeyMap[ImGuiKey_A] = 'A';
    io.KeyMap[ImGuiKey_C] = 'C';
    io.KeyMap[ImGuiKey_V] = 'V';
    io.KeyMap[ImGuiKey_X] = 'X';
    io.KeyMap[ImGuiKey_Y] = 'Y';
    io.KeyMap[ImGuiKey_Z] = 'Z';

    CreateFontTexture();

    m_transformation = m_gpu.AllocateDynamicMemory(sizeof(float) * 16, L"Dynamic CB - DearImgui Transformation");
    assert(m_transformation.IsValid());

    m_cmdList = m_gpu.CreateCmdList(L"ImGui");
    assert(m_cmdList);
}

D3D12ImGui::~D3D12ImGui()
{
    ImGui::DestroyContext();
}

void D3D12ImGui::ProcessInput()
{
    auto& io = ImGui::GetIO();

    // Read keyboard modifiers inputs
    io.KeyCtrl = (::GetKeyState(VK_CONTROL) & 0x8000) != 0;
    io.KeyShift = (::GetKeyState(VK_SHIFT) & 0x8000) != 0;
    io.KeyAlt = (::GetKeyState(VK_MENU) & 0x8000) != 0;
    io.KeySuper = false;
    // io.KeysDown[], io.MousePos, io.MouseDown[], io.MouseWheel: filled by the WndProc handler below.

    // Update OS mouse position
    io.MousePos = ImVec2(-FLT_MAX, -FLT_MAX);
    POINT pos;
    if (::GetActiveWindow() == m_hwnd && ::GetCursorPos(&pos))
        if (::ScreenToClient(m_hwnd, &pos))
            io.MousePos = ImVec2((float)pos.x, (float)pos.y);
}

void D3D12ImGui::BeginFrame(const Resolution& resolution)
{
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(resolution.m_width), static_cast<float>(resolution.m_height));

    m_defaultViewport =
    {
        0.0f, 0.0f,
        static_cast<float>(io.DisplaySize.x),
        static_cast<float>(io.DisplaySize.y),
        D3D12_MIN_DEPTH, D3D12_MAX_DEPTH
    };

    ImGui::NewFrame();
}

ID3D12CommandList* D3D12ImGui::EndFrame(D3D12_CPU_DESCRIPTOR_HANDLE renderTarget,
                                        D3D12_CPU_DESCRIPTOR_HANDLE depthStencilBuffer)
{
    m_cmdList->Open();
    auto cmdList = m_cmdList->GetCmdList();

    ImGui::Render();
    ImDrawData* drawData = ImGui::GetDrawData();
    assert(drawData);

    if (drawData->CmdListsCount == 0)
    {
        // NOTE not the best way of handling the transition of the backbuffer but
        // it is good enough for now
        auto rtToPresent = m_gpu.SwapChainTransition(RenderTarget_To_Present);
        cmdList->ResourceBarrier(1, &rtToPresent);

        m_cmdList->Close();
        return nullptr;
    }

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
    {
        // NOTE not the best way of handling the transition of the backbuffer but
        // it is good enough for now
        auto rtToPresent = m_gpu.SwapChainTransition(RenderTarget_To_Present);
        cmdList->ResourceBarrier(1, &rtToPresent);

        m_cmdList->Close();
        return nullptr;
    }

    cmdList->OMSetRenderTargets(1, &renderTarget, FALSE, &depthStencilBuffer);

    // TODO why is this commented out?
    //UpdateViewportScissor(cmdList, m_gpu.GetCurrentResolution());

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

    // NOTE not the best way of handling the transition of the backbuffer but
    // it is good enough for now
    auto rtToPresent = m_gpu.SwapChainTransition(RenderTarget_To_Present);
    cmdList->ResourceBarrier(1, &rtToPresent);

    m_cmdList->Close();

    return m_cmdList->GetCmdList().Get();
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
        assert(m_vertexBuffer.IsValid());

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
        assert(m_indexBuffer.IsValid());

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