#include "d3d12scenerender.h"

// project includes
#include "d3d12utils.h"
#include "d3d12gpu.h"

// c++ includes
#include <thread>
#include <sstream>

using namespace D3D12Basics;

namespace
{
    static const float g_defaultClearColor[4]   = { 0.0f, 0.2f, 0.4f, 1.0f };
    static const float g_shadowMapClearColor[4] = { 0.0f };

    static const size_t g_quadVertexSizeBytes = 5 * sizeof(float);
    static const size_t g_quadVBSizeBytes = 4 * g_quadVertexSizeBytes;
    static const size_t g_quadIndicesCount = 6;
    static const size_t g_quadIBSizeBytes = sizeof(uint16_t) * g_quadIndicesCount;
 
    static const Resolution g_shadowMapResolution = { 4096, 4096 };

    D3D12_DEPTH_STENCIL_DESC CreateDepthStencilDesc();

    const D3D12PipelineStateDesc g_stdMaterialPipeDesc =
    {
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 20, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "BINORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 44, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
        },
        L"./data/shaders/stdmaterial.hlsl",
        L"./data/shaders/stdmaterial.hlsl",
        std::move(CreateDefaultRasterizerState()),
        std::move(CreateDefaultBlendState()),
        std::move(CreateDepthStencilDesc()),
        D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
        D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST,
        { DXGI_FORMAT_R8G8B8A8_UNORM },
        DXGI_FORMAT_D24_UNORM_S8_UINT,
        { 1, 0 }
    };

    const D3D12PipelineStateDesc g_defaultMaterialPipeDesc =
    {
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 20, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        },
        L"./data/shaders/defaultmaterial.hlsl",
        L"./data/shaders/defaultmaterial.hlsl",
        std::move(CreateDefaultRasterizerState()),
        std::move(CreateDefaultBlendState()),
        std::move(CreateDepthStencilDesc()),
        D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
        D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST,
        { DXGI_FORMAT_R8G8B8A8_UNORM },
        DXGI_FORMAT_D24_UNORM_S8_UINT,
        { 1, 0 }
    };

    const D3D12PipelineStateDesc g_defaultMaterialFixedColorPipeDesc =
    {
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 20, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        },
        L"./data/shaders/defaultmaterial_fixedcolor.hlsl",
        L"./data/shaders/defaultmaterial_fixedcolor.hlsl",
        std::move(CreateDefaultRasterizerState()),
        std::move(CreateDefaultBlendState()),
        std::move(CreateDepthStencilDesc()),
        D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
        D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST,
        { DXGI_FORMAT_R8G8B8A8_UNORM },
        DXGI_FORMAT_D24_UNORM_S8_UINT,
        { 1, 0 }
    };

    const D3D12PipelineStateDesc g_defaultMaterialFixedColorNoShadowsPipeDesc =
    {
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
        },
        L"./data/shaders/defaultmaterial_fixedcolor_noshadows.hlsl",
        L"./data/shaders/defaultmaterial_fixedcolor_noshadows.hlsl",
        std::move(CreateDefaultRasterizerState()),
        std::move(CreateDefaultBlendState()),
        std::move(CreateDepthStencilDesc()),
        D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
        D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST,
        { DXGI_FORMAT_R8G8B8A8_UNORM },
        DXGI_FORMAT_D24_UNORM_S8_UINT,
        { 1, 0 }
    };

    const D3D12PipelineStateDesc g_shadowPipeDesc =
    {
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
        },
        L"./data/shaders/depthonly.hlsl",
        L"./data/shaders/depthonly.hlsl",
        std::move(CreateDefaultRasterizerState()),
        std::move(CreateDefaultBlendState()),
        std::move(CreateDepthStencilDesc()),
        D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
        D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST,
        {},
        DXGI_FORMAT_D32_FLOAT,
        { 1, 0 }
    };

    const D3D12PipelineStateDesc g_shadowDebugPipeDesc =
    {
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
        },
        L"./data/shaders/depthdebug.hlsl",
        L"./data/shaders/depthdebug.hlsl",
        std::move(CreateDefaultRasterizerState()),
        std::move(CreateDefaultBlendState()),
        {},
        D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
        D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST,
        { DXGI_FORMAT_R8G8B8A8_UNORM },
        {},
        { 1, 0 }
    };

    struct ShadingData
    {
        Matrix44 m_worldCamProj;
        Matrix44 m_worldLightProj[2];
        Matrix44 m_tInvWorld;
        Float4   m_lightDirection[2];
    };

    struct ShadingDataNoShadows
    {
        Matrix44 m_worldCamProj;
    };

    D3D12GpuViewHandle CreateDefaultTexture2D(D3D12Gpu& gpu)
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

    D3D12GpuViewHandle CreateNullTexture2D(D3D12Gpu& gpu)
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

        return gpu.CreateNULLTextureView(resourceDesc);
    }

    void UpdateViewportScissor(ID3D12GraphicsCommandListPtr cmdList, const Resolution& resolution)
    {
        D3D12_VIEWPORT viewport =
        {
            0.0f, 0.0f,
            static_cast<float>(resolution.m_width),
            static_cast<float>(resolution.m_height),
            D3D12_MIN_DEPTH, D3D12_MAX_DEPTH
        };

        RECT scissorRect =
        {
            0L, 0L,
            static_cast<long>(resolution.m_width),
            static_cast<long>(resolution.m_height)
        };

        cmdList->RSSetViewports(1, &viewport);
        cmdList->RSSetScissorRects(1, &scissorRect);
    }

    D3D12_DEPTH_STENCIL_DESC CreateDepthStencilDesc()
    {
        D3D12_DEPTH_STENCIL_DESC depthStencilDesc;

        depthStencilDesc.DepthEnable = TRUE;
        depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
        depthStencilDesc.StencilEnable = FALSE;

        return depthStencilDesc;
    }
}

