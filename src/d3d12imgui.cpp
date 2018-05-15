#include "d3d12imgui.h"

// project includes
#include "d3d12utils.h"

// directx
#include <d3d12.h>

// thirdparty libraries include
#include "imgui/imgui.h"

using namespace D3D12Basics;

D3D12ImGui::D3D12ImGui(D3D12Basics::D3D12Gpu& gpu) : m_gpu(gpu), m_vertexBufferSizeBytes(0), m_indexBufferSizeBytes(0)
{
    // Application init
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize.x = 1920.0f;
    io.DisplaySize.y = 1280.0f;
    // TODO: Fill others settings of the io structure later.

    CreatePSO();
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

std::vector<D3D12GpuRenderTask> D3D12ImGui::EndFrame()
{
    ImGui::Render();
    ImDrawData* drawData = ImGui::GetDrawData();
    assert(drawData);

    if (drawData->CmdListsCount == 0)
        return {};

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
    bindings.m_descriptorTables.emplace_back(std::move(descriptorTable));

    D3D12GpuRenderTask commonRenderTask;
    commonRenderTask.m_pipelineState            = m_pso;
    commonRenderTask.m_rootSignature            = m_rootSignature;
    commonRenderTask.m_bindings                 = bindings;
    commonRenderTask.m_viewport                 = m_defaultViewport;
    commonRenderTask.m_vertexBufferId           = m_vertexBuffer;
    commonRenderTask.m_indexBufferId            = m_indexBuffer;
    commonRenderTask.m_vertexBufferSizeBytes    = m_vertexBufferSizeBytes;
    commonRenderTask.m_vertexSizeBytes          = sizeof(ImDrawVert);
    commonRenderTask.m_indexBufferSizeBytes     = m_indexBufferSizeBytes;
    commonRenderTask.m_clear                    = false;

    std::vector<D3D12GpuRenderTask> renderTasks;
    int vertexOffset = 0;
    int indexOffset = 0;
    for (int n = 0; n < drawData->CmdListsCount; n++)
    {
        const ImDrawList* cmd_list = drawData->CmdLists[n];
        for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++)
        {
            const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];
            const D3D12_RECT scissorRect = { (LONG)pcmd->ClipRect.x, (LONG)pcmd->ClipRect.y, (LONG)pcmd->ClipRect.z, (LONG)pcmd->ClipRect.w };

            D3D12GpuRenderTask renderTask = commonRenderTask;
            renderTask.m_scissorRect = scissorRect;
            renderTask.m_indexCountPerInstance = pcmd->ElemCount;
            renderTask.m_indexOffset = indexOffset;
            renderTask.m_vertexOffset = vertexOffset;

            renderTasks.push_back(renderTask);

            indexOffset += pcmd->ElemCount;
        }
        vertexOffset += cmd_list->VtxBuffer.Size;
    }

    return renderTasks;
}

