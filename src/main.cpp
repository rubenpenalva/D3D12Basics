// c++ includes
#include <sstream>
#include <iostream>

// windows includes
#include <windows.h>

// Project includes
#include "d3d12backendrender.h"

using namespace D3D12Basics;
using namespace D3D12Render;

namespace
{
    // NOTE:  Assuming working directory contains the data folder
    const wchar_t* g_sponzaDataWorkingPath = L"./data/sponza/";
    const wchar_t* g_sponzaModel = L"./data/sponza/sponza.dae";
    
    const wchar_t* g_texture256FileName     = L"./data/texture_256.png";
    const wchar_t* g_texture1024FileName    = L"./data/texture_1024.jpg";

    const size_t g_planesCount          = 1;
    const size_t g_planeModelID         = 0;

    const size_t g_spheresCount         = 100;
    const size_t g_spheresModelStartID  = g_planeModelID + g_planesCount;
    const float g_spheresAngleDiff      = (D3D12Basics::M_2PI / g_spheresCount);

    const size_t g_cubesCount           = 25;
    const size_t g_cubesModelStartID    = g_spheresModelStartID + g_spheresCount;
    const float g_cubesAngleDiff        = (D3D12Basics::M_2PI / g_cubesCount);

    const size_t g_modelsCount          = g_planesCount + g_spheresCount + g_cubesCount;

    const wchar_t* g_enableWaitForPresentCmdName        = L"waitForPresent";
    const size_t g_enableWaitForPresentCmdNameLength    = wcslen(g_enableWaitForPresentCmdName);

    struct CommandLine
    {
        bool m_isWaitableForPresentEnabled;
    };

    bool HandleWindowMessages()
    {
        MSG msg = {};
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
            {
                return true;
            }
            else
            {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }

        return false;
    }

    D3D12Basics::Matrix44 CalculateSphereLocalToWorld(size_t sphereID, float totalTime)
    {
        const float longitude = g_spheresAngleDiff * sphereID;
        const float latitude = D3D12Basics::M_PI_2;
        const float altitude = 3.3f;
        const auto sphereOffsetPos = D3D12Basics::Float3(0.0f, 0.3f + (sinf(sphereID - totalTime * 5.0f) * 0.5f + 0.5f)*0.5f, 0.0f);
        D3D12Basics::Float3 spherePos = D3D12Basics::SphericalToCartersian(longitude, latitude, altitude) + sphereOffsetPos;

        return D3D12Basics::Matrix44::CreateScale(0.2f) * D3D12Basics::Matrix44::CreateTranslation(spherePos);
    }

    Scene CreateScene()
    {
        std::vector<Model> models(g_modelsCount);

        // Plane
        {
            D3D12Basics::Matrix44 localToWorld = D3D12Basics::Matrix44::CreateScale(10.0f) * D3D12Basics::Matrix44::CreateRotationX(D3D12Basics::M_PI_2);
            Material material { g_texture256FileName };
            // TODO use a proper constructor
            models[g_planeModelID] = Model{ L"Ground plane", Model::Type::Plane, 0, localToWorld, material };
        }

        // Spheres
        Material sphereMaterial { g_texture1024FileName };
        for (size_t i = 0; i < g_spheresCount; ++i)
        {
            D3D12Basics::Matrix44 localToWorld = CalculateSphereLocalToWorld(i, 0.0f);
            std::wstringstream converter;
            converter << "Sphere " << i;

            models[g_spheresModelStartID + i] = Model{ converter.str().c_str(), Model::Type::Sphere, 0, localToWorld, sphereMaterial };
        }

        // Cubes
        const Material cubeMaterial { g_texture1024FileName };
        for (size_t i = 0; i < g_cubesCount; ++i)
        {
            const float longitude = g_cubesAngleDiff * i;
            const float latitude = D3D12Basics::M_PI_2;
            const float altitude = 2.0f;
            const auto cubeOffsetPos = D3D12Basics::Float3(0.0f, (sinf(static_cast<float>(i)) * 0.5f + 0.5f) * 0.5f + 0.35f, 0.0f);
            D3D12Basics::Float3 cubePos = D3D12Basics::SphericalToCartersian(longitude, latitude, altitude) + cubeOffsetPos;

            D3D12Basics::Matrix44 localToWorld = D3D12Basics::Matrix44::CreateScale(0.35f) * D3D12Basics::Matrix44::CreateTranslation(cubePos);
            std::wstringstream converter;
            converter << "Cube " << i;

            models[g_cubesModelStartID + i] = Model{ converter.str().c_str(), Model::Type::Cube, 0, localToWorld, cubeMaterial};
        }

        Scene scene;
        scene.m_sceneFile = g_sponzaModel;
        scene.m_models = std::move(models);
        return scene;
    }

