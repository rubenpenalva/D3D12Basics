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

#define LOAD_SCENE (1)

using namespace D3D12Basics;

namespace 
{
    static const float g_showSceneLoadedUITime = 5.0f;

    void ShowSplitTimesUI(const char* text, const StopClock::SplitTimeArray& splitTimesArray, float lastSplitTime, bool plotEnabled = true)
    {
        ImGui::Text((text + std::string(" %.3f ms")).c_str(), lastSplitTime * 1000.0f);
        ImGui::NextColumn();

        if (!plotEnabled)
            ImGui::Text("No split times");
        else
            ImGui::PlotHistogram("", &splitTimesArray[0], static_cast<int>(splitTimesArray.size()));

        ImGui::NextColumn();
    }
}

D3D12BasicsEngine::D3D12BasicsEngine(const Settings& settings, 
                                     Scene&& scene)   : m_gpu(settings.m_isWaitableForPresentEnabled),
                                                        m_sceneLoadingDone(false), m_quit(false), 
                                                        m_scene(std::move(scene)), 
                                                        m_fileMonitor(L"./data")
{
    m_window = std::make_unique<CustomWindow>(m_gpu.GetSafestResolutionSupported());
    assert(m_window);

    m_gpu.SetOutputWindow(m_window->GetHWND());

    m_sceneRender = std::make_unique<D3D12SceneRender>(m_gpu, m_fileMonitor, m_scene, m_textureDataCache, m_meshDataCache);
    assert(m_sceneRender);

    m_cameraController = std::make_unique<CameraController>();
    assert(m_cameraController);

    m_appController = std::make_unique<AppController>();
    assert(m_appController);

    m_imgui = std::make_unique<D3D12ImGui>(m_window->GetHWND(), m_gpu, m_fileMonitor);
    assert(m_imgui);

    CreateDepthBuffer();

#if LOAD_SCENE
    // NOTE: delaying load scene to the last thing to avoid concurrent issues when creating gpu memory.
    // TODO: #6. find a proper solution for this when working on concurrent cmd lists
    LoadSceneData(settings.m_dataWorkingPath);
#endif
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
    m_beginToEndClock.ResetMark();

    m_cachedDeltaTime = m_endToEndClock.LastSplitTime();
    m_cachedTotalTime = m_totalTime.Value();

    ProcessWindowEvents();

    m_imgui->ProcessInput();

    ProcessUserEvents();

    m_imgui->BeginFrame(m_gpu.GetCurrentResolution());
}

void D3D12BasicsEngine::RunFrame(void(*UpdateScene)(Scene& scene, float totalTime))
{
    if (m_sceneLoadingDone)
    {
        if (!m_sceneRender->AreGpuResourcesLoaded())
        {
            // Note This calls blocks
            m_sceneRender->LoadGpuResources();
            
            m_sceneLoadedUIStart.Reset();
        }

        assert(m_sceneRender->AreGpuResourcesLoaded());

        m_sceneRender->Update();

        UpdateScene(m_scene, m_cachedTotalTime);
    }

    ShowSceneLoadUI();

    ShowStats();

    RenderFrame();
}

