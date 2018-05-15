#pragma once

// c++ includes
#include <unordered_map>
#include <atomic>

// project includes
#include "scene.h"
#include "d3d12gpu.h"

namespace D3D12Basics
{
    class D3D12SceneRender
    {
    public:
        D3D12SceneRender(D3D12Gpu& gpu, const Scene& scene,
                         const std::unordered_map<std::wstring, TextureData>& textureDataCache,
                         const std::unordered_map<size_t, MeshData>& meshDataCache);

        ~D3D12SceneRender();

        bool AreGpuResourcesLoaded() const { return m_gpuResourcesLoaded; }

        void LoadGpuResources();

        void Update();

        std::vector<D3D12GpuRenderTask> CreateRenderTasks();

    private:
        struct GPUMesh
        {
            D3D12Bindings m_bindings;
            D3D12GpuMemoryHandle m_vertexBuffer;
            D3D12GpuMemoryHandle m_indexBuffer;
            size_t m_vertexBufferSizeBytes;
            size_t m_vertexSizeBytes;
            size_t m_indexBufferSizeBytes;
            size_t m_indicesCount;
        };
        D3D12Gpu& m_gpu;

        const Scene& m_scene;
        const std::unordered_map<std::wstring, TextureData>&  m_textureDataCache;
        const std::unordered_map<size_t, MeshData>&           m_meshDataCache;

        D3D12MaterialPtr m_material;

        D3D12GpuMemoryView m_defaultTexture;

        std::unordered_map<std::wstring, D3D12GpuMemoryView> m_textureCache;
        std::unordered_map<size_t, GPUMesh> m_gpuMeshCache;

        bool m_gpuResourcesLoaded;

        D3D12GpuMemoryView CreateTexture(const std::wstring& textureFile);
    };
}