D3D12SceneRender::D3D12SceneRender(D3D12Gpu& gpu, FileMonitor& fileMonitor, const Scene& scene,
                                   const D3D12Basics::TextureDataCache& textureDataCache,
                                   const D3D12Basics::MeshDataCache& meshDataCache) :
    m_gpu(gpu), m_scene(scene),
    m_textureDataCache(textureDataCache),
    m_meshDataCache(meshDataCache),
    m_gpuResourcesLoaded(false),
    m_stdMaterialPipeState(gpu, fileMonitor, g_stdMaterialPipeDesc, L"D3D12 std material"),
    m_defaultMaterialPipeState(gpu, fileMonitor, g_defaultMaterialPipeDesc, L"D3D12 default material"),
    m_defaultMaterialFixedColorPipeState(gpu, fileMonitor, g_defaultMaterialFixedColorPipeDesc, L"D3D12 default material - fixed color"),
    m_defaultMaterialFixedColorNoShadowsPipeState(gpu, fileMonitor, g_defaultMaterialFixedColorNoShadowsPipeDesc, L"D3D12 default material - fixed color no shadows"),

    m_shadowPipeState(gpu, fileMonitor, g_shadowPipeDesc, L"D3D12 depth only"),
    m_shadowDebugPipeState(gpu, fileMonitor, g_shadowDebugPipeDesc, L"D3D12 depth only debug")
{
    m_defaultTexture = CreateDefaultTexture2D(m_gpu);
    m_nullTexture = CreateNullTexture2D(m_gpu);

    CreateDebugResources();
}

