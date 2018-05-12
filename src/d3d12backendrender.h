#pragma once

// c++ includes
#include <unordered_map>

// project includes
#include "scene.h"
#include "d3d12gpu.h"
#include "d3d12basicsfwd.h"

namespace D3D12Render
{
    // TODO split this in two classes: Render and MeshRenderer
    class D3D12BackendRender
    {
    public:
        D3D12BackendRender(const D3D12Basics::Scene& scene, bool isWaitableForPresentEnabled);

        ~D3D12BackendRender();

        // Window support
        const D3D12Basics::Resolution& GetSafestResolutionSupported() const;

        void SetOutputWindow(HWND hwnd);

        void OnToggleFullScreen();

        void OnResize(const D3D12Basics::Resolution& resolution);

        // Frame processing stages
        void LoadSceneResources(D3D12Basics::SceneLoader& sceneLoader);

        void UpdateSceneResources();

        void BeginFrame();

        void RenderFrame();

        void EndFrame();

    private:
        const D3D12Basics::Scene& m_scene;

        // TODO material needs to be destroyed after gpu. fix this.
        D3D12MaterialPtr m_material;

        D3D12Gpu m_gpu;

        D3D12_VIEWPORT  m_defaultViewport;
        RECT            m_defaultScissorRect;

        std::vector<D3D12GpuRenderTask> m_renderTasks;

        std::unordered_map<std::wstring, D3D12GpuMemoryView> m_textureCache;

        D3D12ImGuiPtr m_imgui;

        void UpdateDefaultNotPSOState();

        D3D12GpuMemoryView CreateTexture(const std::wstring& textureFile, D3D12Basics::SceneLoader& sceneLoader);
    };
}