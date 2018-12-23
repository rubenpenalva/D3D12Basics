#pragma once

// project includes
#include "d3d12gpu.h"
#include "utils.h"
#include "scene.h"
#include "d3d12fwd.h"
#include "filemonitor.h"

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
        using InputControllerPtr    = std::unique_ptr<InputController>;
        using CameraControllerPtr   = std::unique_ptr<CameraController>;
        using AppControllerPtr      = std::unique_ptr<AppController>;

        D3D12Gpu        m_gpu;
        CustomWindowPtr m_window;

        Timer m_beginToEndTimer;
        Timer m_endToEndTimer;

        Scene                                           m_scene;
        std::thread                                     m_sceneLoaderThread;
        std::atomic<bool>                               m_sceneLoadingDone;
        TextureDataCache                                m_textureDataCache;
        MeshDataCache                                   m_meshDataCache;

        D3D12SceneRenderPtr m_sceneRender;
        GpuTexture m_depthBuffer;

        InputControllerPtr  m_inputController;
        CameraControllerPtr m_cameraController;
        AppControllerPtr    m_appController;

        bool m_quit;
        float m_totalTime;
        float m_deltaTime;
        float m_beginToEndDeltaTime;

        D3D12ImGuiPtr m_imgui;

        Timer m_sceneLoadedUITimer;

        FileMonitor m_fileMonitor;

        void ProcessWindowEvents();

        void ProcessUserEvents();

        void LoadSceneData(const std::wstring& dataWorkingPath);

        void ShowSceneLoadUI();

        void ShowStats();

        void RenderFrame();

        void CreateDepthBuffer();
    };
}