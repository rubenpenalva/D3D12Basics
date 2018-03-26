#pragma once

// project includes
#include "scene.h"
#include "d3d12gpu.h"

namespace D3D12Render
{
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

        void RenderFrame();

        void FinishFrame();

    private:
        const D3D12Basics::Scene& m_scene;

        D3D12Gpu m_gpu;

        D3D12_VIEWPORT  m_defaultViewport;
        RECT            m_defaultScissorRect;

        std::vector<D3D12GpuRenderTask> m_renderTasks;

        std::unordered_map<std::wstring, D3D12ResourceID> m_textureCache;

        void UpdateDefaultNotPSOState();

        D3D12ResourceID CreateTexture(const std::wstring& textureFile, D3D12Basics::SceneLoader& sceneLoader);
    };
}