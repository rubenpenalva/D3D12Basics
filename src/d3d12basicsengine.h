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

        struct CachedFrameStats
        {
            StopClock::SplitTimeArray               m_presentTime;
            StopClock::SplitTimeArray               m_waitForPresentTime;
            StopClock::SplitTimeArray               m_waitForFenceTime;
            StopClock::SplitTimeArray               m_frameTime;
            std::vector<StopClock::SplitTimeArray>  m_cmdListTimes;

            CachedFrameStats& operator=(const FrameStats& frameStats)
            {
                m_presentTime = frameStats.m_presentTime.Values();
                m_waitForPresentTime = frameStats.m_waitForPresentTime.Values();
                m_waitForFenceTime = frameStats.m_waitForFenceTime.Values();
                m_frameTime = frameStats.m_frameTime.Values();
                size_t cmdListTimesCount = frameStats.m_cmdListTimes.size();
                m_cmdListTimes.resize(cmdListTimesCount);
                for (size_t i = 0; i < cmdListTimesCount; ++i)
                {
                    m_cmdListTimes[i] = frameStats.m_cmdListTimes[i]->second.Values();
                }

                return *this;
            }
        };

        struct CachedSceneStats
        {
            StopClock::SplitTimeArray m_shadowPassCmdListTime;
            StopClock::SplitTimeArray m_forwardPassCmdListTime;

            CachedSceneStats& operator=(const SceneStats& sceneStats)
            {
                m_shadowPassCmdListTime = sceneStats.m_shadowPassCmdListTime.Values();
                m_forwardPassCmdListTime = sceneStats.m_forwardPassCmdListTime.Values();

                return *this;
            }
        };

        struct CachedStats
        {
            StopClock::SplitTimeArray    m_beginToEndTime;
            StopClock::SplitTimeArray    m_endToEndTime;
            CachedFrameStats        m_frameStats;
            CachedSceneStats        m_sceneStats;

            bool m_enabled;
        };

        D3D12Gpu        m_gpu;
        CustomWindowPtr m_window;

        StopClock       m_beginToEndClock;
        StopClock       m_endToEndClock;
        RunningTime     m_totalTime;
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

        RunningTime m_sceneLoadedUIStart;

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