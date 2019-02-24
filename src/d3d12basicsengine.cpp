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
    static const float g_defaultClearColor[4] = { 0.0f, 0.2f, 0.4f, 1.0f };
    static const float g_shadowMapClearColor[4] = { 0.0f };

    float ImGuiPlotGetter(const void* data, int index)
    {
        const StopClock::SplitTimeBuffer* splitTimeBuffer = (const StopClock::SplitTimeBuffer*)data;
        size_t bufferIndex = StopClock::SplitTimeBuffer::CalculateCircularIndex(splitTimeBuffer->StartIndex() + index);
        return splitTimeBuffer->Values()[bufferIndex];
    }

    void ShowSplitTimesUI(const char* text, const StopClock::SplitTimeBuffer& splitTimeBuffer, 
                          float lastSplitTime, bool plotEnabled = true)
    {
        ImGui::Text((text + std::string(" %.6fms")).c_str(), lastSplitTime * 1000.0f);
        ImGui::NextColumn();

        if (!plotEnabled)
            ImGui::Text("No split times");
        else
        {
            ImGui::PlotHistogram("", &ImGuiPlotGetter, &splitTimeBuffer, 
                                 static_cast<int>(splitTimeBuffer.Values().size()));
        }

        ImGui::NextColumn();
    }

    void ShowTimeUI(const char* text, float time)
    {
        std::string finalText = text;
        finalText += " %.6f";
        if (time < 1.0f)
        {
            finalText += "ms";
            time *= 1000.0f;
        }
        else
        {
            finalText += "s";
        }

        ImGui::Text(finalText.c_str(), time);
    }
}

D3D12BasicsEngine::D3D12BasicsEngine(const Settings& settings, 
                                     Scene&& scene)   : m_gpu(settings.m_isWaitableForPresentEnabled),
                                                        m_sceneLoadingDone(false), m_quit(false), 
                                                        m_scene(std::move(scene)), 
                                                        m_fileMonitor(L"./data"),
                                                        m_enableParallelCmdsLits(false),
                                                        m_sceneLoadingTime(0.0f),
                                                        m_drawCallsCount(0)
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

    m_preCmdList = m_gpu.CreateCmdList(L"Pre render");
    assert(m_preCmdList);

    m_postCmdList = m_gpu.CreateCmdList(L"Post render");
    assert(m_postCmdList);

    m_taskScheduler.Initialize();

    CreateDepthBuffer();

#if LOAD_SCENE
    // NOTE: delaying load scene to the last thing to avoid concurrent issues when creating gpu memory.
    // not pretty but good enough for this project
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

    m_cachedDeltaTime = m_endToEndClock.SplitTimes().LastValue();
    m_cachedTotalTime = m_totalTime.Time();

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

            SceneLoaded();
        }

        assert(m_sceneRender->AreGpuResourcesLoaded());

        m_sceneRender->Update();

        UpdateScene(m_scene, m_cachedTotalTime);
    }

    ShowSceneLoadUI();

    ShowMainUI();

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
        RunningTime loadingTime;

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
        m_sceneLoadingTime = loadingTime.Time();
    });
}

