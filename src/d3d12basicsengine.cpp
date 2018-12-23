#include "d3d12basicsengine.h"

// c++ includes
#include <cassert>
#include <sstream>
#include <filesystem>
#include <iostream>

// project includes
#include "meshgenerator.h"
#include "d3d12scenerender.h"
#include "d3d12imgui.h"
#include "d3d12utils.h"

// thirdparty libraries include
#include "imgui/imgui.h"

using namespace D3D12Basics;

#define ENABLE_IMGUI (1)

namespace 
{
    static const float g_showSceneLoadedUITime = 5.0f;
}

D3D12BasicsEngine::D3D12BasicsEngine(const Settings& settings, 
                                     Scene&& scene)   : m_gpu(settings.m_isWaitableForPresentEnabled),
                                                        m_sceneLoadingDone(false), m_quit(false), 
                                                        m_scene(std::move(scene)), 
                                                        m_beginToEndTimer(10), m_endToEndTimer(10),
                                                        m_fileMonitor(L"./data")
{
    m_window = std::make_unique<CustomWindow>(m_gpu.GetSafestResolutionSupported());
    assert(m_window);

    m_gpu.SetOutputWindow(m_window->GetHWND());

    m_sceneRender = std::make_unique<D3D12SceneRender>(m_gpu, m_fileMonitor, m_scene, m_textureDataCache, m_meshDataCache);
    assert(m_sceneRender);

    m_inputController = std::make_unique<InputController>(m_window->GetHWND());
    assert(m_inputController);

    m_cameraController = std::make_unique<CameraController>(*m_inputController);
    assert(m_cameraController);

    m_appController = std::make_unique<AppController>(*m_inputController);
    assert(m_appController);

#if ENABLE_IMGUI
    m_imgui = std::make_unique<D3D12ImGui>(m_gpu, m_fileMonitor);
    assert(m_imgui);
#endif

    CreateDepthBuffer();

    // NOTE: delaying load scene to the last thing to avoid concurrent issues when creating gpu memory.
    // TODO: find a proper solution for this when working on concurrent cmd lists
    LoadSceneData(settings.m_dataWorkingPath);
}

D3D12BasicsEngine::~D3D12BasicsEngine()
{
    // NOTE: thread is still executing but it has not being joined yet
    // this mean thrad has to be joined before destruction
    if (m_sceneLoaderThread.joinable())
        m_sceneLoaderThread.join();

    // NOTE: Wait for all pending command lists to be done. This is done before
    // any resource (ie, pipeline state) is freed so there arent any
    // concurrency issues.
    m_gpu.WaitAll();
}

void D3D12BasicsEngine::BeginFrame()
{
    m_beginToEndDeltaTime = m_beginToEndTimer.ElapsedAvgTime();

    m_beginToEndTimer.Mark();

    m_deltaTime = m_endToEndTimer.ElapsedTime();
    m_totalTime = m_endToEndTimer.TotalTime();

    ProcessWindowEvents();

    ProcessUserEvents();

#if ENABLE_IMGUI
    m_imgui->BeginFrame();
#endif
}

void D3D12BasicsEngine::RunFrame(void(*UpdateScene)(Scene& scene, float totalTime))
{
    if (m_sceneLoadingDone)
    {
        if (!m_sceneRender->AreGpuResourcesLoaded())
        {
            // Note This calls blocks
            m_sceneRender->LoadGpuResources();

            m_sceneLoadedUITimer.Reset();
        }

        assert(m_sceneRender->AreGpuResourcesLoaded());

        m_sceneRender->Update();

        UpdateScene(m_scene, m_totalTime);
    }

#if ENABLE_IMGUI
    ShowSceneLoadUI();

    ShowStats();
#endif

    RenderFrame();
}

void D3D12BasicsEngine::EndFrame()
{
    m_gpu.PresentFrame();

    m_beginToEndTimer.Mark();
    m_endToEndTimer.Mark();
}

