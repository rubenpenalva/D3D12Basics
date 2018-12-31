#pragma once

// project includes
#include "d3d12gpu.h"
#include "utils.h"
#include "scene.h"
#include "d3d12fwd.h"
#include "filemonitor.h"
#include "d3d12scenerender.h"

// c++ includes
#include <atomic>
#include <string>
#include <thread>
#include <mutex>

namespace D3D12Basics
{
    // NOTE once the scene is passed to the engine, it stays the same. 
    // No adding or removing objects for now.
    class D3D12BasicsEngine
    {
    public:
        struct Settings
        {
            bool m_isWaitableForPresentEnabled = false;
            std::wstring m_dataWorkingPath;
        };

        D3D12BasicsEngine(const Settings& settings, Scene&& scene);

        ~D3D12BasicsEngine();

        // After BeginFrame and before EndFrame scene mods and imgui calls
        void BeginFrame();
        void RunFrame(void(*UpdateScene)(Scene& scene, float totalTime));
        void EndFrame();

        // 
        bool HasUserRequestedToQuit() const { return m_quit; }

    private:
        using CameraControllerPtr   = std::unique_ptr<CameraController>;
        using AppControllerPtr      = std::unique_ptr<AppController>;

        struct CachedStats
        {
            SplitTimes<>    m_beginToEndTime;
            SplitTimes<>    m_endToEndTime;
            FrameStats      m_frameStats;
            SceneStats      m_sceneStats;

            bool m_enabled;
        };

        D3D12Gpu        m_gpu;
        CustomWindowPtr m_window;

        SplitTimes<>    m_beginToEndTime;
        SplitTimes<>    m_endToEndTime;
        SplitTimes<1>   m_totalTime;
        float           m_cachedDeltaTime;
        float           m_cachedTotalTime;

        Scene                                           m_scene;
        std::thread                                     m_sceneLoaderThread;
        std::atomic<bool>                               m_sceneLoadingDone;
        TextureDataCache                                m_textureDataCache;
        MeshDataCache                                   m_meshDataCache;

        D3D12SceneRenderPtr m_sceneRender;
        GpuTexture m_depthBuffer;

        CameraControllerPtr m_cameraController;
        AppControllerPtr    m_appController;

        bool m_quit;

        D3D12ImGuiPtr m_imgui;

        SplitTimes<1> m_sceneLoadedUITime;

        FileMonitor m_fileMonitor;

        CachedStats m_cachedStats;

        void ProcessWindowEvents();

        void ProcessUserEvents();

        void LoadSceneData(const std::wstring& dataWorkingPath);

        void ShowSceneLoadUI();

        void ShowStats();

        void RenderFrame();

        void CreateDepthBuffer();
    };
}