#include "d3d12backendrender.h"

#include <unordered_map>
#include "meshgenerator.h"

#include "d3d12material.h"

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

    D3D12ResourceID CreateDefaultTexture2D(D3D12Gpu& gpu)
    {
        std::vector<D3D12_SUBRESOURCE_DATA> subresources(1);
        uint8_t data[] = { 0, 0, 0, 0, 0, 0, 0, 0 };
        subresources[0].pData = data;
        subresources[0].RowPitch = sizeof(uint8_t) * 4;
        subresources[0].SlicePitch = subresources[0].RowPitch * 2;
        // TODO the resource desc for a texture data is being copy and pasted in several places
        // think of a better way to share code.
        D3D12_RESOURCE_DESC resourceDesc;
        resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        resourceDesc.Alignment = 0;
        resourceDesc.Width = 1;
        resourceDesc.Height = 1;
        resourceDesc.DepthOrArraySize = 1;
        resourceDesc.MipLevels = 1;
        resourceDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        resourceDesc.SampleDesc = { 1, 0 };
        resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
        return gpu.CreateTexture(subresources, resourceDesc, L"Default Texture 2D");
    }
}

D3D12BackendRender::D3D12BackendRender(const D3D12Basics::Scene& scene, bool isWaitableForPresentEnabled) : m_scene(scene), m_gpu(isWaitableForPresentEnabled)
{
    m_material = std::make_unique<D3D12Material>(&m_gpu);
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

void D3D12BackendRender::LoadSceneResources(D3D12Basics::SceneLoader& sceneLoader)
{
    D3D12ResourceID defaultTexture2D = CreateDefaultTexture2D(m_gpu);
    // TODO add fallback material
    //auto defaultDiffuseColorCBID = m_gpu.CreateStaticConstantBuffer(&Material::m_diffuseColor, sizeof(Float3), L"Default Diffuse Color Static CB");

    // Load resources into vram
    // Create a render task per model
    for (const auto& model : m_scene.m_models)
    {
        D3D12DynamicResourceID dynamicCBID = m_gpu.CreateDynamicConstantBuffer(sizeof(Matrix44));

        D3D12ResourceDescriptorTable dynamicCBDescriptorTable;
        dynamicCBDescriptorTable.emplace_back(D3D12ResourceDescriptor{ D3D12ResourceType::DynamicConstantBuffer, dynamicCBID });

        D3D12ResourceDescriptorTable texturesDescriptorTable;
        if (!model.m_material.m_diffuseTexture.empty())
        {
            D3D12ResourceID diffuseTextureID = CreateTexture(model.m_material.m_diffuseTexture, sceneLoader);
            texturesDescriptorTable.emplace_back(D3D12ResourceDescriptor{ D3D12ResourceType::StaticResource, diffuseTextureID });

            // TODO add light
            //if (model.m_material.m_specularTexture)
            //{
            //    D3D12ResourceID specularTextureID = CreateTexture(*model.m_material.m_specularTexture, m_gpu, sceneLoader);
            //    materialResources.push_back(specularTextureID);

            //    assert(model.m_material.m_normalsTexture);
            //    D3D12ResourceID normalsTextureID = CreateTexture(*model.m_material.m_normalsTexture, m_gpu, sceneLoader);
            //    materialResources.push_back(normalsTextureID);
            //}
        }
        else
        {
            assert(model.m_material.m_specularTexture.empty() && model.m_material.m_normalsTexture.empty());

            texturesDescriptorTable.emplace_back(D3D12ResourceDescriptor{ D3D12ResourceType::StaticResource, defaultTexture2D });
            // TODO add fallback material
            //materialResources.push_back(defaultDiffuseColorCBID);
        }
        
        D3D12Bindings materialBindings{ dynamicCBDescriptorTable, texturesDescriptorTable };

        Mesh mesh;
        switch (model.m_type)
        {
        case Model::Type::Cube:
            mesh = D3D12Basics::CreateCube(VertexDesc{ true, false, false });
            break;
        case Model::Type::Plane:
            mesh = D3D12Basics::CreatePlane(VertexDesc{ true, false, false });
            break;
        case Model::Type::Sphere:
            mesh = D3D12Basics::CreateSphere({ true, false, false }, 40, 40);
            break;
        case Model::Type::MeshFile:
        {
            mesh = sceneLoader.LoadMesh(model.m_id);
            break;
        }
        default:
            assert(false);
        }

        D3D12ResourceID vbID = m_gpu.CreateCommittedBuffer(&mesh.Vertices()[0], mesh.VertexBufferSizeBytes(),
                                                            L"vb - " + model.m_name);

        D3D12ResourceID ibID = m_gpu.CreateCommittedBuffer(&mesh.Indices()[0], mesh.IndexBufferSizeBytes(),
                                                            L"ib - " + model.m_name);

        D3D12GpuRenderTask renderTask;
        renderTask.m_pipelineState = m_material->GetPSO();
        renderTask.m_rootSignature = m_material->GetRootSignature();
        renderTask.m_bindings = materialBindings;
        renderTask.m_viewport = m_defaultViewport;
        renderTask.m_scissorRect = m_defaultScissorRect;
        renderTask.m_vertexBufferResourceID = vbID;
        renderTask.m_indexBufferResourceID = ibID;
        renderTask.m_vertexCount = mesh.VerticesCount();
        renderTask.m_vertexSizeBytes = mesh.VertexSizeBytes();
        renderTask.m_indexCount = mesh.IndicesCount();
        memcpy(renderTask.m_clearColor, g_defaultClearColor, sizeof(float) * 4);

        m_renderTasks.push_back(renderTask);
    }
}

void D3D12BackendRender::UpdateSceneResources()
{
    const D3D12Basics::Matrix44 worldToClip = m_scene.m_camera.WorldToCamera() * m_scene.m_camera.CameraToClip();

    assert(m_scene.m_models.size() == m_renderTasks.size());
    for (size_t i = 0; i < m_scene.m_models.size(); ++i)
    {
        const auto& model = m_scene.m_models[i];
        const D3D12Basics::Matrix44 localToClip = (model.m_transform * worldToClip).Transpose();
        // TODO generalize this
        // TODO find the proper way to do this
        assert(m_renderTasks[i].m_bindings[0].back().m_resourceType == D3D12ResourceType::DynamicConstantBuffer);
        m_gpu.UpdateDynamicConstantBuffer(m_renderTasks[i].m_bindings[0].back().m_resourceID, &localToClip, sizeof(D3D12Basics::Matrix44));
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

D3D12ResourceID D3D12BackendRender::CreateTexture(const std::wstring& textureFile, D3D12Basics::SceneLoader& sceneLoader)
{
    if (m_textureCache.count(textureFile))
        return m_textureCache[textureFile];

    auto textureData = sceneLoader.LoadTextureData(textureFile);
    return m_gpu.CreateTexture(textureData.GetSubResources(), textureData.GetDesc(), textureFile);
}