#pragma once

// project includes
#include "scene.h"
#include "d3d12gpu.h"

namespace D3D12Render
{
    class D3D12BackendRender
    {
    public:
        D3D12BackendRender(D3D12Basics::Scene& scene, bool isWaitableForPresentEnabled);

        ~D3D12BackendRender();

        // Window support
        const D3D12Basics::Resolution& GetSafestResolutionSupported() const;

        void SetOutputWindow(HWND hwnd);

        void OnToggleFullScreen();

        void OnResize(const D3D12Basics::Resolution& resolution);

        // Frame processing stages
        void LoadSceneResources();

        void UpdateSceneResources();

        void RenderFrame();

        void FinishFrame();

    private:
        D3D12Basics::Scene& m_scene;

        D3D12Gpu m_gpu;

        D3D12_VIEWPORT  m_defaultViewport;
        RECT            m_defaultScissorRect;

        std::vector<D3D12GpuRenderTask> m_renderTasks;

        void UpdateDefaultNotPSOState();
    };
}