void D3D12BasicsEngine::ProcessWindowEvents()
{
    if (m_window->HasFullscreenChanged())
        m_gpu.OnToggleFullScreen();

    if (m_window->HasResolutionChanged())
    {
        m_gpu.OnResize(m_window->GetResolution());
        CreateDepthBuffer();
    }

    m_window->ResetWndProcEventsState();
}

void D3D12BasicsEngine::ProcessUserEvents()
{
    m_inputController->Update();
    m_cameraController->Update(m_scene.m_camera, m_deltaTime, m_totalTime);
    m_appController->Update(*m_window, m_quit);
}

void D3D12BasicsEngine::LoadSceneData(const std::wstring& dataWorkingPath)
{
    m_sceneLoaderThread = std::thread([&]()
    {
        SceneLoader sceneLoader(m_scene.m_sceneFile, m_scene, dataWorkingPath);

        for (const auto& model : m_scene.m_models)
        {
            if (!model.m_material.m_diffuseTexture.empty())
                m_textureDataCache[model.m_material.m_diffuseTexture] = std::move(sceneLoader.LoadTextureData(model.m_material.m_diffuseTexture));

            if (!model.m_material.m_normalsTexture.empty())
                m_textureDataCache[model.m_material.m_normalsTexture] = std::move(sceneLoader.LoadTextureData(model.m_material.m_normalsTexture));

            if (!model.m_material.m_specularTexture.empty())
                m_textureDataCache[model.m_material.m_specularTexture] = std::move(sceneLoader.LoadTextureData(model.m_material.m_specularTexture));

            MeshData meshData;
            switch (model.m_type)
            {
            case Model::Type::Cube:
                meshData = CreateCube(VertexDesc{ true, true, true }, model.m_uvScaleOffset);
                break;
            case Model::Type::Plane:
                meshData = CreatePlane(VertexDesc{ true, true, true }, model.m_uvScaleOffset);
                break;
            case Model::Type::Sphere:
                meshData = CreateSphere(VertexDesc{ true, true, true }, model.m_uvScaleOffset, 40, 40);
                break;
            case Model::Type::MeshFile:
            {
                meshData = sceneLoader.LoadMesh(model.m_id);
                break;
            }
            default:
                assert(false);
            }

            m_meshDataCache[model.m_id] = std::move(meshData);
        }

        m_sceneLoadingDone = true;
    });
}

void D3D12BasicsEngine::ShowSceneLoadUI()
{
    std::string loadUIStr = "Scene loading!";
    if (m_sceneRender->AreGpuResourcesLoaded())
    {
        if (m_sceneLoadedUITimer.TotalTime() > g_showSceneLoadedUITime)
            return;
        loadUIStr = "Scene loaded!";
    }

    const auto& resolution = m_gpu.GetCurrentResolution();
    ImVec2 window_pos = ImVec2( resolution.m_width / 2.0f, resolution.m_height / 2.0f);
    ImVec2 window_pos_pivot = ImVec2( 0.0f, 0.0f);
    ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always, window_pos_pivot);
    ImGui::SetNextWindowBgAlpha(0.3f); // Transparent background
    const ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove |
                                            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                            ImGuiWindowFlags_NoNav;

    ImGui::Begin("SceneLoadedUI", nullptr, windowFlags);
    ImGui::Text(loadUIStr.c_str());
    ImGui::End();
}

