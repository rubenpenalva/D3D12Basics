#include "d3d12basicsengine.h"

// c++ includes
#include <cassert>

// project includes
#include "meshgenerator.h"
#include "d3d12scenerender.h"
#include "d3d12imgui.h"

// thirdparty libraries include
#include "imgui/imgui.h"

using namespace D3D12Basics;

namespace 
{
    static const float g_defaultClearColor[4] = { 0.0f, 0.2f, 0.4f, 1.0f };

    void UpdateRendertasksResolution(std::vector<D3D12GpuRenderTask>& renderTasks, const Resolution& resolution)
    {
        D3D12_VIEWPORT viewport =
        {
            0.0f, 0.0f,
            static_cast<float>(resolution.m_width),
            static_cast<float>(resolution.m_height),
            D3D12_MIN_DEPTH, D3D12_MAX_DEPTH
        };

        RECT scissorRect =
        {
            0L, 0L,
            static_cast<long>(resolution.m_width),
            static_cast<long>(resolution.m_height)
        };

        for (auto& renderTask : renderTasks)
        {
            renderTask.m_viewport = viewport;
            renderTask.m_scissorRect = scissorRect;
        }
    }
}

D3D12BasicsEngine::D3D12BasicsEngine(const Settings& settings, Scene&& scene)  :   m_gpu(settings.m_isWaitableForPresentEnabled),
                                                                                   m_sceneLoadingDone(false), m_quit(false), m_scene(std::move(scene))
{
    m_window = std::make_unique<CustomWindow>(m_gpu.GetSafestResolutionSupported());
    assert(m_window);

    m_gpu.SetOutputWindow(m_window->GetHWND());

    m_sceneRender = std::make_unique<D3D12SceneRender>(m_gpu, m_scene, m_textureDataCache, m_meshDataCache);
    assert(m_sceneRender);

    m_inputController = std::make_unique<InputController>(m_window->GetHWND());
    assert(m_inputController);

    m_cameraController = std::make_unique<CameraController>(*m_inputController);
    assert(m_cameraController);

    m_appController = std::make_unique<AppController>(*m_inputController);
    assert(m_appController);

    m_imgui = std::make_unique<D3D12ImGui>(m_gpu);

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

    // TODO is it needed? gpu destructor already waits for all the pipeline to be done
    m_gpu.WaitAll();
}

void D3D12BasicsEngine::BeginFrame()
{
    m_beginToEndDeltaTime = m_beginToEndTimer.ElapsedTime();

    m_beginToEndTimer.Mark();

    m_deltaTime = m_endToEndTimer.ElapsedTime();
    m_totalTime = m_endToEndTimer.TotalTime();

    ProcessWindowEvents();

    ProcessUserEvents();

    m_imgui->BeginFrame();

    if (m_sceneLoadingDone && !m_sceneRender->AreGpuResourcesLoaded())
    {
        m_sceneLoaderThread.join();
        m_sceneRender->LoadGpuResources();
    }

    if (m_sceneRender->AreGpuResourcesLoaded())
    {
        m_sceneRender->Update();
    }

    ShowFrameStats();
}

void D3D12BasicsEngine::EndFrame()
{
    // Prepare render tasks: clear + scene + imgui
    {
        std::vector<D3D12GpuRenderTask> renderTasks;
        D3D12GpuRenderTask clearRenderTask;
        clearRenderTask.m_clear = true;
        memcpy(clearRenderTask.m_clearColor, g_defaultClearColor, sizeof(float) * 4);
        renderTasks.emplace_back(std::move(clearRenderTask));

        auto sceneRenderTasks = m_sceneRender->CreateRenderTasks();
        UpdateRendertasksResolution(sceneRenderTasks, m_gpu.GetCurrentResolution());
        renderTasks.insert(renderTasks.end(), sceneRenderTasks.begin(), sceneRenderTasks.end());

        auto imguiRenderTasks = m_imgui->EndFrame();
        renderTasks.insert(renderTasks.end(), imguiRenderTasks.begin(), imguiRenderTasks.end());

        m_gpu.ExecuteRenderTasks(renderTasks);
    }

    m_gpu.FinishFrame();

    m_beginToEndTimer.Mark();
    m_endToEndTimer.Mark();
}

void D3D12BasicsEngine::Update(void (*Update)(Scene& scene, float totalTime))
{
    if (!m_sceneRender->AreGpuResourcesLoaded())
        return;

    Update(m_scene, m_totalTime);
}

void D3D12BasicsEngine::ProcessWindowEvents()
{
    if (m_window->HasFullscreenChanged())
        m_gpu.OnToggleFullScreen();

    if (m_window->HasResolutionChanged())
        m_gpu.OnResize(m_window->GetResolution());

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

            MeshData meshData;
            switch (model.m_type)
            {
            case Model::Type::Cube:
                meshData = CreateCube(VertexDesc{ true, false, false });
                break;
            case Model::Type::Plane:
                meshData = CreatePlane(VertexDesc{ true, false, false });
                break;
            case Model::Type::Sphere:
                meshData = CreateSphere(VertexDesc{ true, false, false }, 40, 40);
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

void D3D12BasicsEngine::ShowFrameStats()
{
    const float DISTANCE = 10.0f;
    static int corner = 0;
    ImVec2 window_pos = ImVec2((corner & 1) ? ImGui::GetIO().DisplaySize.x - DISTANCE : DISTANCE, 
                               (corner & 2) ? ImGui::GetIO().DisplaySize.y - DISTANCE : DISTANCE);
    ImVec2 window_pos_pivot = ImVec2((corner & 1) ? 1.0f : 0.0f, (corner & 2) ? 1.0f : 0.0f);
    ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always, window_pos_pivot);
    ImGui::SetNextWindowBgAlpha(0.3f); // Transparent background
    const ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                         ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove |
                                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                         ImGuiWindowFlags_NoNav;

    auto frameStats = m_gpu.GetFrameStats();

    ImGui::Begin("Example: Fixed Overlay", nullptr, windowFlags);
    ImGui::Text("CPU: begin to end %.3f ms", m_beginToEndDeltaTime * 1000.0f);
    ImGui::Text("CPU: end to end %.3f ms", m_deltaTime * 1000.0f);
    ImGui::Text("CPU: present %.3f ms", frameStats.m_presentTime * 1000.0f);
    ImGui::Text("CPU: waitfor present %.3f ms", frameStats.m_waitForPresentTime * 1000.0f);
    ImGui::Text("CPU: waitfor fence %.3f ms", frameStats.m_waitForFenceTime * 1000.0f);
    ImGui::Text("CPU: frame time %.3f ms", frameStats.m_frameTime * 1000.0f);
    ImGui::Text("GPU: cmdlist time %.3f ms", frameStats.m_cmdListTime * 1000.0f);
    ImGui::End();
}