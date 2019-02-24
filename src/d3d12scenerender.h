#pragma once

// c++ includes
#include <unordered_map>
#include <atomic>

// project includes
#include "scene.h"
#include "d3d12gpu.h"
#include "filemonitor.h"
#include "d3d12pipelinestate.h"

// thirdparty libraries include
#include "imgui/imgui.h"
#include "enkiTS/src/TaskScheduler.h"

namespace D3D12Basics
{
    struct SceneStats
    {
        float m_loadingGPUResourcesTime = 0.0f;

        uint32_t m_shadowPassDrawCallsCount;
        uint32_t m_forwardPassDrawCallsCount;

        StopClock m_shadowPassCmdListTime;
        StopClock m_forwardPassCmdListTime;
        StopClock m_cmdListsTime;
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

        D3D12CmdLists RecordCmdLists(D3D12_CPU_DESCRIPTOR_HANDLE renderTarget,
                                     D3D12_CPU_DESCRIPTOR_HANDLE depthStencilBuffer,
                                     enki::TaskScheduler& taskScheduler,
                                     bool enableParallelCmdLists,
                                     size_t drawCallsCount);

        const SceneStats& GetStats() const { return m_sceneStats; }

        size_t GpuMeshesCount() const { return m_gpuMeshCache.size(); }

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
        const MeshDataCache&    m_meshDataCache;

        D3D12PipelineState m_stdMaterialPipeState;
        D3D12PipelineState m_defaultMaterialPipeState;
        D3D12PipelineState m_defaultMaterialFixedColorPipeState;
        D3D12PipelineState m_defaultMaterialFixedColorNoShadowsPipeState;
        D3D12PipelineState m_shadowPipeState;
        D3D12PipelineState m_shadowDebugPipeState;

        D3D12GpuViewHandle m_defaultTexture;
        D3D12GpuViewHandle m_nullTexture;

        std::unordered_map<std::wstring, D3D12GpuViewHandle> m_textureCache;
        std::unordered_map<size_t, size_t>  m_gpuMeshCache;
        std::vector<GPUMesh>                m_gpuMeshes;

        bool m_gpuResourcesLoaded;
        
        std::vector<ShadowResources> m_shadowResPerLight;

        D3D12GpuMemoryHandle        m_quadVb;
        D3D12GpuMemoryHandle        m_quadIb;

        std::vector<D3D12GraphicsCmdListPtr> m_forwardCmdLists;
        std::vector<D3D12GraphicsCmdListPtr> m_shadowCmdLists;
        
        SceneStats m_sceneStats;

        size_t m_lastDrawCallsCount;

        unsigned int m_shadowPassBinderOffset;
        unsigned int m_forwardPassBinderOffset;

        std::vector<TaskSetPtr> m_renderTasks;

        std::atomic<uint32_t> m_shadowPassDrawCallsCount;
        std::atomic<uint32_t> m_forwardPassDrawCallsCount;

        D3D12GpuViewHandle CreateTexture(const std::wstring& textureFile);

        void CreateDebugResources();

        void SetupRenderDepthFromLight(ID3D12GraphicsCommandListPtr cmdList, size_t lightIndex, bool clear = true);

        void RenderDepthFromLight(ID3D12GraphicsCommandListPtr cmdList, size_t lightIndex,
                                  size_t meshStartIndex, size_t meshEndIndex,
                                  unsigned int concurrentBinderIndex);

        TaskSetPtr CreateRenderDepthFromLightTask(size_t lightIndex, size_t cmdListStartIndex,
                                                  size_t cmdListEndIndex, size_t drawCallsCount);

        bool RenderShadowPass(enki::TaskScheduler& taskScheduler, size_t drawCallsCount, bool enableParallelCmdLists);

        void RenderForwardPassMeshRange(const D3D12GraphicsCmdListPtr& d3d12CmdList,
                                        D3D12_CPU_DESCRIPTOR_HANDLE renderTarget,
                                        D3D12_CPU_DESCRIPTOR_HANDLE depthStencilBuffer,
                                        size_t meshStartIndex, size_t meshEndIndex,
                                        unsigned int concurrentBinderIndex);

        TaskSetPtr CreateForwardPassTask(D3D12_CPU_DESCRIPTOR_HANDLE renderTarget,
                                         D3D12_CPU_DESCRIPTOR_HANDLE depthStencilBuffer,
                                         size_t drawCallsCount);

        void RenderForwardPass(enki::TaskScheduler& taskScheduler, 
                               D3D12_CPU_DESCRIPTOR_HANDLE renderTarget,
                               D3D12_CPU_DESCRIPTOR_HANDLE depthStencilBuffer,
                               size_t drawCallsCount,
                               bool enableParallelCmdLists);

        void UpdateCmdLists(size_t drawCallsCount, bool enableParallelCmdLists);

        void ResetCmdLists(unsigned int concurrentBinders);

        void AddShadowResourcesBarrier(ID3D12GraphicsCommandListPtr cmdList, 
                                       D3D12_RESOURCE_STATES stateBefore,
                                       D3D12_RESOURCE_STATES stateAfter);

        size_t CalculateCmdListsCount(size_t drawCallsCount);

        void RenderDebug(ID3D12GraphicsCommandListPtr cmdList);

        ShadowResources CreateShadowResources(D3D12Gpu& gpu, uint8_t id);
    };
}