void D3D12BasicsEngine::ShowSceneLoadUI()
{
    std::string loadUIStr = "Scene loading!";
    if (m_sceneRender->AreGpuResourcesLoaded())
    {
        if (m_sceneLoadedUIStart.Time() > g_showSceneLoadedUITime)
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

void D3D12BasicsEngine::ShowMainUI()
{
    const float DISTANCE = 10.0f;
    ImVec2 window_pos = ImVec2(DISTANCE, DISTANCE);
    ImVec2 window_pos_pivot = ImVec2(0.0f, 0.0f);
    ImGui::SetNextWindowSize({ 900, 600 });
    ImGui::SetNextWindowBgAlpha(0.3f); // Transparent background
    const ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                         ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove |
                                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                         ImGuiWindowFlags_NoNav;

    const auto& frameStats = m_gpu.GetFrameStats();
    auto sceneStats = m_sceneRender->GetStats();
  
    ImGui::Begin("", nullptr, windowFlags);
    if (m_sceneLoadingDone)
    {
        ImGui::Checkbox("Enable parallel cmdlists", &m_enableParallelCmdsLits);
        if (m_enableParallelCmdsLits)
            ImGui::SliderInt("Drawcalls per cmdlist", &m_drawCallsCount, 1, 
                              static_cast<int>(m_sceneRender->GpuMeshesCount()));
    }

    static bool pausePlots = false;
    bool pausePlotsOld = pausePlots;
    ImGui::Checkbox("Pause plots", &pausePlots);
    if (pausePlots && pausePlotsOld != pausePlots)
    {
        m_cachedStats.m_endToEndTime = m_endToEndClock.SplitTimes();
        m_cachedStats.m_beginToEndTime = m_beginToEndClock.SplitTimes();
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
                                                m_beginToEndClock.SplitTimes(),
                      m_beginToEndClock.AverageSplitTime());
    ShowSplitTimesUI("CPU: end to end",
                      m_cachedStats.m_enabled ? m_cachedStats.m_endToEndTime : 
                                                m_endToEndClock.SplitTimes(),
                      m_endToEndClock.AverageSplitTime());
    ShowSplitTimesUI("CPU: present", 
                      m_cachedStats.m_enabled ? m_cachedStats.m_frameStats.m_presentTime : 
                                                frameStats.m_presentTime.SplitTimes(),
                      frameStats.m_presentTime.AverageSplitTime());
    ShowSplitTimesUI("CPU: waitfor present", 
                      m_cachedStats.m_enabled ? m_cachedStats.m_frameStats.m_waitForPresentTime :
                                                frameStats.m_waitForPresentTime.SplitTimes(),
                      frameStats.m_waitForPresentTime.AverageSplitTime());
    ShowSplitTimesUI("CPU: waitfor fence", 
                      m_cachedStats.m_enabled ? m_cachedStats.m_frameStats.m_waitForFenceTime : 
                                                frameStats.m_waitForFenceTime.SplitTimes(),
                      frameStats.m_waitForFenceTime.AverageSplitTime());
    ShowSplitTimesUI("CPU: frame time", 
                      m_cachedStats.m_enabled ? m_cachedStats.m_frameStats.m_frameTime :
                                                frameStats.m_frameTime.SplitTimes(),
                      frameStats.m_frameTime.AverageSplitTime());
    ShowSplitTimesUI("CPU: shadow pass cmd list(s) time", 
                      m_cachedStats.m_enabled ? m_cachedStats.m_sceneStats.m_shadowPassCmdListTime : 
                                                sceneStats.m_shadowPassCmdListTime.SplitTimes(),
                      sceneStats.m_shadowPassCmdListTime.AverageSplitTime());
    ShowSplitTimesUI("CPU: forward pass cmd list(s) time",
                      m_cachedStats.m_enabled ? m_cachedStats.m_sceneStats.m_forwardPassCmdListTime : 
                                                sceneStats.m_forwardPassCmdListTime.SplitTimes(),
                      sceneStats.m_forwardPassCmdListTime.AverageSplitTime());
    ShowSplitTimesUI("CPU: total cmd lists time",
                      m_cachedStats.m_enabled ? m_cachedStats.m_sceneStats.m_cmdListsTime :
                                                sceneStats.m_cmdListsTime.SplitTimes(),
                      sceneStats.m_cmdListsTime.AverageSplitTime());

    const size_t cmdListTimesCount = frameStats.m_cmdListTimes.size();
    assert(!m_cachedStats.m_enabled ||
            (m_cachedStats.m_enabled && cmdListTimesCount == m_cachedStats.m_frameStats.m_cmdListTimes.size()));
    size_t i = 0;
    for (auto& cmdListTime : frameStats.m_cmdListTimes)
    {
        ShowSplitTimesUI(("GPU: " + ConvertFromUTF16ToUTF8(cmdListTime.first)).c_str(),
                         m_cachedStats.m_enabled ? m_cachedStats.m_frameStats.m_cmdListTimes[i] :
                         *cmdListTime.second,
                         cmdListTime.second->LastValue());
        ++i;
    }
    ShowTimeUI("CPU: delta time", m_cachedDeltaTime);
    ShowTimeUI("CPU: total time", m_cachedTotalTime);
    ImGui::Text("# draw calls: shadow pass %d", sceneStats.m_shadowPassDrawCallsCount);
    ImGui::Text("# draw calls: forward pass %d", sceneStats.m_forwardPassDrawCallsCount);
    ShowTimeUI("CPU: loading gpu resources", sceneStats.m_loadingGPUResourcesTime);
    ShowTimeUI("CPU: loading scene data", m_sceneLoadingTime);

    ImGui::End();
}

void D3D12BasicsEngine::RenderFrame()
{
    ImGui::Render();

    SetupCmdLists();

    D3D12CmdLists cmdLists;
    cmdLists.push_back(m_preCmdList->GetCmdList().Get());

    const auto& depthBufferViewHandle = m_gpu.GetViewCPUHandle(m_depthBuffer.m_dsv);
    const auto& backbufferRT = m_gpu.SwapChainBackBufferViewHandle();
    auto sceneRenderCmdLists = m_sceneRender->RecordCmdLists(backbufferRT, depthBufferViewHandle, 
                                                             m_taskScheduler, m_enableParallelCmdsLits,
                                                             m_drawCallsCount);
    auto imguiCmdList = m_imgui->EndFrame(backbufferRT, depthBufferViewHandle);
    assert(imguiCmdList);
    cmdLists.insert(cmdLists.end(), sceneRenderCmdLists.begin(), sceneRenderCmdLists.end());
    cmdLists.push_back(imguiCmdList);
    cmdLists.push_back(m_postCmdList->GetCmdList().Get());
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

void D3D12BasicsEngine::SetupCmdLists()
{
    const auto& backbufferRT = m_gpu.SwapChainBackBufferViewHandle();
    const auto& depthBufferViewHandle = m_gpu.GetViewCPUHandle(m_depthBuffer.m_dsv);

    {
        m_preCmdList->Open();

        ID3D12GraphicsCommandListPtr cmdList = m_preCmdList->GetCmdList();

        auto presentToRT = m_gpu.SwapChainTransition(Present_To_RenderTarget);
        cmdList->ResourceBarrier(1, &presentToRT);

        cmdList->OMSetRenderTargets(1, &backbufferRT, FALSE, &depthBufferViewHandle);

        cmdList->ClearDepthStencilView(depthBufferViewHandle, 
                                       D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
                                       1.0f, 0, 0, nullptr);
        cmdList->ClearRenderTargetView(backbufferRT, g_defaultClearColor, 0, nullptr);

        m_preCmdList->Close();
    }

    {
        m_postCmdList->Open();

        ID3D12GraphicsCommandListPtr cmdList = m_postCmdList->GetCmdList();

        auto rtToPresent = m_gpu.SwapChainTransition(RenderTarget_To_Present);
        cmdList->ResourceBarrier(1, &rtToPresent);

        m_postCmdList->Close();
    }
}

void D3D12BasicsEngine::SceneLoaded()
{
    if (m_drawCallsCount == 0)
        m_drawCallsCount = static_cast<int>(m_sceneRender->GpuMeshesCount());
}