void D3D12ImGui::CreatePSO()
{
    D3D12_DESCRIPTOR_RANGE1 pixelSRVRanges = CreateDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1);

    const size_t rootParametersCount = 2;
    D3D12_ROOT_PARAMETER1 rootParameters[rootParametersCount]
    {
        CreateCBVRootParameter(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC, D3D12_SHADER_VISIBILITY_VERTEX),
        CreateDescTableRootParameter(&pixelSRVRanges, 1, D3D12_SHADER_VISIBILITY_PIXEL)
    };

    const unsigned int staticSamplerDescsCount = 1;
    D3D12_STATIC_SAMPLER_DESC staticSamplerDesc{ CreateStaticLinearSamplerDesc() };

    D3D12_FEATURE_DATA_ROOT_SIGNATURE d3d12FeatureDataRootSignature = D3D12_FEATURE_DATA_ROOT_SIGNATURE{ D3D_ROOT_SIGNATURE_VERSION_1_1 };
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
    rootSignatureDesc.Version = d3d12FeatureDataRootSignature.HighestVersion;
    rootSignatureDesc.Desc_1_1.NumParameters = rootParametersCount;
    rootSignatureDesc.Desc_1_1.pParameters = rootParameters;
    rootSignatureDesc.Desc_1_1.NumStaticSamplers = staticSamplerDescsCount;
    rootSignatureDesc.Desc_1_1.pStaticSamplers = &staticSamplerDesc;
    rootSignatureDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ID3DBlobPtr signature;
    ID3DBlobPtr errorMessage;
    auto result = D3D12SerializeVersionedRootSignature(&rootSignatureDesc, &signature, &errorMessage);
    if (errorMessage)
        D3D12Basics::OutputDebugBlobErrorMsg(errorMessage);
    D3D12Basics::AssertIfFailed(result);

    m_rootSignature = m_gpu.CreateRootSignature(signature, L"ImGui Root Signature");

    // Define the vertex input layout.
    const size_t inputElementDescsCount = 3;
    D3D12_INPUT_ELEMENT_DESC inputElementDescs[inputElementDescsCount] =
    {
        { "POSITION",   0, DXGI_FORMAT_R32G32_FLOAT,    0, 0,   D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD",   0, DXGI_FORMAT_R32G32_FLOAT,    0, 8,   D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR",      0, DXGI_FORMAT_R8G8B8A8_UNORM,  0, 16,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    const UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
    const char* vertexShaderSrc = R"(   
                                    struct Transformation
                                    {
                                        float4x4 worldCamProj;
                                    };
                                    ConstantBuffer<Transformation> transformation : register(b0, space0);

                                    struct Interpolators
                                    {
                                        float4 m_position   : SV_POSITION;
                                        float2 m_uv         : TEXCOORD;
                                        float4 m_color      : COLOR;
                                    };
    
                                    Interpolators VertexShaderMain(float2 position : POSITION, float2 uv : TEXCOORD, float4 color : COLOR)
                                    {
                                        Interpolators result;
                                        result.m_position = mul(transformation.worldCamProj, float4(position, 0.0f, 1.0f));
                                        result.m_color = color;
                                        result.m_uv = uv;
                                        return result;
                                    }
                                )";
    ID3DBlobPtr vertexShader = CompileShader(vertexShaderSrc, "VertexShaderMain", "vs_5_1", compileFlags);

    const char* pixelShaderSrc = R"(
                                    struct Interpolators
                                    {
                                        float4 m_position   : SV_POSITION;
                                        float2 m_uv         : TEXCOORD;
                                        float4 m_color      : COLOR;
                                    };
    
                                    Texture2D colorTexture : register(t0);
                                    SamplerState linearSampler : register(s0);
    
                                    float4 PixelShaderMain(Interpolators interpolators) : SV_TARGET
                                    {
                                        return interpolators.m_color * colorTexture.Sample(linearSampler, interpolators.m_uv);
                                    }
                                )";
    ID3DBlobPtr pixelShader = CompileShader(pixelShaderSrc, "PixelShaderMain", "ps_5_1", compileFlags);

    // Describe and create the graphics pipeline state object (PSO).
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = { inputElementDescs , inputElementDescsCount };
    psoDesc.pRootSignature = m_rootSignature.Get();
    psoDesc.VS = { vertexShader->GetBufferPointer(), vertexShader->GetBufferSize() };
    psoDesc.PS = { pixelShader->GetBufferPointer(), pixelShader->GetBufferSize() };
    psoDesc.RasterizerState = CreateDefaultRasterizerState();
    psoDesc.BlendState = CreateAlphaBlendState();
    psoDesc.DepthStencilState.DepthEnable = FALSE;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    psoDesc.DepthStencilState.StencilEnable = FALSE;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    psoDesc.SampleDesc.Count = 1;
    m_pso = m_gpu.CreatePSO(psoDesc, L"ImGui PSO");
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
    int vertexBufferOffset = 0;
    int indexBufferOffset = 0;
    for (int i = 0; i < drawData->CmdListsCount; ++i )
    {
        const ImDrawList* cmdList = drawData->CmdLists[i];
        m_gpu.UpdateMemory(m_vertexBuffer, cmdList->VtxBuffer.Data, cmdList->VtxBuffer.Size * sizeof(ImDrawVert), vertexBufferOffset);
        m_gpu.UpdateMemory(m_indexBuffer, cmdList->IdxBuffer.Data, cmdList->IdxBuffer.Size * sizeof(ImDrawIdx), indexBufferOffset);

        vertexBufferOffset += cmdList->VtxBuffer.Size;
        indexBufferOffset += cmdList->IdxBuffer.Size;
    }
}