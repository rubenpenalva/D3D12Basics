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

// thirdparty libraries include
#include "enkiTS/src/TaskScheduler.h"

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

        // TODO really hate this stats code.
        struct CachedFrameStats
        {
            StopClock::SplitTimeBuffer               m_presentTime;
            StopClock::SplitTimeBuffer               m_waitForPresentTime;
            StopClock::SplitTimeBuffer               m_waitForFenceTime;
            StopClock::SplitTimeBuffer               m_frameTime;
            std::vector<StopClock::SplitTimeBuffer>  m_cmdListTimes;

            CachedFrameStats& operator=(const FrameStats& frameStats)
            {
                m_presentTime = frameStats.m_presentTime.SplitTimes();
                m_waitForPresentTime = frameStats.m_waitForPresentTime.SplitTimes();
                m_waitForFenceTime = frameStats.m_waitForFenceTime.SplitTimes();
                m_frameTime = frameStats.m_frameTime.SplitTimes();
                size_t cmdListTimesCount = frameStats.m_cmdListTimes.size();
                m_cmdListTimes.clear();
                m_cmdListTimes.resize(cmdListTimesCount);
                size_t i = 0;
                for (auto& cmdListTime : frameStats.m_cmdListTimes)
                {
                    m_cmdListTimes[i] = *cmdListTime.second;
                    ++i;
                }

                return *this;
            }
        };

        struct CachedSceneStats
        {
            StopClock::SplitTimeBuffer m_shadowPassCmdListTime;
            StopClock::SplitTimeBuffer m_forwardPassCmdListTime;
            StopClock::SplitTimeBuffer m_cmdListsTime;

            CachedSceneStats& operator=(const SceneStats& sceneStats)
            {
                m_shadowPassCmdListTime = sceneStats.m_shadowPassCmdListTime.SplitTimes();
                m_forwardPassCmdListTime = sceneStats.m_forwardPassCmdListTime.SplitTimes();
                m_cmdListsTime = sceneStats.m_cmdListsTime.SplitTimes();

                return *this;
            }
        };

        struct CachedStats
        {
            StopClock::SplitTimeBuffer    m_beginToEndTime;
            StopClock::SplitTimeBuffer    m_endToEndTime;
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

        D3D12GraphicsCmdListPtr m_preCmdList;
        D3D12GraphicsCmdListPtr m_postCmdList;

        Scene                                           m_scene;
        std::thread                                     m_sceneLoaderThread;
        std::atomic<bool>                               m_sceneLoadingDone;
        float                                           m_sceneLoadingTime;
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

        enki::TaskScheduler m_taskScheduler;

        bool m_enableParallelCmdsLits;

        int m_drawCallsCount;

        void ProcessWindowEvents();

        void ProcessUserEvents();

        void LoadSceneData(const std::wstring& dataWorkingPath);

        void ShowSceneLoadUI();

        void ShowMainUI();

        void RenderFrame();

        void CreateDepthBuffer();

        void SetupCmdLists();

        void SceneLoaded();
    };
}