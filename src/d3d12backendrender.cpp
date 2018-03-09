#include "d3d12backendrender.h"

#include <unordered_map>

using namespace D3D12Render;
using namespace D3D12Basics;

namespace
{
    const wchar_t* g_preFinishFrameUUID     = L"a9744ea3-aaaa-4f2f-be6a-42aad08a9c6f";
    const wchar_t* g_postFinishFrameUUID    = L"a9744ea3-bbbb-4f2f-be6a-42aad08a9c6f";

    const wchar_t* g_preFinishFrameName    = L"PRE FINISH_FRAME";
    const wchar_t* g_postFinishFrameName    = L"POST FINISH_FRAME";

    D3D12Basics::GpuViewMarker g_gpuViewMarkerPreFinishFrame(g_preFinishFrameName, g_preFinishFrameUUID);
    D3D12Basics::GpuViewMarker g_gpuViewMarkerPostFinishFrame(g_postFinishFrameName, g_postFinishFrameUUID);

    static const float g_defaultClearColor[4] = { 0.0f, 0.2f, 0.4f, 1.0f };
}

D3D12BackendRender::D3D12BackendRender(D3D12Basics::Scene& scene, bool isWaitableForPresentEnabled) : m_scene(scene), m_gpu(isWaitableForPresentEnabled)
{
}

D3D12BackendRender::~D3D12BackendRender()
{
}

const D3D12Basics::Resolution& D3D12BackendRender::GetSafestResolutionSupported() const
{
    return m_gpu.GetSafestResolutionSupported();
}

void D3D12BackendRender::SetOutputWindow(HWND hwnd)
{
    assert(hwnd);
    m_gpu.SetOutputWindow(hwnd);

    UpdateDefaultNotPSOState();
}

void D3D12BackendRender::OnToggleFullScreen()
{
    m_gpu.OnToggleFullScreen();
}

void D3D12BackendRender::OnResize(const D3D12Basics::Resolution& resolution)
{
    m_gpu.OnResize(resolution);

    UpdateDefaultNotPSOState();

    for (auto& renderTask : m_renderTasks)
    {
        renderTask.m_viewport    = m_defaultViewport;
        renderTask.m_scissorRect = m_defaultScissorRect;
    }
}

void D3D12BackendRender::LoadSceneResources()
{
    std::unordered_map<std::wstring, SimpleMaterialDataPtr> simpleMaterialDatas;

    // Load resources into vram
    // Create a render task per model
    for (auto& model : m_scene.m_models)
    {
        if (simpleMaterialDatas.count(model.m_simpleMaterialTextureFileName) == 0)
            simpleMaterialDatas[model.m_simpleMaterialTextureFileName] = std::make_shared<SimpleMaterialData>(model.m_simpleMaterialTextureFileName);
        
        const auto& simpleMaterialData = simpleMaterialDatas[model.m_simpleMaterialTextureFileName];

        // TODO Cache the gpu texture resource
        D3D12ResourceID textureID = m_gpu.CreateTexture(simpleMaterialData->m_textureData, simpleMaterialData->m_textureSizeBytes,
                                                        simpleMaterialData->m_textureWidth, simpleMaterialData->m_textureHeight,
                                                        DXGI_FORMAT_R8G8B8A8_UNORM, model.m_simpleMaterialTextureFileName);

        
        D3D12ResourceID vbID = m_gpu.CreateCommittedBuffer(&model.m_mesh.m_vertices[0], model.m_mesh.VertexBufferSizeInBytes(),
                                                            L"vb - " + model.m_name);

        D3D12ResourceID ibID = m_gpu.CreateCommittedBuffer(&model.m_mesh.m_indices[0], model.m_mesh.IndexBufferSizeInBytes(),
                                                            L"ib - " + model.m_name);

        D3D12DynamicResourceID dynamicCBID = m_gpu.CreateDynamicConstantBuffer(sizeof(Matrix44));

        D3D12SimpleMaterialResources materialResources{ dynamicCBID, textureID };

        D3D12GpuRenderTask renderTask;
        renderTask.m_simpleMaterialResources    = materialResources;
        renderTask.m_viewport                   = m_defaultViewport;
        renderTask.m_scissorRect                = m_defaultScissorRect;
        renderTask.m_vertexBufferResourceID     = vbID;
        renderTask.m_indexBufferResourceID      = ibID;
        renderTask.m_vertexCount                = model.m_mesh.m_vertices.size();
        renderTask.m_vertexSize                 = model.m_mesh.VertexSize;
        renderTask.m_indexCount                 = model.m_mesh.m_indices.size();
        memcpy(renderTask.m_clearColor, g_defaultClearColor, sizeof(float) * 4);

        m_renderTasks.push_back(renderTask);
    }
}

void D3D12BackendRender::UpdateSceneResources()
{
    const D3D12Basics::Matrix44 worldToClip = m_scene.m_camera.WorldToCamera() * m_scene.m_camera.CameraToClip();

    const auto& modelsCount = m_scene.m_models.size();
    for (size_t i = 0; i < modelsCount; ++i)
    {
        const auto& model = m_scene.m_models[i];
        const D3D12Basics::Matrix44 localToClip = (model.m_transform * worldToClip).Transpose();
        m_gpu.UpdateDynamicConstantBuffer(m_renderTasks[i].m_simpleMaterialResources.m_cbID, &localToClip);
    }
}

void D3D12BackendRender::RenderFrame()
{
    m_gpu.BeginFrame();

    m_gpu.ExecuteRenderTasks(m_renderTasks);
}

void D3D12BackendRender::FinishFrame()
{
    g_gpuViewMarkerPreFinishFrame.Mark();

    // Present the frame
    m_gpu.FinishFrame();

    g_gpuViewMarkerPostFinishFrame.Mark();
}

void D3D12BackendRender::UpdateDefaultNotPSOState()
{
    const auto& currentResolution = m_gpu.GetCurrentResolution();

    m_defaultViewport = 
    {
        0.0f, 0.0f,
        static_cast<float>(currentResolution.m_width),
        static_cast<float>(currentResolution.m_height),
        D3D12_MIN_DEPTH, D3D12_MAX_DEPTH
    };
    
    m_defaultScissorRect =
    {
        0L, 0L,
        static_cast<long>(currentResolution.m_width),
        static_cast<long>(currentResolution.m_height)
    };
}