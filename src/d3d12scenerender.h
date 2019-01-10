#pragma once

// c++ includes
#include <unordered_map>
#include <atomic>

// project includes
#include "scene.h"
#include "d3d12gpu.h"
#include "filemonitor.h"
#include "d3d12pipelinestate.h"

namespace D3D12Basics
{
    struct SceneStats
    {
        uint32_t m_shadowPassDrawCallsCount;
        uint32_t m_forwardPassDrawCallsCount;

        StopClock m_shadowPassCmdListTime;
        StopClock m_forwardPassCmdListTime;
    };

    class D3D12SceneRender
    {
    public:
        D3D12SceneRender(D3D12Gpu& gpu, FileMonitor& fileMonitor, const Scene& scene,
                         const TextureDataCache& textureDataCache,
                         const MeshDataCache& meshDataCache);

        bool AreGpuResourcesLoaded() const { return m_gpuResourcesLoaded; }

        void LoadGpuResources();

        void Update();

        void RecordCmdList(ID3D12GraphicsCommandListPtr cmdList, 
                            D3D12_CPU_DESCRIPTOR_HANDLE renderTarget,
                            D3D12_CPU_DESCRIPTOR_HANDLE depthStencilBuffer);

        const SceneStats& GetStats() const { return m_sceneStats; }

    private:
        enum class PipelineStateId
        {
            StdMaterial,
            DefaultMaterial,
            DefaultMaterial_FixedColor,
            DefaultMaterial_FixedColorNoShadows
        };

        struct GPUMesh
        {
            // TODO lights count
            D3D12Bindings               m_shadowPassBindings[2];
            D3D12Bindings               m_forwardPassBindings;
            D3D12GpuMemoryHandle        m_vertexBuffer;
            D3D12GpuMemoryHandle        m_indexBuffer;
            size_t m_vertexBufferSizeBytes;
            size_t m_vertexSizeBytes;
            size_t m_indexBufferSizeBytes;
            size_t m_indicesCount;

            // TODO think a better place for these. convenient for now.
            D3D12GpuMemoryHandle    m_materialGpuMemHandle;
            D3D12GpuMemoryHandle    m_forwardTransformsGpuMemHandle;
            D3D12GpuMemoryHandle    m_shadowsTransformGpuMemHandles[2];

            // TODO find a generalized way of setting up pipestates
            PipelineStateId m_pipelineStateId;
        };

        struct ShadowResources
        {
            GpuTexture                  m_shadowTexture;
            D3D12_CPU_DESCRIPTOR_HANDLE m_shadowTextureDSVCPUHandle;
        };

        D3D12Gpu& m_gpu;

        const Scene& m_scene;
        const TextureDataCache& m_textureDataCache;
        const MeshDataCache&           m_meshDataCache;

        D3D12PipelineState m_stdMaterialPipeState;
        D3D12PipelineState m_defaultMaterialPipeState;
        D3D12PipelineState m_defaultMaterialFixedColorPipeState;
        D3D12PipelineState m_defaultMaterialFixedColorNoShadowsPipeState;
        D3D12PipelineState m_shadowPipeState;
        D3D12PipelineState m_shadowDebugPipeState;

        D3D12GpuViewHandle m_defaultTexture;
        D3D12GpuViewHandle m_nullTexture;

        std::unordered_map<std::wstring, D3D12GpuViewHandle> m_textureCache;
        std::unordered_map<size_t, GPUMesh> m_gpuMeshCache;

        bool m_gpuResourcesLoaded;
        
        std::vector<ShadowResources> m_shadowResPerLight;

        D3D12GpuMemoryHandle        m_quadVb;
        D3D12GpuMemoryHandle        m_quadIb;

        SceneStats m_sceneStats;

        D3D12GpuViewHandle CreateTexture(const std::wstring& textureFile);

        void CreateDebugResources();

        void RenderShadowPass(ID3D12GraphicsCommandListPtr cmdList);

        void RenderForwardPass(ID3D12GraphicsCommandListPtr cmdList,
                               D3D12_CPU_DESCRIPTOR_HANDLE renderTarget,
                               D3D12_CPU_DESCRIPTOR_HANDLE depthStencilBuffer);

        void RenderDebug(ID3D12GraphicsCommandListPtr cmdList);

        ShadowResources CreateShadowResources(D3D12Gpu& gpu, uint8_t id);
    };
}