void D3D12SceneRender::LoadGpuResources()
{
    // Load shadow resources per light
    const uint8_t lightsCount = static_cast<uint8_t>(m_scene.m_lights.size());
    for (uint8_t i = 0; i < lightsCount && i < 2; ++i)
    {
        m_shadowResPerLight.push_back(CreateShadowResources(m_gpu, i));
    }

    // Load models gpu resources
    // TODO rework this code to use the least amount of copies
    for (const auto& model : m_scene.m_models)
    {
        assert(m_gpuMeshCache.count(model.m_id) == 0);

        GPUMesh gpuMesh;
        {
            D3D12DescriptorTable cbDescriptorTable{ 0,{} };
            {
                if (model.m_material.m_shadowReceiver)
                    gpuMesh.m_forwardTransformsGpuMemHandle = m_gpu.AllocateDynamicMemory(sizeof(ShadingData), L"Dynamic CB - ShadingData " + model.m_name);
                else
                    gpuMesh.m_forwardTransformsGpuMemHandle = m_gpu.AllocateDynamicMemory(sizeof(ShadingDataNoShadows), L"Dynamic CB - ShadingData NoShadows" + model.m_name);

                D3D12GpuViewHandle dynamicCBView = m_gpu.CreateConstantBufferView(gpuMesh.m_forwardTransformsGpuMemHandle);
                cbDescriptorTable.m_views.push_back(dynamicCBView);
            }

            // TODO encapsulate define permutations
            D3D12DescriptorTable slot1DescTable{ 1, {} };
            const bool isDiffuseTextureSet = !model.m_material.m_diffuseTexture.empty();
            if (isDiffuseTextureSet)
            {
                auto diffuseTextureView = CreateTexture(model.m_material.m_diffuseTexture);
                slot1DescTable.m_views.emplace_back(diffuseTextureView);
            }

            const bool isNormalTextureSet = !model.m_material.m_normalsTexture.empty();
            if (isNormalTextureSet)
            {
                auto normalTextureView = CreateTexture(model.m_material.m_normalsTexture);
                slot1DescTable.m_views.emplace_back(normalTextureView);

                assert(isDiffuseTextureSet);
            }
            
            if (isDiffuseTextureSet && isNormalTextureSet)
                gpuMesh.m_pipelineStateId = PipelineStateId::StdMaterial;
            else if (isDiffuseTextureSet)
                gpuMesh.m_pipelineStateId = PipelineStateId::DefaultMaterial;
            else
            {
                assert(slot1DescTable.m_views.empty());
                gpuMesh.m_materialGpuMemHandle = m_gpu.AllocateStaticMemory(&model.m_material.m_diffuseColor, sizeof(Float3), L"Static CB - MaterialData " + model.m_name);
                D3D12GpuViewHandle staticCBView = m_gpu.CreateConstantBufferView(gpuMesh.m_materialGpuMemHandle);
                slot1DescTable.m_views.push_back(staticCBView);
                gpuMesh.m_pipelineStateId = model.m_material.m_shadowReceiver?  PipelineStateId::DefaultMaterial_FixedColor :
                                                                                PipelineStateId::DefaultMaterial_FixedColorNoShadows;
            }

            if (model.m_material.m_shadowReceiver)
            {
                for (const auto& shadowResource : m_shadowResPerLight)
                {
                    slot1DescTable.m_views.emplace_back(shadowResource.m_shadowTexture.m_srv);
                }
            }

            gpuMesh.m_forwardPassBindings.m_descriptorTables = { cbDescriptorTable, slot1DescTable };
        }

        // TODO lights count
        for (size_t i = 0; i < 2; ++i)
        {
            std::wstringstream ss;
            ss << L"Shadow pass" << i << L" Dynamic CB - Transform " + model.m_name;
            gpuMesh.m_shadowsTransformGpuMemHandles[i] = m_gpu.AllocateDynamicMemory(sizeof(Matrix44), ss.str());
            gpuMesh.m_shadowPassBindings[i].m_constantBufferViews = { { 0, gpuMesh.m_shadowsTransformGpuMemHandles[i] } };
        }

        assert(m_meshDataCache.count(model.m_id) == 1);
        const auto& meshData = m_meshDataCache.at(model.m_id);

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
    if (m_scene.m_models.empty())
        return;

    // Update transforms
    assert(m_scene.m_lights.size() == 2);
    const D3D12Basics::Matrix44 worldToCameraClip = m_scene.m_camera.WorldToLocal() * m_scene.m_camera.LocalToClip();
    const D3D12Basics::Matrix44 worldToLightClip[2] = 
    {
        m_scene.m_lights[0].m_transform.WorldToLocal() * m_scene.m_lights[0].m_transform.LocalToClip(),
        m_scene.m_lights[1].m_transform.WorldToLocal() * m_scene.m_lights[1].m_transform.LocalToClip()
    };
    const Float4 light0Fwd{ -m_scene.m_lights[0].m_transform.Forward().x,
                            -m_scene.m_lights[0].m_transform.Forward().y,
                            -m_scene.m_lights[0].m_transform.Forward().z, 0.0f };
    const Float4 light1Fwd{ -m_scene.m_lights[1].m_transform.Forward().x,
                            -m_scene.m_lights[1].m_transform.Forward().y,
                            -m_scene.m_lights[1].m_transform.Forward().z, 0.0f };

    for (auto& model : m_scene.m_models)
    {
        assert(m_gpuMeshCache.count(model.m_id));
        auto& gpuMesh = m_gpuMeshCache[model.m_id];
        Matrix44 worldCameraProj = (model.m_transform * worldToCameraClip).Transpose();
        Matrix44 worldLightProj1 = (model.m_transform * worldToLightClip[0]).Transpose();
        Matrix44 worldLightProj2 = (model.m_transform * worldToLightClip[1]).Transpose();

        if (model.m_material.m_shadowReceiver)
        {
            ShadingData transforms
            {
                worldCameraProj,
                {worldLightProj1, worldLightProj2},
                model.m_normalTransform.Transpose(),
                {light0Fwd, light1Fwd}
            };

            m_gpu.UpdateMemory(gpuMesh.m_forwardTransformsGpuMemHandle, &transforms, sizeof(ShadingData));
        }
        else
        {
            ShadingDataNoShadows transforms
            {
                worldCameraProj
            };

            m_gpu.UpdateMemory(gpuMesh.m_forwardTransformsGpuMemHandle, &transforms, sizeof(ShadingDataNoShadows));
        }

        m_gpu.UpdateMemory(gpuMesh.m_shadowsTransformGpuMemHandles[0], &worldLightProj1, sizeof(D3D12Basics::Matrix44));
        m_gpu.UpdateMemory(gpuMesh.m_shadowsTransformGpuMemHandles[1], &worldLightProj2, sizeof(D3D12Basics::Matrix44));
    }
}

void D3D12SceneRender::RecordCmdList(ID3D12GraphicsCommandListPtr cmdList,
                                     D3D12_CPU_DESCRIPTOR_HANDLE renderTarget,
                                     D3D12_CPU_DESCRIPTOR_HANDLE depthStencilBuffer)
{
    assert(cmdList);

    m_sceneStats = { 0 };

    // Shadows pass
    RenderShadowPass(cmdList);

    // Forward pass
    RenderForwardPass(cmdList, renderTarget, depthStencilBuffer);

    // TODO add imgui ui option to render the debug elements
    //RenderDebug(cmdList);
}

D3D12GpuViewHandle D3D12SceneRender::CreateTexture(const std::wstring& textureFile)
{
    if (m_textureCache.count(textureFile))
        return m_textureCache[textureFile];

    assert(m_textureDataCache.count(textureFile) == 1);

    const auto& textureData = m_textureDataCache.at(textureFile);
    auto memory = m_gpu.AllocateStaticMemory(textureData.GetSubResources(), textureData.GetDesc(), textureFile);

    auto memoryView = m_gpu.CreateTextureView(memory, textureData.GetDesc());

    m_textureCache[textureFile] = std::move(memoryView);

    return m_textureCache[textureFile];
}

void D3D12SceneRender::CreateDebugResources()
{
    float vertices[] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                        1.0f, 0.0f, 0.0f, 1.0f, 0.0f,
                        1.0f, 1.0f, 0.0f, 1.0f, 1.0f,
                        0.0f, 1.0f, 0.0f, 0.0f, 1.0f };

    m_quadVb = m_gpu.AllocateStaticMemory(&vertices[0], g_quadVBSizeBytes, L"vb - screen quad");

    uint16_t indices[] = { 0, 3, 2, 0, 2, 1 };
    m_quadIb = m_gpu.AllocateStaticMemory(&indices[0], g_quadIBSizeBytes, L"ib - screen quad");
}