void D3D12BasicsEngine::EndFrame()
{
    m_gpu.PresentFrame();

    m_beginToEndClock.Mark();
    m_endToEndClock.Mark();
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
    m_cameraController->Update(m_scene.m_camera, m_cachedDeltaTime, m_cachedTotalTime);
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
        if (m_sceneLoadedUIStart.Value() > g_showSceneLoadedUITime)
            return;

        loadUIStr = "Scene loaded!";
    }

    const auto& resolution = m_gpu.GetCurrentResolution();
    ImVec2 window_pos = ImVec2( resolution.m_width / 2.0f, resolution.m_height / 2.0f);
    ImVec2 window_pos_pivot = ImVec2( 0.0f, 0.0f);
    ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always, window_pos_pivot);
    ImGui::SetNextWindowBgAlpha(0.3f); // Transparent background
    const ImGuiWindowFlags windowFlags =    ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
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
    ImVec2 window_pos = ImVec2(DISTANCE, DISTANCE);
    ImVec2 window_pos_pivot = ImVec2(0.0f, 0.0f);
    ImGui::SetNextWindowSize({ 700, 400 });
    ImGui::SetNextWindowBgAlpha(0.3f); // Transparent background
    const ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                         ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove |
                                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                         ImGuiWindowFlags_NoNav;

    const auto& frameStats = m_gpu.GetFrameStats();
    auto sceneStats = m_sceneRender->GetStats();
  
    ImGui::Begin("", nullptr, windowFlags);
    static bool pausePlots = false;
    ImGui::SameLine();
    bool pausePlotsOld = pausePlots;
    ImGui::Checkbox("Pause plots", &pausePlots);
    if (pausePlots && pausePlotsOld != pausePlots)
    {
        m_cachedStats.m_endToEndTime = m_endToEndClock.Values();
        m_cachedStats.m_beginToEndTime = m_beginToEndClock.Values();
        m_cachedStats.m_frameStats = frameStats;
        m_cachedStats.m_sceneStats = sceneStats;
        m_cachedStats.m_enabled = true;
    }
    else if (!pausePlots)
    {
        m_cachedStats.m_enabled = false;
    }

    ImGui::Columns(2, "");

    ShowSplitTimesUI("CPU: begin to end", 
                      m_cachedStats.m_enabled?  m_cachedStats.m_beginToEndTime : 
                                                m_beginToEndClock.Values(), 
                      m_beginToEndClock.LastSplitTime());
    ShowSplitTimesUI("CPU: end to end",
                      m_cachedStats.m_enabled ? m_cachedStats.m_endToEndTime : 
                                                m_endToEndClock.Values(),
                      m_endToEndClock.LastSplitTime());
    ShowSplitTimesUI("CPU: present", 
                      m_cachedStats.m_enabled ? m_cachedStats.m_frameStats.m_presentTime : 
                                                frameStats.m_presentTime.Values(),
                      frameStats.m_presentTime.LastSplitTime());
    ShowSplitTimesUI("CPU: waitfor present", 
                      m_cachedStats.m_enabled ? m_cachedStats.m_frameStats.m_waitForPresentTime :
                                                frameStats.m_waitForPresentTime.Values(),
                      frameStats.m_waitForPresentTime.LastSplitTime());
    ShowSplitTimesUI("CPU: waitfor fence", 
                      m_cachedStats.m_enabled ? m_cachedStats.m_frameStats.m_waitForFenceTime : 
                                                frameStats.m_waitForFenceTime.Values(),
                      frameStats.m_waitForFenceTime.LastSplitTime());
    ShowSplitTimesUI("CPU: frame time", 
                      m_cachedStats.m_enabled ? m_cachedStats.m_frameStats.m_frameTime :
                                                frameStats.m_frameTime.Values(),
                      frameStats.m_frameTime.LastSplitTime());
    ShowSplitTimesUI("CPU: shadow pass cmd list time", 
                      m_cachedStats.m_enabled ? m_cachedStats.m_sceneStats.m_shadowPassCmdListTime : 
                                                sceneStats.m_shadowPassCmdListTime.Values(),
                      sceneStats.m_shadowPassCmdListTime.LastSplitTime());
    ShowSplitTimesUI("CPU: forward pass cmd list time",
                      m_cachedStats.m_enabled ? m_cachedStats.m_sceneStats.m_forwardPassCmdListTime : 
                                                sceneStats.m_forwardPassCmdListTime.Values(),
                      sceneStats.m_forwardPassCmdListTime.LastSplitTime());
    
    const size_t cmdListTimesCount = frameStats.m_cmdListTimes.size();
    assert(!m_cachedStats.m_enabled ||
            (m_cachedStats.m_enabled && cmdListTimesCount == m_cachedStats.m_frameStats.m_cmdListTimes.size()));
    for (size_t i = 0; i < cmdListTimesCount; ++i)
    {
        ShowSplitTimesUI(("GPU: " + ConvertFromUTF16ToUTF8(frameStats.m_cmdListTimes[i]->first)).c_str(),
                         m_cachedStats.m_enabled ? m_cachedStats.m_frameStats.m_cmdListTimes[i] :
                         frameStats.m_cmdListTimes[i]->second.Values(),
                         frameStats.m_cmdListTimes[i]->second.LastValue());
    }
    ImGui::Text("CPU: delta time %.3f ms", m_cachedDeltaTime);
    ImGui::Text("# draw calls: shadow pass %d", sceneStats.m_shadowPassDrawCallsCount);
    ImGui::Text("# draw calls: forward pass %d", sceneStats.m_forwardPassDrawCallsCount);
    ImGui::End();
}

void D3D12BasicsEngine::RenderFrame()
{
    ImGui::Render();

    const auto& depthBufferViewHandle = m_gpu.GetViewCPUHandle(m_depthBuffer.m_dsv);
    const auto& backbufferRT = m_gpu.SwapChainBackBufferViewHandle();

    auto cmdLists = m_sceneRender->RecordCmdLists(backbufferRT, depthBufferViewHandle);
    auto imguiCmdList = m_imgui->EndFrame(backbufferRT, depthBufferViewHandle);
    assert(imguiCmdList);
    cmdLists.push_back(imguiCmdList);
    m_gpu.ExecuteCmdLists(cmdLists);
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