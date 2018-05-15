#include "d3d12scenerender.h"

// project includes
#include "d3d12material.h"

// c++ includes
#include <thread>

using namespace D3D12Basics;

D3D12GpuMemoryView CreateDefaultTexture2D(D3D12Gpu& gpu)
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
    auto memHandle = gpu.AllocateStaticMemory(subresources, resourceDesc, L"Default Texture 2D");

    return gpu.CreateTextureView(memHandle, resourceDesc);
}

D3D12SceneRender::D3D12SceneRender(D3D12Gpu& gpu, const Scene& scene,
                                   const std::unordered_map<std::wstring, TextureData>& textureDataCache,
                                   const std::unordered_map<size_t, MeshData>& meshDataCache) : m_gpu(gpu), m_scene(scene), 
                                                                                                m_textureDataCache(textureDataCache), 
                                                                                                m_meshDataCache(meshDataCache),
                                                                                                m_gpuResourcesLoaded(false)
{
    m_material = std::make_unique<D3D12Material>(m_gpu);
    assert(m_material);

    m_defaultTexture = CreateDefaultTexture2D(m_gpu);
}

D3D12SceneRender::~D3D12SceneRender()
{
}

void D3D12SceneRender::LoadGpuResources()
{
    for (const auto& model : m_scene.m_models)
    {
        assert(m_gpuMeshCache.count(model.m_id) == 0);

        D3D12GpuMemoryHandle dynamicCBMemHandle = m_gpu.AllocateDynamicMemory(sizeof(Matrix44), L"Dynamic CB - Transform " + model.m_name);
        D3D12GpuMemoryView dynamicCBView = m_gpu.CreateConstantBufferView(dynamicCBMemHandle);

        D3D12DescriptorTable dynamicCBDescriptorTable{ 0, {} };
        dynamicCBDescriptorTable.m_views.emplace_back(dynamicCBView);

        D3D12DescriptorTable texturesDescriptorTable{ 1,{} };
        if (!model.m_material.m_diffuseTexture.empty())
        {
            auto diffuseTextureView = CreateTexture(model.m_material.m_diffuseTexture);
            texturesDescriptorTable.m_views.emplace_back(diffuseTextureView);

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

            texturesDescriptorTable.m_views.emplace_back(m_defaultTexture);
            // TODO add fallback material
            //materialResources.push_back(defaultDiffuseColorCBID);
        }

        D3D12Bindings materialBindings;
        materialBindings.m_descriptorTables = { dynamicCBDescriptorTable, texturesDescriptorTable };

        assert(m_meshDataCache.count(model.m_id) == 1);
        const auto& meshData = m_meshDataCache.at(model.m_id);

        GPUMesh gpuMesh;
        gpuMesh.m_bindings = std::move(materialBindings);
        gpuMesh.m_vertexBuffer = m_gpu.AllocateStaticMemory(&meshData.Vertices()[0], meshData.VertexBufferSizeBytes(), L"vb - " + model.m_name);
        gpuMesh.m_indexBuffer = m_gpu.AllocateStaticMemory(&meshData.Indices()[0], meshData.IndexBufferSizeBytes(), L"ib - " + model.m_name);
        gpuMesh.m_vertexBufferSizeBytes = meshData.VertexBufferSizeBytes();
        gpuMesh.m_vertexSizeBytes = meshData.VertexSizeBytes();
        gpuMesh.m_indexBufferSizeBytes = meshData.IndexBufferSizeBytes();
        gpuMesh.m_indicesCount = meshData.IndicesCount();

        m_gpuMeshCache[model.m_id] = std::move(gpuMesh);
    }

    m_gpuResourcesLoaded = true;
}

void D3D12SceneRender::Update()
{
    // TODO Culling?

    const D3D12Basics::Matrix44 worldToClip = m_scene.m_camera.WorldToCamera() * m_scene.m_camera.CameraToClip();
    
    for (auto& model : m_scene.m_models)
    {
        assert(m_gpuMeshCache.count(model.m_id));
        auto& gpuMesh = m_gpuMeshCache[model.m_id];

        const D3D12Basics::Matrix44 localToClip = (model.m_transform * worldToClip).Transpose();

        assert(gpuMesh.m_bindings.m_descriptorTables.size() && gpuMesh.m_bindings.m_descriptorTables[0].m_views.size());
        m_gpu.UpdateMemory(gpuMesh.m_bindings.m_descriptorTables[0].m_views[0].m_memHandle, &localToClip, sizeof(D3D12Basics::Matrix44));
    }
}

std::vector<D3D12GpuRenderTask> D3D12SceneRender::CreateRenderTasks()
{
    std::vector<D3D12GpuRenderTask> renderTasks;

    for (auto& gpuMeshPair : m_gpuMeshCache)
    {
        auto& gpuMesh = gpuMeshPair.second;

        D3D12GpuRenderTask renderTask;
        renderTask.m_pipelineState = m_material->GetPSO();
        renderTask.m_rootSignature = m_material->GetRootSignature();
        renderTask.m_bindings = gpuMesh.m_bindings;
        renderTask.m_vertexBufferId = gpuMesh.m_vertexBuffer;
        renderTask.m_indexBufferId = gpuMesh.m_indexBuffer;
        renderTask.m_vertexBufferSizeBytes = gpuMesh.m_vertexBufferSizeBytes;
        renderTask.m_vertexSizeBytes = gpuMesh.m_vertexSizeBytes;
        renderTask.m_vertexOffset = 0;
        renderTask.m_indexBufferSizeBytes = gpuMesh.m_indexBufferSizeBytes;
        renderTask.m_indexCountPerInstance = gpuMesh.m_indicesCount;
        renderTask.m_indexOffset = 0;
        renderTask.m_clear = false;

        renderTasks.emplace_back(std::move(renderTask));
    }

    return renderTasks;
}

D3D12GpuMemoryView D3D12SceneRender::CreateTexture(const std::wstring& textureFile)
{
    if (m_textureCache.count(textureFile))
        return m_textureCache[textureFile];

    assert(m_textureDataCache.count(textureFile) == 1);

    const auto& textureData = m_textureDataCache.at(textureFile);
    auto memory = m_gpu.AllocateStaticMemory(textureData.GetSubResources(), textureData.GetDesc(), textureFile);

    return m_gpu.CreateTextureView(memory, textureData.GetDesc());
}