void D3D12SceneRender::RenderShadowPass(ID3D12GraphicsCommandListPtr cmdList)
{
    if (m_shadowResPerLight.empty())
        return;

    // Transition from depth write to none
    std::vector<D3D12_RESOURCE_BARRIER> barriersDepthBufferReadWrite(m_shadowResPerLight.size() > 1? 2 : 1);
    barriersDepthBufferReadWrite[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriersDepthBufferReadWrite[0].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barriersDepthBufferReadWrite[0].Transition.pResource = m_gpu.GetResource(m_shadowResPerLight[0].m_shadowTexture.m_memHandle);
    barriersDepthBufferReadWrite[0].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barriersDepthBufferReadWrite[0].Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    barriersDepthBufferReadWrite[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    if (m_shadowResPerLight.size() > 1)
    {
        barriersDepthBufferReadWrite[1] = barriersDepthBufferReadWrite[0];
        barriersDepthBufferReadWrite[1].Transition.pResource = m_gpu.GetResource(m_shadowResPerLight[1].m_shadowTexture.m_memHandle);
    }

    cmdList->ResourceBarrier(static_cast<UINT>(barriersDepthBufferReadWrite.size()), &barriersDepthBufferReadWrite[0]);

    size_t lightIndex = 0;
    for (const auto& shadowRes : m_shadowResPerLight)
    {
        cmdList->OMSetRenderTargets(0, nullptr, FALSE, &shadowRes.m_shadowTextureDSVCPUHandle);

        cmdList->ClearDepthStencilView(shadowRes.m_shadowTextureDSVCPUHandle, D3D12_CLEAR_FLAG_DEPTH,
                                        1.0f, 0, 0, nullptr);

        UpdateViewportScissor(cmdList, g_shadowMapResolution);

        if (!m_shadowPipeState.ApplyState(cmdList))
            continue;

        for (auto& gpuMeshPair : m_gpuMeshCache)
        {
            auto& gpuMesh = gpuMeshPair.second;

            m_gpu.SetBindings(cmdList, gpuMesh.m_shadowPassBindings[lightIndex]);
            m_gpu.SetVertexBuffer(cmdList, gpuMesh.m_vertexBuffer, gpuMesh.m_vertexBufferSizeBytes, gpuMesh.m_vertexSizeBytes);
            m_gpu.SetIndexBuffer(cmdList, gpuMesh.m_indexBuffer, gpuMesh.m_indexBufferSizeBytes);
            cmdList->DrawIndexedInstanced(static_cast<UINT>(gpuMesh.m_indicesCount), 1, 0, 0, 0);
            m_sceneStats.m_shadowPassDrawCallsCount++;
        }

        ++lightIndex;
    }

    for (size_t i = 0; i < barriersDepthBufferReadWrite.size(); ++i)
    {
        barriersDepthBufferReadWrite[i].Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        barriersDepthBufferReadWrite[i].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barriersDepthBufferReadWrite[i].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    }
    cmdList->ResourceBarrier(static_cast<UINT>(barriersDepthBufferReadWrite.size()), &barriersDepthBufferReadWrite[0]);
}

void D3D12SceneRender::RenderForwardPass(ID3D12GraphicsCommandListPtr cmdList, 
                                         D3D12_CPU_DESCRIPTOR_HANDLE renderTarget,
                                         D3D12_CPU_DESCRIPTOR_HANDLE depthStencilBuffer)
{
    cmdList->OMSetRenderTargets(1, &renderTarget, FALSE, &depthStencilBuffer);

    cmdList->ClearDepthStencilView(depthStencilBuffer, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
                                   1.0f, 0, 0, nullptr);
    cmdList->ClearRenderTargetView(renderTarget, g_defaultClearColor, 0, nullptr);

    UpdateViewportScissor(cmdList, m_gpu.GetCurrentResolution());

    for (auto& gpuMeshPair : m_gpuMeshCache)
    {
        auto& gpuMesh = gpuMeshPair.second;

        // TODO generalize this into a material system
        if (gpuMesh.m_pipelineStateId == PipelineStateId::StdMaterial)
        {
            if (!m_stdMaterialPipeState.ApplyState(cmdList))
                continue;
        }
        else if (gpuMesh.m_pipelineStateId == PipelineStateId::DefaultMaterial)
        {
            if (!m_defaultMaterialPipeState.ApplyState(cmdList))
                continue;
        }
        else if (gpuMesh.m_pipelineStateId == PipelineStateId::DefaultMaterial_FixedColor)
        {
            if (!m_defaultMaterialFixedColorPipeState.ApplyState(cmdList))
                continue;
        }
        else
        {
            assert(gpuMesh.m_pipelineStateId == PipelineStateId::DefaultMaterial_FixedColorNoShadows);
            if (!m_defaultMaterialFixedColorNoShadowsPipeState.ApplyState(cmdList))
                continue;
        }

        m_gpu.SetBindings(cmdList, gpuMesh.m_forwardPassBindings);
        m_gpu.SetVertexBuffer(cmdList, gpuMesh.m_vertexBuffer, gpuMesh.m_vertexBufferSizeBytes, gpuMesh.m_vertexSizeBytes);
        m_gpu.SetIndexBuffer(cmdList, gpuMesh.m_indexBuffer, gpuMesh.m_indexBufferSizeBytes);
        cmdList->DrawIndexedInstanced(static_cast<UINT>(gpuMesh.m_indicesCount), 1, 0, 0, 0);
        m_sceneStats.m_forwardPassDrawCallsCount++;
    }
}

void D3D12SceneRender::RenderDebug(ID3D12GraphicsCommandListPtr cmdList)
{
    m_shadowDebugPipeState.ApplyState(cmdList);

    for (const auto& shadowResources : m_shadowResPerLight)
    {
        D3D12Bindings bindings;
        {
            D3D12DescriptorTable texturesDescriptorTable{ 0, {} };
            texturesDescriptorTable.m_views.emplace_back(shadowResources.m_shadowTexture.m_srv);
            bindings.m_descriptorTables = { texturesDescriptorTable };
        }

        m_gpu.SetBindings(cmdList, bindings);
        m_gpu.SetVertexBuffer(cmdList, m_quadVb, g_quadVBSizeBytes, g_quadVertexSizeBytes);
        m_gpu.SetIndexBuffer(cmdList, m_quadIb, g_quadIBSizeBytes);
        cmdList->DrawIndexedInstanced(static_cast<UINT>(g_quadIndicesCount), 1, 0, 0, 0);
    }
}

D3D12SceneRender::ShadowResources D3D12SceneRender::CreateShadowResources(D3D12Gpu& gpu, uint8_t id)
{
    ShadowResources shadowResources;

    const DXGI_FORMAT format = DXGI_FORMAT_D32_FLOAT;
    const D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    auto desc = D3D12Basics::CreateTexture2DDesc(g_shadowMapResolution.m_width, g_shadowMapResolution.m_height,
                                                 format, flags);
    constexpr size_t halfFloatSizeBytes = 2;
    const size_t sizeBytes = g_shadowMapResolution.m_width * g_shadowMapResolution.m_height * halfFloatSizeBytes;
    constexpr D3D12_CLEAR_VALUE clearValue = { format,{ 1.0f, 0.0f, 0.0f, 0.0f } };
    std::wstringstream debugName;
    debugName << L"Shadow map texture " << id;
    shadowResources.m_shadowTexture.m_memHandle = gpu.AllocateStaticMemory(desc, D3D12_RESOURCE_STATE_DEPTH_WRITE,
                                                                           &clearValue, debugName.str().c_str());

    desc.Format = DXGI_FORMAT_R32_FLOAT;
    desc.Flags = D3D12_RESOURCE_FLAG_NONE;
    shadowResources.m_shadowTexture.m_srv = gpu.CreateTextureView(shadowResources.m_shadowTexture.m_memHandle, desc);
    assert(shadowResources.m_shadowTexture.m_srv.IsValid());

    shadowResources.m_shadowTexture.m_dsv = gpu.CreateDepthStencilView(shadowResources.m_shadowTexture.m_memHandle, DXGI_FORMAT_D32_FLOAT);
    assert(shadowResources.m_shadowTexture.m_dsv.IsValid());

    // TODO check for handle validness???
    shadowResources.m_shadowTextureDSVCPUHandle = m_gpu.GetViewCPUHandle(shadowResources.m_shadowTexture.m_dsv);

    return shadowResources;
}