void D3D12BasicsEngine::ShowStats()
{
    const float DISTANCE = 10.0f;
    static int corner = 0;
    const auto& resolution = m_gpu.GetCurrentResolution();
    ImVec2 window_pos = ImVec2((corner & 1) ? resolution.m_width - DISTANCE : DISTANCE,
                               (corner & 2) ? resolution.m_height - DISTANCE : DISTANCE);
    ImVec2 window_pos_pivot = ImVec2((corner & 1) ? 1.0f : 0.0f, (corner & 2) ? 1.0f : 0.0f);
    ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always, window_pos_pivot);
    ImGui::SetNextWindowBgAlpha(0.3f); // Transparent background
    const ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                         ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove |
                                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                         ImGuiWindowFlags_NoNav;

    auto frameStats = m_gpu.GetFrameStats();
    auto sceneStats = m_sceneRender->GetStats();

    ImGui::Begin("Example: Fixed Overlay", nullptr, windowFlags);
    ImGui::Text("CPU: begin to end %.3f ms", m_beginToEndDeltaTime * 1000.0f);
    ImGui::Text("CPU: end to end %.3f ms", m_deltaTime * 1000.0f);
    ImGui::Text("CPU: present %.3f ms", frameStats.m_presentTime * 1000.0f);
    ImGui::Text("CPU: waitfor present %.3f ms", frameStats.m_waitForPresentTime * 1000.0f);
    ImGui::Text("CPU: waitfor fence %.3f ms", frameStats.m_waitForFenceTime * 1000.0f);
    ImGui::Text("CPU: frame time %.3f ms", frameStats.m_frameTime * 1000.0f);
    ImGui::Text("GPU: cmdlist time %.3f ms", frameStats.m_cmdListTime * 1000.0f);
    ImGui::Text("# draw calls: shadow pass %d", sceneStats.m_shadowPassDrawCallsCount);
    ImGui::Text("# draw calls: forward pass %d", sceneStats.m_forwardPassDrawCallsCount);
    ImGui::End();
}

void D3D12BasicsEngine::RenderFrame()
{
#if ENABLE_IMGUI
    ImGui::Render();
#endif
    auto cmdList = m_gpu.StartCurrentCmdList();

    auto presentToRT = m_gpu.SwapChainTransition(Present_To_RenderTarget);
    cmdList->ResourceBarrier(1, &presentToRT);

    const auto& depthBufferViewHandle = m_gpu.GetViewCPUHandle(m_depthBuffer.m_dsv);
    const auto& backbufferRT = m_gpu.SwapChainBackBufferViewHandle();

    // Record command lists: scene and imgui
    {
        m_sceneRender->RecordCmdList(cmdList, backbufferRT, depthBufferViewHandle);

        // IMGUI
#if ENABLE_IMGUI
        m_imgui->EndFrame(cmdList);
#endif
    }

    auto rtToPresent = m_gpu.SwapChainTransition(RenderTarget_To_Present);
    cmdList->ResourceBarrier(1, &rtToPresent);

    m_gpu.EndCurrentCmdList();

    m_gpu.ExecuteCurrentCmdList();
}

void D3D12BasicsEngine::CreateDepthBuffer()
{
    if (m_depthBuffer.m_memHandle.IsValid())
        m_gpu.FreeMemory(m_depthBuffer.m_memHandle);

    const auto& resolution = m_gpu.GetCurrentResolution();

    const D3D12_RESOURCE_DESC desc = D3D12Basics::CreateTexture2DDesc(resolution.m_width, resolution.m_height, 
                                                                      DXGI_FORMAT_D24_UNORM_S8_UINT,
                                                                      D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
    const D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_DEPTH_WRITE;

    D3D12_CLEAR_VALUE optimizedClearValue;
    optimizedClearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    optimizedClearValue.DepthStencil = { 1.0f, 0x0 };

    std::wstringstream sstream;
    sstream << "Depth Buffer - " << resolution.m_width << "x" << resolution.m_height;

    const size_t sizeBytes = resolution.m_width * resolution.m_height * 4;

    m_depthBuffer.m_memHandle = m_gpu.AllocateStaticMemory(desc, initialState, &optimizedClearValue, sstream.str());
    assert(m_depthBuffer.m_memHandle.IsValid());

    m_depthBuffer.m_dsv = m_gpu.CreateDepthStencilView(m_depthBuffer.m_memHandle, DXGI_FORMAT_D24_UNORM_S8_UINT);
    assert(m_depthBuffer.m_dsv.IsValid());
}