    void UpdateScene(Scene& scene, float totalTime, float /*deltaTime*/)
    {
        const float longitude = 2.f * (1.0f / D3D12Basics::M_2PI) * totalTime;
        const float latitude = D3D12Basics::M_PI_4 + D3D12Basics::M_PI_8;
        const float altitude = 5.0f;
        D3D12Basics::Float3 cameraPos = D3D12Basics::SphericalToCartersian(longitude, latitude, altitude);

        scene.m_camera.TranslateLookingAt(cameraPos, D3D12Basics::Float3::Zero);

        for (size_t i = 0; i < g_spheresCount; ++i)
        {
            auto& sphereModel = scene.m_models[g_spheresModelStartID + i];
            
            sphereModel.m_transform = CalculateSphereLocalToWorld(i, totalTime);
        }
    }

    CommandLine ProcessCmndLine(LPWSTR szCmdLine)
    {
        CommandLine cmdLine{};

        if (szCmdLine[0] == '-')
        {
            size_t w = 0;
            for (size_t j = 1;
                szCmdLine[j] != '\0' && szCmdLine[j] != '-' &&
                j < g_enableWaitForPresentCmdNameLength &&
                szCmdLine[j] == g_enableWaitForPresentCmdName[w];
                ++j, ++w);

            cmdLine.m_isWaitableForPresentEnabled = w == g_enableWaitForPresentCmdNameLength - 1;
        }

        return cmdLine;
    }
}

int WINAPI wWinMain(HINSTANCE /*hInstance*/, HINSTANCE /*hPrevInstance*/, LPWSTR szCmdLine, int /*iCmdShow*/)
{
    auto cmdLine = ProcessCmndLine(szCmdLine);

    Scene scene = CreateScene();

    D3D12BackendRender backendRender(scene, cmdLine.m_isWaitableForPresentEnabled);

    D3D12Basics::CustomWindow customWindow(backendRender.GetSafestResolutionSupported());

    backendRender.SetOutputWindow(customWindow.GetHWND());

    // Load scene gpu resources and dispose the cpu memory
    {
        SceneLoader sceneLoader(scene.m_sceneFile, scene, g_sponzaDataWorkingPath);
        backendRender.LoadSceneResources(sceneLoader);
    }

    Timer timer;

    // Game loop
    bool quit = false;
    while (!quit)
    {
        quit = HandleWindowMessages();

        // Process the wndproc events state
        {
            if (customWindow.HasFullscreenChanged())
                backendRender.OnToggleFullScreen();

            if (customWindow.HasResolutionChanged())
                backendRender.OnResize(customWindow.GetResolution());

            customWindow.ResetWndProcEventsState();
        }

        // Kick the work: update scene - update backend resources - render frame - present frame
        UpdateScene(scene, timer.TotalTime(), timer.ElapsedTime());

        backendRender.UpdateSceneResources();

        backendRender.RenderFrame();

        backendRender.FinishFrame();

        // Measure elapsed time
        timer.Mark();
    }

    return 0;
}